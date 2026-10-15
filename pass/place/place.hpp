// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "core/physical_rc.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Options {
  std::vector<std::string> lefs;
  std::string              floorplan, output_dir, cache_dir;
  double                   utilization = 0.5, aspect = 1, density = 0.8;
  double                   resistance_ohm_um = -1, capacitance_ff_um = -1;
  int                      iterations = 500, bins = 32, threads = 1;
  bool                     full = true;
};
struct Result {
  std::shared_ptr<hhds::Graph>          graph;
  std::shared_ptr<physical::Parasitics> parasitics;
  std::string                           qor;
  physical::Floorplan                   floorplan;
};
// flat_library must outlive the returned graph. Source graphs are read-only.
Result place(hhds::Graph& source, hhds::GraphLibrary& flat_library, const Options& options);
// Independent physical check against the LEF/DEF model, after DBU rounding.
void   check_legality(const physical::Technology& tech, const physical::Floorplan& fp);
}  // namespace livehd::placement
