// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/physical_rc.hpp"
#include "dreamplace/native/floorplan.hpp"
#include "dreamplace/native/macro/topology.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Options {
  std::vector<std::string>              lefs;
  std::string                           floorplan, output_dir, cache_dir;
  std::string                           liberty;
  std::string                           macro_mode = "none";
  bool                                  fp_place = true, fp_snap_enabled = true;
  int                                   fp_iterations = 1500, fp_min_copies = 4;
  double                                fp_minimum = 3000, fp_attraction = .25, fp_repulsion = 5, fp_anchor = .25;
  DreamPlace::native::FloorplanOptions  fp_snap;
  DreamPlace::native::macro::Parameters macro_parameters;
  std::shared_ptr<const DreamPlace::native::macro::Topology> macro_topology;
  // In-process ECO reference; independent of persistent-cache admission.
  std::shared_ptr<const physical::Floorplan>                 previous;
  std::vector<std::string>                                   changed_cells;
  std::map<std::string, double>                              net_weights;
  double                                                     utilization = 0.30, aspect = 1, density = 0.5;
  double                                                     halo_x = 5, halo_y = 5;  // microns per side, non-CORE macros only
  bool                                                       scale_macro_density = true;
  bool   automatic_bins = false, exact_macro_density = true, adaptive_density = true, routability = true;
  bool   routability_charge_compensation = true;
  double macro_overlap_weight = 50, macro_jitter = .01;
  double filler_width       = 0;  // microns; zero keeps native mean-cell sizing
  double macro_align_weight = 0, macro_align_sigma = 0, macro_align_overlap_boost = 0;
  int    macro_align_start = 100, macro_align_full = 500;
  bool   macro_align_nearest = false, macro_snap = true;
  double macro_snap_tolerance = .25;
  int    macro_snap_weight    = 100;
  int    schedule_iterations = 500, io_iterations = 500;
  double cohesion          = 0;
  double resistance_ohm_um = -1, capacitance_ff_um = -1;
  int    iterations = 1500, bins = 64, threads = 1;
  bool   full             = true;
  bool   final_cells      = true;
  int    final_iterations = 0;  // zero inherits iterations; only after fp in full mode
  // Diagnostic physical GP coordinates, before macro/cell legalization.
  bool   snapshots        = false;
  // Internal post-synthesis STA snapshot: mapped pins and zero wire RC, no GP
  // or legalization, and no placement-cache publication.
  bool   timing_seed      = false;
  // Try nearest-whitespace placement of edited/new cells before windowed GP.
  bool   legalize_cells   = false;
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
