// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "region_mesh.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
std::vector<double> merge_lines(std::vector<double> values, double low, double high, double gap) {
  std::erase_if(values, [&](double p) { return p <= low || p >= high; });
  values.push_back(low);
  values.push_back(high);
  std::sort(values.begin(), values.end());
  while (values.size() > 2) {
    size_t best = 0;
    for (size_t i = 1; i + 1 < values.size(); ++i) {
      if (values[i + 1] - values[i] < values[best + 1] - values[best]) {
        best = i;
      }
    }
    if (values[best + 1] - values[best] > gap) {
      break;
    }
    values[best] = best == 0 ? low : best + 2 == values.size() ? high : (values[best] + values[best + 1]) / 2;
    values.erase(values.begin() + best + 1);
  }
  return values;
}
}  // namespace

void validate_region_options(const Region_options& o) {
  if (o.cut != "macro" && o.cut != "mincut" && o.cut != "ratio" && o.cut != "ratio_through") {
    throw std::invalid_argument("region cut must be macro, mincut, ratio or ratio_through");
  }
  if (!o.max_cells || o.max_cells > 1000000 || !std::isfinite(o.merge_um) || o.merge_um < 0) {
    throw std::invalid_argument("invalid region cell cap/merge distance");
  }
}

std::optional<Bus_bit> indexed_bus_bit(const std::string& source) {
  std::string name;
  for (size_t i = 0; i < source.size(); ++i) {
    if (source[i] == '\\' && i + 1 < source.size()) {
      ++i;
    }
    name.push_back(source[i]);
  }
  if (name.empty() || name.back() != ']') {
    return {};
  }
  const auto left = name.rfind('[');
  if (left == std::string::npos || left == 0 || left + 2 >= name.size()) {
    return {};
  }
  int        bit    = -1;
  const auto result = std::from_chars(name.data() + left + 1, name.data() + name.size() - 1, bit);
  if (result.ec != std::errc{} || result.ptr != name.data() + name.size() - 1 || bit < 0) {
    return {};
  }
  return Bus_bit{name.substr(0, left), bit};
}

std::vector<Region_edge> region_edges(const std::vector<Region_rect>& mesh) {
  std::vector<Region_edge> edges;
  using Span = std::pair<double, double>;
  std::map<std::pair<int, double>, std::set<Span>> boundaries;
  for (const auto& r : mesh) {
    for (double x : {r.xl, r.xh}) {
      boundaries[{0, x}].emplace(r.yl, r.yh);
    }
    for (double y : {r.yl, r.yh}) {
      boundaries[{1, y}].emplace(r.xl, r.xh);
    }
  }
  for (const auto& [line, spans] : boundaries) {
    std::set<double> ends;
    for (auto [lo, hi] : spans) {
      ends.insert(lo);
      ends.insert(hi);
    }
    for (auto it = ends.begin(); it != ends.end();) {
      const double lo = *it++;
      if (it == ends.end()) {
        break;
      }
      const double hi = *it;
      if (std::any_of(spans.begin(), spans.end(), [&](auto s) { return s.first <= lo && hi <= s.second; })) {
        edges.push_back({line.first, line.second, lo, hi});
      }
    }
  }
  return edges;
}

std::optional<int> Region_mesh::rect_of(double x, double y) const {
  for (size_t i = 0; i < rects.size(); ++i) {
    const auto& r = rects[i];
    if (r.xl <= x && x < r.xh && r.yl <= y && y < r.yh) {
      return int(i);
    }
  }
  return std::nullopt;
}

Region_mesh Region_mesh::from_rects(std::vector<Region_rect> rects) {
  Region_mesh mesh;
  mesh.rects = std::move(rects);
  mesh.edges = region_edges(mesh.rects);
  return mesh;
}

