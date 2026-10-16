// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <set>

#include "dreamplace/native/macro/hierarchy.hpp"
#include "dreamplace/native/macro/report.hpp"
#include "place.hpp"
namespace livehd::placement {
struct Macro_hierarchy {
  DreamPlace::native::macro::Hierarchy hierarchy;
  std::set<std::string>                sequential_masters;
  std::string                          design;
};
Macro_hierarchy                 macro_hierarchy(hhds::Graph&, const physical::Technology&, const std::string& liberty);
DreamPlace::native::Orientation native_orientation(Ann_place::Orientation);
Ann_place::Orientation          physical_orientation(DreamPlace::native::Orientation);
void write_gp_snapshot(const DreamPlace::native::Design&, const std::vector<DreamPlace::native::macro::NodeInfo>&,
                       const std::string& path, int iteration);
DreamPlace::native::macro::Parameters macro_parameters(const Options&, const physical::Technology&);
struct Macro_run {
  DreamPlace::native::macro::Result result;
  bool                              cache_hit = false;
  std::string                       environment;
};
Macro_run place_macros(DreamPlace::native::Design&, const std::vector<DreamPlace::native::macro::NodeInfo>&, const Macro_hierarchy&,
                       const Options&, const std::string& environment);
void      write_macro_report(const Macro_run&, const DreamPlace::native::Design&,
                             const std::vector<DreamPlace::native::macro::NodeInfo>&, const Options&, int64_t dbu_per_um);
}  // namespace livehd::placement
