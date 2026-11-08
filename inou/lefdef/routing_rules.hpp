// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <span>

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
    double                   lateral      = 0;  // largest across-track extent of any parsed extension rule
    std::vector<double>      widths;
    std::vector<Track>       tracks;
    bool                     right_way = false, explicit_tracks = false;
    std::string              error;
  };
  Technology                       rules_;
  int64_t                          dbu_;
  std::map<std::string, Extension> extensions_;
  Rule_check                       spacing_context(const Shape&, const Shape&, std::span<const Shape>, std::span<const Shape>,
                                                   const std::function<bool(const Rect&)>&) const;

public:
  Signal_metal_rules(const Technology&, const Floorplan&);
  Rule_check             rectangle(const Shape&) const;
  // Finite preferred-width candidates in DEF DBU, ascending. Uses the LEF58
  // width table when present, otherwise nominal width. Every placed rectangle
  // still requires rectangle()/spacing() validation.
  std::vector<int64_t>   candidate_widths(const std::string& layer) const;
  Rule_check             spacing(const Shape&, const Shape&) const;
  // The caller supplies actual same-owner metal (or one physical source pin /
  // one member's obstruction union), never foreign geometry or a bounding hull.
  // Nonempty contexts must exactly cover the corresponding queried rectangle.
  // Only directional LEF58 EOL/keepout edges use the exposed union contour;
  // ordinary/PRL spacing and conservative corner checks are unchanged. Empty
  // context retains the rectangular check. This grants no connectivity rights.
  Rule_check             spacing_with_union(const Shape&, const Shape&, std::span<const Shape> source_union = {},
                                            std::span<const Shape> target_union = {}) const;
  // Existing-source metal has no independent EOL at an interior via-pad edge.
  // The caller must prove the source is contained in its original pin metal
  // and that every supplied target footprint was also already present. Each
  // footprint is the exact active directional EOL influence, not the broader
  // query radius. Ordinary, reverse-direction and corner checks still run.
  Rule_check             spacing_with_existing_source(const Shape&, const Shape&,
                                                      const std::function<bool(const Rect&)>& existing_target) const;
  // Conservative query radius for every spacing rule on this layer; missing
  // or unsupported rules return nullopt instead of an unsafe zero radius.
  std::optional<int64_t> clearance(const std::string& layer) const;
  // Conservative across-track bound: the largest edge-to-edge distance perpendicular to a wire at which any rule
  // on this layer can apply (ordinary and every PRL table spacing for any width/length, EOL spacing/WITHIN and
  // parallel-edge values, keepout side extension, corner spacing). nullopt when clearance() is nullopt.
  std::optional<int64_t> lateral_reach(const std::string& layer) const;
};
}  // namespace livehd::physical
