// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "fp_chunks.hpp"

namespace livehd::placement {
// The winning sibling family, one occurrence scope per object. Empty entries are glue.
// An empty vector means that the color inventories did not identify a substantial family.
std::vector<std::string> repeated_chunk_scopes(const std::vector<Chunk_object>& objects, Chunk_family_summary& summary);
}  // namespace livehd::placement
