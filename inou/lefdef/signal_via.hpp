// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <span>

#include "routing_occupancy.hpp"
#include "routing_rules.hpp"

namespace livehd::physical {
struct Signal_via_intrinsic_check {
  Rule_check check;
  // Valid only when check.legal. The index addresses the supplied via shapes.
  size_t     cut_index   = 0;
  int64_t    cut_spacing = 0;
};
// Check only translation-independent via/landing geometry and cut rules.
// Shapes must be decoded by via_geometry() in DEF DBU; both the via shapes and
// complete landing rectangles are relative to the same origin. A legal result
// is not a site certificate: grid/position, DEF overrides, tracks, metal rules,
// die bounds and occupancy still require check_signal_via(). This permits a
// caller to cache impossible landing variants without constructing a floorplan.
Signal_via_intrinsic_check check_signal_via_intrinsics(const Technology&, int64_t dbu, std::span<const Shape> via_shapes,
                                                       const std::array<Shape, 2>& landings);
// Validate a single-cut LEF via between adjacent routing layers, enclosed in
// two complete rectangular route landings (global DEF DBU). Landings include
// the wire extensions needed for area/minsize rules. Checks existing metal
// and cuts; callers must also check against other new vias of the trial net.
// Supports single-cut LEF58 classes, all-DEFAULT spacing tables and the modeled
// enclosure forms. Other qualified rules fail closed.
// Optional exact same-owner union supplies the exposed landing contour only;
// rectangle, enclosure, cut, track and die checks remain unchanged. The caller
// must prove ownership of every supplied shape, as for spacing_with_union().
Rule_check check_signal_via(const Technology&, const Floorplan&, const Signal_metal_rules&, const Routing_occupancy&,
                            const Route_via&, int64_t x, int64_t y, const std::array<Shape, 2>& landings,
                            const std::string& own_net, std::span<const Shape> owner_union = {});
// Cuts of the new vias in one routing proposal. Access and landing checks only
// cover existing cuts; new cuts must also keep signal cut spacing to each other,
// even on one net. Technology and frame must outlive this set.
class Signal_cut_set {
  const Technology&              tech_;
  const Floorplan&               frame_;
  std::vector<Shape>             cuts_;
  std::map<std::string, int64_t> spacing_;

public:
  Signal_cut_set(const Technology& tech, const Floorplan& frame) : tech_(tech), frame_(frame) {}
  // Adds the cuts of a via placed at (x, y); false when one violates spacing to an earlier cut.
  bool add(const Route_via&, int64_t x, int64_t y);
};
}  // namespace livehd::physical