Region_mesh Region_mesh::build(const dp::Design& d, const std::vector<int>& macros, const std::vector<Region_io>& ios,
                               const Region_options& options, double dbu) {
  using Clock      = std::chrono::steady_clock;
  const auto start = Clock::now();
  validate_region_options(options);
  d.validate();
  if (!(std::isfinite(dbu) && dbu > 0)) {
    throw std::invalid_argument("invalid region mesh units");
  }
  for (int i = 0; i < d.num_movable; ++i) {
    if (!d.node_macro.empty() && d.node_macro[i]) {
      throw std::invalid_argument("region mesh requires fixed macros");
    }
  }
  Region_mesh result;
  auto&       mesh      = result.rects;
  auto&       members   = result.members;
  result.options        = options;
  const auto&         l = d.layout;
  const bool          ratio   = options.cut == "ratio" || options.cut == "ratio_through";
  const bool          through = options.cut == "ratio_through";  // the nets crossing a child end to end load its cuts too
  const bool          mincut  = options.cut == "mincut" || ratio;  // no forced line in either
  std::vector<double> raw[2];
  for (int i : macros) {
    if (i < d.num_movable || i >= int(d.node_x.size())) {
      throw std::invalid_argument("invalid region mesh fixed macro index");
    }
    if (!mincut) {
      raw[0].insert(raw[0].end(), {d.node_x[i], d.node_x[i] + d.node_size_x[i]});
      raw[1].insert(raw[1].end(), {d.node_y[i], d.node_y[i] + d.node_size_y[i]});
    }
  }
  std::map<std::pair<std::string, int>, std::pair<double, double>> buses;
  for (const auto& io : ios) {
    const double distances[] = {std::abs(io.x - l.xl), std::abs(io.x - l.xh), std::abs(io.y - l.yl), std::abs(io.y - l.yh)};
    const int    side        = int(std::min_element(std::begin(distances), std::end(distances)) - std::begin(distances));
    const int    axis        = side < 2 ? 1 : 0;
    const double p           = axis == 0 ? io.x : io.y;
    const auto   bit         = indexed_bus_bit(io.name);
    auto [it, added]         = buses.emplace(std::pair{bit ? bit->bus : io.name, side}, std::pair{p, p});
    if (!added) {
      it->second.first  = std::min(it->second.first, p);
      it->second.second = std::max(it->second.second, p);
    }
  }
  for (const auto& [key, span] : buses) {
    const int axis = key.second < 2 ? 1 : 0;
    if (!mincut) {
      raw[axis].insert(raw[axis].end(), {span.first, span.second});
    }
  }
  const auto                    xs = merge_lines(raw[0], l.xl, l.xh, options.merge_um * dbu);
  const auto                    ys = merge_lines(raw[1], l.yl, l.yh, options.merge_um * dbu);
  const size_t                  nx = xs.size() - 1, ny = ys.size() - 1;
  std::vector<std::vector<int>> initial(nx * ny);
  for (int i = 0; i < d.num_movable; ++i) {
    const double x = d.node_x[i] + d.node_size_x[i] / 2, y = d.node_y[i] + d.node_size_y[i] / 2;
    const size_t ix = std::clamp<std::ptrdiff_t>(std::upper_bound(xs.begin(), xs.end(), x) - xs.begin() - 1, 0, nx - 1);
    const size_t iy = std::clamp<std::ptrdiff_t>(std::upper_bound(ys.begin(), ys.end(), y) - ys.begin() - 1, 0, ny - 1);
    initial[iy * nx + ix].push_back(i);
  }
  struct Span_g {
    double xlo, xhi, ylo, yhi, w;
  };
  std::vector<Span_g>              gspan;  // ratio cuts: every net's span over the chip, built on first use
  std::set<std::pair<int, double>> candidates;
  for (size_t i = 1; i + 1 < xs.size(); ++i) {
    candidates.emplace(0, xs[i]);
  }
  for (size_t i = 1; i + 1 < ys.size(); ++i) {
    candidates.emplace(1, ys[i]);
  }
  const auto refine = [&](auto&& self, Region_rect box, std::vector<int> cells, unsigned depth) -> void {
    if (cells.size() <= options.max_cells) {
      box.cells = cells.size();
      mesh.push_back(box);
      members.push_back(std::move(cells));
      return;
    }
    if (depth >= 48) {
      throw std::runtime_error("region mesh cannot split coincident cell centers below the cell cap");
    }
    bool             x   = box.xh - box.xl >= box.yh - box.yl;
    double           mid = x ? (box.xl + box.xh) / 2 : (box.yl + box.yh) / 2;
    if (ratio && !d.flat_net2pin_start.empty()) {
      // both axes, several positions: the split whose children have the lowest worst internal cut ratio. A child's
      // internal cut ratio along an axis is the most nets (cells on both sides, weighted) any line inside it along
      // that axis is forced across, over the tracks across that line (its length times the tracks per micron)
      const double h_tracks = options.h_tracks_per_um > 0 ? options.h_tracks_per_um : 1;
      const double v_tracks = options.v_tracks_per_um > 0 ? options.v_tracks_per_um : 1;
      const auto   pos_x    = [&](int i) { return d.node_x[i] + d.node_size_x[i] / 2; };
      const auto   pos_y    = [&](int i) { return d.node_y[i] + d.node_size_y[i] / 2; };
      std::vector<int> stamp(d.flat_net2pin_start.size(), 0);
      int              gen = 0;
      using Span = Span_g;
      // every net's span over all its cells (the chip), once: a net with cells beyond a child on both sides crosses
      // every cut inside the child, whether or not it has a cell there (the pass-throughs a middle column carries)
      if (gspan.empty()) {
        gspan.assign(d.flat_net2pin_start.size() - 1, Span_g{INFINITY, -INFINITY, INFINITY, -INFINITY, 0});
        for (size_t n = 0; n + 1 < d.flat_net2pin_start.size(); ++n) {
          if (!d.net_mask.empty() && !d.net_mask[n]) {
            continue;
          }
          int cnt = 0;
          for (int q = d.flat_net2pin_start[n]; q < d.flat_net2pin_start[n + 1]; ++q) {
            const int node = d.pin2node[size_t(d.flat_net2pin[size_t(q)])];
            if (node >= 0 && node < d.num_movable) {
              auto& sp = gspan[n];
              sp.xlo   = std::min(sp.xlo, pos_x(node));
              sp.xhi   = std::max(sp.xhi, pos_x(node));
              sp.ylo   = std::min(sp.ylo, pos_y(node));
              sp.yhi   = std::max(sp.yhi, pos_y(node));
              ++cnt;
            }
          }
          gspan[n].w = cnt >= 2 ? (d.net_weights.empty() ? 1.0 : std::max(1.0, d.net_weights[n])) : 0;
        }
      }
      std::vector<char> member(size_t(d.num_movable), 0);
      // the spans inside a child box b: a net touching the child's cells spans its chip span clipped to the box (its
      // cells beyond the box extend it to the box's edge); a net crossing the box with no cell in it spans it whole
      const auto spans_of = [&](const std::vector<int>& cs, const Region_rect& b) {
        std::vector<Span> out;
        ++gen;
        for (int i : cs) {
          member[size_t(i)] = 1;
        }
        const auto clipped = [&](const Span& g0) {
          return Span{std::max(g0.xlo, b.xl), std::min(g0.xhi, b.xh), std::max(g0.ylo, b.yl), std::min(g0.yhi, b.yh), g0.w};
        };
        for (int i : cs) {
          for (int k = d.flat_node2pin_start[size_t(i)]; k < d.flat_node2pin_start[size_t(i) + 1]; ++k) {
            const int n = d.pin2net[size_t(d.flat_node2pin[size_t(k)])];
            if (n < 0 || stamp[size_t(n)] == gen || gspan[size_t(n)].w <= 0) {
              continue;
            }
            stamp[size_t(n)] = gen;
            out.push_back(clipped(gspan[size_t(n)]));
          }
        }
        for (size_t n = 0; through && n < gspan.size(); ++n) {
          const auto& g0 = gspan[n];
          if (stamp[n] == gen || g0.w <= 0) {
            continue;
          }
          const bool across_x = g0.xlo < b.xl && g0.xhi > b.xh && g0.ylo < b.yh && g0.yhi > b.yl;
          const bool across_y = g0.ylo < b.yl && g0.yhi > b.yh && g0.xlo < b.xh && g0.xhi > b.xl;
          if (across_x || across_y) {
            out.push_back(clipped(g0));
          }
        }
        for (int i : cs) {
          member[size_t(i)] = 0;
        }
        return out;
      };
      // the most weighted spans open at any point along an axis (a sweep), over the tracks across a line there
      const auto worst = [&](const std::vector<Span>& sp, bool along_x, double across_um) {
        std::vector<std::pair<double, double>> ev;
        for (const auto& s : sp) {
          const double lo = along_x ? s.xlo : s.ylo, hi = along_x ? s.xhi : s.yhi;
          if (hi > lo) {
            ev.emplace_back(lo, s.w);
            ev.emplace_back(hi, -s.w);
          }
        }
        std::sort(ev.begin(), ev.end());
        double open = 0, peak = 0;
        for (const auto& [p, w] : ev) {
          open += w;
          peak  = std::max(peak, open);
        }
        return peak / std::max(1e-9, across_um * (along_x ? h_tracks : v_tracks));
      };
      const auto child_ratio = [&](const std::vector<int>& cs, const Region_rect& b) {
        const auto sp = spans_of(cs, b);
        return std::max(worst(sp, true, (b.yh - b.yl) / dbu), worst(sp, false, (b.xh - b.xl) / dbu));
      };
      double best = INFINITY;
      for (const bool ax : {true, false}) {
        std::vector<double> ps;
        for (int i : cells) {
          ps.push_back(ax ? pos_x(i) : pos_y(i));
        }
        std::sort(ps.begin(), ps.end());
        const double lo_side = ax ? box.xl : box.yl, hi_side = ax ? box.xh : box.yh;
        for (int q = 1; q < 12; ++q) {
          const size_t k = std::min(ps.size() - 2, size_t(q) * ps.size() / 12);
          if (ps[k] == ps[k + 1]) {
            continue;
          }
          const double p = (ps[k] + ps[k + 1]) / 2;
          if (p < lo_side + 0.25 * (hi_side - lo_side) || p > lo_side + 0.75 * (hi_side - lo_side)) {
            continue;
          }
          std::vector<int> low, high;
          double           area_low = 0, area_high = 0;
          for (int i : cells) {
            const bool is_low = (ax ? pos_x(i) : pos_y(i)) < p;
            (is_low ? low : high).push_back(i);
            (is_low ? area_low : area_high) += d.node_size_x[i] * d.node_size_y[i];
          }
          if (low.empty() || high.empty()) {
            continue;
          }
          auto a = box, b = box;
          (ax ? a.xh : a.yh) = p;
          (ax ? b.xl : b.yl) = p;
          // a child must hold its cells: a split that leaves one side fuller than nine tenths of its area is no split
          const auto area_of = [](const Region_rect& rc) { return std::max(1e-9, (rc.xh - rc.xl) * (rc.yh - rc.yl)); };
          if (area_low / area_of(a) > .9 || area_high / area_of(b) > .9) {
            continue;
          }
          // the split line's own load: the weighted nets crossing it inside the box (pass-throughs included) over the
          // tracks across it; both children carry those as hops on this boundary
          double crossing = 0;
          for (const auto& g0 : gspan) {
            if (g0.w <= 0) {
              continue;
            }
            const double lo = ax ? g0.xlo : g0.ylo, hi = ax ? g0.xhi : g0.yhi;
            const double olo = ax ? g0.ylo : g0.xlo, ohi = ax ? g0.yhi : g0.xhi;
            const double blo = ax ? box.yl : box.xl, bhi = ax ? box.yh : box.xh;
            if (lo < p && hi > p && olo < bhi && ohi > blo) {
              crossing += g0.w;
            }
          }
          const double split_len   = (ax ? box.yh - box.yl : box.xh - box.xl) / dbu;
          const double split_ratio = crossing / std::max(1e-9, split_len * (ax ? h_tracks : v_tracks));
          const double score = std::max({child_ratio(low, a), child_ratio(high, b), split_ratio})
                               + 1e-6 * std::abs(p - (lo_side + hi_side) / 2) / std::max(1e-9, hi_side - lo_side);
          if (score < best) {
            best = score;
            x    = ax;
            mid  = p;
          }
        }
      }
    } else if (mincut && !d.flat_net2pin_start.empty()) {
      // the cut that the fewest nets cross, within the middle half of the side: a net's cells inside the box span
      // [lo, hi] along the axis; it crosses a cut at p iff lo < p <= hi. The crossings over p come from a sweep of
      // those spans, sampled at the cells' positions (a cut lands between two cells)
      std::vector<char> in_box(size_t(d.num_movable), 0);
      for (int i : cells) {
        in_box[size_t(i)] = 1;
      }
      const auto pos = [&](int i) { return x ? d.node_x[i] + d.node_size_x[i] / 2 : d.node_y[i] + d.node_size_y[i] / 2; };
      std::vector<std::pair<double, double>> spans;  // (lo, hi) per net with two or more cells in the box, weighted below
      std::vector<double>                    weights;
      {
        std::vector<int> seen(d.flat_net2pin_start.size(), 0);
        for (int i : cells) {
          for (int k = d.flat_node2pin_start[size_t(i)]; k < d.flat_node2pin_start[size_t(i) + 1]; ++k) {
            const int n = d.pin2net[size_t(d.flat_node2pin[size_t(k)])];
            if (n < 0 || seen[size_t(n)] || (!d.net_mask.empty() && !d.net_mask[size_t(n)])) {
              continue;
            }
            seen[size_t(n)] = 1;
            double lo = INFINITY, hi = -INFINITY;
            int    inside = 0;
            for (int q = d.flat_net2pin_start[size_t(n)]; q < d.flat_net2pin_start[size_t(n) + 1]; ++q) {
              const int node = d.pin2node[size_t(d.flat_net2pin[size_t(q)])];
              if (node >= 0 && node < d.num_movable && in_box[size_t(node)]) {
                const double p = pos(node);
                lo             = std::min(lo, p);
                hi             = std::max(hi, p);
                ++inside;
              }
            }
            if (inside >= 2 && hi > lo) {
              spans.emplace_back(lo, hi);
              weights.push_back(d.net_weights.empty() ? 1.0 : std::max(1.0, d.net_weights[size_t(n)]));
            }
          }
        }
      }
      // candidate cuts: between consecutive cell positions inside the middle half of the side
      std::vector<double> ps;
      for (int i : cells) {
        ps.push_back(pos(i));
      }
      std::sort(ps.begin(), ps.end());
      const double lo_side = x ? box.xl : box.yl, hi_side = x ? box.xh : box.yh;
      const double a = lo_side + 0.25 * (hi_side - lo_side), b = lo_side + 0.75 * (hi_side - lo_side);
      std::vector<std::tuple<double, double, int>> events;  // (position, weight, +1 open / -1 close)
      for (size_t k = 0; k < spans.size(); ++k) {
        events.emplace_back(spans[k].first, weights[k], 1);
        events.emplace_back(spans[k].second, weights[k], -1);
      }
      std::sort(events.begin(), events.end());
      double best = INFINITY, open = 0;
      size_t e    = 0;
      for (size_t k = 0; k + 1 < ps.size(); ++k) {
        if (ps[k] == ps[k + 1]) {
          continue;
        }
        const double p = (ps[k] + ps[k + 1]) / 2;  // the cut between two cells: lo < p <= hi counts
        if (p < a || p > b) {
          continue;
        }
        while (e < events.size() && std::get<0>(events[e]) < p) {
          open += std::get<2>(events[e]) * std::get<1>(events[e]);
          ++e;
        }
        // the spans closed exactly at p stay crossed (lo < p <= hi): open counts them until the sweep passes hi
        const double cost = open + 1e-3 * std::abs(p - (lo_side + hi_side) / 2) / std::max(1e-9, hi_side - lo_side);
        if (cost < best) {
          best = cost;
          mid  = p;
        }
      }
    }
    std::vector<int> low, high;
    for (int i : cells) {
      const double p = x ? d.node_x[i] + d.node_size_x[i] / 2 : d.node_y[i] + d.node_size_y[i] / 2;
      (p < mid ? low : high).push_back(i);
    }
    auto a = box, b = box;
    if (x) {
      a.xh = mid;
      b.xl = mid;
    } else {
      a.yh = mid;
      b.yl = mid;
    }
    candidates.emplace(x ? 0 : 1, mid);
    self(self, a, std::move(low), depth + 1);
    self(self, b, std::move(high), depth + 1);
  };
  for (size_t y = 0; y < ny; ++y) {
    for (size_t x = 0; x < nx; ++x) {
      refine(refine, {xs[x], ys[y], xs[x + 1], ys[y + 1], 0}, std::move(initial[y * nx + x]), 0);
    }
  }
  result.initial_rectangles = mesh.size();
  // Rank distances ONCE using the original mesh, then alternate H / V.
  std::vector<std::tuple<double, double, double>> ranked[2];
  for (int axis : {0, 1}) {
    std::vector<double> positions{axis == 0 ? l.xl : l.yl, axis == 0 ? l.xh : l.yh};
    for (auto [a, p] : candidates) {
      if (a == axis) {
        positions.push_back(p);
      }
    }
    std::sort(positions.begin(), positions.end());
    for (size_t i = 1; i + 1 < positions.size(); ++i) {
      const double p = positions[i], gap = std::min(p - positions[i - 1], positions[i + 1] - p);
      ranked[axis].emplace_back(gap, axis == 1 ? -p : p, p);
    }
    std::sort(ranked[axis].begin(), ranked[axis].end());
  }
  for (size_t k = 0; k < std::max(ranked[0].size(), ranked[1].size()); ++k) {
    for (int axis : {1, 0}) {
      if (k >= ranked[axis].size()) {
        continue;
      }
      const double                                p = std::get<2>(ranked[axis][k]);
      std::map<std::pair<double, double>, size_t> low, high;
      for (size_t i = 0; i < mesh.size(); ++i) {
        const auto& r    = mesh[i];
        const auto  span = axis == 0 ? std::pair{r.yl, r.yh} : std::pair{r.xl, r.xh};
        if ((axis == 0 ? r.xh : r.yh) == p) {
          low.emplace(span, i);
        }
        if ((axis == 0 ? r.xl : r.yl) == p) {
          high.emplace(span, i);
        }
      }
      if (low.empty() || low.size() != high.size()) {
        continue;
      }
      bool valid = true;
      for (const auto& [span, i] : low) {
        const auto found = high.find(span);
        if (found == high.end() || mesh[i].cells + mesh[found->second].cells > options.max_cells) {
          valid = false;
          break;
        }
      }
      if (!valid) {
        continue;
      }
      std::vector<bool>             consumed(mesh.size());
      std::vector<Region_rect>      next;
      std::vector<std::vector<int>> next_members;
      for (const auto& [span, i] : low) {
        const auto j  = high.at(span);
        auto       r  = mesh[i];
        r.cells      += mesh[j].cells;
        if (axis == 0) {
          r.xh = mesh[j].xh;
        } else {
          r.yh = mesh[j].yh;
        }
        consumed[i] = consumed[j] = true;
        next.push_back(r);
        next_members.push_back(std::move(members[i]));
        next_members.back().insert(next_members.back().end(), members[j].begin(), members[j].end());
      }
      for (size_t i = 0; i < mesh.size(); ++i) {
        if (!consumed[i]) {
          next.push_back(mesh[i]);
          next_members.push_back(std::move(members[i]));
        }
      }
      mesh    = std::move(next);
      members = std::move(next_members);
      ++(axis == 0 ? result.removed_v : result.removed_h);
    }
  }
  uint64_t total = 0;
  for (size_t i = 0; i < mesh.size(); ++i) {
    if (mesh[i].cells > options.max_cells || mesh[i].cells != members[i].size()) {
      throw std::runtime_error("region mesh cell cap violated");
    }
    std::sort(members[i].begin(), members[i].end());
    total += mesh[i].cells;
  }
  if (total != unsigned(d.num_movable)) {
    throw std::runtime_error("region mesh cell count mismatch");
  }
  result.edges         = region_edges(mesh);
  result.build_seconds = std::chrono::duration<double>(Clock::now() - start).count();
  return result;
}

