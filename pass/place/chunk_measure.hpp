// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "fp_chunks.hpp"

namespace livehd::placement {
// Measure physical occupancy against the saved rectangles, including fractional area across an edge.
// No per-cell coordinates are kept by this accumulator.
class Chunk_measure {
  struct Bin {
    Chunk_box box;
    int       a = -1, b = -1, chunk = -1;
  };
  struct Row {
    std::string            id;
    Chunk_box              box;
    double                 area = 0, macro = 0;
    std::vector<Chunk_box> shared;
    double                 capacity = 0;
  };
  struct Color {
    Chunk_box slot;
    int       home;
    double    area = 0, in_slot = 0, in_home = 0;
  };
  std::vector<Bin>                          tree;
  std::vector<Row>                          rows;
  std::map<std::string, Color, std::less<>> colors;

public:
  explicit Chunk_measure(const rapidjson::Value& plan);
  void                cell(const Chunk_box&, bool macro, std::string_view color);
  rapidjson::Document record(const std::string& design) const;
};
}  // namespace livehd::placement
