// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/metadata.hpp"

namespace livehd::placement {
// Cell bodies, independent macro bodies, logic pin/net incidences, macro pin/net incidences.
using Memory_counts                         = std::array<uint64_t, 4>;
inline constexpr char memory_sample_class[] = "livehd.placement.chunk_memory_sample";
struct Memory_sample {
  std::string   id;
  Memory_counts counts{};
  uint64_t      start = 0, peak = 0;
};
struct Chunk_predictor {
  std::string                context;
  uint64_t                   baseline = 0;
  std::array<double, 4>      rate{8192. * 4 / 3, 8192. * 4 / 3, 128. * 4 / 3, 128. * 4 / 3};
  Memory_counts              observed_max{};
  unsigned                   samples = 0;
  std::vector<Memory_counts> observed;
  std::vector<Memory_sample> evidence;  // Raw observations, separate from a replayed plan's historical estimate.
  void                       append_evidence(metadata::Bundle&) const;
  bool                       covers(const Memory_counts&) const;
  uint64_t                   variable(const Memory_counts&) const;
  uint64_t                   peak(const Memory_counts&) const;
  static Chunk_predictor     fit(const std::string& context, const std::vector<Memory_sample>&);
};
Chunk_predictor load_chunk_predictor(const std::string& context, const std::string& path, const metadata::Bundle* input);
void save_chunk_sample(const std::string& context, const Memory_counts&, uint64_t start, uint64_t peak, const std::string& path,
                       const std::string& bundle_path);
class Placement_peak {
public:
  Placement_peak();
  ~Placement_peak();
  uint64_t       finish();
  const uint64_t start;

private:
  std::mutex              mutex;
  std::condition_variable changed;
  bool                    stopped = false;
  const uint64_t          initial_self_peak;
  uint64_t                high;
  std::thread             sampler;
};
}  // namespace livehd::placement
