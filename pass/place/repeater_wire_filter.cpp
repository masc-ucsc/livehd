// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "repeater_wire_filter.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <tuple>

#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_occupancy.hpp"
#include "inou/lefdef/routing_rules.hpp"

namespace livehd::placement::detail {
std::function<bool(const physical::Component&)> repeater_wire_filter(const physical::Technology& tech,
                                                                   const physical::Floorplan& frame) {
  if (std::none_of(frame.nets.begin(), frame.nets.end(), [](const auto& net) { return !net.wires.empty(); })) {
    return {};
  }
  auto occupied = std::make_shared<physical::Routing_occupancy>(std::max<int64_t>(1, frame.dbu));
  std::set<std::string> wired_layers;
  for (const auto& net : frame.nets) {
    for (const auto& shape : physical::route_geometry(tech, frame, net)) {
      occupied->insert({shape, net.name});
      wired_layers.insert(shape.layer);
    }
  }
  const physical::Signal_metal_rules rules(tech, frame);
  std::map<std::string, std::optional<int64_t>> clearances;
  for (const auto& layer : wired_layers) {
    clearances.emplace(layer, rules.clearance(layer));
  }
  using Key = std::tuple<std::string, physical::Orientation, int64_t, int64_t>;
  using Bounds = std::map<std::string, physical::Rect>;
  return [&tech, dbu = frame.dbu, occupied, clearances = std::move(clearances), cache = std::map<Key, Bounds>{}]
         (const physical::Component& cell) mutable {
    const Key key{cell.macro, cell.place.orientation, cell.place.width, cell.place.height};
    auto cached = cache.find(key);
    if (cached == cache.end()) {
      const auto&          master = tech.macros.at(cell.macro);
      const physical::Rect body{0.0, 0.0, double(cell.place.width), double(cell.place.height)};
      Bounds               bounds;
      const auto include = [&](const physical::Shape& shape) {
        auto [it, inserted] = bounds.try_emplace(shape.layer, body);
        const auto r        = physical::orient_rect(shape.rect.translated(master.origin_x, master.origin_y),
                                             master.width,
                                             master.height,
                                             cell.place.orientation);
        it->second          = it->second.hull({r.xl * dbu, r.yl * dbu, r.xh * dbu, r.yh * dbu});
      };
      for (const auto& [name, pin] : master.pins) {
        for (const auto& shape : pin.shapes) {
          include(shape);
        }
      }
      for (const auto& shape : master.obstructions) {
        include(shape);
      }
      for (const auto& layer : master.blocked_layers) {
        bounds.try_emplace(layer, body);
      }
      for (const auto& layer : master.conservative_obstructions) {
        bounds.try_emplace(layer, body);
      }
      // LEF coordinates and orientation arithmetic are floating point. Enclose
      // the complete geometry in integer DBU before querying the exact index.
      for (auto& [layer, box] : bounds) {
        box.xl = std::floor(box.xl);
        box.yl = std::floor(box.yl);
        box.xh = std::ceil(box.xh);
        box.yh = std::ceil(box.yh);
      }
      cached = cache.emplace(key, std::move(bounds)).first;
    }
    for (const auto& [layer, local] : cached->second) {
      const auto clearance = clearances.find(layer);
      if (clearance == clearances.end()) {
        continue;
      }
      if (!clearance->second) {
        throw std::invalid_argument("cannot prove repeater clearance to existing wiring on " + layer);
      }
      // Reserve body and any pin/OBS overhang on every layer the cell occupies.
      if (!occupied->query(layer, local.translated(cell.place.x, cell.place.y), *clearance->second).empty()) {
        return false;
      }
    }
    return true;
  };
}
}  // namespace livehd::placement::detail
