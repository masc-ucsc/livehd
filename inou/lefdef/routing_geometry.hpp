// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>

#include "physical.hpp"

namespace livehd::physical {
// Exact rectangular geometry in floorplan DBU. This is geometric decoding,
// not a DRC/legal-access verdict. Unsupported or unrepresentable geometry throws.
// Via coordinates are local to the via origin, including DEF ORIGIN/OFFSET and
// the path orientation; callers translate them to the path's current point.
std::vector<Shape> via_geometry(const Technology& tech, const Floorplan& fp, const Route_via& via);
// Trial rules supplement installed rules without modifying the frame. A conflicting
// definition is rejected, even if the route does not reference it.
// Stream in the same order as route_geometry without retaining a whole net's
// decoded shapes. Visitor exceptions propagate; the caller owns any partial state.
void               visit_route_geometry(const Technology&, const Floorplan&, const Net&, const std::function<void(const Shape&)>&,
                                        bool special = false, const Route_width_rules* trial_rules = nullptr);
std::vector<Shape> route_geometry(const Technology& tech, const Floorplan& fp, const Net& net, bool special = false,
                                  const Route_width_rules* trial_rules = nullptr);
// Ordinary-net path inverses of route_geometry: a flush (zero-extension) wire on
// the centerline of an integral rectangle, and one via placed from a lower layer.
Route_path         flush_wire_path(const Shape& metal, bool horizontal, std::string taper_rule = {});
Route_path         via_path(const std::string& lower_layer, int64_t x, int64_t y, const std::string& via);
}  // namespace livehd::physical
