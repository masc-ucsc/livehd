// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "dreamplace/native/placement.hpp"

namespace livehd::placement {
struct Clock_cluster_step {
  std::vector<DreamPlace::native::MovablePosition> moves;
  size_t                                           groups = 0, congested = 0, data_rejections = 0;
  double                                           overflow = 0, displacement = 0;
};
Clock_cluster_step clock_cluster_step(const DreamPlace::native::Design&, std::vector<std::vector<int>> domains, double strength,
                                      double target_density);
}  // namespace livehd::placement