void Region_mesh::write(const std::string& dir) const {
  if (dir.empty()) {
    return;
  }
  std::filesystem::create_directories(dir);
  rapidjson::StringBuffer                    buffer;
  rapidjson::Writer<rapidjson::StringBuffer> json(buffer);
  json.StartObject();
  json.Key("max_cells");
  json.Uint(options.max_cells);
  json.Key("merge_um");
  json.Double(options.merge_um);
  json.Key("initial_rectangles");
  json.Uint(initial_rectangles);
  json.Key("removed_horizontal");
  json.Uint(removed_h);
  json.Key("removed_vertical");
  json.Uint(removed_v);
  json.Key("build_seconds");
  json.Double(build_seconds);
  json.Key("edges");
  json.Uint64(edges.size());
  json.Key("rectangles");
  json.StartArray();
  for (const auto& r : rects) {
    json.StartObject();
    json.Key("bounds_dbu");
    json.StartArray();
    for (double v : {r.xl, r.yl, r.xh, r.yh}) {
      json.Double(v);
    }
    json.EndArray();
    json.Key("initial_cells");
    json.Uint(r.cells);
    json.EndObject();
  }
  json.EndArray();
  json.EndObject();
  std::ofstream file(dir + "/region_mesh.json");
  file << buffer.GetString() << '\n';
  if (!file) {
    throw std::runtime_error("cannot write region mesh report");
  }
}
}  // namespace livehd::placement
