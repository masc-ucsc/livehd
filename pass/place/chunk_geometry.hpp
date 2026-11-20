// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "fp_chunks.hpp"

namespace livehd::placement {
struct Chunk_edit {
  std::string              move;
  std::array<double, 2>    to{};
  double                   overlap = 0;
  std::vector<std::string> locks;
  bool                     lock = false;
  std::string              all_but;
  std::string              reshape;
  double                   aspect = 0;
  std::string              step, peer;
  double                   depth = 0, band = 0, offset = 0;
};
using Chunk_edits = std::vector<Chunk_edit>;
Chunk_edits read_chunk_edits(const metadata::Bundle*, const std::string& design);
// Start from a disjoint tiling and apply the ordered physical-instance edits.
// On exact replay the reference tiling has already been validated and restored.
void        edit_chunk_boxes(Chunk_plan&, const Chunk_edits&, const std::vector<Chunk_object>&, Chunk_box, std::array<double, 2>);
// Exchange equal-area strips on an exclusive edge shared by two physical homes.
void apply_chunk_step(Chunk_plan&, const Chunk_edit&, const std::vector<Chunk_object>&, std::array<double, 2>);
// One Jacobi update: all overlap edges read the same previous color centers.
void relax_shared_colors(const Chunk_plan&, const std::vector<Chunk_object>&, const std::vector<std::array<double, 2>>& previous,
                         std::vector<std::array<double, 2>>& next);
}  // namespace livehd::placement
