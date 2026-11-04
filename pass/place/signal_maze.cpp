// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <tuple>

#include "core/debug_env.hpp"
#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "pin_escape_guard.hpp"
#include "signal_router.hpp"
#include "steiner_topology.hpp"

namespace livehd::placement {
namespace ph = physical;
namespace {
// Developer diagnostic, sampled once per invocation through the process-cached
// environment accessor. One exit line per request, never per-node logging.
// LHD_SIGNAL_MAZE_TRACE=1 explains which work consumes the shared pattern budget;
// it does not change that budget, routing costs, or any physical acceptance check.
// charged = expanded + nondefault_via_trials + junction_trials. The first
// intrinsic-invalid nondefault pair is charged; later invalid cache hits are
// uncharged. A nominal-width pair never adds a nondefault-via charge, including
// its first intrinsic check. Thus invalid hits need not equal work saved.
struct Maze_trace {
  bool                                  enabled;
  const std::string&                    net;
  const char*                           mode;
  const Signal_route_trial&             out;
  size_t                                limit;
  int                                   exceptions = std::uncaught_exceptions();
  std::chrono::steady_clock::time_point start      = std::chrono::steady_clock::now();
  const char*                           budget     = "none";
  std::string                           reason{}, layers{};
  double                                xmin = 0, ymin = 0, xmax = 0, ymax = 0;
  size_t                                nx = 0, ny = 0, branches = 0, started = 0, completed = 0;
  size_t                                sources = 0, targets = 0, pops = 0, expanded = 0, pushes = 0, reached_peak = 0;
  size_t                                via_trials = 0, nondefault_via_trials = 0, via_cache_hits = 0, junction_trials = 0;
  size_t                                nominal_overlap_sites = 0;
  size_t intrinsic_checks = 0, intrinsic_invalid_pairs = 0, intrinsic_invalid_hits = 0, intrinsic_valid_hits = 0;
  size_t self_cut_checks = 0, self_cut_rejections = 0;
  std::map<std::string, size_t> via_failures{};
  ~Maze_trace() {
    if (!enabled) {
      return;
    }
    try {
      const auto         why = std::uncaught_exceptions() > exceptions ? "exception"
                               : !reason.empty()                       ? reason.c_str()
                               : out.timed_out                         ? "route deadline"
                               : out.geometry                          ? "complete"
                                                                       : "no geometry";
      std::ostringstream line;
      line << "signal_maze_trace net=" << std::quoted(net) << " mode=" << mode << " reason=" << std::quoted(why)
           << " window=" << xmin << ',' << ymin << ',' << xmax << ',' << ymax << " grid=" << nx << ',' << ny
           << " layers=" << std::quoted(layers) << " branches=" << branches << " started=" << started << " completed=" << completed
           << " sources=" << sources << " targets=" << targets << " pops=" << pops << " expanded=" << expanded
           << " pushes=" << pushes << " reached_peak=" << reached_peak << " via_trials=" << via_trials
           << " nondefault_via_trials=" << nondefault_via_trials << " via_cache_hits=" << via_cache_hits
           << " nominal_overlap_sites=" << nominal_overlap_sites << " intrinsic_checks=" << intrinsic_checks
           << " intrinsic_invalid_pairs=" << intrinsic_invalid_pairs << " intrinsic_invalid_hits=" << intrinsic_invalid_hits
           << " intrinsic_valid_hits=" << intrinsic_valid_hits << " junction_trials=" << junction_trials
           << " self_cut_checks=" << self_cut_checks << " self_cut_rejections=" << self_cut_rejections
           << " prior_cut_rejections=" << out.prior_cut_rejections << " charged=" << out.patterns_examined << " limit=" << limit
           << " access=" << out.access_examined << " budget=" << budget
           << " seconds=" << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
      std::ostringstream failures;
      for (const auto& [message, count] : via_failures) {
        failures << count << ':' << message << '|';
      }
      line << " via_failures=" << std::quoted(failures.str());
      std::fprintf(stderr, "%s\n", line.str().c_str());
    } catch (...) {
      // Diagnostics must never alter a route result or mask its exception.
    }
  }
};
}  // namespace
Signal_route_trial Signal_router::propose_maze(const ph::Net& net, const Signal_route_limits& limits,
                                               std::span<const Signal_route_via> prior) const {
  return propose_maze_impl(net, limits, nullptr, nullptr, nullptr, prior);
}
Signal_route_trial Signal_router::propose_tap(const ph::Net& net, const Signal_route_limits& limits,
                                              const std::vector<ph::Shape>&        anchors,
                                              const std::map<std::string, double>& layer_cost,
                                              std::span<const Signal_route_via>    prior) const {
  return propose_maze_impl(net, limits, nullptr, &anchors, &layer_cost, prior);
}
Signal_route_trial Signal_router::propose_join(const ph::Net& net, const Signal_route_limits& limits,
                                               const std::vector<ph::Shape>& sources, const std::vector<ph::Shape>& anchors,
                                               const std::map<std::string, double>& layer_cost,
                                               std::span<const Signal_route_via>    prior) const {
  return propose_maze_impl(net, limits, &sources, &anchors, &layer_cost, prior);
}
Signal_route_trial Signal_router::propose_maze_impl(const ph::Net& net, const Signal_route_limits& limits,
                                                    const std::vector<ph::Shape>* sources, const std::vector<ph::Shape>* anchors,
                                                    const std::map<std::string, double>* layer_cost,
                                                    std::span<const Signal_route_via>    prior) const {
  Signal_route_trial out;
  Maze_trace         trace{livehd::debug::env<"LHD_SIGNAL_MAZE_TRACE">() != nullptr,
                           net.name,
                           sources   ? "join"
                           : anchors ? "tap"
                                     : "net",
                           out,
                           limits.patterns};
  const Stage_trial  stage{out, limits.deadline};
  const auto         fail = [&](std::string why) {
    if (trace.enabled) {
      trace.reason = why;
    }
    return stage.fail(std::move(why));
  };
  if (stage.expired()) {
    return out;
  }
  if (sources) {
    if (sources->empty() || !anchors || anchors->empty() || !net.connections.empty() || !net.wires.empty()) {
      return fail("join requires its net's source and anchor metal and no pins");
    }
  } else if (anchors) {
    if (anchors->empty() || net.connections.size() != 1 || !net.wires.empty() || (!net.use.empty() && net.use != "SIGNAL")) {
      return fail("tap requires one unwired signal pin and its net's anchors");
    }
  } else if (!ph::unrouted_signal(net)) {
    return fail("maze requires an unwired multi-pin signal net");
  }
  std::set<std::pair<std::string, std::string>> endpoint_ids;
  for (const auto& pin : net.connections) {
    if (!endpoint_ids.emplace(pin.instance, pin.pin).second) {
      return fail("duplicate endpoint");
    }
  }
  if (!limits.patterns) {
    return fail("maze budget");
  }
  if (profile_.signal_layers.empty() || profile_.signal_layers.front() != profile_.access.escape) {
    return fail("maze profile must start at the access escape layer");
  }
  std::vector<std::string> layers;
  std::vector<double>      widths, half;
  std::vector<bool>        horizontal;
  const auto               grid_dbu = ph::explicit_manufacturing_grid_dbu(tech_, frame_.dbu);
  if (!grid_dbu) {
    return fail("unsupported manufacturing grid");
  }
  const double grid = *grid_dbu;
  // Only layers with modeled rules are usable. An unsupported upper layer
  // cannot invalidate a legal lower-layer route, and is never approximated.
  for (const auto& name : profile_.signal_layers) {
    if (!rules_.clearance(name)) {
      break;
    }
    const auto&  layer   = tech_.layers.at(name);
    const auto   nominal = ph::wire_width_dbu(layer, frame_.dbu);
    const double area    = layer.rules.area * frame_.dbu * frame_.dbu;
    if (!nominal || !std::isfinite(area) || area < 0 || (layer.direction != "HORIZONTAL" && layer.direction != "VERTICAL")) {
      break;
    }
    const double width         = *nominal;
    const bool   is_horizontal = layer.direction == "HORIZONTAL";
    if ((!horizontal.empty() && horizontal.back() == is_horizontal) || (horizontal.empty() && !is_horizontal)) {
      break;
    }
    layers.push_back(name);
    widths.push_back(width);
    horizontal.push_back(is_horizontal);
    half.push_back(ph::min_area_half_length(width, area, grid));
  }
  if (layers.size() < 2) {
    return fail("unsupported maze layers");
  }
  std::vector<std::string>           connecting_vias;
  std::map<std::string, std::string> via_layers;
  for (size_t lower = 0; lower + 1 < layers.size(); ++lower) {
    std::string selected;
    for (const auto& [name, definition] : tech_.vias) {
      if (stage.expired()) {
        return out;
      }
      if (!definition.unsupported.empty() || definition.shapes.size() != 3) {
        continue;
      }
      const auto                      shapes = ph::via_geometry(tech_, frame_, {name});
      std::array<const ph::Shape*, 2> pads{};
      bool                            has_cut = false;
      for (const auto& shape : shapes) {
        for (size_t side = 0; side < 2; ++side) {
          if (shape.layer == layers[lower + side]) {
            pads[side] = &shape;
          }
        }
        has_cut |= tech_.cut_layers.contains(shape.layer);
      }
      if (!pads[0] || !pads[1] || !has_cut) {
        continue;
      }
      bool matches = true;
      for (size_t side = 0; side < 2; ++side) {
        const auto  l = lower + side;
        const auto& r = pads[side]->rect;
        matches
            &= horizontal[l] ? r.yl == -widths[l] / 2 && r.yh == widths[l] / 2 : r.xl == -widths[l] / 2 && r.xh == widths[l] / 2;
      }
      if (!matches) {
        continue;
      }
      for (size_t side = 0; side < 2; ++side) {
        const auto  l = lower + side;
        const auto& r = pads[side]->rect;
        half[l]       = std::max({half[l], horizontal[l] ? -r.xl : -r.yl, horizontal[l] ? r.xh : r.yh});
      }
      selected = name;
      break;
    }
    if (selected.empty()) {
      break;
    }
    connecting_vias.push_back(selected);
    via_layers.emplace(selected, layers[lower]);
  }
  // A missing adjacent via terminates the reachable layer stack.
  layers.resize(connecting_vias.size() + 1);
  widths.resize(layers.size());
  half.resize(layers.size());
  horizontal.resize(layers.size());
  if (layers.size() < 2) {
    return fail("no supported maze via");
  }
  if (trace.enabled) {
    for (const auto& name : layers) {
      trace.layers += (trace.layers.empty() ? "" : ",") + name;
    }
  }
  std::vector<ph::Pin_access_result> access(net.connections.size());
  double                             xmin = frame_.die.xh, xmax = frame_.die.xl, ymin = frame_.die.yh, ymax = frame_.die.yl;
  for (size_t i = 0; i < access.size(); ++i) {
    const auto cell = components_.find(net.connections[i].instance);
    if (cell == components_.end()) {
      return fail("non-cell endpoint");
    }
    if (out.access_examined >= limits.access_candidates) {
      return fail("pin-access budget");
    }
    access[i] = endpoint_access(*cell->second, net.connections[i].pin, net, limits, out);
    if (stage.expired()) {
      return out;
    }
    std::erase_if(access[i].points, [&](const auto& p) {
      return p.escape.layer != profile_.access.escape || p.escape.rect.yh - p.escape.rect.yl != widths[0];
    });
    if (access[i].points.empty()) {
      return fail("pin has no legal access");
    }
    for (const auto& p : access[i].points) {
      xmin    = std::min(xmin, double(p.x));
      xmax    = std::max(xmax, double(p.x));
      ymin    = std::min(ymin, double(p.y));
      ymax    = std::max(ymax, double(p.y));
      half[0] = std::max({half[0], p.x - p.escape.rect.xl, p.escape.rect.xh - p.x});
    }
  }
  // The reference point: the pin's first access point, or (a join) the source metal's point nearest to the anchors.
  // A tap's or join's window covers each piece's point nearest to it, not the whole (possibly long) piece.
  const auto maze_layer = [&](const ph::Shape& s) -> std::optional<size_t> {
    const auto found = std::find(layers.begin(), layers.end(), s.layer);
    return found == layers.end() ? std::nullopt : std::optional<size_t>(found - layers.begin());
  };
  // A piece offers landings only on its centreline inside the die (with a whole-die frame, every piece does).
  const auto in_window = [&](const ph::Shape& a) {
    const auto l = maze_layer(a);
    if (!l) {
      return false;
    }
    const auto&  r  = a.rect;
    const double c  = horizontal[*l] ? (r.yl + r.yh) / 2 : (r.xl + r.xh) / 2;
    return horizontal[*l] ? c >= frame_.die.yl && c <= frame_.die.yh && r.xh >= frame_.die.xl && r.xl <= frame_.die.xh
                          : c >= frame_.die.xl && c <= frame_.die.xh && r.yh >= frame_.die.yl && r.yl <= frame_.die.yh;
  };
  // The window views: in-window pieces trimmed along their own direction (centreline and width unchanged), built once
  // and used for the reference, the extent and the goals alike. Occupancy is untouched.
  const auto trim = [&](const std::vector<ph::Shape>& pieces) {
    std::vector<ph::Shape> view;
    for (const auto& a : pieces) {
      if (!in_window(a)) {
        continue;
      }
      auto t = a;
      if (horizontal[*maze_layer(a)]) {
        t.rect.xl = std::max(t.rect.xl, frame_.die.xl);
        t.rect.xh = std::min(t.rect.xh, frame_.die.xh);
      } else {
        t.rect.yl = std::max(t.rect.yl, frame_.die.yl);
        t.rect.yh = std::min(t.rect.yh, frame_.die.yh);
      }
      view.push_back(t);
    }
    return view;
  };
  const auto anchor_view = anchors ? trim(*anchors) : std::vector<ph::Shape>{};
  const auto source_view = sources ? trim(*sources) : std::vector<ph::Shape>{};
  double ref_x = 0, ref_y = 0;
  if (sources) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& s : source_view) {
      for (const auto& a : anchor_view) {
        const double ax = (a.rect.xl + a.rect.xh) / 2, ay = (a.rect.yl + a.rect.yh) / 2;
        const double sx = std::clamp(ax, s.rect.xl, s.rect.xh), sy = std::clamp(ay, s.rect.yl, s.rect.yh);
        const double d = std::max({a.rect.xl - sx, sx - a.rect.xh, 0.0}) + std::max({a.rect.yl - sy, sy - a.rect.yh, 0.0});
        if (d < best) {
          best  = d;
          ref_x = sx;
          ref_y = sy;
        }
      }
    }
    if (!std::isfinite(best)) {
      const auto on_layer = [&](const std::vector<ph::Shape>& v) {
        return std::ranges::any_of(v, [&](const ph::Shape& x) { return maze_layer(x).has_value(); });
      };
      // pieces on maze layers that all lie outside the window are a window failure, not a layer one
      return fail(on_layer(*sources) && on_layer(*anchors) ? "no anchor inside the maze window" : "no join piece on a maze layer");
    }
  } else if (anchors) {
    ref_x = double(access[0].points.front().x);
    ref_y = double(access[0].points.front().y);
  }
  std::vector<std::pair<size_t, ph::Rect>> anchor_metal, source_metal;  // (maze layer, metal)
  size_t                                   outside_window = 0;  // pieces with no landing inside frame_.die
  const auto                               collect = [&](const std::vector<ph::Shape>& pieces, auto& metal) {
    for (const auto& a : pieces) {
      const auto l = maze_layer(a);
      if (!l) {
        continue;
      }
      // A piece offers landings only on its centreline inside the die: drop one whose centreline or along-span lies
      // outside, and trim a kept one only along its direction (its centreline and width never move). With a whole-die
      // frame (Final) nothing changes.
      ph::Rect   r  = a.rect;
      const bool hz = horizontal[*l];
      if (!in_window(a)) {
        ++outside_window;
        continue;
      }
      if (hz) {
        r.xl = std::max(r.xl, frame_.die.xl);
        r.xh = std::min(r.xh, frame_.die.xh);
      } else {
        r.yl = std::max(r.yl, frame_.die.yl);
        r.yh = std::min(r.yh, frame_.die.yh);
      }
      metal.emplace_back(*l, r);
      const double nx = std::clamp(ref_x, r.xl, r.xh), ny = std::clamp(ref_y, r.yl, r.yh);
      xmin = std::min(xmin, nx);
      xmax = std::max(xmax, nx);
      ymin = std::min(ymin, ny);
      ymax = std::max(ymax, ny);
    }
  };
  if (anchors) {
    collect(anchor_view, anchor_metal);
    if (anchor_metal.empty()) {
      const bool any_on_layer = std::ranges::any_of(*anchors, [&](const ph::Shape& x) { return maze_layer(x).has_value(); });
      return fail(any_on_layer || outside_window ? "no anchor inside the maze window" : "no anchor on a maze layer");
    }
  }
  if (sources) {
    collect(source_view, source_metal);
  }
  double pitch = 0;
  for (const auto& t : frame_.tracks) {
    if (std::find(layers.begin(), layers.end(), t.layer) == layers.end()) {
      continue;
    }
    if (!t.supported()) {
      return fail("unsupported maze tracks");
    }
    pitch = std::max(pitch, double(t.step));
  }
  // One endpoint-box span of detour room, at least four track pitches. The
  // die, node budget and wall-clock deadline independently bound exploration.
  const double margin = std::max({xmax - xmin, ymax - ymin, 4 * pitch});
  xmin                = std::max(frame_.die.xl, xmin - margin);
  xmax                = std::min(frame_.die.xh, xmax + margin);
  ymin                = std::max(frame_.die.yl, ymin - margin);
  ymax                = std::min(frame_.die.yh, ymax + margin);
  trace.xmin          = xmin;
  trace.xmax          = xmax;
  trace.ymin          = ymin;
  trace.ymax          = ymax;
  std::vector<std::set<int64_t>> tracks(layers.size());
  std::set<int64_t>              xcoords, ycoords;
  for (const auto& t : frame_.tracks) {
    const auto found = std::find(layers.begin(), layers.end(), t.layer);
    if (found == layers.end()) {
      continue;
    }
    const auto l = found - layers.begin();
    if (t.axis != (horizontal[l] ? "Y" : "X")) {
      continue;
    }
    auto& coords             = tracks[l];
    const auto [first, last] = horizontal[l] ? t.index_range(ymin, ymax) : t.index_range(xmin, xmax);
    for (auto n = first; n <= last; ++n) {
      if (stage.expired()) {
        return out;
      }
      coords.insert(t.start + n * t.step);
      if (coords.size() > limits.patterns) {
        trace.budget = "coordinates";
        return fail("maze coordinate budget");
      }
    }
    auto& all = horizontal[l] ? ycoords : xcoords;
    all.insert(coords.begin(), coords.end());
    if (all.size() > limits.patterns) {
      trace.budget = "coordinates";
      return fail("maze coordinate budget");
    }
  }
  // A piece's centerline is a track of its layer, and its point nearest to the reference is always a node.
  for (const auto* metal : {&anchor_metal, &source_metal}) {
    for (const auto& [l, r] : *metal) {
      const auto c = std::llround(horizontal[l] ? (r.yl + r.yh) / 2 : (r.xl + r.xh) / 2);
      if (horizontal[l] ? c < ymin || c > ymax : c < xmin || c > xmax) {
        continue;
      }
      tracks[l].insert(c);
      (horizontal[l] ? ycoords : xcoords).insert(c);
      const auto along = std::llround(horizontal[l] ? std::clamp(ref_x, r.xl + half[l], std::max(r.xl + half[l], r.xh - half[l]))
                                                    : std::clamp(ref_y, r.yl + half[l], std::max(r.yl + half[l], r.yh - half[l])));
      // a key outside the search window is extra grid work only (legality rejects its metal): keep keys inside it
      if (horizontal[l] ? along >= xmin && along <= xmax : along >= ymin && along <= ymax) {
        (horizontal[l] ? xcoords : ycoords).insert(along);
      }
    }
  }
  if (tracks[0].empty() || tracks[1].empty()) {
    return fail("missing maze tracks");
  }
  for (const auto& a : access) {
    for (const auto& p : a.points) {
      xcoords.insert(p.x);
    }
  }
  const std::vector<int64_t> xs(xcoords.begin(), xcoords.end()), ys(ycoords.begin(), ycoords.end());
  trace.nx       = xs.size();
  trace.ny       = ys.size();
  using Key      = std::tuple<size_t, size_t, size_t>;  // x, y, layer
  const auto key = [&](const auto& p) {
    return Key{std::lower_bound(xs.begin(), xs.end(), p.x) - xs.begin(),
               std::lower_bound(ys.begin(), ys.end(), p.y) - ys.begin(),
               0};
  };
  const auto pad = [&](const Key& k) {
    const auto [ix, iy, l] = k;
    const double x = xs[ix], y = ys[iy];
    return ph::Shape{
        layers[l],
        horizontal[l] ? ph::Rect{      x - half[l], y - widths[l] / 2,       x + half[l], y + widths[l] / 2}
                      : ph::Rect{x - widths[l] / 2,       y - half[l], x + widths[l] / 2,       y + half[l]}
    };
  };
  const auto legal = [&](const ph::Shape& s) {
    if (!rules_.rectangle(s).legal || !frame_.die.contains(s.rect)) {
      return false;
    }
    for (auto id : occupancy_.query(s.layer, s.rect, *rules_.clearance(s.layer), net.name)) {
      if (stage.expired() || !rules_.spacing(s, occupancy_.at(id).shape).legal) {
        return false;
      }
    }
    return true;
  };
  const auto segment = [&](const Key& a, const Key& b) {
    auto s = pad(a);
    s.rect = s.rect.hull(pad(b).rect);
    return s;
  };
  using Via_site = std::tuple<int64_t, int64_t, size_t>;
  std::map<Via_site, std::optional<std::array<ph::Shape, 2>>> via_landings;
  std::vector<std::vector<int64_t>>                           landing_widths;
  for (const auto& layer : layers) {
    landing_widths.push_back(rules_.candidate_widths(layer));
  }
  struct Intrinsic_transition {
    bool                                        decoded = false;
    std::optional<std::vector<ph::Shape>>       shapes;
    std::map<std::pair<int64_t, int64_t>, bool> pairs;
  };
  // Via definition, ordered layers, DBU, direction and half-lengths are fixed
  // throughout this request. A width pair therefore uniquely identifies its
  // centered landing geometry. Never retain this cache across requests/frames.
  std::vector<Intrinsic_transition> intrinsic(layers.size() - 1);
  const auto                        turn_legal = [&](const Key& a, const Key& b) {
    const auto [ix, iy, l] = a;
    const size_t   lower   = std::min(l, std::get<2>(b));
    const Via_site site{xs[ix], ys[iy], lower};
    if (const auto found = via_landings.find(site); found != via_landings.end()) {
      ++trace.via_cache_hits;
      return bool(found->second);
    }
    auto& selected = via_landings[site];
    // Every width pair below contains these nominal pads: wa/wb cannot shrink
    // the width, and length=max(half,width/2) cannot shrink the length. A positive
    // intersection with foreign metal therefore makes *all* landing widths
    // impossible at this site. Do not use spacing/EOL/enclosure rejection here:
    // widening can change those rules and repair a nominal landing.
    for (const auto& minimum : {pad(a), pad(b)}) {
      for (auto id : occupancy_.query(minimum.layer, minimum.rect, *rules_.clearance(minimum.layer), net.name)) {
        const auto& obstacle = occupancy_.at(id).shape.rect;
        if (std::min(minimum.rect.xh, obstacle.xh) > std::max(minimum.rect.xl, obstacle.xl)
            && std::min(minimum.rect.yh, obstacle.yh) > std::max(minimum.rect.yl, obstacle.yl)) {
          ++trace.nominal_overlap_sites;
          return false;
        }
      }
    }
    for (const auto wa : landing_widths[lower]) {
      for (const auto wb : landing_widths[lower + 1]) {
        if (stage.expired()) {
          return false;
        }
        if (wa < widths[lower] || wb < widths[lower + 1] || wa % int64_t(2 * grid) || wb % int64_t(2 * grid)) {
          continue;
        }
        auto&      cache = intrinsic[lower];
        const auto pair  = std::pair{wa, wb};
        const auto known = cache.pairs.find(pair);
        if (known != cache.pairs.end() && !known->second) {
          ++trace.intrinsic_invalid_hits;
          continue;
        }
        if (known != cache.pairs.end()) {
          ++trace.intrinsic_valid_hits;
        }
        if (wa != widths[lower] || wb != widths[lower + 1]) {
          if (out.patterns_examined >= limits.patterns) {
            trace.budget = "nondefault_via";
            return false;
          }
          ++out.patterns_examined;
          ++trace.nondefault_via_trials;
        }
        ++trace.via_trials;
        std::array<ph::Shape, 2> candidate;
        for (size_t side = 0; side < 2; ++side) {
          const size_t layer  = lower + side;
          const double width  = side ? wb : wa;
          const double length = std::ceil(std::max(half[layer], width / 2) / grid) * grid;
          candidate[side]     = {
              layers[layer],
              horizontal[layer] ? ph::Rect{   -length, -width / 2,    length, width / 2}
                                : ph::Rect{-width / 2,    -length, width / 2,    length}
          };
          // The overlap shortcut above relies on this containment for every
          // candidate, including intrinsically invalid ones. Keep the assertion
          // tied to pad() so future changes to either geometry cannot silently
          // invalidate the pruning proof.
          assert(candidate[side].rect.translated(xs[ix], ys[iy]).contains(pad({ix, iy, layer}).rect));
        }
        if (!cache.decoded) {
          cache.decoded = true;
          try {
            cache.shapes = ph::via_geometry(tech_, frame_, {connecting_vias[lower]});
          } catch (const std::exception&) {
            // Retain the full checker's normal decoder failure classification;
            // an exception is not a reusable intrinsic validity result.
          }
        }
        if (known == cache.pairs.end() && cache.shapes) {
          const auto check = ph::check_signal_via_intrinsics(tech_, frame_.dbu, *cache.shapes, candidate);
          ++trace.intrinsic_checks;
          cache.pairs.emplace(pair, check.check.legal);
          if (!check.check.legal) {
            ++trace.intrinsic_invalid_pairs;
            if (trace.enabled) {
              ++trace.via_failures[check.check.reason];
            }
            continue;
          }
        }
        for (auto& shape : candidate) {
          shape.rect = shape.rect.translated(xs[ix], ys[iy]);
        }
        // Intrinsic validity is not site legality: retain the complete positioned
        // check, including DEF shadowing, tracks, die, cuts and foreign metal.
        const auto valid = ph::check_signal_via(tech_,
                                                frame_,
                                                rules_,
                                                occupancy_,
                                                {connecting_vias[lower]},
                                                xs[ix],
                                                ys[iy],
                                                candidate,
                                                net.name);
        if (valid.legal) {
          selected = candidate;
          return true;
        }
        if (trace.enabled) {
          ++trace.via_failures[valid.reason];
        }
      }
    }
    return false;
  };
  struct Branch_search {
    std::vector<Key> sources, targets;
  };
  std::vector<Branch_search> branches;
  Signal_route_geometry      trial;
  if (anchors) {
    // the nodes on a piece of metal: its centerline coordinate, every crossing coordinate inside it
    // geometric keys on the given metal, each once: overlapping rectangles of one piece do not make distinct
    // states (the source identity of a join start is its key)
    const auto on_metal = [&](const std::vector<std::pair<size_t, ph::Rect>>& metal, std::vector<Key>& keys) {
      std::set<Key> seen(keys.begin(), keys.end());
      for (const auto& [l, r] : metal) {
        const auto  c = std::llround(horizontal[l] ? (r.yl + r.yh) / 2 : (r.xl + r.xh) / 2);
        const auto& v = horizontal[l] ? ys : xs;
        const auto  i = std::lower_bound(v.begin(), v.end(), c) - v.begin();
        if (size_t(i) >= v.size() || v[i] != c) {
          continue;
        }
        const auto& w = horizontal[l] ? xs : ys;
        for (size_t j = 0; j < w.size(); ++j) {
          const double at = double(w[j]);
          if (horizontal[l] ? at >= r.xl && at <= r.xh : at >= r.yl && at <= r.yh) {
            const Key k = horizontal[l] ? Key{j, size_t(i), l} : Key{size_t(i), j, l};
            if (seen.insert(k).second) {
              keys.push_back(k);
            }
          }
        }
      }
    };
    branches.emplace_back();
    if (sources) {
      on_metal(source_metal, branches.back().sources);
    } else {
      for (const auto& p : access[0].points) {
        branches.back().sources.push_back(key(p));
      }
    }
    on_metal(anchor_metal, branches.back().targets);
    if (branches.back().targets.empty() || branches.back().sources.empty()) {
      return fail("piece outside the maze window");
    }
  } else if (access.size() == 2) {
    branches.emplace_back();
    for (const auto& p : access[0].points) {
      branches.back().sources.push_back(key(p));
    }
    for (const auto& p : access[1].points) {
      branches.back().targets.push_back(key(p));
    }
  } else {
    // Fix one legal access per pin before building the Steiner tree. Alternate
    // access combinations are not explored by this bounded multi-pin proposal.
    std::vector<Steiner_location> pins;
    for (const auto& a : access) {
      const auto& p = a.points.front();
      pins.emplace_back(p.x, p.y);
      trial.endpoints.push_back(p);
    }
    Steiner_topology topology;
    try {
      topology = steiner_topology(pins);
    } catch (const std::exception& e) {
      return fail(e.what());
    }
    if (stage.expired()) {
      return out;
    }
    std::vector<std::optional<Key>> vertices(topology.locations.size());
    for (size_t i = 0; i < pins.size(); ++i) {
      vertices[topology.terminals[i]] = key(access[i].points.front());
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
      if (vertices[i]) {
        continue;
      }
      // Snap Steiner junctions to escape/branch intersections, trying closest legal
      // landings first. Junction trials consume the same budget as expansions.
      const auto [x, y] = topology.locations[i];
      std::vector<std::pair<int64_t, int64_t>> near_x, near_y;
      for (auto v : tracks[1]) {
        near_x.emplace_back(std::abs(v - x), v);
      }
      for (auto v : tracks[0]) {
        near_y.emplace_back(std::abs(v - y), v);
      }
      std::sort(near_x.begin(), near_x.end());
      std::sort(near_y.begin(), near_y.end());
      // A local junction window keeps candidate construction bounded as well.
      if (near_x.size() > 8) {
        near_x.resize(8);
      }
      if (near_y.size() > 8) {
        near_y.resize(8);
      }
      std::vector<std::tuple<int64_t, int64_t, int64_t>> candidates;
      for (const auto& [dx, xx] : near_x) {
        for (const auto& [dy, yy] : near_y) {
          candidates.emplace_back(dx + dy, xx, yy);
        }
      }
      std::sort(candidates.begin(), candidates.end());
      for (const auto& [distance, xx, yy] : candidates) {
        if (stage.expired()) {
          return out;
        }
        if (out.patterns_examined >= limits.patterns) {
          trace.budget = "junction";
          return fail("maze junction budget");
        }
        ++out.patterns_examined;
        ++trace.junction_trials;
        Key k{std::lower_bound(xs.begin(), xs.end(), xx) - xs.begin(), std::lower_bound(ys.begin(), ys.end(), yy) - ys.begin(), 0};
        if (legal(pad(k))) {
          vertices[i] = k;
          break;
        }
      }
      if (!vertices[i]) {
        return fail("maze Steiner junction blocked");
      }
    }
    for (const auto& [a, b] : topology.edges) {
      branches.push_back({{*vertices[a]}, {*vertices[b]}});
    }
    // Coincident pins have no topology edges but still need their landing.
    if (branches.empty()) {
      branches.push_back({{*vertices.front()}, {*vertices.front()}});
    }
  }
  std::vector<double> weight(layers.size(), 1);
  if (layer_cost) {
    for (size_t l = 0; l < layers.size(); ++l) {
      if (const auto w = layer_cost->find(layers[l]); w != layer_cost->end()) {
        weight[l] = std::max(1.0, w->second);
      }
    }
  }
  std::vector<std::vector<Key>> paths;
  using Via_key = std::tuple<std::string, ph::Orientation, int64_t, int64_t>;
  struct Proposed_cut {
    ph::Shape shape;
    int64_t   spacing;
  };
  std::map<Via_key, std::vector<Proposed_cut>> proposed_cuts;
  const auto                                   cuts_for = [&](const Via_key& via) -> const std::vector<Proposed_cut>& {
    const auto [found, inserted] = proposed_cuts.try_emplace(via);
    if (inserted) {
      const auto& [name, orientation, x, y] = via;
      for (const auto& shape : ph::via_geometry(tech_, frame_, {name, orientation})) {
        if (const auto cut = tech_.cut_layers.find(shape.layer); cut != tech_.cut_layers.end()) {
          found->second.push_back({
              {shape.layer, shape.rect.translated(x, y)},
              ph::signal_cut_spacing(cut->second, frame_.dbu)
          });
        }
      }
    }
    return found->second;
  };
  struct Cut_history {
    Via_key               via;
    std::optional<size_t> parent;
  };
  std::vector<Cut_history> history;
  std::set<Via_key>        committed_vias;
  std::set<Via_key>        prior_keys;
  const auto               via_of = [](const ph::Pin_access_point& p) { return Via_key{p.via, ph::Orientation::N, p.x, p.y}; };
  const auto               compatible_cuts = [&](const Proposed_cut& a, const Proposed_cut& b) {
    if (stage.expired()) {
      return false;
    }
    if (a.shape.layer != b.shape.layer) {
      // The supported ASAP7 cut rules have only same-layer spacing. cuts_for()
      // rejects unsupported qualifiers/properties through signal_cut_spacing;
      // inter-layer or stacked-via restrictions must not be silently ignored.
      return true;
    }
    ++trace.self_cut_checks;
    return !ph::violates_cut_spacing(a.shape.rect, b.shape.rect, a.spacing);
  };
  // Prefix-dependent rejection must never enter via_landings: that cache only
  // describes the immutable external occupancy. Exact emitted-via reuse adds
  // no new cut, as in the final deduplicated via set.
  const auto can_use_via = [&](const Via_key& via, std::optional<size_t> prefix, bool& reused) {
    const auto& candidate = cuts_for(via);
    const auto  reject    = [&] {
      if (!out.timed_out) {
        ++trace.self_cut_rejections;
      }
      return false;
    };
    for (size_t i = 0; i < candidate.size(); ++i) {
      for (size_t j = 0; j < i; ++j) {
        if (!compatible_cuts(candidate[i], candidate[j])) {
          return reject();
        }
      }
    }
    const auto compatible = [&](const Via_key& old) {
      if (via == old) {
        reused = true;
        return !stage.expired();
      }
      for (const auto& a : candidate) {
        for (const auto& b : cuts_for(old)) {
          if (!compatible_cuts(a, b)) {
            return false;
          }
        }
      }
      return !stage.expired();
    };
    for (const auto& old : committed_vias) {
      if (!compatible(old)) {
        if (!out.timed_out && prior_keys.contains(old)) {
          ++out.prior_cut_rejections;
        }
        return reject();
      }
    }
    for (auto id = prefix; id; id = history[*id].parent) {
      if (!compatible(history[*id].via)) {
        return reject();
      }
    }
    return !stage.expired();
  };
  // Retained and earlier pending pieces are immutable context, not geometry
  // to append to this proposal. Validate before seeding exact identities;
  // otherwise a malformed prior could make exact-reuse bypass cut checks.
  try {
    for (const auto& old : prior) {
      const Via_key via{old.via.name, old.via.orientation, old.x, old.y};
      bool          reused = false;
      if (!can_use_via(via, {}, reused)) {
        return out.timed_out ? out : fail("maze prior cut spacing");
      }
      committed_vias.insert(via);
      prior_keys.insert(via);
    }
  } catch (const std::exception& e) {
    return fail(std::string("maze prior cuts: ") + e.what());
  }
  // Multi-pin topology already selected these access points. They constrain
  // every branch, while two-pin/tap accesses are selected by the search below.
  for (const auto& point : trial.endpoints) {
    const auto via    = via_of(point);
    bool       reused = false;
    if (!can_use_via(via, {}, reused)) {
      return out.timed_out ? out : fail("maze selected pin cut spacing");
    }
    committed_vias.insert(via);
  }
  // Every layer change has a positive cost, even in an empty routing window.
  // The layer stack alternates direction: a same-layer target displaced across
  // that layer's direction needs an excursion to a neighbor and back.
  std::vector<double> layer_distance(layers.size(), 0), excursion(layers.size(), std::numeric_limits<double>::infinity());
  for (size_t l = 1; l < layers.size(); ++l) {
    const double cost = widths[l - 1] + widths[l];
    layer_distance[l] = layer_distance[l - 1] + cost;
    excursion[l - 1]  = std::min(excursion[l - 1], 2 * cost);
    excursion[l]      = std::min(excursion[l], 2 * cost);
  }
  trace.branches = branches.size();
  for (const auto& branch : branches) {
    ++trace.started;
    trace.sources += branch.sources.size();
    trace.targets += branch.targets.size();
    std::map<Key, size_t> targets;
    for (size_t i = 0; i < branch.targets.size(); ++i) {
      targets.emplace(branch.targets[i], i);
    }
    const auto heuristic = [&](const Key& k) {
      const auto [ix, iy, l] = k;
      double distance        = std::numeric_limits<double>::infinity();
      for (const auto& p : branch.targets) {
        const auto [tx, ty, tl] = p;
        const double dx = std::abs(double(xs[ix]) - xs[tx]), dy = std::abs(double(ys[iy]) - ys[ty]);
        const double turns
            = l == tl && (horizontal[l] ? dy != 0 : dx != 0) ? excursion[l] : std::abs(layer_distance[l] - layer_distance[tl]);
        distance = std::min(distance, dx + dy + turns);
      }
      return distance;
    };
    struct Record {
      Key                   key;
      double                cost;
      std::optional<size_t> parent, cuts;
      size_t                source;
    };
    // Retain one best label per coordinate as a bounded heuristic. A more
    // expensive prefix with different cuts can be lost, so exhaustion is not
    // physical UNSAT. Immutable parent IDs keep checked prefixes unchanged
    // when a coordinate later receives a cheaper label. At most four labels
    // are inserted per charged expansion, in addition to bounded sources.
    std::vector<Record>   labels;
    // live labels per coordinate: at most K (limits.maze_labels, default 1 = the single best label). With K > 1 a
    // costlier label survives when its cut history differs, so a cheaper prefix whose via blocks a later one does not
    // erase it. Still a bounded heuristic: exhaustion is not physical UNSAT.
    const size_t                       K = std::max<size_t>(1, limits.maze_labels);
    std::map<Key, std::vector<size_t>> reached;
    const auto live = [&](const Key& at, size_t id) {
      const auto it = reached.find(at);
      return it != reached.end() && std::ranges::find(it->second, id) != it->second.end();
    };
    // A search state is (source access, the exact set of new cuts on its prefix): the same cut set reached by different
    // paths is the same state; different sources stay distinct. Cut sets are canonicalized (sorted, interned).
    std::vector<size_t>                  hist_canon;  // per history node: interned cut-set id (SIZE_MAX: not yet)
    std::map<std::vector<Via_key>, size_t> interned;
    std::vector<std::vector<Via_key>>      cut_sets{{}};  // id 0: no new cuts
    interned[{}] = 0;
    const std::function<size_t(std::optional<size_t>)> canon = [&](std::optional<size_t> h) -> size_t {
      if (!h) {
        return 0;
      }
      if (hist_canon.size() <= *h) {
        hist_canon.resize(history.size(), SIZE_MAX);
      }
      if (hist_canon[*h] != SIZE_MAX) {
        return hist_canon[*h];
      }
      auto set = cut_sets[canon(history[*h].parent)];
      set.insert(std::ranges::lower_bound(set, history[*h].via), history[*h].via);
      const auto [it, fresh] = interned.try_emplace(set, cut_sets.size());
      if (fresh) {
        cut_sets.push_back(std::move(set));
      }
      return hist_canon[*h] = it->second;
    };
    // whether a label at `at` with this cost, cut history and source is kept. K = 1: the single best label (the old
    // rule). K > 1: an equivalent live state is replaced only by a cheaper label; a distinct state takes a free slot or
    // evicts the costliest live label when cheaper. Records stay immutable; only live membership changes.
    const auto admit = [&](const Key& at, double c, const std::optional<size_t>& cuts, size_t id) {
      auto& bucket = reached[at];
      if (K == 1) {
        if (!bucket.empty() && labels[bucket.front()].cost <= c) {
          return false;
        }
        bucket.assign(1, id);
        return true;
      }
      const auto state = std::pair{labels[id].source, canon(cuts)};
      for (auto& other : bucket) {
        if (std::pair{labels[other].source, canon(labels[other].cuts)} == state) {
          if (labels[other].cost <= c) {
            return false;
          }
          other = id;
          return true;
        }
      }
      if (bucket.size() < K) {
        bucket.push_back(id);
        return true;
      }
      auto worst = std::ranges::max_element(bucket, {}, [&](size_t other) { return labels[other].cost; });
      if (labels[*worst].cost > c) {
        *worst = id;
        return true;
      }
      return false;
    };
    // a cheap pre-check before computing a history: no label at this cost can be admitted
    const auto hopeless = [&](const Key& at, double c) {
      const auto it = reached.find(at);
      if (it == reached.end() || it->second.size() < K) {
        return false;
      }
      return std::ranges::all_of(it->second, [&](size_t other) { return labels[other].cost <= c; });
    };
    // Among equal lower bounds, finish the most advanced path first rather
    // than flooding the rectangle of equally short partial paths.
    using Entry = std::tuple<double, double, Key, size_t>;  // estimated total, negative cost, node, immutable label
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    for (size_t i = 0; i < branch.sources.size(); ++i) {
      const auto k = branch.sources[i];
      if (std::get<1>(k) >= ys.size() || !legal(pad(k))) {
        continue;
      }
      if (reached.size() >= limits.patterns) {
        trace.budget = "source_states";
        return fail("maze node budget");
      }
      std::optional<size_t> prefix;
      if (!sources && (anchors || access.size() == 2)) {
        const auto via    = via_of(access[0].points[i]);
        bool       reused = false;
        if (!can_use_via(via, {}, reused)) {
          if (out.timed_out) {
            return out;
          }
          continue;
        }
        if (!reused) {
          prefix = history.size();
          history.push_back({via, {}});
        }
      }
      if (!reached.contains(k) || K > 1) {
        labels.push_back({k, 0, {}, prefix, i});
        if (!admit(k, 0, prefix, labels.size() - 1)) {
          labels.pop_back();
          continue;
        }
        queue.emplace(heuristic(k), 0, k, labels.size() - 1);
        ++trace.pushes;
        trace.reached_peak = std::max(trace.reached_peak, reached.size());
      }
    }
    std::optional<size_t> end, end_cuts;
    bool                  node_limited = false;
    while (!queue.empty()) {
      if (stage.expired()) {
        return out;
      }
      const auto [estimate, negative_cost, k, label] = queue.top();
      const double cost                              = -negative_cost;
      queue.pop();
      ++trace.pops;
      if (!live(k, label)) {
        continue;
      }
      if (out.patterns_examined >= limits.patterns) {
        if (std::string_view(trace.budget) != "nondefault_via") {
          trace.budget = "charged_work";
        }
        return fail("maze node budget");
      }
      ++out.patterns_examined;
      ++trace.expanded;
      if (targets.contains(k)) {
        auto prefix     = labels[label].cuts;
        bool compatible = true;
        if (!anchors && access.size() == 2) {
          const auto via    = via_of(access[1].points[targets.at(k)]);
          bool       reused = false;
          compatible        = can_use_via(via, prefix, reused);
          if (compatible && !reused) {
            const auto old = prefix;
            prefix         = history.size();
            history.push_back({via, old});
          }
        }
        if (compatible) {
          end      = label;
          end_cuts = prefix;
          break;
        }
        if (out.timed_out) {
          return out;
        }
      }
      const auto [ix, iy, l] = k;
      std::vector<Key> neighbors;
      if (horizontal[l]) {
        if (ix > 0) {
          neighbors.emplace_back(ix - 1, iy, l);
        }
        if (ix + 1 < xs.size()) {
          neighbors.emplace_back(ix + 1, iy, l);
        }
      } else {
        if (iy > 0) {
          neighbors.emplace_back(ix, iy - 1, l);
        }
        if (iy + 1 < ys.size()) {
          neighbors.emplace_back(ix, iy + 1, l);
        }
      }
      for (auto next_layer : {l ? l - 1 : layers.size(), l + 1}) {
        if (next_layer >= layers.size()) {
          continue;
        }
        if (tracks[next_layer].contains(horizontal[next_layer] ? ys[iy] : xs[ix])) {
          neighbors.emplace_back(ix, iy, next_layer);
        }
      }
      for (const auto& next : neighbors) {
        if (stage.expired()) {
          return out;
        }
        const auto   next_layer = std::get<2>(next);
        const bool   turn       = next_layer != l;
        const double next_cost
            = cost
              + (turn
                     ? widths[l] + widths[next_layer]
                     : weight[l]
                           * (std::abs(double(xs[ix]) - xs[std::get<0>(next)]) + std::abs(double(ys[iy]) - ys[std::get<1>(next)])));
        const auto found = reached.find(next);
        if (hopeless(next, next_cost)) {
          continue;
        }
        if (found == reached.end() && reached.size() >= limits.patterns) {
          node_limited = true;
          continue;
        }
        if (turn ? !turn_legal(k, next) : !legal(segment(k, next))) {
          continue;
        }
        auto                   prefix = labels[label].cuts;
        std::optional<Via_key> new_via;
        if (turn) {
          const Via_key via{connecting_vias[std::min(l, next_layer)], ph::Orientation::N, xs[ix], ys[iy]};
          bool          reused = false;
          if (!can_use_via(via, prefix, reused)) {
            if (out.timed_out) {
              return out;
            }
            continue;
          }
          if (!reused) {
            new_via = via;
          }
        }
        // escape-layer metal over other pins' access points costs, a pin's last one most (the route's check decides)
        double total = next_cost;
        if (pin_escapes_) {
          std::optional<ph::Shape> on_escape;
          if (!turn && layers[l] == profile_.access.escape) {
            on_escape = segment(k, next);
          } else if (turn && layers[std::min(l, next_layer)] == profile_.access.escape) {
            const auto landing = via_landings.find({xs[ix], ys[iy], std::min(l, next_layer)});
            if (landing != via_landings.end() && landing->second) {
              on_escape = (*landing->second)[0];
            }
          }
          if (on_escape) {
            const auto c  = pin_escapes_->cost(*on_escape, net.name);
            total        += double(c.points) * 2 * pitch + (c.fatal ? 20 * pitch : 0);
            if (hopeless(next, total)) {
              continue;
            }
          }
        }
        if (new_via) {
          const auto old = prefix;
          prefix         = history.size();
          history.push_back({*new_via, old});
        }
        const auto source = labels[label].source;
        labels.push_back({next, total, label, prefix, source});
        if (!admit(next, total, prefix, labels.size() - 1)) {
          labels.pop_back();
          continue;
        }
        queue.emplace(total + heuristic(next), -total, next, labels.size() - 1);
        ++trace.pushes;
        trace.reached_peak = std::max(trace.reached_peak, reached.size());
      }
    }
    if (!end) {
      if (node_limited) {
        trace.budget = "reached_states";
      }
      return fail(node_limited ? "maze node budget" : "maze exhausted");
    }
    ++trace.completed;
    if (sources) {
      // a join has no pin endpoints
    } else if (anchors) {
      trial.endpoints = {access[0].points[labels[*end].source]};
    } else if (access.size() == 2) {
      trial.endpoints = {access[0].points[labels[*end].source], access[1].points[targets.at(labels[*end].key)]};
    }
    auto& path = paths.emplace_back();
    for (auto id = end; id; id = labels[*id].parent) {
      path.push_back(labels[*id].key);
    }
    for (auto id = end_cuts; id; id = history[*id].parent) {
      committed_vias.insert(history[*id].via);
    }
  }
  std::map<std::pair<size_t, int64_t>, std::vector<std::pair<double, double>>> intervals;
  const auto                                                                   add_metal = [&](const ph::Shape& s, size_t l) {
    const int64_t coordinate = std::llround(horizontal[l] ? (s.rect.yl + s.rect.yh) / 2 : (s.rect.xl + s.rect.xh) / 2);
    intervals[{l, coordinate}].emplace_back(horizontal[l] ? s.rect.xl : s.rect.yl, horizontal[l] ? s.rect.xh : s.rect.yh);
  };
  std::set<std::tuple<std::string, int64_t, int64_t>> vias;
  for (const auto& p : trial.endpoints) {
    vias.emplace(p.via, p.x, p.y);
  }
  for (const auto& path : paths) {
    for (size_t i = 0; i < path.size(); ++i) {
      if (stage.expired()) {
        return out;
      }
      const auto [ix, iy, l] = path[i];
      add_metal(pad(path[i]), l);
      if (i && std::get<2>(path[i - 1]) == l) {
        add_metal(segment(path[i - 1], path[i]), l);
      } else if (i) {
        vias.emplace(connecting_vias[std::min(l, std::get<2>(path[i - 1]))], xs[ix], ys[iy]);
      }
    }
  }
  std::vector<std::pair<ph::Shape, ph::Route_point>> patches;
  for (const auto& [name, x, y] : vias) {
    if (!via_layers.contains(name)) {
      continue;
    }  // Pin-layer access is already checked.
    const auto lower = std::find(layers.begin(), layers.end(), via_layers.at(name)) - layers.begin();
    const auto found = via_landings.find({x, y, size_t(lower)});
    if (found == via_landings.end() || !found->second) {
      return fail("missing checked maze via landings");
    }
    for (size_t side = 0; side < 2; ++side) {
      const auto  l     = lower + side;
      const auto& shape = (*found->second)[side];
      const auto  width = horizontal[l] ? shape.rect.yh - shape.rect.yl : shape.rect.xh - shape.rect.xl;
      if (width > widths[l]) {
        // Include the extension in the centerline and length report.
        add_metal(shape, l);
        patches.push_back({
            shape,
            ph::Route_point{x, y, {}}
        });
      }
    }
  }
  std::vector<ph::Shape> metal;
  for (const auto& [line, spans] : intervals) {
    const auto [l, c] = line;
    for (const auto& [low, high] : ph::merge_intervals(spans)) {
      if (stage.expired()) {
        return out;
      }
      // ASAP7 RECTONLY applies to the connected metal polygon, not each
      // emitted primitive. A local wide patch on a narrow wire makes a step;
      // widen the complete collinear interval instead and recheck its extent.
      double width = widths[l];
      for (const auto& [patch, origin] : patches) {
        if (patch.layer != layers[l] || (horizontal[l] ? origin.y : origin.x) != c) {
          continue;
        }
        const auto start = horizontal[l] ? patch.rect.xl : patch.rect.yl;
        const auto end   = horizontal[l] ? patch.rect.xh : patch.rect.yh;
        if (start <= high && end >= low) {
          width = std::max(width, horizontal[l] ? patch.rect.yh - patch.rect.yl : patch.rect.xh - patch.rect.xl);
        }
      }
      ph::Shape s{
          layers[l],
          horizontal[l] ? ph::Rect{          low, c - width / 2,          high, c + width / 2}
            : ph::Rect{c - width / 2,           low, c + width / 2,          high}
      };
      if (!legal(s)) {
        // the same predicate, rerun to name the first rejecting rule and blocker (in parentheses: callers group reasons by
        // the text before " (")
        const auto box = [](const ph::Rect& r) { return std::format("[{} {} {} {}]", r.xl, r.yl, r.xh, r.yh); };
        std::string why;
        if (const auto own = rules_.rectangle(s); !own.legal) {
          why = "shape: " + own.reason;
        } else if (!frame_.die.contains(s.rect)) {
          why = "outside the die";
        } else if (stage.expired()) {
          why = "deadline";
        } else {
          for (auto id : occupancy_.query(s.layer, s.rect, *rules_.clearance(s.layer), net.name)) {
            const auto& o = occupancy_.at(id);
            if (const auto check = rules_.spacing(s, o.shape); !check.legal) {
              why = std::format("vs {} {} {}: {}", o.net.empty() ? std::string("blockage") : o.net,
                                o.routed ? "routed" : "pin/obstruction", box(o.shape.rect), check.reason);
              break;
            }
          }
        }
        return fail(std::format("maze merged metal blocked ({} {} {})", s.layer, box(s.rect), why));
      }
      for (const auto& old : metal) {
        if (old.layer == s.layer) {
          if (const auto check = rules_.spacing(old, s); !check.legal) {
            // Two of this candidate's own merged lines that touch or overlap (e.g. a widened line covering the adjacent
            // track the path also used) form a non-rectangular union: rejected as an overlap, not a spacing violation.
            // The rejected pair stays in parentheses: callers group reasons by the text before " (".
            const bool overlap = old.rect.xl <= s.rect.xh && s.rect.xl <= old.rect.xh && old.rect.yl <= s.rect.yh && s.rect.yl <= old.rect.yh;
            return fail(std::format("{} ({} [{} {} {} {}] vs [{} {} {} {}]: {})", overlap ? "maze self overlap" : "maze self spacing",
                                    s.layer, old.rect.xl, old.rect.yl, old.rect.xh, old.rect.yh, s.rect.xl, s.rect.yl, s.rect.xh,
                                    s.rect.yh, overlap ? "own lines merge into a non-rectangular shape" : check.reason));
          }
        }
      }
      metal.push_back(s);
      // Encode the actual width on the path: RECT-only widening is invisible
      // to path-based extraction. OpenROAD requires widths for every metal
      // layer in a DEF NDR, including layers not used by this particular path.
      std::string taper_rule;
      if (width != widths[l]) {
        std::map<std::string, int64_t> rule;
        for (const auto& [name, layer] : tech_.layers) {
          const auto nominal = layer.width * frame_.dbu;
          if (!std::isfinite(nominal) || nominal <= 0 || nominal > INT32_MAX || std::abs(nominal - std::round(nominal)) > 1e-6) {
            return fail("unrepresentable nondefault rule width");
          }
          rule.emplace(name, std::llround(nominal));
        }
        rule.at(layers[l]) = std::llround(width);
        const auto base    = "livehd_width_" + std::to_string(l) + "_" + std::to_string(std::llround(width));
        taper_rule         = base;
        for (size_t suffix = 0;; ++suffix) {
          const auto old     = frame_.nondefault_rules.find(taper_rule);
          const auto pending = trial.nondefault_rules.find(taper_rule);
          if ((old == frame_.nondefault_rules.end() || old->second == rule)
              && (pending == trial.nondefault_rules.end() || pending->second == rule)) {
            break;
          }
          if (stage.expired()) {
            return out;
          }
          taper_rule = base + "_" + std::to_string(suffix + 1);
        }
        trial.nondefault_rules.emplace(taper_rule, std::move(rule));
      }
      add_wire(trial, s, std::move(taper_rule));
    }
  }
  ph::Signal_cut_set cuts(tech_, frame_);
  for (const auto& [name, x, y] : vias) {
    if (stage.expired()) {
      return out;
    }
    trial.paths.push_back(ph::via_path(via_layers.contains(name) ? via_layers.at(name) : profile_.access.pin, x, y, name));
    if (!cuts.add({name}, x, y)) {
      return fail("maze self cut spacing");
    }
  }
  trial.vias = vias.size();
  if (anchors) {  // a partial net: the caller checks it once every pin is tapped
    if (auto error = check_prior_cuts(trial, prior, limits.deadline); !error.empty()) {
      return stage.expired() ? out : fail(std::move(error));
    }
    out.geometry = std::move(trial);
    return out;
  }
  const auto connected = connectivity(net, trial.paths, &trial.nondefault_rules);
  if (stage.expired()) {
    return out;
  }
  if (!connected.connected) {
    return fail(connected.reason);
  }
  if (auto error = check_prior_cuts(trial, prior, limits.deadline); !error.empty()) {
    return stage.expired() ? out : fail(std::move(error));
  }
  out.geometry = std::move(trial);
  return out;
}
}  // namespace livehd::placement
