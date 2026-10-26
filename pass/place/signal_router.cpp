// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "signal_router.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>

#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/signal_via.hpp"
#include "pin_escape_guard.hpp"

namespace livehd::placement {
namespace ph = physical;
void apply_signal_route_rules(ph::Floorplan& frame, const Signal_route_geometry& geometry) {
  for (const auto& [name, widths] : geometry.nondefault_rules) {
    const auto existing = frame.nondefault_rules.find(name);
    if (existing != frame.nondefault_rules.end() && existing->second != widths) {
      throw std::invalid_argument("conflicting signal route width rule: " + name);
    }
  }
  frame.nondefault_rules.insert(geometry.nondefault_rules.begin(), geometry.nondefault_rules.end());
}
Signal_router::Signal_router(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Routing_occupancy& occupancy)
    : Signal_router(tech, frame, occupancy, asap7_signal_route_profile(tech)) {}
Signal_router::Signal_router(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Routing_occupancy& occupancy,
                             const ph::Signal_metal_rules& rules)
    : Signal_router(tech, frame, occupancy, rules, asap7_signal_route_profile(tech)) {}
Signal_router::Signal_router(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Routing_occupancy& occupancy,
                             Signal_route_profile profile, const Pin_escape_guard* pin_escapes)
    : tech_(tech)
    , profile_(std::move(profile))
    , frame_(frame)
    , occupancy_(occupancy)
    , owned_rules_(std::make_unique<ph::Signal_metal_rules>(tech, frame))
    , rules_(*owned_rules_)
    , components_(frame)
    , pin_escapes_(pin_escapes) {}
Signal_router::Signal_router(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Routing_occupancy& occupancy,
                             const ph::Signal_metal_rules& rules, Signal_route_profile profile, const Pin_escape_guard* pin_escapes)
    : tech_(tech)
    , profile_(std::move(profile))
    , frame_(frame)
    , occupancy_(occupancy)
    , rules_(rules)
    , components_(frame)
    , pin_escapes_(pin_escapes) {}
Signal_route_proposals Signal_router::try_route(const ph::Net& net, const Signal_route_request& request) const {
  Signal_route_proposals result;
  if (!std::isfinite(request.max_parallel_exposure) || request.max_parallel_exposure < 0) {
    result.reason = "invalid parallel exposure limit";
    return result;
  }
  if (request.compare_layers && !request.corridor) {
    result.reason = "layer comparison requires a retained alignment";
    return result;
  }
  if (request.corridor && request.corridor->layer != profile_.access.pin && !profile_.allows(request.corridor->layer)) {
    result.reason = "alignment layer excluded by routing profile";
    return result;
  }
  // All proposal strategies require search work. Reject exhausted requests
  // before constructing the whole-frame pin-access guard.
  if (std::chrono::steady_clock::now() >= request.limits.deadline) {
    result.timed_out = true;
    result.reason    = "route deadline";
    return result;
  }
  if (!request.limits.patterns) {
    result.reason = "pattern budget";
    return result;
  }
  struct Access_deadline {};
  std::unique_ptr<Pin_escape_guard> local_escapes;
  const auto                        expired = [&] { return std::chrono::steady_clock::now() >= request.limits.deadline; };
  const auto*                       escapes = pin_escapes_;
  if (!escapes) {
    try {
      local_escapes = std::make_unique<Pin_escape_guard>(
          tech_,
          frame_,
          occupancy_,
          request.limits.access_candidates,
          [&] {
            if (expired()) {
              throw Access_deadline{};
            }
          },
          profile_.access,
          &rules_);
      escapes = local_escapes.get();
    } catch (const Access_deadline&) {
      result.candidates.clear();
      result.timed_out = true;
      result.reason    = "pin escape setup deadline";
      return result;
    }
  }
  auto base                = request.corridor        ? propose_aligned(net, *request.corridor, request.limits)
                             : request.straight_span ? propose_span_impl(net, request.limits, nullptr, escapes)
                                                     : propose(net, request.limits);
  result.access_examined   = base.access_examined;
  result.patterns_examined = base.patterns_examined;
  result.timed_out         = base.timed_out;
  result.reason            = base.reason;
  if (!base.geometry) {
    return result;
  }
  result.candidates.push_back(std::move(*base.geometry));
  if (request.compare_layers) {
    for (const auto& layer : profile_.signal_layers) {
      if (result.patterns_examined >= request.limits.patterns || result.timed_out) {
        break;
      }
      auto lifted               = propose_aligned_layer(net,
                                          result.candidates.front(),
                                          layer,
                                                        {0, request.limits.patterns - result.patterns_examined, request.limits.deadline});
      result.patterns_examined += lifted.patterns_examined;
      result.timed_out         |= lifted.timed_out;
      if (lifted.geometry) {
        result.candidates.push_back(std::move(*lifted.geometry));
      }
    }
  }
  // Filter after constructing layer alternatives: a congested baseline must
  // not prevent trying a cleaner upper layer with the same endpoint centers.
  std::string access_rejection;
  bool        rejected_parallel = false;
  std::erase_if(result.candidates, [&](Signal_route_geometry& candidate) {
    if (std::chrono::steady_clock::now() >= request.limits.deadline) {
      result.timed_out = true;
      return true;
    }
    ph::Net proposed;
    proposed.name = net.name;
    proposed.wires.push_back({"FIXED", candidate.paths});
    const auto shapes = ph::route_geometry(tech_, frame_, proposed, false, &candidate.nondefault_rules);
    const auto access = escapes->check(shapes, net.name, expired);
    if (!access.legal) {
      result.timed_out |= expired();
      access_rejection  = access.reason;
      return true;
    }
    for (const auto& shape : shapes) {
      const auto layer = tech_.layers.find(shape.layer);
      if (layer == tech_.layers.end()) {
        continue;  // Via cuts have no parallel wire coupling.
      }
      const bool   horizontal = layer->second.direction == "HORIZONTAL";
      const auto&  r          = shape.rect;
      const double lo = horizontal ? r.xl : r.yl, hi = horizontal ? r.xh : r.yh;
      const double side_lo = horizontal ? r.yl : r.xl, side_hi = horizontal ? r.yh : r.xh;
      const double width = side_hi - side_lo;
      if (width <= 0 || hi - lo < 4 * width) {
        continue;  // Short via pads are not long parallel runs.
      }
      for (auto id : occupancy_.query(shape.layer, r, static_cast<int64_t>(std::ceil(2 * width)), net.name)) {
        if (std::chrono::steady_clock::now() >= request.limits.deadline) {
          result.timed_out = true;
          return true;
        }
        const auto   neighbor = occupancy_.at(id);
        const auto&  n        = neighbor.shape.rect;
        const double nlo = horizontal ? n.xl : n.yl, nhi = horizontal ? n.xh : n.yh;
        const double nslo = horizontal ? n.yl : n.xl, nshi = horizontal ? n.yh : n.xh;
        const double overlap = std::min(hi, nhi) - std::max(lo, nlo);
        const double gap     = std::max(nslo - side_hi, side_lo - nshi);
        if (overlap <= 0 || gap <= 0 || gap > 2 * width || nhi - nlo < 4 * (nshi - nslo)) {
          continue;
        }
        candidate.parallel_exposure += overlap / gap;
      }
    }
    const bool reject  = !request.try_hard && candidate.parallel_exposure > request.max_parallel_exposure;
    rejected_parallel |= reject;
    return reject;
  });
  if (result.candidates.empty()) {
    if (result.timed_out) {
      result.reason = "route acceptance check deadline";
    } else if (!access_rejection.empty()) {
      result.reason = access_rejection;
    } else if (rejected_parallel) {
      result.reason = "long parallel foreign metal exposure exceeds limit";
    }
  }
  return result;
}
Signal_route_probe Signal_router::probe_route(const ph::Net& net, const Signal_route_request& request) const {
  auto               proposals = try_route(net, request);
  Signal_route_probe result;
  result.access_examined   = proposals.access_examined;
  result.patterns_examined = proposals.patterns_examined;
  result.timed_out         = proposals.timed_out;
  result.budget_exhausted  = result.timed_out || proposals.reason.find("budget") != std::string::npos;
  result.reason            = std::move(proposals.reason);
  if (proposals.candidates.empty()) {
    return result;
  }
  const auto& candidate = proposals.candidates.front();
  // Span construction validates its vias/trunk incrementally. A predicate
  // must also certify the complete net before reporting a routing witness.
  if (request.straight_span && !request.corridor) {
    auto routed = net;
    routed.wires.push_back({"FIXED", candidate.paths});
    const auto connected = ph::check_route_connectivity(tech_, frame_, routed, &components_);
    if (!connected.connected) {
      result.reason = connected.reason;
      return result;
    }
  }
  if (std::chrono::steady_clock::now() >= request.limits.deadline) {
    result.timed_out = result.budget_exhausted = true;
    result.reason                              = "route deadline";
    return result;
  }
  result.routable = true;
  result.corridor = candidate.straight_corridor;
  return result;
}
Signal_route_trial Signal_router::propose(const ph::Net& net, const Signal_route_limits& limits) const {
  Signal_route_trial result;
  const std::array   searches{&Signal_router::propose_span, &Signal_router::propose_pattern, &Signal_router::propose_maze};
  for (size_t i = 0; i < searches.size(); ++i) {
    if (result.geometry || result.timed_out) {
      break;
    }
    const auto remaining_stages  = searches.size() - i;
    // Reserve work and access enumeration for every remaining strategy. Any
    // unused allowance carries forward; the net's total limits never grow.
    const auto work              = (limits.patterns - result.patterns_examined) / remaining_stages;
    const auto access            = (limits.access_candidates - result.access_examined) / remaining_stages;
    auto       next              = (this->*searches[i])(net, {access, work, limits.deadline});
    next.access_examined        += result.access_examined;
    next.patterns_examined      += result.patterns_examined;
    result                       = std::move(next);
  }
  return result;
}
Signal_route_trial Signal_router::propose_aligned(const ph::Net& net, const ph::Shape& corridor,
                                                  const Signal_route_limits& limits) const {
  auto direct = propose_straight(net, corridor, limits);
  if (direct.geometry || direct.timed_out || net.connections.size() != 2 || corridor.layer != profile_.access.escape
      || direct.patterns_examined >= limits.patterns) {
    return direct;
  }
  auto result
      = propose_span_impl(net, {limits.access_candidates, limits.patterns - direct.patterns_examined, limits.deadline}, &corridor);
  result.patterns_examined += direct.patterns_examined;
  return result;
}
Signal_route_trial Signal_router::propose_aligned_layer(const ph::Net& net, const Signal_route_geometry& aligned,
                                                        const std::string& target, const Signal_route_limits& limits) const {
  Signal_route_trial out;
  const auto         fail = [&](const std::string& reason) {
    out.reason = reason;
    return out;
  };
  const auto expired = [&] {
    out.timed_out = std::chrono::steady_clock::now() >= limits.deadline;
    return out.timed_out;
  };
  if (expired()) {
    return fail("route deadline");
  }
  if (net.connections.size() != 2 || !net.wires.empty() || aligned.endpoints.size() != 2
      || (!net.use.empty() && net.use != "SIGNAL")) {
    return fail("layer lift requires an unwired aligned two-pin signal");
  }
  const auto& a          = aligned.endpoints[0];
  const auto& b          = aligned.endpoints[1];
  const bool  horizontal = a.y == b.y;
  if ((a.x == b.x) == horizontal) {
    return fail("layer lift requires distinct aligned endpoints");
  }
  const auto end = std::find(tech_.routing_layers.begin(), tech_.routing_layers.end(), target);
  if (end == tech_.routing_layers.end() || !profile_.allows(target)) {
    return fail("unsupported lift layer");
  }
  const auto start = std::find(tech_.routing_layers.begin(), tech_.routing_layers.end(), a.escape.layer);
  if (start >= end || b.escape.layer != a.escape.layer
      || tech_.layers.at(target).direction != (horizontal ? "HORIZONTAL" : "VERTICAL")) {
    return fail("lift must preserve trunk direction");
  }
  const double grid = tech_.manufacturing_grid * frame_.dbu;
  if (!std::isfinite(grid) || grid < 1 || grid != std::round(grid)) {
    return fail("unsupported manufacturing grid");
  }
  Signal_route_geometry trial;
  trial.endpoints = aligned.endpoints;
  std::vector<ph::Shape> metal, cuts;
  struct Via {
    std::string name, lower;
    int64_t     x, y;
  };
  std::vector<Via> vias;
  for (const auto& point : trial.endpoints) {
    if (!point.via.empty()) {
      vias.push_back({point.via, profile_.access.pin, point.x, point.y});
    }
  }
  std::array<std::vector<ph::Shape>, 2> landings;
  for (auto layer = start; layer <= end; ++layer) {
    if (expired()) {
      return fail("route deadline");
    }
    if (!rules_.clearance(*layer)) {
      return fail("unsupported lift rules");
    }
    const auto&  definition = tech_.layers.at(*layer);
    const double width      = definition.width * frame_.dbu;
    const double area       = definition.rules.area * frame_.dbu * frame_.dbu;
    if (!std::isfinite(width) || width <= 0 || width > INT32_MAX || width != std::round(width) || int64_t(width) % int64_t(2 * grid)
        || !std::isfinite(area) || area < 0 || (definition.direction != "HORIZONTAL" && definition.direction != "VERTICAL")) {
      return fail("unsupported lift dimensions");
    }
    const bool h    = definition.direction == "HORIZONTAL";
    double     half = std::ceil(std::max(width, area / width) / (2 * grid)) * grid;
    for (const auto& size : definition.rules.min_size) {
      half = std::max(half, std::ceil(std::max(size[0], size[1]) * frame_.dbu / (2 * grid)) * grid);
    }
    for (size_t i = 0; i < 2; ++i) {
      const auto& point = trial.endpoints[i];
      ph::Shape   shape{
          *layer,
          h ? ph::Rect{     point.x - half, point.y - width / 2,      point.x + half, point.y + width / 2}
              : ph::Rect{point.x - width / 2,      point.y - half, point.x + width / 2,      point.y + half}
      };
      if (layer == start && !point.via.empty()) {
        shape.rect.xl = std::min(shape.rect.xl, point.escape.rect.xl);
        shape.rect.xh = std::max(shape.rect.xh, point.escape.rect.xh);
        shape.rect.yl = std::min(shape.rect.yl, point.escape.rect.yl);
        shape.rect.yh = std::max(shape.rect.yh, point.escape.rect.yh);
      }
      if (layer == end) {
        if (h) {
          shape.rect.xl = std::min(a.x, b.x) - half;
          shape.rect.xh = std::max(a.x, b.x) + half;
        } else {
          shape.rect.yl = std::min(a.y, b.y) - half;
          shape.rect.yh = std::max(a.y, b.y) + half;
        }
      }
      if (!rules_.rectangle(shape).legal || shape.rect.xl < frame_.die.xl || shape.rect.yl < frame_.die.yl
          || shape.rect.xh > frame_.die.xh || shape.rect.yh > frame_.die.yh) {
        return fail("illegal lift landing");
      }
      for (auto id : occupancy_.query(*layer, shape.rect, *rules_.clearance(*layer), net.name)) {
        if (expired()) {
          return fail("route deadline");
        }
        if (!rules_.spacing(shape, occupancy_.at(id).shape).legal) {
          return fail("blocked lift landing");
        }
      }
      landings[i].push_back(shape);
      if (layer != end || i == 0) {
        metal.push_back(shape);
      }
    }
  }
  for (size_t i = 0; i < 2; ++i) {
    for (size_t l = 0; l + 1 < landings[i].size(); ++l) {
      bool found = false;
      for (const auto& [name, definition] : tech_.vias) {
        if (expired()) {
          return fail("route deadline");
        }
        if (!definition.unsupported.empty() || definition.shapes.size() != 3) {
          continue;
        }
        bool lower = false, upper = false;
        for (const auto& s : definition.shapes) {
          lower |= s.layer == landings[i][l].layer;
          upper |= s.layer == landings[i][l + 1].layer;
        }
        if (!lower || !upper) {
          continue;
        }
        if (out.patterns_examined >= limits.patterns) {
          return fail("lift pattern budget");
        }
        ++out.patterns_examined;
        const auto& point = trial.endpoints[i];
        if (ph::check_signal_via(tech_,
                                 frame_,
                                 rules_,
                                 occupancy_,
                                 {name},
                                 point.x,
                                 point.y,
                                 {landings[i][l], landings[i][l + 1]},
                                 net.name)
                .legal) {
          vias.push_back({name, landings[i][l].layer, point.x, point.y});
          found = true;
          break;
        }
      }
      if (!found) {
        return fail("no legal endpoint via stack");
      }
    }
  }
  for (size_t i = 0; i < metal.size(); ++i) {
    const auto& shape = metal[i];
    for (size_t j = 0; j < i; ++j) {
      if (metal[j].layer == shape.layer && !rules_.spacing(shape, metal[j]).legal) {
        return fail("lift self spacing");
      }
    }
    const bool  h = tech_.layers.at(shape.layer).direction == "HORIZONTAL";
    const auto& r = shape.rect;
    trial.paths.push_back({
        shape.layer,
        {},
        {},
        {ph::Route_point{std::llround(h ? r.xl : (r.xl + r.xh) / 2), std::llround(h ? (r.yl + r.yh) / 2 : r.yl), 0},
                ph::Route_point{std::llround(h ? r.xh : (r.xl + r.xh) / 2), std::llround(h ? (r.yl + r.yh) / 2 : r.yh), 0}}
    });
    trial.layer_wire_length_um[shape.layer] += (h ? r.xh - r.xl : r.yh - r.yl) / frame_.dbu;
  }
  for (const auto& via : vias) {
    trial.paths.push_back({
        via.lower,
        {},
        {},
        {ph::Route_point{via.x, via.y, {}}, ph::Route_via{via.name}}
    });
    for (auto shape : ph::via_geometry(tech_, frame_, {via.name})) {
      if (!tech_.cut_layers.contains(shape.layer)) {
        continue;
      }
      shape.rect.xl      += via.x;
      shape.rect.xh      += via.x;
      shape.rect.yl      += via.y;
      shape.rect.yh      += via.y;
      const auto spacing  = ph::signal_cut_spacing(tech_.cut_layers.at(shape.layer), frame_.dbu);
      for (const auto& old : cuts) {
        if (old.layer != shape.layer) {
          continue;
        }
        const auto dx = std::max({old.rect.xl - shape.rect.xh, shape.rect.xl - old.rect.xh, 0.});
        const auto dy = std::max({old.rect.yl - shape.rect.yh, shape.rect.yl - old.rect.yh, 0.});
        if ((dx == 0 && dy == 0) || (dx < spacing && dy < spacing)) {
          return fail("lift cut spacing");
        }
      }
      cuts.push_back(shape);
    }
  }
  auto routed = net;
  routed.wires.push_back({"FIXED", trial.paths});
  const auto connected = ph::check_route_connectivity(tech_, frame_, routed, &components_);
  if (!connected.connected) {
    return fail(connected.reason);
  }
  if (expired()) {
    return fail("route deadline");
  }
  trial.vias = vias.size();
  for (const auto& [layer, length] : trial.layer_wire_length_um) {
    trial.wire_length_um += length;
  }
  out.geometry = std::move(trial);
  return out;
}
Signal_route_trial Signal_router::propose_straight(const ph::Net& net, const ph::Shape& corridor,
                                                   const Signal_route_limits& limits) const {
  Signal_route_trial out;
  const auto         fail = [&](const std::string& reason) {
    out.reason = reason;
    return out;
  };
  if (std::chrono::steady_clock::now() >= limits.deadline) {
    out.timed_out = true;
    return fail("route deadline");
  }
  if (!limits.patterns) {
    return fail("pattern budget");
  }
  if (!net.wires.empty() || net.connections.size() != 2 || (!net.use.empty() && net.use != "SIGNAL")) {
    return fail("straight route requires an unwired two-pin signal net");
  }
  const auto layer     = tech_.layers.find(corridor.layer);
  const auto clearance = rules_.clearance(corridor.layer);
  if (layer == tech_.layers.end() || !clearance) {
    return fail("unsupported straight-route layer");
  }
  const bool horizontal = layer->second.direction == "HORIZONTAL";
  if (!horizontal && layer->second.direction != "VERTICAL") {
    return fail("missing route direction");
  }
  const double half = layer->second.width * frame_.dbu / 2;
  const auto&  r    = corridor.rect;
  const double x1   = horizontal ? r.xl + half : (r.xl + r.xh) / 2;
  const double y1   = horizontal ? (r.yl + r.yh) / 2 : r.yl + half;
  const double x2   = horizontal ? r.xh - half : x1;
  const double y2   = horizontal ? y1 : r.yh - half;
  for (double v : {x1, y1, x2, y2}) {
    if (!std::isfinite(v) || std::abs(v) > double(std::numeric_limits<int32_t>::max()) || std::abs(v - std::round(v)) > 1e-6) {
      return fail("straight-route endpoint is not integral DBU");
    }
  }
  if (x2 < x1 || y2 < y1 || (x1 == x2 && y1 == y2)) {
    return fail("empty straight route");
  }
  Signal_route_geometry geometry;
  geometry.straight_corridor = corridor;
  geometry.paths.push_back({
      corridor.layer,
      {},
      {},
      {ph::Route_point{std::llround(x1), std::llround(y1), {}}, ph::Route_point{std::llround(x2), std::llround(y2), {}}}
  });
  auto trial = net;
  trial.wires.push_back({"FIXED", geometry.paths});
  const auto shapes = ph::route_geometry(tech_, frame_, trial);
  if (shapes.size() != 1) {
    return fail("straight route must have one metal rectangle");
  }
  ++out.patterns_examined;
  const auto rule = rules_.rectangle(shapes.front());
  if (!rule.legal) {
    return fail(rule.reason);
  }
  for (auto id : occupancy_.query(corridor.layer, shapes.front().rect, *clearance, net.name)) {
    if (std::chrono::steady_clock::now() >= limits.deadline) {
      out.timed_out = true;
      return fail("route deadline");
    }
    const auto check = rules_.spacing(shapes.front(), occupancy_.at(id).shape);
    if (!check.legal) {
      return fail(check.reason);
    }
  }
  // Associate each endpoint with its actual transformed LEF pin, independent
  // of net connection order. Geometry alone does not establish RC pin identity.
  std::set<size_t>                               used;
  const std::array<std::pair<double, double>, 2> points{
      {{x1, y1}, {x2, y2}}
  };
  for (const auto& pin : net.connections) {
    const auto cell = components_.find(pin.instance);
    if (pin.instance == "PIN" || cell == components_.end()) {
      return fail("non-cell straight-route endpoint");
    }
    const auto& c     = *cell->second;
    const auto& m     = tech_.macros.at(c.macro);
    const auto  found = m.pins.find(pin.pin);
    if (found == m.pins.end()) {
      return fail("unknown straight-route pin");
    }
    bool matched = false;
    for (size_t i = 0; i < points.size() && !matched; ++i) {
      if (used.contains(i)) {
        continue;
      }
      for (const auto& shape : found->second.shapes) {
        if (shape.layer != corridor.layer) {
          continue;
        }
        const auto a
            = ph::orient_point(shape.rect.xl + m.origin_x, shape.rect.yl + m.origin_y, m.width, m.height, c.place.orientation);
        const auto b
            = ph::orient_point(shape.rect.xh + m.origin_x, shape.rect.yh + m.origin_y, m.width, m.height, c.place.orientation);
        const auto [x, y] = points[i];
        if (x < c.place.x + std::min(a.first, b.first) * frame_.dbu || x > c.place.x + std::max(a.first, b.first) * frame_.dbu
            || y < c.place.y + std::min(a.second, b.second) * frame_.dbu
            || y > c.place.y + std::max(a.second, b.second) * frame_.dbu) {
          continue;
        }
        geometry.endpoints.push_back({std::llround(x), std::llround(y), {}, corridor, corridor.layer});
        used.insert(i);
        matched = true;
        break;
      }
    }
    if (!matched) {
      return fail("straight-route endpoint misses its pin");
    }
  }
  const auto connected = ph::check_route_connectivity(tech_, frame_, trial, &components_);
  if (!connected.connected) {
    return fail(connected.reason);
  }
  geometry.wire_length_um                       = (x2 - x1 + y2 - y1) / frame_.dbu;
  geometry.layer_wire_length_um[corridor.layer] = geometry.wire_length_um;
  out.geometry                                  = std::move(geometry);
  return out;
}

Signal_route_trial Signal_router::propose_span(const ph::Net& net, const Signal_route_limits& limits) const {
  return propose_span_impl(net, limits, nullptr);
}
Signal_route_trial Signal_router::propose_span_impl(const ph::Net& net, const Signal_route_limits& limits,
                                                    const ph::Shape* corridor, const Pin_escape_guard* escapes) const {
  Signal_route_trial out;
  const auto         expired = [&]() {
    if (std::chrono::steady_clock::now() < limits.deadline) {
      return false;
    }
    out.timed_out = true;
    out.reason    = "route deadline";
    return true;
  };
  if (expired()) {
    return out;
  }
  const auto fail = [&](const std::string& reason) {
    out.reason = reason;
    return out;
  };
  if (!net.wires.empty()) {
    return fail("net already has wiring");
  }
  if ((!net.use.empty() && net.use != "SIGNAL") || net.connections.size() < 2) {
    return fail("not a multi-terminal signal net");
  }
  if (!profile_.allows(profile_.access.escape)) {
    return fail("access layer excluded by routing profile");
  }
  const auto clearance = rules_.clearance(profile_.access.escape);
  if (!clearance || !tech_.layers.contains(profile_.access.escape) || !tech_.cut_layers.contains(profile_.access.cut)) {
    return fail("unsupported " + profile_.access.escape + "/" + profile_.access.cut + " rules");
  }
  if (tech_.layers.at(profile_.access.escape).direction != "HORIZONTAL") {
    return fail(profile_.access.escape + " span requires a horizontal layer");
  }
  const auto wire_width = tech_.layers.at(profile_.access.escape).width * frame_.dbu;
  if (!std::isfinite(wire_width) || wire_width <= 0 || wire_width > std::numeric_limits<int32_t>::max()
      || wire_width != std::round(wire_width) || int64_t(wire_width) % 2) {
    return fail("unsupported signal width");
  }
  if (corridor
      && (corridor->layer != profile_.access.escape || !rules_.rectangle(*corridor).legal
          || corridor->rect.yh - corridor->rect.yl != wire_width)) {
    return fail("invalid aligned " + profile_.access.escape + " corridor");
  }
  std::set<std::pair<std::string, std::string>>  pins;
  std::vector<std::vector<ph::Pin_access_point>> accesses;
  std::set<int64_t>                              common_tracks;
  for (const auto& connection : net.connections) {
    if (expired()) {
      return out;
    }
    const auto cell = components_.find(connection.instance);
    if (cell == components_.end() || !pins.emplace(connection.instance, connection.pin).second) {
      return fail("non-cell or duplicate endpoint");
    }
    if (out.access_examined >= limits.access_candidates) {
      return fail("pin-access budget");
    }
    auto result          = ph::enumerate_pin_access(tech_,
                                           frame_,
                                           *cell->second,
                                           connection.pin,
                                           occupancy_,
                                           rules_,
                                           limits.access_candidates - out.access_examined,
                                           net.name,
                                           profile_.access);
    out.access_examined += result.examined;
    if (expired()) {
      return out;
    }
    if (result.points.empty()) {
      return fail(result.exhausted ? "pin-access budget" : "pin has no legal access");
    }
    std::set<int64_t> tracks;
    std::erase_if(result.points, [&](const auto& p) {
      if (p.escape.layer != profile_.access.escape || p.escape.rect.yh - p.escape.rect.yl != wire_width) {
        return true;
      }
      return corridor
             && (p.escape.rect.xl < corridor->rect.xl || p.escape.rect.xh > corridor->rect.xh
                 || p.escape.rect.yl != corridor->rect.yl || p.escape.rect.yh != corridor->rect.yh);
    });
    for (const auto& p : result.points) {
      tracks.insert(p.y);
    }
    if (accesses.empty()) {
      common_tracks = std::move(tracks);
    } else {
      std::erase_if(common_tracks, [&](int64_t y) { return !tracks.contains(y); });
    }
    if (common_tracks.empty()) {
      return fail("no common " + profile_.access.escape + " track");
    }
    accesses.push_back(std::move(result.points));
  }
  int64_t cut_spacing;
  try {
    cut_spacing = ph::signal_cut_spacing(tech_.cut_layers.at(profile_.access.cut), frame_.dbu);
  } catch (const std::invalid_argument& error) {
    return fail(error.what());
  }
  std::map<std::string, std::vector<ph::Shape>> via_shapes;
  ph::Floorplan                                 via_frame;
  via_frame.dbu = frame_.dbu;
  for (auto y : common_tracks) {
    std::vector<std::vector<const ph::Pin_access_point*>> options;
    for (const auto& pin : accesses) {
      options.emplace_back();
      for (const auto& point : pin) {
        if (point.y == y) {
          options.back().push_back(&point);
        }
      }
    }
    std::vector<size_t> choice(options.size(), 0);
    bool                more = true;
    while (more) {
      if (expired()) {
        return out;
      }
      if (out.patterns_examined >= limits.patterns) {
        return fail("pattern budget");
      }
      ++out.patterns_examined;
      Signal_route_geometry                               trial;
      std::set<std::tuple<int64_t, int64_t, std::string>> unique_vias;
      std::vector<ph::Rect>                               cuts;
      ph::Rect                                            span = options[0][choice[0]]->escape.rect;
      for (size_t i = 0; i < choice.size(); ++i) {
        const auto& p = *options[i][choice[i]];
        trial.endpoints.push_back(p);
        span.xl = std::min(span.xl, p.escape.rect.xl);
        span.xh = std::max(span.xh, p.escape.rect.xh);
        if (!unique_vias.emplace(p.x, p.y, p.via).second) {
          continue;
        }
        trial.paths.push_back({
            profile_.access.pin,
            {},
            {},
            {ph::Route_point{p.x, p.y, {}}, ph::Route_via{p.via}}
        });
        if (!via_shapes.contains(p.via)) {
          via_shapes[p.via] = ph::via_geometry(tech_, via_frame, {p.via});
        }
        for (const auto& shape : via_shapes.at(p.via)) {
          if (shape.layer == profile_.access.cut) {
            cuts.push_back({shape.rect.xl + p.x, shape.rect.yl + p.y, shape.rect.xh + p.x, shape.rect.yh + p.y});
          }
        }
      }
      bool legal = rules_.rectangle({profile_.access.escape, span}).legal;
      if (legal) {
        for (auto id : occupancy_.query(profile_.access.escape, span, *clearance, net.name)) {
          if (expired()) {
            return out;
          }
          if (!rules_.spacing({profile_.access.escape, span}, occupancy_.at(id).shape).legal) {
            legal = false;
            break;
          }
        }
      }
      // New endpoint vias must also satisfy cut spacing against each other,
      // including vias on this same net. Access already checked existing cuts.
      std::sort(cuts.begin(), cuts.end(), [](const auto& a, const auto& b) { return a.xl < b.xl; });
      std::vector<ph::Rect> active;
      if (legal) {
        for (const auto& cut : cuts) {
          if (expired()) {
            return out;
          }
          std::erase_if(active, [&](const auto& old) { return old.xh + cut_spacing < cut.xl; });
          for (const auto& old : active) {
            const auto dx = std::max({old.xl - cut.xh, cut.xl - old.xh, 0.});
            const auto dy = std::max({old.yl - cut.yh, cut.yl - old.yh, 0.});
            if ((dx == 0 && dy == 0) || (dx < cut_spacing && dy < cut_spacing)) {
              legal = false;
              break;
            }
          }
          if (!legal) {
            break;
          }
          active.push_back(cut);
        }
      }
      if (legal) {
        trial.paths.push_back({
            profile_.access.escape,
            {},
            {},
            {ph::Route_point{std::llround(span.xl), y, 0}, ph::Route_point{std::llround(span.xh), y, 0}}
        });
        trial.wire_length_um                               = (span.xh - span.xl) / frame_.dbu;
        trial.layer_wire_length_um[profile_.access.escape] = trial.wire_length_um;
        trial.vias                                         = unique_vias.size();
        trial.straight_corridor                            = ph::Shape{profile_.access.escape, span};
        if (expired()) {
          return out;
        }
        bool preserves_access = true;
        if (escapes) {
          ph::Net proposed;
          proposed.name = net.name;
          proposed.wires.push_back({"FIXED", trial.paths});
          const auto access = escapes->check(ph::route_geometry(tech_, frame_, proposed), net.name, expired);
          preserves_access  = access.legal;
          if (!preserves_access) {
            out.reason = access.reason;
          }
        }
        if (preserves_access) {
          out.geometry = std::move(trial);
          return out;
        }
      }
      // Iterative Cartesian enumeration avoids recursion depth depending on
      // net fanout. Work and wall-clock limits bound unsuccessful exploration.
      more = false;
      for (size_t i = 0; i < choice.size(); ++i) {
        if (++choice[i] < options[i].size()) {
          more = true;
          break;
        }
        choice[i] = 0;
      }
    }
  }
  return out.reason.empty() ? fail(profile_.access.escape + " span blocked") : out;
}

Signal_route_trial Signal_router::propose_pattern(const ph::Net& net, const Signal_route_limits& limits) const {
  Signal_route_trial out;
  const auto         expired = [&]() {
    if (std::chrono::steady_clock::now() < limits.deadline) {
      return false;
    }
    out.timed_out = true;
    out.reason    = "route deadline";
    return true;
  };
  const auto fail = [&](const std::string& reason) {
    out.reason = reason;
    return out;
  };
  if (expired()) {
    return out;
  }
  if (net.connections.size() < 2 || !net.wires.empty() || (!net.use.empty() && net.use != "SIGNAL")) {
    return fail("not an unwired multi-pin signal net");
  }
  std::set<std::pair<std::string, std::string>> pins;
  for (const auto& pin : net.connections) {
    if (!pins.emplace(pin.instance, pin.pin).second) {
      return fail("duplicate endpoint");
    }
  }
  if (!profile_.allows(profile_.access.escape) || !profile_.allows(profile_.branch_layer)) {
    return fail("pattern layers excluded by routing profile");
  }
  if (!rules_.clearance(profile_.access.escape) || !rules_.clearance(profile_.branch_layer)) {
    return fail("unsupported " + profile_.access.escape + "/" + profile_.branch_layer + " rules");
  }
  if (tech_.layers.at(profile_.access.escape).direction != "HORIZONTAL"
      || tech_.layers.at(profile_.branch_layer).direction != "VERTICAL") {
    return fail(profile_.access.escape + "/" + profile_.branch_layer + " pattern requires horizontal/vertical layers");
  }
  std::vector<ph::Pin_access_result> accesses(net.connections.size());
  for (size_t i = 0; i < accesses.size(); ++i) {
    const auto cell = components_.find(net.connections[i].instance);
    if (cell == components_.end()) {
      return fail("non-cell endpoint");
    }
    if (out.access_examined >= limits.access_candidates) {
      return fail("pin-access budget");
    }
    accesses[i]          = ph::enumerate_pin_access(tech_,
                                           frame_,
                                           *cell->second,
                                           net.connections[i].pin,
                                           occupancy_,
                                           rules_,
                                           limits.access_candidates - out.access_examined,
                                           net.name,
                                           profile_.access);
    out.access_examined += accesses[i].examined;
    if (expired()) {
      return out;
    }
    if (accesses[i].points.empty()) {
      return fail("pin has no legal access");
    }
  }
  // Only real branch-layer track locations are enumerated. Trunks stay within the
  // endpoints' X span; expanded-box detours belong to the bounded maze stage.
  std::set<int64_t> xs;
  int64_t           xmin = INT64_MAX, xmax = INT64_MIN;
  for (const auto& a : accesses) {
    for (const auto& p : a.points) {
      xmin = std::min(xmin, p.x);
      xmax = std::max(xmax, p.x);
    }
  }
  for (const auto& t : frame_.tracks) {
    if (t.layer != profile_.branch_layer || t.axis != "X") {
      continue;
    }
    if (t.mask || t.same_mask || t.step <= 0 || t.count <= 0 || t.step > INT32_MAX || t.count > INT32_MAX || t.start < INT32_MIN
        || t.start > INT32_MAX) {
      return fail("unsupported " + profile_.branch_layer + " tracks");
    }
    const auto first = std::max<int64_t>(0, std::ceil(double(xmin - t.start) / t.step));
    const auto last  = std::min<int64_t>(t.count - 1, std::floor(double(xmax - t.start) / t.step));
    for (auto i = first; i <= last; ++i) {
      if (expired()) {
        return out;
      }
      if (xs.size() >= limits.patterns) {
        break;
      }
      xs.insert(t.start + i * t.step);
    }
  }
  const auto width2 = tech_.layers.at(profile_.access.escape).width * frame_.dbu;
  const auto width3 = tech_.layers.at(profile_.branch_layer).width * frame_.dbu;
  for (auto w : {width2, width3}) {
    if (!std::isfinite(w) || w <= 0 || w > INT32_MAX || w != std::round(w) || int64_t(w) % 2) {
      return fail("unsupported signal width");
    }
  }
  ph::Floorplan via_frame;
  via_frame.dbu = frame_.dbu;
  for (const auto& [name, via] : tech_.vias) {
    if (expired()) {
      return out;
    }
    if (!via.unsupported.empty() || via.shapes.size() != 3) {
      continue;
    }
    const auto       shapes = ph::via_geometry(tech_, via_frame, {name});
    const ph::Shape *m2 = nullptr, *m3 = nullptr, *cut = nullptr;
    for (const auto& s : shapes) {
      if (s.layer == profile_.access.escape) {
        m2 = &s;
      } else if (s.layer == profile_.branch_layer) {
        m3 = &s;
      } else if (s.layer == profile_.branch_cut) {
        cut = &s;
      }
    }
    if (!m2 || !m3 || !cut || m2->rect.yl != -width2 / 2 || m2->rect.yh != width2 / 2 || m3->rect.xl != -width3 / 2
        || m3->rect.xh != width3 / 2) {
      continue;
    }
    // Iterative access combinations keep stack usage independent of fanout.
    // One branch per escape-layer track avoids overlapping same-net rectangles and
    // duplicate branch-via cuts when several endpoints share that track.
    std::vector<size_t> choice(accesses.size(), 0);
    bool                more = true;
    while (more) {
      if (expired()) {
        return out;
      }
      std::map<int64_t, ph::Shape> branches;
      Signal_route_geometry        seed;
      for (size_t i = 0; i < accesses.size(); ++i) {
        const auto& point = accesses[i].points[choice[i]];
        seed.endpoints.push_back(point);
        auto [it, inserted] = branches.emplace(point.y, point.escape);
        if (!inserted) {
          it->second.rect.xl = std::min(it->second.rect.xl, point.escape.rect.xl);
          it->second.rect.xh = std::max(it->second.rect.xh, point.escape.rect.xh);
        }
      }
      if (branches.size() > 1) {
        for (auto x : xs) {
          if (expired()) {
            return out;
          }
          if (out.patterns_examined >= limits.patterns) {
            return fail("pattern budget");
          }
          ++out.patterns_examined;
          auto      landings = branches;
          ph::Shape trunk{
              profile_.branch_layer,
              {x - width3 / 2, branches.begin()->first + m3->rect.yl, x + width3 / 2, branches.rbegin()->first + m3->rect.yh}
          };
          bool legal = true;
          for (auto& [y, landing] : landings) {
            if (expired()) {
              return out;
            }
            landing.rect.xl = std::min(landing.rect.xl, x + m2->rect.xl);
            landing.rect.xh = std::max(landing.rect.xh, x + m2->rect.xh);
            if (landing.rect.yh - landing.rect.yl != width2
                || !ph::check_signal_via(tech_, frame_, rules_, occupancy_, {name}, x, y, {landing, trunk}, net.name).legal) {
              legal = false;
              break;
            }
          }
          for (auto i = landings.begin(); legal && i != landings.end(); ++i) {
            for (auto j = landings.begin(); j != i; ++j) {
              if (expired()) {
                return out;
              }
              if (!rules_.spacing(i->second, j->second).legal) {
                legal = false;
                break;
              }
            }
          }
          if (!legal) {
            continue;
          }
          auto                                                trial = seed;
          std::set<std::tuple<std::string, int64_t, int64_t>> vias;
          for (const auto& endpoint : trial.endpoints) {
            vias.emplace(endpoint.via, endpoint.x, endpoint.y);
          }
          for (const auto& [y, landing] : landings) {
            vias.emplace(name, x, y);
          }
          std::vector<ph::Shape> cuts;
          for (const auto& [vname, vx, vy] : vias) {
            if (expired()) {
              return out;
            }
            trial.paths.push_back({
                vname == name ? profile_.access.escape : profile_.access.pin,
                {},
                {},
                {ph::Route_point{vx, vy, {}}, ph::Route_via{vname}}
            });
            for (auto shape : ph::via_geometry(tech_, via_frame, {vname})) {
              if (tech_.cut_layers.contains(shape.layer)) {
                shape.rect.xl += vx;
                shape.rect.xh += vx;
                shape.rect.yl += vy;
                shape.rect.yh += vy;
                cuts.push_back(shape);
              }
            }
          }
          // Access and landing checks cover existing metal. All newly added
          // cuts must also obey spacing against each other, even on one net.
          for (size_t i = 0; legal && i < cuts.size(); ++i) {
            for (size_t j = 0; j < i; ++j) {
              if (expired()) {
                return out;
              }
              if (cuts[i].layer != cuts[j].layer) {
                continue;
              }
              const auto  spacing = ph::signal_cut_spacing(tech_.cut_layers.at(cuts[i].layer), frame_.dbu);
              const auto& u       = cuts[i].rect;
              const auto& v       = cuts[j].rect;
              const auto  dx      = std::max({u.xl - v.xh, v.xl - u.xh, 0.});
              const auto  dy      = std::max({u.yl - v.yh, v.yl - u.yh, 0.});
              if ((dx == 0 && dy == 0) || (dx < spacing && dy < spacing)) {
                legal = false;
                break;
              }
            }
          }
          if (!legal) {
            continue;
          }
          for (const auto& [y, landing] : landings) {
            trial.paths.push_back({
                profile_.access.escape,
                {},
                {},
                {ph::Route_point{std::llround(landing.rect.xl), y, 0}, ph::Route_point{std::llround(landing.rect.xh), y, 0}}
            });
            trial.layer_wire_length_um[profile_.access.escape] += (landing.rect.xh - landing.rect.xl) / frame_.dbu;
          }
          trial.paths.push_back({
              profile_.branch_layer,
              {},
              {},
              {ph::Route_point{x, std::llround(trunk.rect.yl), 0}, ph::Route_point{x, std::llround(trunk.rect.yh), 0}}
          });
          trial.layer_wire_length_um[profile_.branch_layer] = (trunk.rect.yh - trunk.rect.yl) / frame_.dbu;
          trial.wire_length_um
              = trial.layer_wire_length_um[profile_.access.escape] + trial.layer_wire_length_um[profile_.branch_layer];
          trial.vias  = vias.size();
          auto routed = net;
          routed.wires.push_back({"FIXED", trial.paths});
          const auto connected = ph::check_route_connectivity(tech_, frame_, routed, &components_);
          if (expired()) {
            return out;
          }
          if (connected.connected) {
            out.geometry = std::move(trial);
            return out;
          }
        }
      }
      more = false;
      for (size_t i = choice.size(); i-- > 0;) {
        if (++choice[i] < accesses[i].points.size()) {
          more = true;
          break;
        }
        choice[i] = 0;
      }
    }
  }
  return fail("no legal " + profile_.access.escape + "/" + profile_.branch_layer + " pattern");
}
}  // namespace livehd::placement
