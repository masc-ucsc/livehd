// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ann_place.hpp"

namespace livehd::physical {
using Orientation = Ann_place::Orientation;
using Status      = Ann_place::Status;
struct Rect {
  double      xl = 0, yl = 0, xh = 0, yh = 0;
  bool        empty() const { return xl >= xh || yl >= yh; }
  bool        contains(const Rect& r) const { return xl <= r.xl && yl <= r.yl && xh >= r.xh && yh >= r.yh; }
  // Positive-area overlap; rectangles sharing only an edge do not overlap.
  bool        overlaps(const Rect& r) const { return xl < r.xh && r.xl < xh && yl < r.yh && r.yl < yh; }
  // Empty (possibly inverted) when the rectangles do not overlap.
  Rect        intersection(const Rect& r) const {
    return {std::max(xl, r.xl), std::max(yl, r.yl), std::min(xh, r.xh), std::min(yh, r.yh)};
  }
  // Smallest rectangle covering both.
  Rect        hull(const Rect& r) const { return {std::min(xl, r.xl), std::min(yl, r.yl), std::max(xh, r.xh), std::max(yh, r.yh)}; }
  Rect        translated(double dx, double dy) const { return {xl + dx, yl + dy, xh + dx, yh + dy}; }
  friend bool operator==(const Rect&, const Rect&) = default;
};
// Finite integral coordinates within the signed 32-bit DEF/OpenDB range.
inline bool integral_dbu(const Rect& r) {
  for (auto v : {r.xl, r.yl, r.xh, r.yh}) {
    if (!std::isfinite(v) || v != std::round(v) || std::abs(v) > std::numeric_limits<int32_t>::max()) {
      return false;
    }
  }
  return true;
}
// W/E/FW/FE place a cell with its width and height exchanged.
inline bool swaps_axes(Orientation o) {
  return o == Orientation::W || o == Orientation::E || o == Orientation::FW || o == Orientation::FE;
}
struct Shape {
  std::string layer;
  Rect        rect;
  friend bool operator==(const Shape&, const Shape&) = default;
};
struct Pin {
  std::string        direction, use;
  std::vector<Shape> shapes;
};
struct Macro {
  double                     width = 0, height = 0, origin_x = 0, origin_y = 0;
  std::string                site, klass;
  bool                       symmetry_x = false, symmetry_y = false;
  bool                       virtual_proxy = false;  // synthesized RC geometry, not a routable LEF abstract
  std::map<std::string, Pin> pins;
  std::vector<Shape>         obstructions;
  std::set<std::string>      conservative_obstructions;
  std::set<std::string>      blocked_layers;  // OBS layers; routability uses conservative full-footprint blocking
};
struct Site {
  double width = 0, height = 0;
  bool   symmetry_x = false;
};
// LEF dimensions remain in microns. A via may be present for placement/import
// even when its geometry cannot yet be used by the native routing oracle.
struct Lef_via {
  std::vector<Shape>    shapes;
  double                resistance = 0;
  bool                  is_default = false;
  std::set<std::string> unsupported;
  friend bool           operator==(const Lef_via&, const Lef_via&) = default;
};
struct Via_rule_layer {
  std::string                          name, direction;
  std::optional<std::array<double, 2>> enclosure, width, spacing;
  std::optional<Rect>                  cut;
  std::optional<double>                resistance;
  friend bool                          operator==(const Via_rule_layer&, const Via_rule_layer&) = default;
};
struct Via_rule {
  bool                        generate = false, is_default = false;
  std::vector<Via_rule_layer> layers;  // LEF order; identify cut/metal by layer type, not index
  std::vector<std::string>    vias;
  std::set<std::string>       unsupported;
  friend bool                 operator==(const Via_rule&, const Via_rule&) = default;
};
struct Cut_layer {
  double                                          width = 0;
  std::vector<double>                             spacing;  // unqualified edge-to-edge distances only
  std::set<std::string>                           unsupported;
  std::map<std::string, std::vector<std::string>> properties;
  friend bool                                     operator==(const Cut_layer&, const Cut_layer&) = default;
};
struct Eol_spacing {
  double                               width = 0, within = 0;
  std::optional<std::array<double, 2>> parallel_edge;  // spacing, within
  bool                                 two_edges = false;
};
struct Metal_spacing {
  double                               distance = 0;
  std::optional<std::array<double, 2>> range;
  std::optional<Eol_spacing>           eol;
};
struct Parallel_spacing {
  std::vector<double>              widths, lengths;
  std::vector<std::vector<double>> spacing;  // indexed by width, then parallel run length
};
struct Min_step {
  double                distance = 0;
  std::string           type;
  std::optional<double> length_sum, adjacent_length, between_length;
  std::optional<int>    max_edges;
  bool                  same_corners = false;
};
struct Metal_rules {
  bool                                            loaded = false;  // false for synthetic layers without a technology rule model
  double                                          area   = 0;
  std::optional<double>                           min_width, max_width;
  std::vector<Metal_spacing>                      spacing;
  std::vector<Parallel_spacing>                   parallel;
  std::vector<std::array<double, 2>>              min_size;
  std::vector<Min_step>                           min_step;
  // Preserve extension text for technology-specific parsing. Until interpreted,
  // its PROPERTY entry remains unsupported and the routing oracle must reject it.
  std::map<std::string, std::vector<std::string>> properties;
  std::set<std::string>                           unsupported;
};
struct Layer {
  double      width = 0, resistance = 0, capacitance = 0, edge_cap = 0;
  std::string direction;
  double      pitch_x = 0, pitch_y = 0;
  double      offset_x = 0, offset_y = 0;
  bool        has_offset = false;
  Metal_rules rules{};
};
// Nominal wire width in DBU when it is a positive even integer within the
// signed 32-bit range, so that centerline paths have integral edges.
std::optional<int64_t> wire_width_dbu(const Layer&, int64_t dbu);
// LEF geometry is in microns; graph/DEF positions are integer DBU.
struct Technology {
  int64_t                          dbu = 1000;
  std::map<std::string, Site>      sites;
  std::map<std::string, Macro>     macros;
  std::map<std::string, Layer>     layers;
  std::vector<std::string>         routing_layers;          // LEF routing-stack order
  double                           manufacturing_grid = 0;  // microns; 0 means unspecified
  std::map<std::string, Lef_via>   vias;
  std::map<std::string, Via_rule>  via_rules;
  std::map<std::string, Cut_layer> cut_layers;
};
// Manufacturing grid in DBU: 1 when LEF leaves it unspecified, empty when it
// is not a positive integral DBU within the signed 32-bit range.
std::optional<int64_t> manufacturing_grid_dbu(const Technology&, int64_t dbu);
struct Row {
  std::string name, site;
  int64_t     x = 0, y = 0, count = 0, step = 0;
  Orientation orientation                        = Orientation::N;
  friend bool operator==(const Row&, const Row&) = default;
};
struct Component {
  std::string name, macro;
  Ann_place   place;
  friend bool operator==(const Component&, const Component&) = default;
};
// Reserved pinless masks emitted by the fixed-frame replay adapter. Old masks
// are hard; the soft prefix preserves initial-placement-only blockages. These
// are placement constraints, not physical macros or routing obstructions.
enum class Placement_mask { none, hard, soft };
inline Placement_mask placement_mask(const Component& c, const Technology& tech) {
  if (!c.name.starts_with("__lhd_frame_mask_")) {
    return Placement_mask::none;
  }
  const auto m = tech.macros.find(c.macro);
  if (m == tech.macros.end() || m->second.klass.starts_with("CORE") || !m->second.pins.empty()
      || (c.place.status != Status::FIXED && c.place.status != Status::LOCKED) || c.place.orientation != Orientation::N) {
    throw std::invalid_argument("placement mask must be fixed, pinless BLOCK geometry in N orientation: " + c.name);
  }
  return c.name.starts_with("__lhd_frame_mask_soft_") ? Placement_mask::soft : Placement_mask::hard;
}
struct Terminal {
  std::string        name, net, direction;
  int64_t            x = 0, y = 0;
  Orientation        orientation = Orientation::N;
  Status             status      = Status::UNPLACED;
  std::vector<Shape> shapes;  // DEF-local DBU rectangles
  std::string        use                                          = "SIGNAL";
  friend bool        operator==(const Terminal&, const Terminal&) = default;
};
struct Connection {
  std::string instance, pin;
  friend bool operator==(const Connection&, const Connection&) = default;
};
// Timing/demand endpoint identity: "instance:pin", or the bare DEF PIN name.
inline std::string connection_label(const Connection& c) { return c.instance == "PIN" ? c.pin : c.instance + ":" + c.pin; }
// Routing geometry uses integer DEF DBU, including via-local coordinates.
// Keep extensions explicit: an omitted extension means half the wire width for
// ordinary nets and zero for special nets. Explicit zero is a flush endpoint.
struct Route_point {
  int64_t                x = 0, y = 0;
  std::optional<int64_t> extension;
  friend bool            operator==(const Route_point&, const Route_point&) = default;
};
struct Route_via {
  std::string name;
  Orientation orientation                                    = Orientation::N;
  friend bool operator==(const Route_via&, const Route_via&) = default;
};
// DEF RECT offsets relative to the preceding route point, on the current
// routing layer (including a layer change through a preceding via).
struct Route_rect {
  int64_t     xl = 0, yl = 0, xh = 0, yh = 0;
  friend bool operator==(const Route_rect&, const Route_rect&) = default;
};
struct Route_path {
  std::string            layer;
  std::optional<int64_t> width;  // mandatory for special wiring, implicit for ordinary signal wiring
  std::string            shape;
  std::vector<std::variant<Route_point, Route_via, Route_rect>> steps;
  std::string                                                   taper_rule;  // width-only DEF NONDEFAULTRULE applied to this path
  friend bool                                                   operator==(const Route_path&, const Route_path&) = default;
};
struct Wire {
  std::string             type = "FIXED";
  std::vector<Route_path> paths;
  friend bool             operator==(const Wire&, const Wire&) = default;
};
struct Generated_via {
  std::string                           rule;
  std::array<std::string, 3>            layers;  // bottom metal, cut, top metal
  std::array<int64_t, 2>                cut_size{}, cut_spacing{};
  std::array<int64_t, 4>                enclosure{};  // bottom x/y, top x/y
  std::optional<std::array<int64_t, 2>> row_col, origin;
  std::optional<std::array<int64_t, 4>> offset;
  friend bool                           operator==(const Generated_via&, const Generated_via&) = default;
};
struct Via {
  std::string                  name;
  std::vector<Shape>           shapes;  // explicit rectangular metal and cut shapes, DEF DBU
  std::optional<Generated_via> generated;
  friend bool                  operator==(const Via&, const Via&) = default;
};
struct Net {
  std::string             name;
  std::vector<Connection> connections;
  std::vector<Wire>       wires{};
  std::string             use{};
  friend bool             operator==(const Net&, const Net&) = default;
};
struct Track {
  std::string axis, layer;
  int64_t     start = 0, count = 0, step = 0;
  int         mask      = 0;
  bool        same_mask = false;
  // Unmasked, nonempty pattern whose start/step/count fit the signed 32-bit range.
  bool        supported() const {
    return !mask && !same_mask && step > 0 && count > 0 && step <= std::numeric_limits<int32_t>::max()
           && count <= std::numeric_limits<int32_t>::max() && start >= std::numeric_limits<int32_t>::min()
           && start <= std::numeric_limits<int32_t>::max();
  }
  // Inclusive index range of the track coordinates within [lo, hi]; empty when
  // first > last. Requires supported().
  std::pair<int64_t, int64_t> index_range(double lo, double hi) const {
    return {std::max<int64_t>(0, std::ceil((lo - start) / step)), std::min<int64_t>(count - 1, std::floor((hi - start) / step))};
  }
  friend bool operator==(const Track&, const Track&) = default;
};
// Read-only routed snapshots retain all constraints as provenance. Placement
// consumes only the supported rectangular subset through Placement_blockage.
struct Blockage {
  std::string       layer, component;
  bool              placement = false, soft = false, slots = false, fills = false, pushdown = false, except_pg = false;
  double            partial_density = -1;
  int               spacing = -1, design_rule_width = -1, mask = 0;
  std::vector<Rect> rectangles;
  std::vector<std::vector<std::pair<int64_t, int64_t>>> polygons;
  friend bool                                           operator==(const Blockage&, const Blockage&) = default;
};
// DEF placement-only rectangles in absolute DBU. Component association is
// retained for export; moving associated components needs a separate update.
struct Placement_blockage {
  bool              soft = false;
  std::string       component;
  std::vector<Rect> rectangles;
  friend bool       operator==(const Placement_blockage&, const Placement_blockage&) = default;
};
struct Gcell_grid {
  std::string axis;
  int64_t     start = 0, count = 0, step = 0;
  friend bool operator==(const Gcell_grid&, const Gcell_grid&) = default;
};
// Width-only DEF rules, also used as non-owning trial overlays during routing.
using Route_width_rules = std::map<std::string, std::map<std::string, int64_t>>;
struct Placement_exclusion {
  Rect           rect;
  Placement_mask kind = Placement_mask::hard;
};
struct Floorplan {
  std::string                     design;
  int64_t                         dbu = 1000;
  Rect                            die;
  std::vector<Row>                rows;
  std::vector<Track>              tracks;
  std::vector<Component>          components;
  std::vector<Terminal>           terminals;
  std::vector<Net>                nets;
  std::vector<Placement_blockage> blockages;
  std::vector<Via>                vias;
  std::vector<Net>                special_nets;
  std::vector<Gcell_grid>         gcell_grid;
  Route_width_rules               nondefault_rules;
  std::vector<Blockage>           snapshot_blockages;  // Read-only routed provenance; never placement authority.
};
Technology                       read_lef(const std::vector<std::string>& paths);
Floorplan                        read_def(const std::string& path);
Floorplan                        read_def_snapshot(const std::string& path);
void                             write_def(const Floorplan& floorplan, const std::string& path);
std::string                      orientation_name(Orientation orientation);
Orientation                      parse_orientation(const std::string& name);
std::pair<double, double>        orient_point(double x, double y, double width, double height, Orientation orientation);
// Both corners oriented within a width x height frame, normalized.
Rect                             orient_rect(const Rect& r, double width, double height, Orientation orientation);
// Macro-local LEF rectangle (microns) of a placed instance, in unrounded DBU.
Rect                             placed_rect(const Macro&, const Ann_place&, int64_t dbu, const Rect& local);
void                             verify_def_roundtrip(const Floorplan& expected, const Floorplan& actual);
// Disjoint hard-first geometry, clipped to the solver domain. Existing fixed
// occupancy is subtracted so overlapping macro bodies are not charged twice.
// Associated components must be fixed: their rectangles cannot silently stay
// behind during macro movement. The original Floorplan remains unchanged.
std::vector<Placement_exclusion> placement_exclusions(const Floorplan& floorplan, const Rect& domain,
                                                      const std::vector<Rect>& fixed_occupancy = {});
// Stage adapter preserving available sites and orientations on a rectangular
// row grid. Missing sites become hard placement rectangles. Restore original
// rows/blockages on final DEF export.
Floorplan                        rectangular_placement_frame(const Floorplan& original, const Technology& technology);
}  // namespace livehd::physical
