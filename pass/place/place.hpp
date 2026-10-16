// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/physical_rc.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Options {
  std::vector<std::string>                   lefs;
  std::string                                floorplan, output_dir, cache_dir;
  std::string                                liberty;
  // In-process ECO reference; independent of persistent-cache admission.
  std::shared_ptr<const physical::Floorplan> previous;
  std::vector<std::string>                   changed_cells;
  std::map<std::string, double>              net_weights;
  double                                     utilization = 0.5, aspect = 1, density = 0.8;
  double                                     halo_x = 0, halo_y = 0;  // microns per side, non-CORE macros only
  double                                     resistance_ohm_um = -1, capacitance_ff_um = -1;
  int                                        iterations = 500, bins = 32, threads = 1;
  bool                                       full           = true;
  // Internal post-synthesis STA snapshot: mapped pins and zero wire RC, no GP
  // or legalization, and no placement-cache publication.
  bool                                       timing_seed    = false;
  // Try nearest-whitespace placement of edited/new cells before windowed GP.
  bool                                       legalize_cells = false;
};
struct Result {
  std::map<std::string, std::pair<double, double>> pin_locations;
  std::map<std::string, double>                    net_weights;
  std::shared_ptr<hhds::Graph>                     graph;
  std::shared_ptr<physical::Parasitics>            parasitics;
  std::string                                      qor;
  physical::Floorplan                              floorplan;
};
using Point = std::pair<double, double>;
struct Steiner_tree {
  std::vector<Point>                         points;
  std::vector<std::pair<unsigned, unsigned>> edges;
  std::vector<unsigned>                      terminals;
};
Steiner_tree steiner_tree(const std::vector<Point>& points);
// flat_library must outlive the returned graph. Source graphs are read-only.
Result       place(hhds::Graph& source, hhds::GraphLibrary& flat_library, const Options& options);
// Independent physical check against the LEF/DEF model, after DBU rounding.
void         check_legality(const physical::Technology& tech, const physical::Floorplan& fp, double halo_x = 0, double halo_y = 0);
}  // namespace livehd::placement
