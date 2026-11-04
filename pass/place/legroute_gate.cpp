// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include <limits>
#include <deque>
#include <format>
#include <functional>
#include <iomanip>
#include <sstream>

#include "legroute_internal.hpp"

namespace livehd::placement {
Legroute_replan::Legroute_replan(Legroute_gate_report report)
    : std::runtime_error(std::format(
          "legroute: pre-SAT planning required: {} oversized regions, {} regions above {} physical cell density, "
          "{} edges above {} routing density, {} coarse overflow tracks, {} unresolved nets "
          "({} search-limited, {} topology-disconnected), {} regions with {} boxes lacking distinct fitting tiles; "
          "candidate was not admitted to SAT",
          report.oversized_regions, report.cell_dense_regions, report.density_limit, report.routing_dense_edges,
          report.density_limit, report.overflow_tracks, report.unrouted,
          report.search_limited, report.topology_disconnected, report.unfittable_regions, report.unfittable_boxes))
    , gates(std::move(report)) {}

std::string legroute_gate_json(const Legroute_gate_report& r) {
  std::ostringstream out;
  out << std::setprecision(17);
  const auto number = [&](double value) {
    if (std::isfinite(value)) {
      out << value;
    } else {
      out << "null";
    }
  };
  out << "{\"stage\":\"" << (r.stage == Legroute_gate_report::Stage::primary ? "primary" : "secondary")
      << "\",\"requires_replan\":" << (r.requires_replan() ? "true" : "false")
      << ",\"density_limit\":" << r.density_limit << ",\"cell_density_basis\":\"movable_CORE_LEF_area_over_free_fixed_obstacle_subtracted_row_sites\""
      << ",\"max_cell_density\":";
  number(r.max_cell_density);
  out << ",\"max_routing_density\":";
  number(r.max_routing_density);
  out << ",\"region_target\":" << r.region_target << ",\"oversized_regions\":" << r.oversized_regions
      << ",\"cell_dense_regions\":" << r.cell_dense_regions << ",\"routing_dense_edges\":" << r.routing_dense_edges
      << ",\"overflow_edges\":" << r.overflow_edges << ",\"overflow_tracks\":" << r.overflow_tracks
      << ",\"unfittable_regions\":" << r.unfittable_regions << ",\"unfittable_boxes\":" << r.unfittable_boxes
      << ",\"unrouted\":" << r.unrouted << ",\"search_limited\":" << r.search_limited
      << ",\"topology_disconnected\":" << r.topology_disconnected << ",\"regions\":[";
  for (size_t i = 0; i < r.regions.size(); ++i) {
    if (i) {
      out << ',';
    }
    const auto& region = r.regions[i];
    out << "{\"region\":" << region.region << ",\"cells\":" << region.cells << ",\"unfittable_boxes\":" << region.unfittable_boxes
        << ",\"cell_area_dbu2\":" << region.cell_area_dbu2 << ",\"placeable_area_dbu2\":" << region.placeable_area_dbu2
        << ",\"cell_density\":";
    number(region.cell_density);
    out << '}';
  }
  out << "],\"affected_components\":[";
  for (size_t i = 0; i < r.affected_components.size(); ++i) {
    if (i) {
      out << ',';
    }
    out << r.affected_components[i];
  }
  out << "],\"hotspots_dbu\":[";
  for (size_t i = 0; i < r.hotspots.size(); ++i) {
    if (i) {
      out << ',';
    }
    const auto& p = r.hotspots[i];
    out << '[' << p.xl << ',' << p.yl << ',' << p.xh << ',' << p.yh << ']';
  }
  out << "]}";
  return out.str();
}

namespace lr {
Window_fit assess_window_fit(const Design& d, const Grid& g, double cap, unsigned window) {
  Window_fit out;
  out.tile_pressure.assign(g.size(), 1);
  if (cap <= 0 || window == 0 || g.cols <= 0 || g.rows <= 0) {
    return out;
  }
  // Same bases as the region gate: free row sites after fixed bodies, movable CORE area at the cell centre.
  std::vector<double> free_area(g.size()), cell_area(g.size());
  for (size_t t = 0; t < g.size(); ++t) {
    int64_t sites = 0;
    if (g.placeable_sites.size() == g.size()) {
      sites = g.placeable_sites[t];
    } else {
      for (const auto& segment : g.segs(int(t))) {
        sites += segment.hi - segment.lo;
      }
    }
    free_area[t] = double(sites) * double(g.site) * double(g.row_h);
  }
  for (size_t i = 0; i < d.movable.size(); ++i) {
    const auto  component = size_t(d.movable[i]);
    const auto& pose      = d.fp.components[component].place;
    const auto& macro     = *d.macro[component];
    const int   tile = g.tile_of(double(pose.x) + macro.width * d.fp.dbu / 2, double(pose.y) + macro.height * d.fp.dbu / 2);
    cell_area[size_t(tile)] += macro.width * macro.height * double(d.fp.dbu) * double(d.fp.dbu);
  }
  // Roughly square windows in DBU: tiles are usually much wider than tall.
  const double side = double(window) * double(std::max(g.tile_w, g.tile_h));
  out.cols          = std::clamp(int(std::lround(side / double(g.tile_w))), 1, g.cols);
  out.rows          = std::clamp(int(std::lround(side / double(g.tile_h))), 1, g.rows);
  // 2D prefix sums over tiles.
  const auto prefix = [&](const std::vector<double>& v) {
    std::vector<double> p(size_t(g.cols + 1) * size_t(g.rows + 1), 0);
    for (int y = 0; y < g.rows; ++y) {
      for (int x = 0; x < g.cols; ++x) {
        p[size_t(y + 1) * size_t(g.cols + 1) + size_t(x + 1)] = v[size_t(g.tile(x, y))] + p[size_t(y) * size_t(g.cols + 1) + size_t(x + 1)]
                                                               + p[size_t(y + 1) * size_t(g.cols + 1) + size_t(x)]
                                                               - p[size_t(y) * size_t(g.cols + 1) + size_t(x)];
      }
    }
    return p;
  };
  const auto pf = prefix(free_area), pc = prefix(cell_area);
  const auto sum = [&](const std::vector<double>& p, int x0, int y0, int x1, int y1) {  // [x0, x1) x [y0, y1)
    const auto at = [&](int x, int y) { return p[size_t(y) * size_t(g.cols + 1) + size_t(x)]; };
    return at(x1, y1) - at(x0, y1) - at(x1, y0) + at(x0, y0);
  };
  // Origins every half window, the last one flush with the far edge, so no dense spot hides on a window seam.
  const auto origins = [](int extent, int size) {
    std::vector<int> v;
    const int        stride = std::max(1, size / 2);
    for (int o = 0; o + size < extent; o += stride) {
      v.push_back(o);
    }
    v.push_back(extent - size);
    return v;
  };
  for (int y0 : origins(g.rows, out.rows)) {
    for (int x0 : origins(g.cols, out.cols)) {
      ++out.windows;
      const double cells = sum(pc, x0, y0, x0 + out.cols, y0 + out.rows);
      if (cells <= 0) {
        continue;
      }
      const double room    = sum(pf, x0, y0, x0 + out.cols, y0 + out.rows);
      const double density = room > 0 ? cells / room : INFINITY;
      out.max_density      = std::max(out.max_density, density);
      if (density <= cap + 1e-12) {
        continue;
      }
      ++out.dense_windows;
      const double pressure = std::isfinite(density) ? density / cap : 2;
      for (int y = y0; y < y0 + out.rows; ++y) {
        for (int x = x0; x < x0 + out.cols; ++x) {
          auto& p = out.tile_pressure[size_t(g.tile(x, y))];
          p       = std::max(p, pressure);
        }
      }
    }
  }
  return out;
}

Fit_match match_box_fit(const Design& d, const Grid& g, Boxes& boxes, bool apply) {
  // the movement policy: at most 1% of the movable boxes beyond the window, none beyond far_reach windows
  constexpr int far_reach = 4;
  Fit_match        out;
  std::vector<int> B(boxes.boxes.size()), T;
  out.tile_pressure.assign(g.size(), 1);
  for (size_t b = 0; b < B.size(); ++b) {
    B[b] = int(b);
  }
  for (size_t t = 0; t < g.size(); ++t) {
    if (!g.blocked[t]) {
      T.push_back(int(t));
    }
  }
  if (B.empty()) {
    out.matched = out.fits = true;
    return out;
  }
  // One work budget for the whole check (strict matching, domain growth and rematch). Exhaustion is "budget",
  // never a proof that no fitting assignment exists.
  struct Budget_exhausted {};
  const auto started = std::chrono::steady_clock::now();
  size_t     calls   = 0;
  const auto check   = [&] {
    if ((++calls & 0x3ff) == 0 && std::chrono::steady_clock::now() - started > std::chrono::seconds(10)) {
      throw Budget_exhausted{};
    }
  };
  // Fixed boxes are part of the fit: each must sit on a distinct in-range, unblocked tile it fits.
  std::vector<char> taken(g.size(), 0);
  for (const auto& box : boxes.boxes) {
    if (!box.fixed) {
      ++out.movable_boxes;
      continue;
    }
    if (box.tile < 0 || size_t(box.tile) >= g.size() || g.blocked[size_t(box.tile)] || taken[size_t(box.tile)]
        || !box_fits(d, g, box, box.tile)) {
      out.reason = "fixed";
      return out;
    }
    taken[size_t(box.tile)] = 1;
  }
  // Pigeonhole: movable boxes against the free tiles the fixed boxes leave. No search can beat it.
  for (int t : T) {
    out.free_tiles += !taken[size_t(t)];
  }
  if (out.movable_boxes > out.free_tiles) {
    out.reason = "count";
    return out;
  }
  const int limit = int(std::max(1u, d.opt.window_tiles));
  // box_fits depends on the box and the tile's free-segment pattern (plus the placed-home exception, which is not
  // cached): memoize per (box, pattern) instead of repeating identical packing searches.
  std::unordered_map<uint64_t, bool> fit_memo;
  const auto                         fits_cached = [&](size_t b, int t) {
    const auto& box = boxes.boxes[b];
    if (box.placed || g.key.size() != g.size()) {
      return box_fits(d, g, box, t);
    }
    const uint64_t k             = (uint64_t(b) << 32) | g.key[size_t(t)];
    const auto [it, fresh]       = fit_memo.try_emplace(k, false);
    if (fresh) {
      it->second = box_fits(d, g, box, t);
    }
    return it->second;
  };
  // Strict radius first: every movable box on a distinct fitting tile within window_tiles of its home. A complete
  // matching here has no far box by construction, so the heuristic far count of the grown domains below can only
  // reject what this exact bounded graph could not place.
  {
    std::vector<std::vector<int>> near(B.size());
    std::vector<int>              home(B.size(), -1);
    for (size_t b = 0; b < B.size(); ++b) {
      const auto& box = boxes.boxes[b];
      home[b]         = box.fixed && box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy);
      if (box.fixed) {
        continue;
      }
      const int c = g.col(home[b]), r = g.row(home[b]);
      for (int y = std::max(0, r - limit); y <= std::min(g.rows - 1, r + limit); ++y) {
        try {
          check();
        } catch (const Budget_exhausted&) {
          out.reason = "budget";
          return out;
        }
        for (int x = std::max(0, c - limit); x <= std::min(g.cols - 1, c + limit); ++x) {
          const int t = g.tile(x, y);
          if (!g.blocked[size_t(t)] && !taken[size_t(t)] && fits_cached(b, t)) {
            near[b].push_back(t);
          }
        }
      }
      const auto distance = [&](int t) { return std::abs(double(g.cx(g.col(t))) - box.cx) + std::abs(double(g.cy(g.row(t))) - box.cy); };
      std::ranges::sort(near[b], [&](int a, int e) { return std::pair{distance(a), a} < std::pair{distance(e), e}; });
    }
    std::vector<int>               owner(g.size(), -1), strict(B.size(), -1);
    std::vector<char>              seen(g.size());
    std::vector<int>               touched;
    const std::function<bool(int)> augment = [&](int i) {
      check();
      for (int t : near[size_t(i)]) {
        if (seen[size_t(t)]) {
          continue;
        }
        seen[size_t(t)] = 1;
        touched.push_back(t);
        if (owner[size_t(t)] < 0 || augment(owner[size_t(t)])) {
          owner[size_t(t)]  = i;
          strict[size_t(i)] = t;
          return true;
        }
      }
      return false;
    };
    std::vector<char> pressed(B.size(), 0);
    const auto        press = [&](size_t b) {
      if (!pressed[b] && !boxes.boxes[b].fixed) {
        pressed[b] = 1;
        out.pressure_boxes.push_back(int(b));
      }
    };
    // Kuhn's augmenting paths over every box: a maximum matching of the strict-radius graph.
    size_t unmatched = 0;
    for (size_t b = 0; b < B.size(); ++b) {
      if (boxes.boxes[b].fixed) {
        strict[b] = boxes.boxes[b].tile;
        continue;
      }
      try {
        if (!augment(int(b))) {  // the depth-first search touches most boxes: the 2r search below names the owners
          ++unmatched;
          press(b);
        }
      } catch (const Budget_exhausted&) {
        out.reason = "budget";
        out.pressure_boxes.clear();  // a computational unknown, not a located physical deficit
        return out;
      }
      for (int t : touched) {
        seen[size_t(t)] = 0;
      }
      touched.clear();
    }
    bool complete = unmatched == 0;
    // each reach rematches from this strict witness
    const auto   strict0 = strict, owner0 = owner;
    const size_t unmatched0 = unmatched;
    // Certificate: every complete assignment moves at least `unmatched` boxes beyond the window. Above the 1%
    // quota (of the movable boxes, the same denominator as the final check) no search can satisfy the far policy:
    // reject without growing the domains. far_boxes is then a lower bound, not observed movement.
    if (unmatched * 100 > out.movable_boxes) {
      out.far_boxes = unmatched;
      out.reason    = "far_bound";
      return out;
    }
    // Within the quota: a minimum-far augmentation seeded by the strict matching. Every movable box may now reach
    // out to twice the window (a near tile costs 0, a far one 1); each unmatched box takes the 0-1 BFS shortest
    // alternating path, so a matched small box can step out to free a tile a large box needs. Reverse edges are
    // counted at 0, so the path cost is an upper bound and the witness's far count is measured afterwards.
    if (!complete) {
      // pressure comes from the final failing stage: the strict roots the 2r search rescues are not named
      out.pressure_boxes.clear();
      std::ranges::fill(pressed, 0);
      try {
        // 2r first; when it leaves boxes unplaced, or completes over the far quota, rematch from the strict witness
        // once at 4r (more candidates can need fewer far moves), under the same 1% quota
        for (const int reach : {2, far_reach}) {
        strict    = strict0;
        owner     = owner0;
        unmatched = unmatched0;
        out.pressure_boxes.clear();  // pressure from the final pass only
        std::ranges::fill(pressed, 0);
        out.tile_pressure.assign(g.size(), 1);
        const int                     wide = reach * limit;
        std::vector<std::vector<int>> cand(B.size());
        for (size_t b = 0; b < B.size(); ++b) {
          const auto& box = boxes.boxes[b];
          if (box.fixed) {
            continue;
          }
          const int c = g.col(home[b]), r = g.row(home[b]);
          for (int y = std::max(0, r - wide); y <= std::min(g.rows - 1, r + wide); ++y) {
            check();
            for (int x = std::max(0, c - wide); x <= std::min(g.cols - 1, c + wide); ++x) {
              const int t = g.tile(x, y);
              if (!g.blocked[size_t(t)] && !taken[size_t(t)] && fits_cached(b, t)) {
                cand[b].push_back(t);
              }
            }
          }
        }
        const auto far = [&](size_t b, int t) {
          return std::max(std::abs(g.col(t) - g.col(home[b])), std::abs(g.row(t) - g.row(home[b]))) > limit;
        };
        constexpr int                       inf = std::numeric_limits<int>::max();
        std::vector<int>                    dist(B.size(), inf);
        std::vector<std::pair<int, int>>    via(B.size(), {-1, -1});  // box -> (previous box, tile it takes)
        std::vector<size_t>                 visited;
        for (size_t u = 0; u < B.size() && unmatched; ++u) {
          if (boxes.boxes[u].fixed || strict[u] >= 0) {
            continue;
          }
          for (size_t v : visited) {
            dist[v] = inf;
            via[v]  = {-1, -1};
          }
          visited.clear();
          std::deque<size_t> queue{u};
          dist[u] = 0;
          visited.push_back(u);
          int best = inf, end_box = -1, end_tile = -1;
          while (!queue.empty()) {
            check();
            const size_t bx = queue.front();
            queue.pop_front();
            if (dist[bx] >= best) {
              continue;
            }
            for (int t : cand[bx]) {
              const int w = far(bx, t) ? 1 : 0, nd = dist[bx] + w;
              const int o = owner[size_t(t)];
              if (o < 0) {
                if (nd < best) {
                  best     = nd;
                  end_box  = int(bx);
                  end_tile = t;
                }
                continue;
              }
              if (size_t(o) == bx || nd >= dist[size_t(o)]) {
                continue;
              }
              if (dist[size_t(o)] == inf) {
                visited.push_back(size_t(o));
              }
              dist[size_t(o)] = nd;
              via[size_t(o)]  = {int(bx), t};
              w ? queue.push_back(size_t(o)) : queue.push_front(size_t(o));
            }
          }
          if (end_box < 0) {
            // No alternating path to a free tile within this reach: this box stays unmatched in every maximum
            // matching of the enumerated graph. Press its home (only the last pass's presses are kept).
            out.tile_pressure[size_t(home[u])] = std::max(out.tile_pressure[size_t(home[u])], 1.1);
            for (size_t v : visited) {  // u and the matched owners its search reached near u: those compete for its tiles
              if (std::max(std::abs(g.col(home[v]) - g.col(home[u])), std::abs(g.row(home[v]) - g.row(home[u]))) <= wide) {
                press(v);
              }
            }
            continue;
          }
          // augment: end_box takes end_tile, each predecessor takes the tile its successor vacated
          int bx = end_box, t = end_tile;
          while (true) {
            const int old           = strict[size_t(bx)];
            owner[size_t(t)]        = bx;
            strict[size_t(bx)]      = t;
            if (size_t(bx) == u) {
              break;
            }
            const auto [prev, prev_tile] = via[size_t(bx)];
            (void)prev_tile;  // == old: the tile bx gives up
            bx = prev;
            t  = old;
          }
          --unmatched;
        }
        if (!unmatched) {  // accept this reach's witness only within the far quota; otherwise try the wider one
          size_t far_count = 0;
          for (size_t b = 0; b < B.size(); ++b) {
            far_count += !boxes.boxes[b].fixed
                         && std::max(std::abs(g.col(strict[b]) - g.col(home[b])), std::abs(g.row(strict[b]) - g.row(home[b]))) > limit;
          }
          if (far_count * 100 <= out.movable_boxes) {
            break;
          }
        }
        }
      } catch (const Budget_exhausted&) {
        out.reason = "budget";
        out.pressure_boxes.clear();
        return out;
      }
      complete = unmatched == 0;
      if (!complete) {  // proven for the enumerated graph: only moves beyond far_reach windows remain, which the policy rejects
        out.far_boxes = unmatched;
        out.reason    = "far_bound";
        return out;
      }
    }
    if (complete && std::chrono::steady_clock::now() - started > std::chrono::seconds(10)) {
      out.reason = "budget";  // found after the budget: never published
      return out;
    }
    if (complete) {
      out.matched = true;
      out.pressure_boxes.clear();  // a complete witness names only its far boxes
      std::ranges::fill(pressed, 0);
      for (size_t b = 0; b < B.size(); ++b) {
        const int move = std::max(std::abs(g.col(strict[b]) - g.col(home[b])), std::abs(g.row(strict[b]) - g.row(home[b])));
        out.max_move   = std::max(out.max_move, size_t(move));
        out.far_boxes += move > limit;
        if (move > limit) {
          press(b);
        }
      }
      // the augmentation may have stepped matched boxes out too: judge the witness against the same policy
      out.fits = out.far_boxes * 100 <= out.movable_boxes && out.max_move <= size_t(far_reach) * size_t(limit);
      if (!out.fits) {
        out.reason = "far";
        return out;
      }
      out.pressure_boxes.clear();
      if (apply) {
        for (size_t b = 0; b < B.size(); ++b) {
          auto& box = boxes.boxes[b];
          if (!box.fixed) {
            box.cx = double(g.cx(g.col(strict[b])));
            box.cy = double(g.cy(g.row(strict[b])));
          }
        }
        out.applied = true;
      }
      return out;
    }
  }
  // The joint matching grows windows until it completes, inside the same budget.
  const std::vector<char> pinned(g.size(), 0);
  Box_domains             domains;
  try {
    domains = prepare_box_domains(d, d.opt, g, 0, B, T, boxes, {}, pinned, check);
  } catch (const Budget_exhausted&) {
    out.reason = "budget";
    return out;
  }
  out.matched = domains.matching.size() == B.size() && std::ranges::none_of(domains.matching, [](int t) { return t < 0; });
  if (!out.matched) {
    out.reason = "domains";
    return out;
  }
  // Inside the same domains, prefer each box's nearest tiles (augmenting paths over distance-sorted choices),
  // as the box route seed does: the complete witness above guarantees this matching completes too.
  std::vector<std::vector<int>> choices(B.size());
  for (size_t i = 0; i < B.size(); ++i) {
    try {
      choices[i] = box_domain_tiles(d, g, B[i], domains.windows[i], boxes, {}, domains.lifted, pinned, T, check);
    } catch (const Budget_exhausted&) {
      out.matched = false;
      out.reason  = "budget";
      return out;
    }
    const auto& box = boxes.boxes[size_t(B[i])];
    const auto  distance
        = [&](int t) { return std::abs(double(g.cx(g.col(t))) - box.cx) + std::abs(double(g.cy(g.row(t))) - box.cy); };
    std::ranges::sort(choices[i], [&](int a, int b) { return std::pair{distance(a), a} < std::pair{distance(b), b}; });
  }
  std::vector<int>               owner(g.size(), -1), matching(B.size(), -1);
  std::vector<char>              seen(g.size());
  const std::function<bool(int)> augment = [&](int i) {
    check();
    for (int t : choices[size_t(i)]) {
      if (seen[size_t(t)]) {
        continue;
      }
      seen[size_t(t)] = 1;
      if (owner[size_t(t)] < 0 || augment(owner[size_t(t)])) {
        owner[size_t(t)]    = i;
        matching[size_t(i)] = t;
        return true;
      }
    }
    return false;
  };
  try {
    for (size_t i = 0; i < B.size(); ++i) {
      std::ranges::fill(seen, 0);
      if (!augment(int(i))) {
        out.matched = false;  // cannot happen with a complete witness in the same domains
        out.reason  = "domains";
        return out;
      }
    }
  } catch (const Budget_exhausted&) {
    out.matched = false;
    out.reason  = "budget";
    return out;
  }
  for (size_t b = 0; b < B.size(); ++b) {
    const auto& box  = boxes.boxes[b];
    const int   home = box.fixed && box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy);
    const int   t    = matching[b];
    const int   move = std::max(std::abs(g.col(t) - g.col(home)), std::abs(g.row(t) - g.row(home)));
    out.max_move     = std::max(out.max_move, size_t(move));
    if (move > limit) {
      ++out.far_boxes;
      out.tile_pressure[size_t(home)] = std::max(out.tile_pressure[size_t(home)], 1. + .05 * double(move - limit));
    }
  }
  out.fits = out.far_boxes * 100 <= out.movable_boxes && out.max_move <= size_t(far_reach) * size_t(limit);
  if (!out.fits) {
    out.reason = "far";
  }
  if (out.fits && std::chrono::steady_clock::now() - started > std::chrono::seconds(10)) {
    out.fits   = false;  // a witness found after the budget is still budget exhaustion: never apply it
    out.reason = "budget";
    return out;
  }
  if (out.fits && apply) {
    for (size_t b = 0; b < B.size(); ++b) {
      auto& box = boxes.boxes[b];
      if (!box.fixed) {
        const int t = matching[b];
        box.cx      = double(g.cx(g.col(t)));
        box.cy      = double(g.cy(g.row(t)));
      }
    }
    out.applied = true;
  }
  return out;
}

