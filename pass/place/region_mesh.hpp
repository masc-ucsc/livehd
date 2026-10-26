// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "dreamplace/native/placement.hpp"

namespace livehd::placement {
// The `region` partition role (synth.place.region.*): irregular rectangles of
// the detail-final phase, aligned to fixed macro edges and IO bus spans, each
// holding at most max_cells movable cells. Unlike the uniform GP `grid` and
// the legalization `bin`s, a region has explicit cell membership; crossings,
// virtual pins and bus order live on its edges. Built once per detail-final
// phase and shared by bus order, boundary pins and pin alignment.
struct Region_options {
  unsigned max_cells = 10000;  // split along the longest side until met, then merge back under the cap
  double   merge_um  = 5;      // macro-edge/IO-span cut lines closer than this merge
};
void validate_region_options(const Region_options&);
struct Bus_bit {
  std::string bus;
  int         bit = -1;
};
// "name[3]" (escaped brackets accepted) -> {name, 3}; scalar names have none.
std::optional<Bus_bit> indexed_bus_bit(const std::string&);
struct Region_io {
  std::string name;
  double      x = 0, y = 0;
};
struct Region_rect {
  double   xl, yl, xh, yh;
  unsigned cells = 0;
};
struct Region_edge {
  int    axis     = 0;  // normal axis: vertical boundary = 0, horizontal = 1
  double position = 0, low = 0, high = 0;
};
// Split shared collinear boundaries at every endpoint, including T junctions.
std::vector<Region_edge> region_edges(const std::vector<Region_rect>&);

struct Region_mesh {
  std::vector<Region_rect>      rects;
  std::vector<Region_edge>      edges;
  std::vector<std::vector<int>> members;  // movable cell indices per rect, by cell center
  Region_options                options;
  unsigned                      initial_rectangles = 0, removed_h = 0, removed_v = 0;
  double                        build_seconds = 0;
  // All movable nodes must be standard cells; `macros` are fixed node indices.
  // Geometry is in the native design's coordinate units.
  static Region_mesh build(const DreamPlace::native::Design&, const std::vector<int>& macros, const std::vector<Region_io>&,
                           const Region_options&, double dbu_per_um);
  // Given rectangles with derived edges and no membership (tests, replays).
  static Region_mesh from_rects(std::vector<Region_rect>);
  // Half-open [xl,xh) x [yl,yh) lookup; empty outside every rectangle.
  std::optional<int> rect_of(double x, double y) const;
  // <dir>/region_mesh.json: the one mesh description every consumer report references.
  void               write(const std::string& dir) const;
};
}  // namespace livehd::placement
