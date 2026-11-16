// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>

#include "chunk_jobs.hpp"
#include "dreamplace/native/placement.hpp"
#include "fp_chunks.hpp"

namespace livehd::placement {
struct Chunk_refine_stats {
  size_t solved = 0, reused = 0, refined = 0, deferred = 0;
};
// Seed the downstream placer one chunk at a time. Physical inputs, macro proposals and cut pins are fixed context.
// Identical local problems reuse a detailed placement; a changed boundary starts from that solution and refines.
Chunk_refine_stats refine_chunk_seeds(DreamPlace::native::Design&, const Chunk_model&, const Chunk_plan&,
                                      std::span<const int> owners, std::span<const std::string> local_names, int iterations,
                                      int threads, bool detailed = true,
                                      const std::function<void(size_t, Chunk_job&)>& export_job = {},
                                      std::optional<size_t>                          only       = {});
}  // namespace livehd::placement
