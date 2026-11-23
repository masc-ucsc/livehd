// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "core/binary_message.hpp"
#include "dreamplace/native/placement.hpp"

namespace DreamPlace::native {
// Private wire format between processes from the same LiveHD build.
template <class A>
void field(A& a, DensityReservation& v) {
  livehd::binary_message::fields(a, v.owner, v.x, v.y, v.width, v.height, v.materialized_node);
}
template <class A>
void field(A& a, PositionBox& v) {
  livehd::binary_message::fields(a, v.xl, v.yl, v.xh, v.yh);
}
template <class A>
void field(A& a, RelativeOrder& v) {
  livehd::binary_message::fields(a, v.from, v.to, v.axis, v.separation, v.strength.hard, v.strength.weight, v.id);
}
template <class A>
void field(A& a, CohesionGroup& v) {
  livehd::binary_message::fields(a, v.nodes, v.weight, v.areas, v.offsets, v.has_target, v.target_x, v.target_y, v.target_weight);
}
template <class A>
void field(A& a, CentroidGroup& v) {
  livehd::binary_message::fields(a,
                                 v.name,
                                 v.parent,
                                 v.family,
                                 v.scope_macros,
                                 v.scope_cells,
                                 v.equivalent_cells,
                                 v.nodes,
                                 v.colors,
                                 v.copy_parent,
                                 v.copies_per_parent,
                                 v.repulsion_scope,
                                 v.repulsion_area,
                                 v.areas,
                                 v.offsets);
}
template <class A>
void field(A& a, PinGapPull& v) {
  livehd::binary_message::fields(a, v.driver, v.x, v.y, v.fraction, v.critical, v.suppressed_sinks);
}
template <class A>
void field(A& a, PinTiming& v) {
  livehd::binary_message::fields(a, v.driver, v.weight, v.coefficient, v.gap_pulls, v.maximum_gap_fraction);
}
// Preserve every physical input. The inverse connectivity is derived and is rebuilt on receipt.
template <class A>
void field(A& a, Design& d) {
  livehd::binary_message::fields(a,
                                 d.layout.xl,
                                 d.layout.yl,
                                 d.layout.xh,
                                 d.layout.yh,
                                 d.layout.site_width,
                                 d.layout.row_height,
                                 d.num_movable,
                                 d.node_x,
                                 d.node_y,
                                 d.node_size_x,
                                 d.node_size_y,
                                 d.node_macro,
                                 d.node_placement_mask,
                                 d.node_blocked_capacity_h,
                                 d.node_blocked_capacity_v,
                                 d.node_halo_x,
                                 d.node_halo_y,
                                 d.node_pad_x,
                                 d.node_pad_y,
                                 d.node_orientation,
                                 d.node_orientation_fixed,
                                 d.node_inflatable,
                                 d.node_legalize_fixed,
                                 d.node_legalize_last,
                                 d.node_legalize_weights,
                                 d.node_legalize_anchor_x,
                                 d.node_legalize_anchor_y,
                                 d.node_group_id,
                                 d.node_boxes,
                                 d.density_reservations,
                                 d.order_edges,
                                 d.node_align_class,
                                 d.node_snap_class,
                                 d.cohesion_groups,
                                 d.centroid_groups,
                                 d.centroid_targets,
                                 d.node_centroid_color,
                                 d.centroid_color_names,
                                 d.centroid_node_area,
                                 d.pin2node,
                                 d.pin_offset_x,
                                 d.pin_offset_y,
                                 d.flat_net2pin,
                                 d.flat_net2pin_start,
                                 d.net_weights,
                                 d.net_mask,
                                 d.routing_exclusions,
                                 d.pin_timing);
  if constexpr (std::is_same_v<A, livehd::binary_message::Decoder>) {
    d.build_maps();
    d.validate();
  }
}
template <class A>
void field(A& a, MovablePosition& v) {
  livehd::binary_message::fields(a, v.node, v.x, v.y, v.fix_iterations);
}
template <class A>
void field(A& a, CentroidTargetUpdate& v) {
  livehd::binary_message::fields(a, v.group, v.x, v.y);
}
template <class A>
void field(A& a, InteractUpdate& v) {
  livehd::binary_message::fields(a, v.moves, v.pins, v.releases, v.centroid_targets, v.release_centroids, v.extend);
}
}  // namespace DreamPlace::native
