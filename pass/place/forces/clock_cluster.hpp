// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "dreamplace/native/placement.hpp"

namespace livehd::placement {
struct Clock_anchor_group {
  int              gate = -1;
  std::vector<int> members;
};
// Normalize the incident weight at each flop and at gates shared by several leaves.
std::vector<DreamPlace::native::PairAnchor> clock_leaf_anchors(const std::vector<Clock_anchor_group>& groups, double strength,
                                                               const std::vector<double>& criticality);
// A gate can participate in several leaves. A flop belongs to exactly one.
size_t retained_clock_anchors(const std::vector<Clock_anchor_group>& groups, const std::vector<DreamPlace::native::PairAnchor>& previous,
                              size_t node_count);
struct Clock_cluster_step {
  std::vector<DreamPlace::native::MovablePosition> moves;
  size_t                                           groups = 0, congested = 0, data_rejections = 0;
  double                                           overflow = 0, displacement = 0;
};
Clock_cluster_step clock_cluster_step(const DreamPlace::native::Design&, std::vector<std::vector<int>> domains, double strength,
                                      double target_density);
}  // namespace livehd::placement
