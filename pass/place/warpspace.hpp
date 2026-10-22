// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>

#include "density_warp.hpp"
#include "dreamplace/native/floorplan.hpp"
#include "dreamplace/native/placement.hpp"
namespace livehd::placement {
struct Warp_options {
  bool     enabled            = false;
  bool     once_after_density = false;  // macro anchors are generated once, after native GP density gating
  unsigned bins = 16, iterations = 2, updates = 10;
  double   weight = .25, warmup = .1, seconds = 3;
  double   dbu_per_um = 1;  // technology-owned units, not a user option
};
void validate_warp_options(const Warp_options&);
void write_warp_status(const std::string& path, const std::string& status, const std::string& reason);
struct Warp_field {
  std::map<int, warp::Point> delta;
  warp::Density_result       density;
};
Warp_field placement_warp(const DreamPlace::native::Design&, const std::vector<int>& samples, const Warp_options&,
                          bool sample_cells = false);
struct Warp_centroid_shift {
  warp::Point sum;
  size_t      count = 0, memories = 0;
};
// Deepest unique owner; all target additions use a saved original target.
std::vector<Warp_centroid_shift> shift_warp_centroids(const DreamPlace::native::Design&, const std::map<int, warp::Point>&,
                                                      std::vector<DreamPlace::native::FloorplanCircle>&, double gain = 1);
void                             write_warp_field(const Warp_field&, const Warp_options&, const std::string& path, int iteration,
                                                  const DreamPlace::native::Design&);
class Macro_warp {
  Warp_options options;
  std::string  prefix;
  unsigned     attempts = 0, successes = 0;
  double       elapsed_seconds  = 0;
  int          iteration_budget = 0;
  int          next = 0, stride = 1, last = -1;

public:
  Macro_warp(Warp_options, int iteration_budget, std::string prefix);
  std::optional<std::vector<DreamPlace::native::MacroPull>> refresh(const DreamPlace::native::Design&, const std::vector<int>&,
                                                                    int iteration);
  void                                                      finish(int actual_iterations) const;
};
}  // namespace livehd::placement
