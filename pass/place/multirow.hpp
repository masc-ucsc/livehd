// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "inou/lefdef/row_access.hpp"

namespace livehd::placement {
// Anchor prepass for movable multi-row CORE cells (two-row domino masters).
// Terminal legalization packs only single-row cells, but reserves a FIXED
// multi-row CORE cell in every row it overlaps. The packer gives each
// multi-row cell one site-aligned x interval that is free in all of its
// consecutive rows, takes the start row's orientation (as check_legality
// requires) and marks it FIXED. Terminal legalization then packs the
// single-row cells around these anchors; release_multirow_anchors restores
// PLACED for the handoff. Free sites, obstacles and the bin capacity follow
// terminal legalization's row occupancy model, so the anchors never exceed a
// bin cap that terminal legalization checks for its fixed input cells.

// Row-access classes of a master's local row halves, bottom half first.
struct Multirow_template {
  std::array<physical::Row_access, 2> half;
};
struct Multirow_options {
  // With both set, a master with a template starts only where each local half
  // lands on a row of its class; otherwise only LEF SYMMETRY filters starts.
  const physical::Row_access_convention*          access        = nullptr;
  const std::map<std::string, Multirow_template>* templates     = nullptr;  // by master
  // Free sites kept on both sides of an anchor and of a fixed logic cell, in
  // every row they span.
  unsigned                                        padding_sites = 0;
  // Start rows and sites searched on each side of the global-placement pose;
  // a cell without a legal pose there searches the whole core.
  unsigned                                        window_rows = 8, window_sites = 256;
  // Copy these from the terminal legalization options.
  unsigned                                        bins_x = 64, bins_y = 64;
  double                                          max_density      = .8;
  int64_t                                         macro_halo_x_dbu = 0, macro_halo_y_dbu = 0;
  uint64_t                                        work = 4'000'000;  // start rows and poses examined across all cells
  // The row-access origin by SITE: y of the first core row. Record it with
  // first_core_rows before rows are cut (macro halos, handoff taps), so a
  // removed bottom row does not flip every start's parity; a site absent here
  // counts from its lowest row in the frame.
  std::map<std::string, int64_t>                  core_origins;
  // Anchor only the masters this admits; null admits every multi-row CORE
  // master. Placement admits the provisional domino abstracts only, so a
  // library multi-row cell still fails terminal legalization loudly.
  bool (*admits)(std::string_view master) = nullptr;
};
struct Multirow_anchor {
  size_t    component;  // index into Floorplan::components
  Ann_place before;
};
struct Multirow_report {
  std::vector<Multirow_anchor> anchors;          // in packing order
  size_t                       candidates  = 0;  // poses checked against the bin cap
  // Start-row parity, counted from the first core row (the row-access origin).
  size_t                       even_starts = 0, odd_starts = 0;
  size_t                       full_core_scans       = 0;  // cells without a pose in their window
  uint64_t                     work                  = 0;
  double                       total_displacement_um = 0, max_displacement_um = 0;  // Manhattan
};
// The reason names the cell; the frame is unchanged.
struct Multirow_failure : std::runtime_error {
  using std::runtime_error::runtime_error;
};
// Rows a CORE master spans: its height over its SITE height when that is an
// integer of at least two; zero otherwise.
size_t      multirow_rows(const physical::Technology&, const physical::Macro&);
inline bool is_multirow_core(const physical::Technology& tech, const physical::Macro& m) { return multirow_rows(tech, m) != 0; }
// The lowest row y of each SITE in the frame.
std::map<std::string, int64_t> first_core_rows(const physical::Floorplan&);
// Whether the multi-row master may start on fp.rows[start_row] with that row's
// orientation: its LEF SYMMETRY must permit the orientation and, under a
// template, each local half must land on a row of its access class. Rows count
// from the master site's first core row (Multirow_options::core_origins). Row
// availability is not checked.
bool multirow_start_allowed(const physical::Technology&, const physical::Floorplan&, size_t start_row, const std::string& master,
                            const Multirow_options&);
// Anchor every movable multi-row CORE cell the options admit, widest first,
// then by name. Each
// takes the legal pose nearest its current one (Manhattan distance), within
// the window or else anywhere in the core. Atomic: on failure throws
// Multirow_failure and leaves fp unchanged.
Multirow_report anchor_multirow_cells(const physical::Technology&, physical::Floorplan&, const Multirow_options&);
// FIXED -> PLACED for every anchor; throws std::invalid_argument when one is
// no longer FIXED.
void            release_multirow_anchors(physical::Floorplan&, const Multirow_report&);
}  // namespace livehd::placement
