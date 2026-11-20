// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <span>
#include <vector>

#include "rapidjson/document.h"

namespace livehd::placement {
// Track counts on a physical tile grid. Coordinates and pitch are in the same DBU frame.
class Chunk_demand {
public:
  using Pair = std::array<double, 2>;
  using Box  = std::array<double, 4>;
  Chunk_demand(Box frame, double tile_pitch, Pair tracks_per_dbu);
  void             reserve(Box box, Pair fraction, bool internal_demand = false);
  void             internal(Box bounds);
  void             route(std::vector<Pair> terminals);
  rapidjson::Value record(rapidjson::Document& doc, int tracks);

private:
  struct Tile {
    Pair crossing{}, internal{}, blocked{};
  };
  Box                frame;
  double             pitch;
  Pair               tracks;
  int                nx, ny;
  std::vector<Tile>  grid;
  std::vector<int>   seen;
  std::vector<Pair>  difference;
  double             extent(int axis, int index) const;
  void               integrate();
  int                net = 0;
  std::array<int, 2> at(Pair p) const;
};
}  // namespace livehd::placement
