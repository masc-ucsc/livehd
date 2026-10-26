// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "multirow.hpp"

namespace livehd::placement {
// Post-placement ECO of movable multi-row CORE cells (two-row domino masters)
// whose master changed at an unchanged pose, e.g. a variant swap that changes
// the width. Each edited cell, widest first then by name, takes the first of:
//   1. not wider: its pose; the new footprint is a subset of the old one;
//   2. the nearest site-aligned x of its start row (same rows, orientation and
//      row-access start) within shift_sites of its x, free in every row it
//      spans of obstacles, fixed cells and other multi-row cells (each with
//      its padding) and of single-row cells;
//   3. the nearest such x whose footprint overlaps at most max_displaced
//      single-row cells, which become displaced;
//   4. the anchor packer's nearest pose for this cell alone (its window, then
//      the whole core) that is free of single-row cells too: it displaces none.
// "Overlap" includes the occupancy padding_sites kept free on both sides of the
// new footprint in every row it spans, as the anchor prepass keeps them. Every
// pose passes multirow_start_allowed; every pose but a kept one also passes
// the hard bin density cap, counting every logic cell except the edited one
// (displaced single-row cells at their current poses).
struct Multirow_eco_bounds {
  unsigned shift_sites = 64, max_displaced = 16;
};
struct Multirow_eco_options {
  // The anchor prepass controls (multirow.hpp); `admits` is not consulted.
  Multirow_options    anchor;
  Multirow_eco_bounds bounds;
};
struct Multirow_eco_cell {
  enum class Outcome : uint8_t { in_place, shifted, displaced, re_anchored };
  std::string name;
  Outcome     outcome        = Outcome::in_place;
  // Rounded sites summed over every row the cell spans.
  int64_t     released_sites = 0, grown_sites = 0;
  size_t      displaced_cells = 0;
  double      displacement_um = 0;  // Manhattan
};
struct Multirow_eco_report {
  std::vector<Multirow_eco_cell> cells;      // in processing order
  std::vector<std::string>       displaced;  // single-row cells to re-place, sorted, unique
};
std::string_view    multirow_eco_outcome_name(Multirow_eco_cell::Outcome);
// `edited` name movable multi-row CORE cells of fp, each carrying its new
// master and width at the pose `reference` (the frame before the edit) gives
// it; every other cell keeps its own. On success the edited cells take their
// new poses and keep their status; the displaced single-row cells keep their
// overlapping poses for the caller to re-place. Atomic: on failure throws
// Multirow_failure, naming the cell, and leaves fp unchanged. Usage errors
// (unknown, fixed, single-row or unreferenced cells) throw
// std::invalid_argument.
Multirow_eco_report legalize_multirow_edits(const physical::Technology&, physical::Floorplan&, const physical::Floorplan& reference,
                                            const std::vector<std::string>& edited, const Multirow_eco_options&);
}  // namespace livehd::placement
