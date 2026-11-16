// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <filesystem>

#include "dreamplace/native/placement.hpp"
#include "fp_chunks.hpp"

namespace livehd::placement {
// The producer relinquishes its flat View before the independent workers run.
struct Chunk_jobs_ready {
  std::string manifest;
};
struct Chunk_job {
  std::string                design, id;
  uint64_t                   plan_key = 0, budget = 0;
  int                        dbu = 0, iterations = 0, threads = 1;
  bool                       detailed = true, multirow = false;
  double                     density = .5;
  Chunk_box                  box{};
  std::vector<std::string>   names;          // full physical names in native movable order
  std::vector<int>           boundary_nets;  // local net indexes; contact is its final, fixed pin
  std::vector<uint64_t>      original_nets;
  DreamPlace::native::Design physical;
};
int place_chunk_worker(const std::filesystem::path& input, const std::filesystem::path& output, const std::string& checksum);
int place_chunk_batch(const std::string& executable, const std::filesystem::path& manifest, const std::filesystem::path& output,
                      const std::optional<std::string>& selected = {});
// Private payloads never contain sibling cells. The manifest contains portable planner metadata.
std::string write_chunk_jobs(const std::filesystem::path& manifest, const metadata::Bundle& base, const Chunk_model&,
                             const Chunk_plan&, DreamPlace::native::Design&, std::span<const int> owners,
                             std::span<const std::string> names, const std::string& design, int dbu, int iterations, int threads,
                             bool detailed, bool strict, const std::string& selected = {},
                             const Chunk_predictor* current = nullptr);
}  // namespace livehd::placement
