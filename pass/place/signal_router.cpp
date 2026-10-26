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
ph::Pin_access_result Signal_router::endpoint_access(const ph::Component& cell, const std::string& pin, const ph::Net& net,
                                                     const Signal_route_limits& limits, Signal_route_trial& trial) const {
  auto result            = ph::enumerate_pin_access(tech_,
                                         frame_,
                                         cell,
                                         pin,
                                         occupancy_,
                                         rules_,
                                         limits.access_candidates - trial.access_examined,
                                         net.name,
                                         profile_.access);
  trial.access_examined += result.examined;
  return result;
}
std::vector<ph::Shape> Signal_router::proposal_shapes(const ph::Net& net, const std::vector<ph::Route_path>& paths,
                                                      const ph::Route_width_rules* rules) const {
  ph::Net proposed;
  proposed.name = net.name;
  proposed.wires.push_back({"FIXED", paths});
  return ph::route_geometry(tech_, frame_, proposed, false, rules);
}
ph::Route_connectivity Signal_router::connectivity(const ph::Net& net, const std::vector<ph::Route_path>& paths,
                                                   const ph::Route_width_rules* rules) const {
  auto routed = net;
  routed.wires.push_back({"FIXED", paths});
  return ph::check_route_connectivity(tech_, frame_, routed, &components_, rules);
}
void Signal_router::add_wire(Signal_route_geometry& geometry, const ph::Shape& metal, std::string taper_rule) const {
  const bool  horizontal = tech_.layers.at(metal.layer).direction == "HORIZONTAL";
  const auto& r          = metal.rect;
  geometry.paths.push_back(ph::flush_wire_path(metal, horizontal, std::move(taper_rule)));
  geometry.layer_wire_length_um[metal.layer] += (horizontal ? r.xh - r.xl : r.yh - r.yl) / frame_.dbu;
  // Total in layer order, so it is exactly the sum the per-layer report shows.
  geometry.wire_length_um                     = 0;
  for (const auto& [layer, length] : geometry.layer_wire_length_um) {
    geometry.wire_length_um += length;
  }
}
bool Signal_router::Stage_trial::expired() const {
  if (std::chrono::steady_clock::now() < deadline) {
    return false;
  }
  out.timed_out = true;
  out.reason    = "route deadline";
  return true;
}
Signal_route_trial Signal_router::Stage_trial::fail(std::string reason) const {
  out.reason = std::move(reason);
  return out;
}
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
  const auto expired = [&] { return std::chrono::steady_clock::now() >= request.limits.deadline; };
  // All proposal strategies require search work. Reject exhausted requests
  // before constructing the whole-frame pin-access guard.
  if (expired()) {
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
  // The screen records each candidate's exposure, so it must not run as a
  // (non-mutating) erase_if predicate.
  std::string access_rejection;
  bool        rejected_parallel = false;
  const auto  rejected          = [&](Signal_route_geometry& candidate) {
    if (expired()) {
      result.timed_out = true;
      return true;
    }
    const auto shapes = proposal_shapes(net, candidate.paths, &candidate.nondefault_rules);
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
        if (expired()) {
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
  };
  std::vector<Signal_route_geometry> accepted;
  for (auto& candidate : result.candidates) {
    if (!rejected(candidate)) {
      accepted.push_back(std::move(candidate));
    }
  }
  result.candidates = std::move(accepted);
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
    const auto connected = connectivity(net, candidate.paths);
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
  const Stage_trial  stage{out, limits.deadline};
  if (stage.expired()) {
    return out;
  }
  if (!ph::unrouted_signal(net) || net.connections.size() != 2 || aligned.endpoints.size() != 2) {
    return stage.fail("layer lift requires an unwired aligned two-pin signal");
  }
  const auto& a          = aligned.endpoints[0];
  const auto& b          = aligned.endpoints[1];
  const bool  horizontal = a.y == b.y;
  if ((a.x == b.x) == horizontal) {
    return stage.fail("layer lift requires distinct aligned endpoints");
  }
  const auto end = std::find(tech_.routing_layers.begin(), tech_.routing_layers.end(), target);
  if (end == tech_.routing_layers.end() || !profile_.allows(target)) {
    return stage.fail("unsupported lift layer");
  }
  const auto start = std::find(tech_.routing_layers.begin(), tech_.routing_layers.end(), a.escape.layer);
  if (start >= end || b.escape.layer != a.escape.layer
      || tech_.layers.at(target).direction != (horizontal ? "HORIZONTAL" : "VERTICAL")) {
    return stage.fail("lift must preserve trunk direction");
  }
  const auto grid_dbu = ph::explicit_manufacturing_grid_dbu(tech_, frame_.dbu);
  if (!grid_dbu) {
    return stage.fail("unsupported manufacturing grid");
  }
  const double          grid = *grid_dbu;
  Signal_route_geometry trial;
  trial.endpoints = aligned.endpoints;
  std::vector<ph::Shape> metal;
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
    if (stage.expired()) {
      return out;
    }
    if (!rules_.clearance(*layer)) {
      return stage.fail("unsupported lift rules");
    }
    const auto&  definition = tech_.layers.at(*layer);
    const auto   nominal    = ph::wire_width_dbu(definition, frame_.dbu);
    const double area       = definition.rules.area * frame_.dbu * frame_.dbu;
    if (!nominal || *nominal % (2 * *grid_dbu) || !std::isfinite(area) || area < 0
        || (definition.direction != "HORIZONTAL" && definition.direction != "VERTICAL")) {
      return stage.fail("unsupported lift dimensions");
    }
    const double width = *nominal;
    const bool   h     = definition.direction == "HORIZONTAL";
    double       half  = ph::min_area_half_length(width, area, grid);
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
        shape.rect = shape.rect.hull(point.escape.rect);
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
      if (!rules_.rectangle(shape).legal || !frame_.die.contains(shape.rect)) {
        return stage.fail("illegal lift landing");
      }
      for (auto id : occupancy_.query(*layer, shape.rect, *rules_.clearance(*layer), net.name)) {
        if (stage.expired()) {
          return out;
        }
        if (!rules_.spacing(shape, occupancy_.at(id).shape).legal) {
          return stage.fail("blocked lift landing");
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
        if (stage.expired()) {
          return out;
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
          return stage.fail("lift pattern budget");
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
        return stage.fail("no legal endpoint via stack");
      }
    }
  }
  for (size_t i = 0; i < metal.size(); ++i) {
    const auto& shape = metal[i];
    for (size_t j = 0; j < i; ++j) {
      if (metal[j].layer == shape.layer && !rules_.spacing(shape, metal[j]).legal) {
        return stage.fail("lift self spacing");
      }
    }
    add_wire(trial, shape);
  }
  ph::Signal_cut_set cuts(tech_, frame_);
  for (const auto& via : vias) {
    trial.paths.push_back(ph::via_path(via.lower, via.x, via.y, via.name));
    if (!cuts.add({via.name}, via.x, via.y)) {
      return stage.fail("lift cut spacing");
    }
  }
  const auto connected = connectivity(net, trial.paths);
  if (!connected.connected) {
    return stage.fail(connected.reason);
  }
  if (stage.expired()) {
    return out;
  }
  trial.vias   = vias.size();
  out.geometry = std::move(trial);
  return out;
}
Signal_route_trial Signal_router::propose_straight(const ph::Net& net, const ph::Shape& corridor,
                                                   const Signal_route_limits& limits) const {
  Signal_route_trial out;
  const Stage_trial  stage{out, limits.deadline};
  if (stage.expired()) {
    return out;
  }
  if (!limits.patterns) {
    return stage.fail("pattern budget");
  }
  if (!ph::unrouted_signal(net) || net.connections.size() != 2) {
    return stage.fail("straight route requires an unwired two-pin signal net");
  }
  const auto layer     = tech_.layers.find(corridor.layer);
  const auto clearance = rules_.clearance(corridor.layer);
  if (layer == tech_.layers.end() || !clearance) {
    return stage.fail("unsupported straight-route layer");
  }
  const bool horizontal = layer->second.direction == "HORIZONTAL";
  if (!horizontal && layer->second.direction != "VERTICAL") {
    return stage.fail("missing route direction");
  }
  const double half = layer->second.width * frame_.dbu / 2;
  const auto&  r    = corridor.rect;
  const double x1   = horizontal ? r.xl + half : (r.xl + r.xh) / 2;
  const double y1   = horizontal ? (r.yl + r.yh) / 2 : r.yl + half;
  const double x2   = horizontal ? r.xh - half : x1;
  const double y2   = horizontal ? y1 : r.yh - half;
  for (double v : {x1, y1, x2, y2}) {
    if (!std::isfinite(v) || std::abs(v) > double(std::numeric_limits<int32_t>::max()) || std::abs(v - std::round(v)) > 1e-6) {
      return stage.fail("straight-route endpoint is not integral DBU");
    }
  }
  if (x2 < x1 || y2 < y1 || (x1 == x2 && y1 == y2)) {
    return stage.fail("empty straight route");
  }
  Signal_route_geometry geometry;
  geometry.straight_corridor = corridor;
  geometry.paths.push_back({
      corridor.layer,
      {},
      {},
      {ph::Route_point{std::llround(x1), std::llround(y1), {}}, ph::Route_point{std::llround(x2), std::llround(y2), {}}}
  });
  const auto shapes = proposal_shapes(net, geometry.paths);
  if (shapes.size() != 1) {
    return stage.fail("straight route must have one metal rectangle");
  }
  ++out.patterns_examined;
  const auto rule = rules_.rectangle(shapes.front());
  if (!rule.legal) {
    return stage.fail(rule.reason);
  }
  for (auto id : occupancy_.query(corridor.layer, shapes.front().rect, *clearance, net.name)) {
    if (stage.expired()) {
      return out;
    }
    const auto check = rules_.spacing(shapes.front(), occupancy_.at(id).shape);
    if (!check.legal) {
      return stage.fail(check.reason);
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
      return stage.fail("non-cell straight-route endpoint");
    }
    const auto& c     = *cell->second;
    const auto& m     = tech_.macros.at(c.macro);
    const auto  found = m.pins.find(pin.pin);
    if (found == m.pins.end()) {
      return stage.fail("unknown straight-route pin");
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
        const auto pin_rect = ph::placed_rect(m, c.place, frame_.dbu, shape.rect);
        const auto [x, y]   = points[i];
        if (x < pin_rect.xl || x > pin_rect.xh || y < pin_rect.yl || y > pin_rect.yh) {
          continue;
        }
        geometry.endpoints.push_back({std::llround(x), std::llround(y), {}, corridor, corridor.layer});
        used.insert(i);
        matched = true;
        break;
      }
    }
    if (!matched) {
      return stage.fail("straight-route endpoint misses its pin");
    }
  }
  const auto connected = connectivity(net, geometry.paths);
  if (!connected.connected) {
    return stage.fail(connected.reason);
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
  const Stage_trial  stage{out, limits.deadline};
  if (stage.expired()) {
    return out;
  }
  if (!net.wires.empty()) {
    return stage.fail("net already has wiring");
  }
  if (!ph::unrouted_signal(net)) {
    return stage.fail("not a multi-terminal signal net");
  }
  if (!profile_.allows(profile_.access.escape)) {
    return stage.fail("access layer excluded by routing profile");
  }
  const auto clearance = rules_.clearance(profile_.access.escape);
  if (!clearance || !tech_.layers.contains(profile_.access.escape) || !tech_.cut_layers.contains(profile_.access.cut)) {
    return stage.fail("unsupported " + profile_.access.escape + "/" + profile_.access.cut + " rules");
  }
  if (tech_.layers.at(profile_.access.escape).direction != "HORIZONTAL") {
    return stage.fail(profile_.access.escape + " span requires a horizontal layer");
  }
  const auto escape_width = ph::wire_width_dbu(tech_.layers.at(profile_.access.escape), frame_.dbu);
  if (!escape_width) {
    return stage.fail("unsupported signal width");
  }
  const double wire_width = *escape_width;
  if (corridor
      && (corridor->layer != profile_.access.escape || !rules_.rectangle(*corridor).legal
          || corridor->rect.yh - corridor->rect.yl != wire_width)) {
    return stage.fail("invalid aligned " + profile_.access.escape + " corridor");
  }
  std::set<std::pair<std::string, std::string>>  pins;
  std::vector<std::vector<ph::Pin_access_point>> accesses;
  std::set<int64_t>                              common_tracks;
  for (const auto& connection : net.connections) {
    if (stage.expired()) {
      return out;
    }
    const auto cell = components_.find(connection.instance);
    if (cell == components_.end() || !pins.emplace(connection.instance, connection.pin).second) {
      return stage.fail("non-cell or duplicate endpoint");
    }
    if (out.access_examined >= limits.access_candidates) {
      return stage.fail("pin-access budget");
    }
    auto result = endpoint_access(*cell->second, connection.pin, net, limits, out);
    if (stage.expired()) {
      return out;
    }
    if (result.points.empty()) {
      return stage.fail(result.exhausted ? "pin-access budget" : "pin has no legal access");
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
      return stage.fail("no common " + profile_.access.escape + " track");
    }
    accesses.push_back(std::move(result.points));
  }
  int64_t cut_spacing;
  try {
    cut_spacing = ph::signal_cut_spacing(tech_.cut_layers.at(profile_.access.cut), frame_.dbu);
  } catch (const std::invalid_argument& error) {
    return stage.fail(error.what());
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
      if (stage.expired()) {
        return out;
      }
      if (out.patterns_examined >= limits.patterns) {
        return stage.fail("pattern budget");
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
        trial.paths.push_back(ph::via_path(profile_.access.pin, p.x, p.y, p.via));
        if (!via_shapes.contains(p.via)) {
          via_shapes[p.via] = ph::via_geometry(tech_, via_frame, {p.via});
        }
        for (const auto& shape : via_shapes.at(p.via)) {
          if (shape.layer == profile_.access.cut) {
            cuts.push_back(shape.rect.translated(p.x, p.y));
          }
        }
      }
      bool legal = rules_.rectangle({profile_.access.escape, span}).legal;
      if (legal) {
        for (auto id : occupancy_.query(profile_.access.escape, span, *clearance, net.name)) {
          if (stage.expired()) {
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
          if (stage.expired()) {
            return out;
          }
          std::erase_if(active, [&](const auto& old) { return old.xh + cut_spacing < cut.xl; });
          for (const auto& old : active) {
            if (ph::violates_cut_spacing(old, cut, cut_spacing)) {
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
        add_wire(trial, {profile_.access.escape, span});
        trial.vias              = unique_vias.size();
        trial.straight_corridor = ph::Shape{profile_.access.escape, span};
        if (stage.expired()) {
          return out;
        }
        bool preserves_access = true;
        if (escapes) {
          const auto access = escapes->check(proposal_shapes(net, trial.paths), net.name, [&] { return stage.expired(); });
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
  return out.reason.empty() ? stage.fail(profile_.access.escape + " span blocked") : out;
}

Signal_route_trial Signal_router::propose_pattern(const ph::Net& net, const Signal_route_limits& limits) const {
  Signal_route_trial out;
  const Stage_trial  stage{out, limits.deadline};
  if (stage.expired()) {
    return out;
  }
  if (!ph::unrouted_signal(net)) {
    return stage.fail("not an unwired multi-pin signal net");
  }
  std::set<std::pair<std::string, std::string>> pins;
  for (const auto& pin : net.connections) {
    if (!pins.emplace(pin.instance, pin.pin).second) {
      return stage.fail("duplicate endpoint");
    }
  }
  if (!profile_.allows(profile_.access.escape) || !profile_.allows(profile_.branch_layer)) {
    return stage.fail("pattern layers excluded by routing profile");
  }
  if (!rules_.clearance(profile_.access.escape) || !rules_.clearance(profile_.branch_layer)) {
    return stage.fail("unsupported " + profile_.access.escape + "/" + profile_.branch_layer + " rules");
  }
  if (tech_.layers.at(profile_.access.escape).direction != "HORIZONTAL"
      || tech_.layers.at(profile_.branch_layer).direction != "VERTICAL") {
    return stage.fail(profile_.access.escape + "/" + profile_.branch_layer + " pattern requires horizontal/vertical layers");
  }
  std::vector<ph::Pin_access_result> accesses(net.connections.size());
  for (size_t i = 0; i < accesses.size(); ++i) {
    const auto cell = components_.find(net.connections[i].instance);
    if (cell == components_.end()) {
      return stage.fail("non-cell endpoint");
    }
    if (out.access_examined >= limits.access_candidates) {
      return stage.fail("pin-access budget");
    }
    accesses[i] = endpoint_access(*cell->second, net.connections[i].pin, net, limits, out);
    if (stage.expired()) {
      return out;
    }
    if (accesses[i].points.empty()) {
      return stage.fail("pin has no legal access");
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
    if (!t.supported()) {
      return stage.fail("unsupported " + profile_.branch_layer + " tracks");
    }
    const auto [first, last] = t.index_range(double(xmin), double(xmax));
    for (auto i = first; i <= last; ++i) {
      if (stage.expired()) {
        return out;
      }
      if (xs.size() >= limits.patterns) {
        break;
      }
      xs.insert(t.start + i * t.step);
    }
  }
  const auto escape_width = ph::wire_width_dbu(tech_.layers.at(profile_.access.escape), frame_.dbu);
  const auto branch_width = ph::wire_width_dbu(tech_.layers.at(profile_.branch_layer), frame_.dbu);
  if (!escape_width || !branch_width) {
    return stage.fail("unsupported signal width");
  }
  const double  width2 = *escape_width, width3 = *branch_width;
  ph::Floorplan via_frame;
  via_frame.dbu = frame_.dbu;
  for (const auto& [name, via] : tech_.vias) {
    if (stage.expired()) {
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
      if (stage.expired()) {
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
          if (stage.expired()) {
            return out;
          }
          if (out.patterns_examined >= limits.patterns) {
            return stage.fail("pattern budget");
          }
          ++out.patterns_examined;
          auto      landings = branches;
          ph::Shape trunk{
              profile_.branch_layer,
              {x - width3 / 2, branches.begin()->first + m3->rect.yl, x + width3 / 2, branches.rbegin()->first + m3->rect.yh}
          };
          bool legal = true;
          for (auto& [y, landing] : landings) {
            if (stage.expired()) {
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
              if (stage.expired()) {
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
          ph::Signal_cut_set cuts(tech_, frame_);
          for (const auto& [vname, vx, vy] : vias) {
            if (stage.expired()) {
              return out;
            }
            trial.paths.push_back(ph::via_path(vname == name ? profile_.access.escape : profile_.access.pin, vx, vy, vname));
            legal = legal && cuts.add({vname}, vx, vy);
          }
          if (!legal) {
            continue;
          }
          for (const auto& [y, landing] : landings) {
            add_wire(trial, landing);
          }
          add_wire(trial, trunk);
          trial.vias           = vias.size();
          const auto connected = connectivity(net, trial.paths);
          if (stage.expired()) {
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
  return stage.fail("no legal " + profile_.access.escape + "/" + profile_.branch_layer + " pattern");
}
}  // namespace livehd::placement
