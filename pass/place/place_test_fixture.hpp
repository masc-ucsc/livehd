// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Placement fixtures shared by place_test and the engines that re-place
// through it (custom_sizing_test).
#include <algorithm>
#include <cmath>
#include <map>
#include <string>

#include "inou/lefdef/physical.hpp"
#include "place.hpp"

namespace livehd::placement {
// Fixtures for the independent plain/H2D/ECO paths pin their original controls.
inline Options plain_options() {
  Options o;
  o.optimization_pending = true;  // These fixtures exercise the legacy final-placement paths.
  o.fp_place             = false;
  o.fp_pin_adjust = o.macro_pin_adjust = false;
  o.final_cells                        = false;
  o.macro_snap                         = false;
  o.macro_align_weight                 = 0;
  o.macro_align_global                 = false;
  o.utilization                        = .5;
  o.density                            = .8;
  o.halo_x = o.halo_y   = 0;
  o.iterations          = 500;
  o.bins                = 32;
  o.scale_macro_density = o.exact_macro_density = o.adaptive_density = o.routability = false;
  o.macro_overlap_weight = o.macro_jitter = 0;
  o.schedule_iterations = o.io_iterations = 0;
  return o;
}

// HPWL in microns from LEF pin geometry in each component's final
// orientation (pin shape 0's center, the point the placer reads), independent
// of the placer's own pin offsets.
inline double lef_hpwl_um(const physical::Technology& tech, const physical::Floorplan& fp) {
  std::map<std::string, const physical::Component*> components;
  std::map<std::string, const physical::Terminal*>  terminals;
  for (const auto& c : fp.components) {
    components.emplace(c.name, &c);
  }
  for (const auto& t : fp.terminals) {
    terminals.emplace(t.name, &t);
  }
  double total = 0;
  for (const auto& net : fp.nets) {
    if (net.connections.size() < 2) {
      continue;
    }
    double xl = INFINITY, yl = INFINITY, xh = -INFINITY, yh = -INFINITY;
    for (const auto& [instance, pin] : net.connections) {
      double x = 0, y = 0;
      if (instance == "PIN") {
        x = terminals.at(pin)->x;
        y = terminals.at(pin)->y;
      } else {
        const auto& c       = *components.at(instance);
        const auto& m       = tech.macros.at(c.macro);
        const auto [ox, oy] = physical::oriented_center(m, m.pins.at(pin).shapes.front().rect, c.place.orientation);
        x                   = c.place.x + ox * tech.dbu;
        y                   = c.place.y + oy * tech.dbu;
      }
      xl = std::min(xl, x);
      xh = std::max(xh, x);
      yl = std::min(yl, y);
      yh = std::max(yh, y);
    }
    total += xh - xl + yh - yl;
  }
  return total / tech.dbu;
}
}  // namespace livehd::placement
