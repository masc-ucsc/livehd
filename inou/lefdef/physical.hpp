// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "ann_place.hpp"

namespace livehd::physical {
using Orientation = Ann_place::Orientation;
using Status      = Ann_place::Status;
struct Rect {
  double      xl = 0, yl = 0, xh = 0, yh = 0;
  friend bool operator==(const Rect&, const Rect&) = default;
};
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
struct Layer {
  double      width = 0, resistance = 0, capacitance = 0, edge_cap = 0;
  std::string direction;
  double      pitch_x = 0, pitch_y = 0;
  double      offset_x = 0, offset_y = 0;
  bool        has_offset = false;
};
// LEF geometry is in microns; graph/DEF positions are integer DBU.
struct Technology {
  int64_t                      dbu = 1000;
  std::map<std::string, Site>  sites;
  std::map<std::string, Macro> macros;
  std::map<std::string, Layer> layers;
  std::vector<std::string>     routing_layers;          // LEF routing-stack order
  double                       manufacturing_grid = 0;  // microns; 0 means unspecified
};
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
  friend bool        operator==(const Terminal&, const Terminal&) = default;
};
struct Connection {
  std::string instance, pin;
  friend bool operator==(const Connection&, const Connection&) = default;
};
struct Net {
  std::string             name;
  std::vector<Connection> connections;
  friend bool             operator==(const Net&, const Net&) = default;
};
struct Track {
  std::string axis, layer;
  int64_t     start = 0, count = 0, step = 0;
  int         mask                                   = 0;
  bool        same_mask                              = false;
  friend bool operator==(const Track&, const Track&) = default;
};
// DEF placement-only rectangles in absolute DBU. Component association is
// retained for export; moving associated components needs a separate update.
struct Placement_blockage {
  bool              soft = false;
  std::string       component;
  std::vector<Rect> rectangles;
  friend bool       operator==(const Placement_blockage&, const Placement_blockage&) = default;
};
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
};
Technology                       read_lef(const std::vector<std::string>& paths);
Floorplan                        read_def(const std::string& path);
void                             write_def(const Floorplan& floorplan, const std::string& path);
std::string                      orientation_name(Orientation orientation);
Orientation                      parse_orientation(const std::string& name);
std::pair<double, double>        orient_point(double x, double y, double width, double height, Orientation orientation);
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
