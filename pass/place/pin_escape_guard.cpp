// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_escape_guard.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

#include "inou/lefdef/cut_rules.hpp"
#include "inou/lefdef/routing_connectivity.hpp"
#include "inou/lefdef/routing_geometry.hpp"

namespace livehd::placement {
namespace ph = physical;
Pin_escape_guard::Pin_escape_guard(const ph::Technology& tech, const ph::Floorplan& frame, const ph::Routing_occupancy& occupancy,
                                   size_t access_budget, const std::function<void()>& checkpoint, ph::Pin_access_layers layers,
                                   const ph::Signal_metal_rules* rules)
    : layers_(std::move(layers))
    , owned_rules_(rules ? nullptr : std::make_unique<ph::Signal_metal_rules>(tech, frame))
    , rules_(rules ? *rules : *owned_rules_)
    , index_(frame.dbu)
    , uncertain_index_(frame.dbu) {
  const auto clearance = rules_.clearance(layers_.escape);
  const auto cut       = tech.cut_layers.find(layers_.cut);
  if (!clearance || cut == tech.cut_layers.end()) {
    incomplete_reason_ = "unsupported pin escape stack";
    return;
  }
  metal_clearance_       = *clearance;
  const auto cut_spacing = ph::unqualified_cut_spacing(cut->second, frame.dbu);
  if (!cut_spacing) {
    throw std::invalid_argument("invalid pin escape cut clearance");
  }
  cut_clearance_ = *cut_spacing;
  // Unknown control/supply access reserves a conservative local volume rather
  // than disappearing. Include via overhang and directional escape distance.
  std::map<std::string, double> via_reach;
  for (const auto& [name, via] : tech.vias) {
    // Match the native access oracle's single-cut LEF candidate family. Large
    // DEF PG via definitions are not pin-access candidates; their placed shapes
    // are checked separately in occupancy and must not inflate local margins.
    if (!via.unsupported.empty() || via.shapes.size() != 3) {
      continue;
    }
    if (checkpoint) {
      checkpoint();
    }
    for (const auto& shape : via.shapes) {
      for (auto v : {shape.rect.xl, shape.rect.yl, shape.rect.xh, shape.rect.yh}) {
        if (!std::isfinite(v)) {
          incomplete_reason_ = "invalid via geometry in pin protection";
          return;
        }
        via_reach[shape.layer] = std::max(via_reach[shape.layer], std::abs(v) * frame.dbu);
      }
    }
  }
  for (const auto& [name, layer] : tech.layers) {
    const auto   spacing   = rules_.clearance(name);
    const double base      = spacing ? std::ceil(*spacing + via_reach[name]) : -1;
    // The lower landing stays inside the existing pin metal. Only the escape
    // layer needs a three-width extension, along its preferred direction.
    const double extension = name == layers_.escape ? std::ceil(3 * layer.width * frame.dbu) : 0;
    if (!std::isfinite(base) || base < 0 || !std::isfinite(extension) || extension < 0 || base + extension > INT32_MAX) {
      uncertainty_margin_[name] = {-1, -1};
    } else {
      uncertainty_margin_[name] = {int64_t(base + (layer.direction == "HORIZONTAL" ? extension : 0)),
                                   int64_t(base + (layer.direction == "VERTICAL" ? extension : 0))};
    }
  }
  for (const auto& [name, layer] : tech.cut_layers) {
    double margin = via_reach[name];
    for (double spacing : layer.spacing) {
      margin = std::max(margin, via_reach[name] + spacing * frame.dbu);
    }
    const auto value
        = layer.unsupported.empty() && layer.properties.empty() && std::isfinite(margin) && margin >= 0 && margin <= INT32_MAX
              ? int64_t(std::ceil(margin))
              : -1;
    uncertainty_margin_[name] = {value, value};
  }
  const auto reserve_unknown = [&](const ph::Component& cell, const ph::Net& net, const std::string& pin_name) {
    const auto master = tech.macros.find(cell.macro);
    if (master == tech.macros.end()) {
      incomplete_reason_ = "missing master in pin protection";
      return;
    }
    const auto pin = master->second.pins.find(pin_name);
    if (pin == master->second.pins.end()) {
      incomplete_reason_ = "missing pin in pin protection";
      return;
    }
    const auto& use = pin->second.use;
    if (use != "CLOCK" && use != "POWER" && use != "GROUND" && net.use != "CLOCK" && net.use != "POWER" && net.use != "GROUND") {
      return;
    }
    const auto& m     = master->second;
    const auto& place = cell.place;
    ph::Rect    bounds{0, 0, m.width, m.height};
    // Rectangular supply rails have a known physical footprint even when the
    // signal via-access algorithm cannot enumerate them. Reserve that footprint
    // plus the conservative escape/via margin, not the entire standard cell.
    if ((use == "POWER" || use == "GROUND") && !pin->second.shapes.empty()
        && std::all_of(pin->second.shapes.begin(), pin->second.shapes.end(), [&](const auto& shape) {
             const auto& r = shape.rect;
             return tech.layers.contains(shape.layer) && std::isfinite(r.xl) && std::isfinite(r.yl) && std::isfinite(r.xh)
                    && std::isfinite(r.yh) && r.xl < r.xh && r.yl < r.yh;
           })) {
      bounds = pin->second.shapes.front().rect.translated(m.origin_x, m.origin_y);
    }
    for (const auto& shape : pin->second.shapes) {
      bounds = bounds.hull(shape.rect.translated(m.origin_x, m.origin_y));
    }
    const auto     local = ph::orient_rect(bounds, m.width, m.height, place.orientation);
    const ph::Rect region{std::floor(place.x + local.xl * frame.dbu),
                          std::floor(place.y + local.yl * frame.dbu),
                          std::ceil(place.x + local.xh * frame.dbu),
                          std::ceil(place.y + local.yh * frame.dbu)};
    for (auto v : {region.xl, region.yl, region.xh, region.yh}) {
      if (!std::isfinite(v) || std::abs(v) > INT32_MAX) {
        incomplete_reason_ = "invalid unresolved pin bounds";
        return;
      }
    }
    if (place.status == ph::Status::UNPLACED || region.empty()) {
      incomplete_reason_ = "unplaced or empty unresolved pin bounds";
      return;
    }
    ++unresolved_pins_;
    if (checkpoint) {
      checkpoint();
    }
    // Every layer reserves the same footprint; only its query margin differs.
    // Keep one spatial record instead of duplicating it across the metal stack.
    uncertain_index_.insert({
        {"unresolved", region},
        net.name
    });
    uncertain_labels_.push_back(cell.name + ":" + pin_name);
  };
  ph::Route_components                          components(frame);
  std::map<std::string, size_t>                 group_ids;
  // Access is a physical property, including pins absent from signal NETS.
  // Retain only connection lists here; never copy the potentially large PG wires.
  std::vector<ph::Net>                          pending;
  std::set<std::pair<std::string, std::string>> connected;
  const auto                                    collect = [&](const ph::Net& net, bool special) {
    if (checkpoint) {
      checkpoint();
    }
    const bool complete = !special && !net.wires.empty() && ph::check_route_connectivity(tech, frame, net, &components).connected;
    ph::Net    item;
    item.name      = net.name;
    item.use       = net.use;
    const auto add = [&](const std::string& instance, const std::string& pin) {
      connected.emplace(instance, pin);
      if (!complete) {
        item.connections.push_back({instance, pin});
      }
    };
    for (const auto& connection : net.connections) {
      if (checkpoint) {
        checkpoint();
      }
      if (connection.instance == "*") {
        for (const auto& cell : frame.components) {
          if (checkpoint) {
            checkpoint();
          }
          const auto master = tech.macros.find(cell.macro);
          if (master != tech.macros.end() && master->second.pins.contains(connection.pin)) {
            add(cell.name, connection.pin);
          }
        }
      } else {
        add(connection.instance, connection.pin);
      }
    }
    if (!item.connections.empty()) {
      pending.push_back(std::move(item));
    }
  };
  for (const auto& net : frame.nets) {
    collect(net, false);
  }
  for (const auto& net : frame.special_nets) {
    collect(net, true);
  }
  for (const auto& cell : frame.components) {
    if (checkpoint) {
      checkpoint();
    }
    const auto master = tech.macros.find(cell.macro);
    if (master == tech.macros.end()) {
      continue;
    }
    for (const auto& [name, pin] : master->second.pins) {
      if ((pin.use == "CLOCK" || pin.use == "POWER" || pin.use == "GROUND") && !connected.contains({cell.name, name})) {
        // Empty owner means a physical pin not assigned to any net yet. It must
        // never be skipped by a route's own-net exemption.
        ph::Net item;
        item.connections.push_back({cell.name, name});
        pending.push_back(std::move(item));
      }
    }
  }
  std::map<std::string, std::vector<ph::Shape>> access_vias;
  for (const auto& net : pending) {
    for (const auto& connection : net.connections) {
      if (checkpoint) {
        checkpoint();
      }
      const auto cell = components.find(connection.instance);
      if (cell == components.end()) {
        continue;
      }
      // A supply pin already touching decoded same-net metal has a physical
      // attachment. Do not confuse its own LEF rectangle in occupancy with a
      // connection, or require a second hypothetical via through the PG cuts.
      const auto master          = tech.macros.find(cell->second->macro);
      bool       supply_attached = false;
      if (!net.name.empty() && master != tech.macros.end() && !master->second.virtual_proxy
          && cell->second->place.status != ph::Status::UNPLACED) {
        const auto pin = master->second.pins.find(connection.pin);
        if (pin != master->second.pins.end() && (pin->second.use == "POWER" || pin->second.use == "GROUND")) {
          const auto& m     = master->second;
          const auto& place = cell->second->place;
          for (const auto& shape : pin->second.shapes) {
            if (!tech.layers.contains(shape.layer)) {
              continue;
            }
            auto box           = ph::placed_rect(m, place, frame.dbu, shape.rect);
            bool representable = true;
            for (auto* v : {&box.xl, &box.yl, &box.xh, &box.yh}) {
              representable &= std::isfinite(*v) && std::abs(*v) <= INT32_MAX && std::abs(*v - std::round(*v)) <= 1e-6;
              *v             = std::round(*v);
            }
            if (!representable || box.empty()) {
              continue;
            }
            for (auto id : occupancy.query(shape.layer, box, 0)) {
              if (checkpoint) {
                checkpoint();
              }
              const auto wire = occupancy.at(id);
              if (wire.routed && wire.net == net.name && box.overlaps(wire.shape.rect)) {
                supply_attached = true;
                break;
              }
            }
            if (supply_attached) {
              break;
            }
          }
        }
      }
      if (supply_attached) {
        continue;
      }
      const auto access = ph::enumerate_pin_access(tech,
                                                   frame,
                                                   *cell->second,
                                                   connection.pin,
                                                   occupancy,
                                                   rules_,
                                                   access_budget,
                                                   net.name,
                                                   layers_);
      if (access.points.empty()) {
        reserve_unknown(*cell->second, net, connection.pin);
        if (access.exhausted && incomplete_reason_.empty()) {
          incomplete_reason_ = "pin access budget exhausted while protecting " + connection.instance + ":" + connection.pin;
        }
        continue;
      }
      const size_t pin_id    = pins_.size();
      auto [group, inserted] = group_ids.try_emplace(connection.instance, groups_.size());
      if (inserted) {
        groups_.emplace_back();
      }
      groups_[group->second].push_back(pin_id);
      pins_.push_back({net.name, connection.instance + ":" + connection.pin, 0, group->second, {}});
      for (const auto& point : access.points) {
        if (checkpoint) {
          checkpoint();
        }
        // Each access template is immutable within this guard. Decode once,
        // not once per access point of every pin in the candidate frame.
        auto [via, new_via] = access_vias.try_emplace(point.via);
        if (new_via) {
          via->second = ph::via_geometry(tech, frame, {point.via});
        }
        const auto& shapes = via->second;
        const auto  via_cut
            = std::find_if(shapes.begin(), shapes.end(), [&](const auto& shape) { return shape.layer == layers_.cut; });
        if (via_cut == shapes.end()) {
          throw std::logic_error("pin escape has no " + layers_.cut + " cut");
        }
        const ph::Shape shape{via_cut->layer, via_cut->rect.translated(point.x, point.y)};
        const auto      upper
            = std::find_if(shapes.begin(), shapes.end(), [&](const auto& item) { return item.layer == layers_.escape; });
        if (upper == shapes.end()) {
          throw std::logic_error("pin escape has no " + layers_.escape + " landing");
        }
        const ph::Shape landing{upper->layer, upper->rect.translated(point.x, point.y)};
        // OpenROAD's standard-cell access check requires a preferred-direction
        // escape three wire widths long, with a half-width end cap. Preserve
        // either direction independently; blocking one must not retire both.
        const auto& layer  = tech.layers.at(layers_.escape);
        const auto  width  = std::llround(layer.width * frame.dbu);
        const auto  half   = width / 2;
        for (int direction : {-1, 1}) {
          ph::Shape probe{
              layers_.escape,
              {double(point.x - half), double(point.y - half), double(point.x + half), double(point.y + half)}
          };
          if (layer.direction == "HORIZONTAL") {
            probe.rect.xl = direction < 0 ? point.x - 3 * width - half : point.x;
            probe.rect.xh = direction > 0 ? point.x + 3 * width + half : point.x;
          } else {
            probe.rect.yl = direction < 0 ? point.y - 3 * width - half : point.y;
            probe.rect.yh = direction > 0 ? point.y + 3 * width + half : point.y;
          }
          bool clear = true;
          for (const auto& access_shape : {landing, probe}) {
            for (auto obstacle : occupancy.query(layers_.escape, access_shape.rect, metal_clearance_, net.name)) {
              if (checkpoint) {
                checkpoint();
              }
              if (!rules_.spacing(access_shape, occupancy.at(obstacle).shape).legal) {
                clear = false;
                break;
              }
            }
            if (!clear) {
              break;
            }
          }
          if (!clear) {
            continue;
          }
          const size_t id = points_.size();
          points_.push_back({pin_id, point.escape, landing, shape, probe});
          pins_.back().points.push_back(id);
          ++pins_.back().remaining;
          for (const auto& indexed : {point.escape, landing, shape, probe}) {
            index_.insert({indexed, net.name});
            indexed_points_.push_back(id);
          }
        }
      }
      if (pins_.back().points.empty()) {
        reserve_unknown(*cell->second, net, connection.pin);
        groups_[group->second].pop_back();
        pins_.pop_back();
      }
    }
  }
  // Only claim preservation of combinations which exist in the input frame.
  for (size_t group = 0; group < groups_.size(); ++group) {
    if (checkpoint) {
      checkpoint();
    }
    group_supported_.push_back(compatible(group, {}, {}, [&] {
      if (checkpoint) {
        checkpoint();
      }
      return false;
    }));
  }
}
bool Pin_escape_guard::compatible(size_t group, const std::set<size_t>& blocked, const std::string& retiring,
                                  const std::function<bool()>& expired) const {
  std::vector<std::vector<size_t>> choices;
  for (auto pin_id : groups_.at(group)) {
    const auto& pin = pins_[pin_id];
    if ((!pin.net.empty() && pin.net == retiring) || routed_.contains(pin.net)) {
      continue;
    }
    choices.emplace_back();
    for (auto id : pin.points) {
      if (!points_[id].active || blocked.contains(id)) {
        continue;
      }
      // The two directional probes share a landing and cut. Once either
      // survives, it is one choice for the whole-cell landing assignment.
      // Do not double the bounded search tree for identical landing geometry.
      if (!choices.back().empty()) {
        const auto& previous = points_[choices.back().back()];
        if (previous.landing == points_[id].landing && previous.cut == points_[id].cut) {
          continue;
        }
      }
      choices.back().push_back(id);
    }
    if (choices.back().empty()) {
      return false;
    }
  }
  std::sort(choices.begin(), choices.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
  std::vector<size_t> chosen;
  size_t              examined = 0;
  const auto          search   = [&](auto&& self, size_t depth) -> bool {
    if (depth == choices.size()) {
      return true;
    }
    for (auto id : choices[depth]) {
      if (++examined > 10000 || (expired && expired())) {
        return false;
      }
      const auto& point = points_[id];
      bool        legal = true;
      for (auto other_id : chosen) {
        const auto& other    = points_[other_id];
        const bool  same_net = !pins_[point.pin].net.empty() && pins_[point.pin].net == pins_[other.pin].net;
        if (same_net && point.cut.rect == other.cut.rect) {
          continue;
        }
        if ((!same_net && !rules_.spacing(point.landing, other.landing).legal)
            || ph::violates_cut_spacing(point.cut.rect, other.cut.rect, cut_clearance_)) {
          legal = false;
          break;
        }
      }
      if (!legal) {
        continue;
      }
      chosen.push_back(id);
      if (self(self, depth + 1)) {
        return true;
      }
      chosen.pop_back();
    }
    return false;
  };
  return search(search, 0);
}
Pin_escape_trial Pin_escape_guard::check(const std::vector<ph::Shape>& shapes, const std::string& net,
                                         const std::function<bool()>& expired) const {
  Pin_escape_trial out;
  out.owner      = this;
  out.generation = generation_;
  out.net        = net;
  if (!incomplete_reason_.empty()) {
    out.reason = incomplete_reason_;
    return out;
  }
  if (expired && expired()) {
    out.reason = "net deadline";
    return out;
  }
  std::set<size_t>         blocked;
  std::map<size_t, size_t> lost;
  for (const auto& shape : shapes) {
    if (expired && expired()) {
      out.reason = "net deadline";
      return out;
    }
    if (uncertain_index_.size()) {
      const auto margin = uncertainty_margin_.find(shape.layer);
      if (margin == uncertainty_margin_.end() || margin->second.first < 0 || margin->second.second < 0) {
        out.reason = "unsupported clearance around unresolved pin access";
        return out;
      }
      for (auto id : uncertain_index_.query("unresolved", shape.rect, std::max(margin->second.first, margin->second.second), net)) {
        if (expired && expired()) {
          out.reason = "net deadline";
          return out;
        }
        const auto  region = uncertain_index_.at(id);
        const auto& r      = region.shape.rect;
        const auto& q      = shape.rect;
        if (r.xl > q.xh + margin->second.first || r.xh < q.xl - margin->second.first || r.yl > q.yh + margin->second.second
            || r.yh < q.yl - margin->second.second) {
          continue;
        }
        if (!region.net.empty() && routed_.contains(region.net)) {
          continue;
        }
        out.reason = "cannot preserve unresolved pin access: " + uncertain_labels_.at(id) + " layer=" + shape.layer
                     + " margin_dbu=" + std::to_string(margin->second.first) + "," + std::to_string(margin->second.second);
        return out;
      }
    }
    if (shape.layer != layers_.escape && shape.layer != layers_.cut) {
      continue;
    }
    const auto clearance = shape.layer == layers_.escape ? metal_clearance_ : cut_clearance_;
    for (const auto index : index_.query(shape.layer, shape.rect, clearance, net)) {
      if (expired && expired()) {
        out.reason = "net deadline";
        return out;
      }
      const auto  id    = indexed_points_.at(index);
      const auto& point = points_.at(id);
      const auto& pin   = pins_.at(point.pin);
      if (!point.active || routed_.contains(pin.net) || blocked.contains(id)) {
        continue;
      }
      bool collides;
      if (shape.layer == layers_.escape) {
        // OpenROAD also checks whole-cell patterns using bare via landings.
        // Their shorter ends may violate EOL rules even when the extended
        // escape used by individual pin access clears the same fixed trace.
        collides = !rules_.spacing(point.escape, shape).legal || !rules_.spacing(point.landing, shape).legal
                   || !rules_.spacing(point.probe, shape).legal;
      } else {
        collides = ph::violates_cut_spacing(point.cut.rect, shape.rect, cut_clearance_);
      }
      if (!collides) {
        continue;
      }
      blocked.insert(id);
      if (++lost[point.pin] == pin.remaining) {
        out.reason = "route blocks remaining pin access: " + pin.label;
        return out;
      }
    }
  }
  std::set<size_t> affected;
  for (auto id : blocked) {
    affected.insert(pins_[points_[id].pin].group);
  }
  for (auto group : affected) {
    if (group_supported_[group] && !compatible(group, blocked, net, expired)) {
      out.reason = "route blocks compatible cell pin access";
      return out;
    }
  }
  out.blocked.assign(blocked.begin(), blocked.end());
  out.legal = true;
  return out;
}
void Pin_escape_guard::commit(const Pin_escape_trial& trial) {
  if (!trial.legal || trial.owner != this || trial.generation != generation_) {
    throw std::invalid_argument("invalid or stale pin escape trial");
  }
  for (auto id : trial.blocked) {
    auto& point  = points_.at(id);
    point.active = false;
    --pins_.at(point.pin).remaining;
  }
  routed_.insert(trial.net);
  ++generation_;
}
}  // namespace livehd::placement
