// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "chunk_jobs.hpp"
#include "dreamplace/native/placement.hpp"
#include "fp_chunks.hpp"

namespace livehd::placement {
// What happened to one chunk and why: `stored` is whether its seed came from the persistent store (else from a sibling in the
// run), and shape/ports say whether the local problem and the boundary contacts matched the seed's.
struct Chunk_refine_decision {
  std::string id, action;  // solved, reused, refined or deferred
  bool        stored = false, shape_same = false, ports_same = false;
};
struct Chunk_refine_stats {
  size_t solved = 0, reused = 0, refined = 0, deferred = 0;
  size_t persisted = 0;  // chunks whose seed came from the persistent store (reused or refined from it)
  std::vector<Chunk_refine_decision> decisions;
};
// A persistent store of finished local chunk placements, for `synth.place.chunk.seed_cache`. An entry is keyed by what the
// chunk contains (partition, implementation, and a hash of its member names, sizes and net structure) and holds the
// positions normalized to the chunk box, so it says nothing about where the chunk sits: a chunk that was only moved, or
// whose neighbors moved, finds its interior again and is replaced instead of placed. `shape` hashes the local problem
// (box size, densities, fixed obstacles and strips in chunk-local coordinates, which nets reach the boundary); `ports`
// hashes where the boundary contacts of the cut nets are, in chunk-local coordinates.
//   exact:     reuse when shape and ports both match, else refine from the stored placement
//   translate: reuse when the shape matches (the boundary contacts moved: the interior is kept as it is), else refine
// The file is `<dir>/chunk_seeds.bin`, replaced atomically; a damaged or foreign file is ignored.
class Chunk_seed_store {
public:
  enum class Mode { off, exact, translate };
  struct Entry {
    uint64_t                          shape = 0, ports = 0;
    double                            w = 0, h = 0;
    std::vector<std::array<double, 2>> xy;
  };
  Chunk_seed_store() = default;
  Chunk_seed_store(std::string dir, Mode mode);
  static Mode parse_mode(std::string_view text);  // off, exact or translate; anything else throws std::invalid_argument
  Mode        mode() const { return mode_; }
  bool        enabled() const { return mode_ != Mode::off && !dir_.empty(); }
  const Entry* find(std::string_view partition, std::string_view implementation, uint64_t key) const;
  void         put(std::string_view partition, std::string_view implementation, uint64_t key, Entry entry);
  bool         dirty() const { return dirty_; }
  size_t       size() const { return entries_.size(); }
  // Writes the file (temporary name, then rename); false when it cannot.
  bool save();

private:
  using Key = std::tuple<std::string, std::string, uint64_t>;
  std::string        dir_;
  Mode               mode_ = Mode::off;
  bool               dirty_ = false;
  std::map<Key, Entry> entries_;
};
// Seed the downstream placer one chunk at a time. Physical inputs, macro proposals and cut pins are fixed context.
// Identical local problems reuse a detailed placement; a changed boundary starts from that solution and refines.
Chunk_refine_stats refine_chunk_seeds(DreamPlace::native::Design&, const Chunk_model&, const Chunk_plan&,
                                      std::span<const int> owners, std::span<const std::string> local_names, int iterations,
                                      int threads, bool detailed = true,
                                      const std::function<void(size_t, Chunk_job&)>& export_job = {},
                                      std::optional<size_t>                          only       = {},
                                      Chunk_seed_store*                              store      = nullptr);
}  // namespace livehd::placement
