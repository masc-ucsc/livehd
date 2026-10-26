// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "physical.hpp"

namespace livehd::physical {
struct Rule_check {
  bool        legal = false;
  std::string reason;
};
// Half-length of the shortest grid-centered wire of this width (at least a
// square) that meets the minimum area, rounded up to the manufacturing grid.
inline double min_area_half_length(double width, double area, double grid) {
  return std::ceil(std::max(width, area / width) / (2 * grid)) * grid;
}
// Rectangular metal primitives for the signal-routing oracle. Coordinates are
// integer DEF DBU. These checks do not certify a net: connectivity, vias, track
// access and the union of touching same-net shapes are separate obligations.
// Unsupported rules fail closed, including extension properties not yet modeled.
Rule_check check_metal_rectangle(const Technology&, int64_t dbu, const Shape&);
// Check spacing between two different-net metal rectangles on the same layer.
// Rules conditional on context use a conservative upper bound when checking
// only this pair (e.g. EOL parallel-edge conditions).
Rule_check check_metal_spacing(const Technology&, int64_t dbu, const Shape&, const Shape&);
// Compiles the supported ASAP7 LEF58 forms once. EOL/keepout checks are
// directional and corner checks require facing convex corners. Parallel-edge
// trigger conditions are conservatively assumed active. The original LEF model is
// never modified. Explicit DEF tracks are required for RIGHTWAYONGRIDONLY and
// FIRSTLASTPITCH; there is no uniform-pitch fallback for multi-pattern grids.
class Signal_metal_rules {
  struct Eol_rule {
    double width, spacing, within, end_to_end;
  };
  struct Eol_keepout {
    double width, backward, side, forward;
    bool   corner_only;
  };
  struct Extension {
    std::vector<Eol_rule>    eol;
    std::vector<Eol_keepout> keepouts;
    std::vector<double>      corner_spacing;
    double                   query_radius = 0;
    std::vector<double>      widths;
    std::vector<Track>       tracks;
    bool                     right_way = false, explicit_tracks = false;
    std::string              error;
  };
  Technology                       rules_;
  int64_t                          dbu_;
  std::map<std::string, Extension> extensions_;

public:
  Signal_metal_rules(const Technology&, const Floorplan&);
  Rule_check             rectangle(const Shape&) const;
  // Finite preferred-width candidates in DEF DBU, ascending. Uses the LEF58
  // width table when present, otherwise nominal width. Every placed rectangle
  // still requires rectangle()/spacing() validation.
  std::vector<int64_t>   candidate_widths(const std::string& layer) const;
  Rule_check             spacing(const Shape&, const Shape&) const;
  // Conservative query radius for every spacing rule on this layer; missing
  // or unsupported rules return nullopt instead of an unsafe zero radius.
  std::optional<int64_t> clearance(const std::string& layer) const;
};
}  // namespace livehd::physical