Legroute_gate_report assess_legroute_gates(const Design& d, const Grid& g, const Boxes* boxes, const Global_routes& routes,
                                           const std::vector<int>& tree_nets, const std::vector<int>& cap_h,
                                           const std::vector<int>& cap_v, bool regions_ready, std::vector<char>* hot_tiles) {
  Legroute_gate_report out;
  out.density_limit = d.opt.density_ceiling;
  out.region_target   = d.opt.partition_cells ? d.opt.partition_cells : d.opt.region_cell_limit;
  out.unrouted        = routes.unrouted;
  out.overflow_edges  = routes.overflow_edges;
  out.overflow_tracks = routes.overflow_tracks;
  out.regions.resize(size_t(g.regions));
  std::vector<char> hot(g.size()), hot_region(size_t(g.regions)), affected(d.fp.components.size());
  std::vector<int>  cell_tile(d.movable.size()), cell_region(d.movable.size());
  for (int r = 0; r < g.regions; ++r) {
    out.regions[size_t(r)].region = r;
  }
  for (size_t t = 0; t < g.size(); ++t) {
    int64_t sites = 0;
    if (g.placeable_sites.size() == g.size()) {
      sites = g.placeable_sites[t];
    } else {
      for (const auto& segment : g.segs(int(t))) {
        sites += segment.hi - segment.lo;
      }
    }
    out.regions[size_t(g.region[t])].placeable_area_dbu2 += double(sites) * double(g.site) * double(g.row_h);
  }
  for (size_t i = 0; i < d.movable.size(); ++i) {
    const auto  component = size_t(d.movable[i]);
    const auto& pose      = d.fp.components[component].place;
    const auto& macro     = *d.macro[component];
    int         tile      = g.tile_of(double(pose.x) + macro.width * d.fp.dbu / 2, double(pose.y) + macro.height * d.fp.dbu / 2);
    int         region    = g.region[size_t(tile)];
    if (boxes && boxes->box_of[i] >= 0) {
      const auto& box = boxes->boxes[size_t(boxes->box_of[i])];
      tile            = box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy);
      region          = g.region[size_t(tile)];
    }
    cell_tile[i]   = tile;
    cell_region[i] = region;
    auto& result   = out.regions[size_t(region)];
    ++result.cells;
    result.cell_area_dbu2 += macro.width * macro.height * double(d.fp.dbu) * double(d.fp.dbu);
  }
  for (auto& region : out.regions) {
    region.cell_density                = region.placeable_area_dbu2 > 0 ? region.cell_area_dbu2 / region.placeable_area_dbu2
                                         : region.cells                 ? INFINITY
                                                                        : 0;
    const bool dense                   = region.cell_density > d.opt.density_ceiling + 1e-12;
    const bool large                   = regions_ready && out.region_target && region.cells > out.region_target;
    out.cell_dense_regions            += dense;
    out.oversized_regions             += large;
    out.max_cell_density               = std::max(out.max_cell_density, region.cell_density);
    hot_region[size_t(region.region)]  = dense || large;
  }
  if (regions_ready && boxes && movable_route_partition(d.opt)) {
    // A low area density need not provide a distinct fitting tile for every
    // box. Test the full region (a superset of SAT windows), without changing
    // homes, grouping or ownership. Already dense/oversized regions need no
    // matching: they return to planning before any model is constructed.
    std::vector<std::vector<int>> tiles(size_t(g.regions)), members(size_t(g.regions));
    for (size_t t = 0; t < g.size(); ++t) {
      if (!g.blocked[t]) {
        tiles[size_t(g.region[t])].push_back(int(t));
      }
    }
    for (size_t b = 0; b < boxes->boxes.size(); ++b) {
      const auto& box  = boxes->boxes[b];
      const auto  tile = box.tile >= 0 ? box.tile : g.tile_of(box.cx, box.cy);
      members[size_t(g.region[size_t(tile)])].push_back(int(b));
    }
    for (int r = 0; r < g.regions; ++r) {
      if (hot_region[size_t(r)]) {
        continue;
      }
      const auto&                    ids  = members[size_t(r)];
      const auto&                    room = tiles[size_t(r)];
      std::vector<int>               owner(room.size(), -1);
      std::vector<char>              seen(room.size());
      const std::function<bool(int)> augment = [&](int i) {
        const auto& box = boxes->boxes[size_t(ids[size_t(i)])];
        // Prefer free room; only displace an earlier box when necessary. Do
        // not materialize a box-by-tile matrix for a sparse, large region.
        for (bool occupied : {false, true}) {
          for (size_t j = 0; j < room.size(); ++j) {
            if (seen[j] || (owner[j] >= 0) != occupied || (box.fixed && box.tile != room[j]) || !box_fits(d, g, box, room[j])) {
              continue;
            }
            seen[j] = 1;
            if (!occupied || augment(owner[j])) {
              owner[j] = i;
              return true;
            }
          }
        }
        return false;
      };
      size_t missing = 0;
      for (size_t i = 0; i < ids.size(); ++i) {
        std::fill(seen.begin(), seen.end(), 0);
        missing += !augment(int(i));
      }
      if (missing) {
        out.regions[size_t(r)].unfittable_boxes  = missing;
        out.unfittable_boxes                    += missing;
        ++out.unfittable_regions;
        hot_region[size_t(r)] = true;
      }
    }
  }
  std::vector<size_t> use_h(g.size()), use_v(g.size());
  for (size_t k = 0; k < routes.trees.size(); ++k) {
    const auto& tree = routes.trees[k];
    for (const auto& [a, b] : tree.edges) {
      ++(g.row(a) == g.row(b) ? use_h : use_v)[size_t(std::min(a, b))];
    }
    const int n = tree_nets.empty() ? int(k) : tree_nets.at(k);
    if (tree.routed || n < 0 || size_t(n) >= d.nets.size() || !d.nets[size_t(n)].routed) {
      continue;
    }
    out.search_limited        += tree.search_limited;
    out.topology_disconnected += tree.topology_disconnected;
    for (const auto& pin : d.nets[size_t(n)].pins) {
      int tile = physical_pin_access(d, g, pin).tile;
      if (pin.comp >= 0 && d.cell[size_t(pin.comp)] >= 0) {
        const auto cell            = size_t(d.cell[size_t(pin.comp)]);
        tile                       = cell_tile[cell];
        affected[size_t(pin.comp)] = true;
      }
      hot[size_t(tile)] = true;
    }
  }
  for (size_t t = 0; t < g.size(); ++t) {
    hot[t] |= hot_region[size_t(g.region[t])];
    for (bool horizontal : {true, false}) {
      const bool exists = horizontal ? g.col(int(t)) + 1 < g.cols : g.row(int(t)) + 1 < g.rows;
      if (!exists) {
        continue;
      }
      const size_t q           = t + (horizontal ? 1 : size_t(g.cols));
      const auto&  cap         = horizontal ? cap_h : cap_v;
      const int    capacity    = std::max(0, std::min(cap[t], cap[q]));
      const size_t use         = (horizontal ? use_h : use_v)[t];
      const double density     = capacity > 0 ? double(use) / capacity : use ? INFINITY : 0;
      out.max_routing_density  = std::max(out.max_routing_density, density);
      const bool dense         = density > d.opt.density_ceiling + 1e-12;
      const bool over          = use > size_t(std::max(0, int(std::floor(admission_share(d.opt) * capacity))));
      out.routing_dense_edges += dense;
      if (dense || over) {
        hot[t] = hot[q] = true;
      }
    }
  }
  for (size_t i = 0; i < d.movable.size(); ++i) {
    if (hot[size_t(cell_tile[i])] || hot_region[size_t(cell_region[i])]) {
      affected[size_t(d.movable[i])] = true;
    }
  }
  for (size_t i = 0; i < affected.size(); ++i) {
    if (affected[i]) {
      out.affected_components.push_back(i);
    }
  }
  for (int r = 0; r < g.rows; ++r) {
    for (int c = 0; c < g.cols;) {
      if (!hot[size_t(g.tile(c, r))]) {
        ++c;
        continue;
      }
      const int first = c;
      while (c < g.cols && hot[size_t(g.tile(c, r))]) {
        ++c;
      }
      out.hotspots.push_back({double(g.left(first)), double(g.bottom(r)), double(g.left(c)), double(g.bottom(r) + g.tile_h)});
    }
  }
  if (hot_tiles) {
    *hot_tiles = std::move(hot);
  }
  return out;
}
}  // namespace lr
}  // namespace livehd::placement
