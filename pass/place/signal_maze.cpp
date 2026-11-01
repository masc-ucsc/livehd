// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <tuple>

#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "pin_escape_guard.hpp"
#include "signal_router.hpp"
#include "steiner_topology.hpp"

namespace livehd::placement {
namespace ph = physical;
Signal_route_trial Signal_router::propose_maze(const ph::Net& net, const Signal_route_limits& limits) const {
  return propose_maze_impl(net, limits, nullptr, nullptr, nullptr);
}
Signal_route_trial Signal_router::propose_tap(const ph::Net& net, const Signal_route_limits& limits,
                                              const std::vector<ph::Shape>&        anchors,
                                              const std::map<std::string, double>& layer_cost) const {
  return propose_maze_impl(net, limits, nullptr, &anchors, &layer_cost);
}
Signal_route_trial Signal_router::propose_join(const ph::Net& net, const Signal_route_limits& limits,
                                               const std::vector<ph::Shape>& sources, const std::vector<ph::Shape>& anchors,
                                               const std::map<std::string, double>& layer_cost) const {
  return propose_maze_impl(net, limits, &sources, &anchors, &layer_cost);
}
Signal_route_trial Signal_router::propose_maze_impl(const ph::Net& net, const Signal_route_limits& limits,
                                                    const std::vector<ph::Shape>* sources, const std::vector<ph::Shape>* anchors,
                                                    const std::map<std::string, double>* layer_cost) const {
  Signal_route_trial out;
  const Stage_trial  stage{out, limits.deadline};
  if (stage.expired()) {
    return out;
  }
  if (sources) {
    if (sources->empty() || !anchors || anchors->empty() || !net.connections.empty() || !net.wires.empty()) {
      return stage.fail("join requires its net's source and anchor metal and no pins");
    }
  } else if (anchors) {
    if (anchors->empty() || net.connections.size() != 1 || !net.wires.empty() || (!net.use.empty() && net.use != "SIGNAL")) {
      return stage.fail("tap requires one unwired signal pin and its net's anchors");
    }
  } else if (!ph::unrouted_signal(net)) {
    return stage.fail("maze requires an unwired multi-pin signal net");
  }
  std::set<std::pair<std::string, std::string>> endpoint_ids;
  for (const auto& pin : net.connections) {
    if (!endpoint_ids.emplace(pin.instance, pin.pin).second) {
      return stage.fail("duplicate endpoint");
    }
  }
  if (!limits.patterns) {
    return stage.fail("maze budget");
  }
  if (profile_.signal_layers.empty() || profile_.signal_layers.front() != profile_.access.escape) {
    return stage.fail("maze profile must start at the access escape layer");
  }
  std::vector<std::string> layers;
  std::vector<double>      widths, half;
  std::vector<bool>        horizontal;
  const auto               grid_dbu = ph::explicit_manufacturing_grid_dbu(tech_, frame_.dbu);
  if (!grid_dbu) {
    return stage.fail("unsupported manufacturing grid");
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
    return stage.fail("unsupported maze layers");
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
    return stage.fail("no supported maze via");
  }
  std::vector<ph::Pin_access_result> access(net.connections.size());
  double                             xmin = frame_.die.xh, xmax = frame_.die.xl, ymin = frame_.die.yh, ymax = frame_.die.yl;
  for (size_t i = 0; i < access.size(); ++i) {
    const auto cell = components_.find(net.connections[i].instance);
    if (cell == components_.end()) {
      return stage.fail("non-cell endpoint");
    }
    if (out.access_examined >= limits.access_candidates) {
      return stage.fail("pin-access budget");
    }
    access[i] = endpoint_access(*cell->second, net.connections[i].pin, net, limits, out);
    if (stage.expired()) {
      return out;
    }
    std::erase_if(access[i].points, [&](const auto& p) {
      return p.escape.layer != profile_.access.escape || p.escape.rect.yh - p.escape.rect.yl != widths[0];
    });
    if (access[i].points.empty()) {
      return stage.fail("pin has no legal access");
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
  double ref_x = 0, ref_y = 0;
  if (sources) {
    double best = std::numeric_limits<double>::infinity();
    for (const auto& s : *sources) {
      for (const auto& a : *anchors) {
        if (!maze_layer(s) || !maze_layer(a)) {
          continue;
        }
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
      return stage.fail("no join piece on a maze layer");
    }
  } else if (anchors) {
    ref_x = double(access[0].points.front().x);
    ref_y = double(access[0].points.front().y);
  }
  std::vector<std::pair<size_t, ph::Rect>> anchor_metal, source_metal;  // (maze layer, metal)
  const auto                               collect = [&](const std::vector<ph::Shape>& pieces, auto& metal) {
    for (const auto& a : pieces) {
      const auto l = maze_layer(a);
      if (!l) {
        continue;
      }
      metal.emplace_back(*l, a.rect);
      const double nx = std::clamp(ref_x, a.rect.xl, a.rect.xh), ny = std::clamp(ref_y, a.rect.yl, a.rect.yh);
      xmin = std::min(xmin, nx);
      xmax = std::max(xmax, nx);
      ymin = std::min(ymin, ny);
      ymax = std::max(ymax, ny);
    }
  };
  if (anchors) {
    collect(*anchors, anchor_metal);
    if (anchor_metal.empty()) {
      return stage.fail("no anchor on a maze layer");
    }
  }
  if (sources) {
    collect(*sources, source_metal);
  }
  double pitch = 0;
  for (const auto& t : frame_.tracks) {
    if (std::find(layers.begin(), layers.end(), t.layer) == layers.end()) {
      continue;
    }
    if (!t.supported()) {
      return stage.fail("unsupported maze tracks");
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
        return stage.fail("maze coordinate budget");
      }
    }
    auto& all = horizontal[l] ? ycoords : xcoords;
    all.insert(coords.begin(), coords.end());
    if (all.size() > limits.patterns) {
      return stage.fail("maze coordinate budget");
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
      (horizontal[l] ? xcoords : ycoords).insert(along);
    }
  }
  if (tracks[0].empty() || tracks[1].empty()) {
    return stage.fail("missing maze tracks");
  }
  for (const auto& a : access) {
    for (const auto& p : a.points) {
      xcoords.insert(p.x);
    }
  }
  const std::vector<int64_t> xs(xcoords.begin(), xcoords.end()), ys(ycoords.begin(), ycoords.end());
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
  const auto turn_legal = [&](const Key& a, const Key& b) {
    const auto [ix, iy, l] = a;
    const size_t   lower   = std::min(l, std::get<2>(b));
    const Via_site site{xs[ix], ys[iy], lower};
    if (const auto found = via_landings.find(site); found != via_landings.end()) {
      return bool(found->second);
    }
    auto& selected = via_landings[site];
    for (const auto wa : landing_widths[lower]) {
      for (const auto wb : landing_widths[lower + 1]) {
        if (stage.expired()) {
          return false;
        }
        if (wa < widths[lower] || wb < widths[lower + 1] || wa % int64_t(2 * grid) || wb % int64_t(2 * grid)) {
          continue;
        }
        if (wa != widths[lower] || wb != widths[lower + 1]) {
          if (out.patterns_examined >= limits.patterns) {
            return false;
          }
          ++out.patterns_examined;
        }
        std::array<ph::Shape, 2> candidate;
        for (size_t side = 0; side < 2; ++side) {
          const size_t layer  = lower + side;
          const double width  = side ? wb : wa;
          const double length = std::ceil(std::max(half[layer], width / 2) / grid) * grid;
          const double x = xs[ix], y = ys[iy];
          candidate[side] = {
              layers[layer],
              horizontal[layer] ? ph::Rect{   x - length, y - width / 2,    x + length, y + width / 2}
                                : ph::Rect{x - width / 2,    y - length, x + width / 2,    y + length}
          };
        }
        if (ph::check_signal_via(tech_, frame_, rules_, occupancy_, {connecting_vias[lower]}, xs[ix], ys[iy], candidate, net.name)
                .legal) {
          selected = candidate;
          return true;
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
    const auto on_metal = [&](const std::vector<std::pair<size_t, ph::Rect>>& metal, std::vector<Key>& keys) {
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
            keys.push_back(horizontal[l] ? Key{j, size_t(i), l} : Key{size_t(i), j, l});
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
      return stage.fail("piece outside the maze window");
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
      return stage.fail(e.what());
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
          return stage.fail("maze junction budget");
        }
        ++out.patterns_examined;
        Key k{std::lower_bound(xs.begin(), xs.end(), xx) - xs.begin(), std::lower_bound(ys.begin(), ys.end(), yy) - ys.begin(), 0};
        if (legal(pad(k))) {
          vertices[i] = k;
          break;
        }
      }
      if (!vertices[i]) {
        return stage.fail("maze Steiner junction blocked");
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
  for (const auto& branch : branches) {
    std::map<Key, size_t> targets;
    for (size_t i = 0; i < branch.targets.size(); ++i) {
      targets.emplace(branch.targets[i], i);
    }
    const auto heuristic = [&](const Key& k) {
      const auto [ix, iy, l] = k;
      double distance        = std::numeric_limits<double>::infinity();
      for (const auto& p : branch.targets) {
        distance
            = std::min(distance, std::abs(double(xs[ix]) - xs[std::get<0>(p)]) + std::abs(double(ys[iy]) - ys[std::get<1>(p)]));
      }
      return distance;
    };
    struct Record {
      double             cost;
      std::optional<Key> parent;
      size_t             source;
    };
    std::map<Key, Record> reached;
    using Entry = std::tuple<double, double, Key>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> queue;
    for (size_t i = 0; i < branch.sources.size(); ++i) {
      const auto k = branch.sources[i];
      if (std::get<1>(k) >= ys.size() || !legal(pad(k))) {
        continue;
      }
      if (reached.size() >= limits.patterns) {
        return stage.fail("maze node budget");
      }
      if (reached.emplace(k, Record{0, {}, i}).second) {
        queue.emplace(heuristic(k), 0, k);
      }
    }
    std::optional<Key> end;
    while (!queue.empty()) {
      if (stage.expired()) {
        return out;
      }
      const auto [estimate, cost, k] = queue.top();
      queue.pop();
      if (cost != reached.at(k).cost) {
        continue;
      }
      if (out.patterns_examined >= limits.patterns) {
        return stage.fail("maze node budget");
      }
      ++out.patterns_examined;
      if (targets.contains(k)) {
        end = k;
        break;
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
        if (found != reached.end() && found->second.cost <= next_cost) {
          continue;
        }
        if (found == reached.end() && reached.size() >= limits.patterns) {
          continue;
        }
        if (turn ? !turn_legal(k, next) : !legal(segment(k, next))) {
          continue;
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
            if (found != reached.end() && found->second.cost <= total) {
              continue;
            }
          }
        }
        reached.insert_or_assign(next, Record{total, k, reached.at(k).source});
        queue.emplace(total + heuristic(next), total, next);
      }
    }
    if (!end) {
      return stage.fail("maze exhausted");
    }
    if (sources) {
      // a join has no pin endpoints
    } else if (anchors) {
      trial.endpoints = {access[0].points[reached.at(*end).source]};
    } else if (access.size() == 2) {
      trial.endpoints = {access[0].points[reached.at(*end).source], access[1].points[targets.at(*end)]};
    }
    auto& path = paths.emplace_back();
    for (auto k = end; k; k = reached.at(*k).parent) {
      path.push_back(*k);
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
      return stage.fail("missing checked maze via landings");
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
        return stage.fail("maze merged metal blocked");
      }
      for (const auto& old : metal) {
        if (old.layer == s.layer && !rules_.spacing(old, s).legal) {
          return stage.fail("maze self spacing");
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
            return stage.fail("unrepresentable nondefault rule width");
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
      return stage.fail("maze self cut spacing");
    }
  }
  trial.vias = vias.size();
  if (anchors) {  // a partial net: the caller checks it once every pin is tapped
    out.geometry = std::move(trial);
    return out;
  }
  const auto connected = connectivity(net, trial.paths, &trial.nondefault_rules);
  if (stage.expired()) {
    return out;
  }
  if (!connected.connected) {
    return stage.fail(connected.reason);
  }
  out.geometry = std::move(trial);
  return out;
}
}  // namespace livehd::placement
