// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <set>

#include "fp_chunks.hpp"
namespace livehd::placement {
struct Home_edit {
  enum Kind { split, fuse, transfer } kind = fuse;
  std::string                            first, second;
  std::map<std::string, Chunk_box>       before;
  std::set<std::pair<bool, std::string>> members;
};
using Home_history = std::map<std::string, std::set<std::string>>;  // Retired (logical partition, instance) pairs.
std::vector<Home_edit> read_home_edits(const metadata::Bundle*, const std::string&);
Home_history repartition(std::vector<Fp_chunk>&, const std::vector<Home_edit>&, const std::vector<Chunk_object>&, Chunk_box,
                         std::array<double, 2>);
}  // namespace livehd::placement
