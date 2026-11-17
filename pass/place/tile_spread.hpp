// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <vector>

#include "inou/lefdef/physical.hpp"

namespace livehd::placement {
struct Tile_spread_report {
  int64_t tile_w   = 0;  // routing-tile width used (DBU)
  int64_t moved    = 0;  // cells displaced
  int64_t aligned  = 0;  // cells placed so a pin lines up with a connected pin in the row below
  int64_t distance = 0;  // total displacement (DBU)
};
// The routing tile of the global router: OpenROAD/FastRoute's GCell is 15 track pitches of the 4th routing layer
// (or of the top one when fewer exist), here rounded to whole sites. 0 when the LEF has no such pitch or site.
int64_t routing_tile_width(const physical::Technology& tech, const physical::Floorplan& fp);
// A small legalizer help after cell legalization. A legalizer packs cells against each other; the detailed router
// then finds abutting pins with no room for their end-of-line keep-outs and access vias. Inside every routing tile
// and row, the movable single-row CORE cells that sit wholly in a free stretch (between row ends, tile edges and any
// fixed, multi-row or tile-crossing cell) keep their order and share that stretch's free sites evenly. Rows go
// bottom-up: a cell that shares a net with a cell in the row below, within the same tile, takes the site that lines
// its pin up with that pin when its window allows (a straight vertical connection). Positions stay on the row's site
// grid and inside the stretch, so a legal placement stays legal. `tile_w` 0 uses routing_tile_width.
// `obstacles` (DBU) are further keep-outs no cell may enter (macro halos, hard placement blockages); `frozen`
// (empty, or one flag per component) marks cells that must keep their pose (placement domains).
struct Spread_rect {
  int64_t xl = 0, yl = 0, xh = 0, yh = 0;
};
Tile_spread_report spread_cells_in_tiles(const physical::Technology& tech, physical::Floorplan& fp, int64_t tile_w = 0,
                                         const std::vector<Spread_rect>& obstacles = {}, const std::vector<char>& frozen = {});
}  // namespace livehd::placement
