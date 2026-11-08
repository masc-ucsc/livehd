// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
// Routability-driven area sizing for a fixed (imported) core: "fake die reduction".
//
// An imported floorplan (the DEF fixes die, core and rows) may be larger than the design needs. With
// place.area=auto the placer searches for the smallest placement region (a fence: a sub-rectangle of the core,
// rows and sites clipped to it) that still routes: each probe is a complete placement (macros, global placement,
// legalization) inside the fence, and the oracle is the native legroute global router (legroute_plan with
// measure_only: maze routing of every signal net over the whole core's tile grid, so the wires may
// use the tracks above the empty area outside the fence). The region area is bisected between the full core
// (the reference probe) and the densest region the utilization ceiling allows. The chosen region's placement
// is returned with the core's original rows, so the result is legal in the real core.
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace hhds {
class Graph;
class GraphLibrary;
}  // namespace hhds

namespace livehd::placement {
struct Area_fit_options {
  bool        enabled         = false;  // place.area=auto (fixed: the given core, the default)
  double      min_fraction    = .5;     // the smallest region area tried, as a fraction of the core area
  double      max_utilization = .90;    // ceiling of standard-cell area / (region area - macro area)
  double      tolerance       = .03;    // stop when the routable and failing area fractions are this close
  unsigned    probes          = 4;      // placements after the reference (full core) one, before the final placement
  // a region routes when unrouted <= reference + floor and overflow tracks <= reference * (1 + slack) + floor, the
  // floor being overflow_floor of the signal nets
  double      overflow_slack  = .05;
  double      overflow_floor  = .001;
  std::string anchor          = "io";        // io: centered at the IO pin centroid; center: the core center
  std::string oracle          = "legroute";  // legroute (rudy when it throws) | rudy
  unsigned    net_pops        = 100000;      // legroute maze budget per net
  bool        pattern         = false;       // legroute FastRoute L/Z pattern seed before the maze (slower here)
};
void validate_area_fit_options(const Area_fit_options&);

// Row lattice of a floorplan: the bounding box of its rows (DEF DBU), the site step and the row height.
struct Area_fit_core {
  physical::Rect rect;
  int64_t        site = 0, row = 0;
};
Area_fit_core area_fit_core(const physical::Technology&, const physical::Floorplan&);

// Area totals of the placed components (DEF DBU^2): CORE cells and soft blocks (cell clusters) as cells, hard BLOCK
// masters as macros; PAD/COVER/RING/IO ENDCAP masters (outside the rows) are not counted. The largest extents cover
// hard and soft blocks.
struct Area_fit_areas {
  double  cells = 0, macros = 0;
  int64_t max_macro_w = 0, max_macro_h = 0;  // the largest macro extents (oriented as placed)
};
Area_fit_areas area_fit_areas(const physical::Technology&, const physical::Floorplan&);

// The region for an area fraction of the core: the core's aspect ratio, each side rounded up to whole sites/rows and
// at least the largest macro; centered at `anchor` (DEF DBU), slid inside the core, snapped to the site/row grid,
// and grown to cover every FIXED component.
physical::Rect area_fit_region(const physical::Technology&, const physical::Floorplan&, const Area_fit_core&, const Area_fit_areas&,
                               double fraction, std::pair<double, double> anchor);
// The IO pin centroid (anchor=io; the core center without placed pins) or the core center (anchor=center).
std::pair<double, double> area_fit_anchor(const physical::Floorplan&, const Area_fit_core&, const std::string& anchor);
// The floorplan with its rows clipped to the region (whole sites; rows outside it dropped).
physical::Floorplan       area_fit_fence(const physical::Floorplan&, const physical::Rect&);

// The routability oracle on a placed floorplan.
struct Area_fit_route {
  std::string oracle;                 // legroute | rudy
  std::string error;                  // legroute failure that fell back to rudy
  bool        capacity_valid = true;  // RUDY: every direction with demand has positive routing capacity
  size_t      signals = 0, unrouted = 0, overflow_edges = 0, overflow_tracks = 0;
  double      overflow = 0;  // legroute: overflow tracks / signals; rudy: overflowed demand / total capacity
  double      peak     = 0;  // rudy: the highest bin demand / capacity
  double      seconds  = 0;
};
Area_fit_route area_fit_route(const physical::Technology&, physical::Floorplan, const Area_fit_options&);
// A probe passes against the full-core reference (null: absolute, no unrouted/overflow above the floor).
bool           area_fit_routable(const Area_fit_route& probe, const Area_fit_route* reference, const Area_fit_options&);
// A fresh final placement must satisfy the same frozen-reference acceptance rule as its selected probe.
void           validate_area_fit_final_route(const Area_fit_route&, const Area_fit_route& reference, const Area_fit_options&);
std::string    area_fit_route_json(const Area_fit_route&);

struct Options;
struct Result;
// place() with options.area_fit.enabled on an imported floorplan: the search above, then the chosen region placed
// into `library`. Writes <output_dir>/area_fit.json and adds an "area_fit" object to the result QoR.
Result place_area_fit(hhds::Graph& source, hhds::GraphLibrary& library, const Options& options,
                      const std::function<void()>& release_source);
// Whether place() runs the area search for these options (imported floorplan, fresh full placement).
bool   area_fit_applies(const Options& options);
}  // namespace livehd::placement
