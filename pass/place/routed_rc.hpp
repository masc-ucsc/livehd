// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "inou/lefdef/routing_connectivity.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
struct Route_rc_pin {
  std::string pin, layer;  // physical label instance:pin and access metal
  int64_t     x = 0, y = 0;
};
// Extract a connected resistor tree from actual Manhattan DEF trace extents.
// Splits at wire intersections, via origins and validated physical pin accesses;
// deduplicates collinear overlapping traces and identical via placements.
// Centered RECT landing patches follow their layer's preferred direction.
// Collinear metal is unioned and split at width changes; R scales inversely
// with width and effective C scales with width relative to nominal metal.
// This area-equivalent C approximation is not width/coupling extraction.
// Each wire uses its own layer R/C, distributed as a pi section. This uses
// characterized effective ground capacitance, not coupling extraction. Via
// capacitance is not modeled. A cut-layer override wins over LEF via resistance.
// Missing characterization, multi-cut/generated vias, non-centerline contacts,
// opens and resistor loops fail explicitly; no spanning-tree approximation.
physical::Rc_net routed_rc(const physical::Technology&, const physical::Floorplan&, const physical::Net&, const std::string& driver,
                           const std::vector<Route_rc_pin>&, const Route_technology_rc&,
                           const physical::Route_components* = nullptr);
}  // namespace livehd::placement
