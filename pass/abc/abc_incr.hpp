// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// INCREMENTAL pass.abc (todo/livehd/2opt-incr.html subtasks A + C).
//
// The decision-oracle loop is: edit a little RTL, resynthesize, compare QoR.
// A full pass.abc pays ABC for every region on every iteration, but a small
// edit changes a handful of regions — the rest map to byte-identical netlists.
// This engine gives each region a CANONICAL 128-BIT DIGEST of everything that
// determines its mapping (logic, boundary, resolved per-region ABC options)
// and keeps a persistent cache directory of previously mapped region bodies,
// content-addressed by that digest. On a hit the cached netlist is cloned into
// the fresh region module and ABC never runs; the LiveSynth lineage's diff --
// 83-95% of its live runtime, and the step both commercial flows fail at (24
// of Anubis's 144 changes are no-ops they re-synthesize anyway) -- becomes a
// hash lookup.
//
// The cache is a SPEEDUP, never an oracle of record: a miss only costs an ABC
// run, and anything the digest cannot canonicalize REFUSES to cache rather
// than risk a wrong reuse (the semdiff/cone_digest refusal discipline).
//
// What the digest folds (soundness inventory):
//   * every region node: op, driver widths/signs, sink-port-grouped fanin
//     (commutative-normalized within a port class, the fold_operands rule),
//     constant values, Sub target names, instance names when present;
//   * stateful nodes (Flop/Latch/Fflop/Memory) are anchored by their NAME --
//     read-back rebuilds registers under the original name and LEC pairs flops
//     by name, so the name is part of the function. Anonymous state => refuse;
//   * the boundary: each crossing input as (bits, sign) refined by its fanout
//     structure (Weisfeiler-Lehman rounds); each output as (driver sig, port,
//     bits, sign). Two boundary pins the refinement cannot tell apart would
//     make the cached-port -> fresh-port stitch ambiguous -- a swapped stitch
//     is a MISCOMPILE -- so ambiguity => refuse;
//   * the resolved per-region ABC recipe (flow with {D}/{L} substituted,
//     adder/multiplier architecture, block size).
// Library content, register/memory mapping mode and the DFF cell are global,
// so they salt the whole cache file instead (Incr_cache::make_salt).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "pass_partition.hpp"  // livehd::partition::Region_body

namespace livehd::abc {

struct Region_qor;  // abc_map.hpp

// Canonical digest of one region, plus the canonical boundary ranking that a
// reuse needs to stitch cached module ports onto the freshly named ones.
// `valid == false` means "do not cache": anonymous state, ambiguous boundary,
// or a port list that does not match the crossing-pin walk.
struct Region_digest {
  uint64_t h0 = 0, h1 = 0;  // two independent lanes; a collision is a miscompile
  bool     valid = false;
  // Why valid == false, for the verbose miss report ("" when valid). A region
  // that never caches is a standing tax on every iteration; the reason is what
  // lets someone fix the RTL (name the state) or the engine.
  const char* refused = "";

  // in_by_rank[r] = index into Region_body::inputs of the port whose external
  // driver holds canonical rank r (ranks are sorted boundary tokens, a pure
  // function of the region content -- NOT of nids, names or port order, all of
  // which shift under an edit). Same for outputs.
  std::vector<size_t> in_by_rank;
  std::vector<size_t> out_by_rank;

  [[nodiscard]] std::string hex() const;  // 32 hex chars
};

// Compute the digest. `opts_sig` is the caller's hash of the RESOLVED
// region-mapping options (post region_opts overlay) -- two regions with equal
// logic but different ABC recipes must never share a cache row.
[[nodiscard]] Region_digest region_digest(const livehd::partition::Region_body& rb, uint64_t opts_sig);

// Hash of the per-region ABC recipe, for `opts_sig`: the two resolved flow
// strings ({D}/{L} already substituted) plus the arithmetic architecture.
[[nodiscard]] uint64_t incr_opts_hash(std::string_view comb_flow, std::string_view seq_flow, int adder, int block_size,
                                      int multiplier);

// The persistent cache: a directory holding
//   * abc_cache.json  -- {schema, salt, regions: {digest: {module, in[], out[], qor...}}}
//   * an hhds GraphLibrary of cached region bodies, one module per digest
//     (content-addressed name "r_<digest>"), liberty-cell/child IO decls cloned
//     opaque.
// Load is salt-gated: a mismatch starts the metadata empty (stale module
// bodies linger in the library harmlessly -- they are unreachable without a
// row, and a later store under the same name replaces the body).
class Incr_cache {
public:
  struct Row {
    std::string              module;  // module name inside the cache library
    std::vector<std::string> in;      // cached-module input port name per canonical rank
    std::vector<std::string> out;     // cached-module output port name per canonical rank
    // Cached Region_qor payload (module/color are per-run, re-stamped on hit).
    int         gates = 0;
    double      area  = 0.0;
    float       delay = -1.0f;
    int         crit_out_rank = -1;  // canonical rank of the critical output, -1 = none
    std::string crit_src;
    int         div_blackbox = 0;
  };

  // Opens (creates) the cache directory and loads abc_cache.json when its salt
  // matches. The library singleton is opened lazily on first body access.
  Incr_cache(std::string dir, uint64_t salt);

  [[nodiscard]] const Row* lookup(const Region_digest& d) const;

  // Record a freshly mapped region: clone rb.body into the cache library under
  // the content-addressed name and add the metadata row. Failures warn and
  // skip (the cache is only ever a speedup).
  void store(const livehd::partition::Region_body& rb, const Region_digest& d, const Region_qor& q);

  // Clone the cached module's body into rb.body (whose IO is already declared
  // and materialized by the partitioner), mapping cached port names to the
  // fresh ones through the canonical ranks. Returns false (and the caller maps
  // normally) when the cached module is missing or malformed.
  [[nodiscard]] bool reuse(const livehd::partition::Region_body& rb, const Region_digest& d, const Row& row,
                           hhds::GraphLibrary* outlib);

  // Persist abc_cache.json (atomic tmp+rename) and the cache library. No-op
  // when nothing was stored.
  void save();

  [[nodiscard]] int hits() const { return hits_; }
  [[nodiscard]] int misses() const { return misses_; }
  [[nodiscard]] int stores() const { return stores_; }
  [[nodiscard]] int refused() const { return refused_; }
  void note_miss(bool digest_valid) {
    ++misses_;
    if (!digest_valid) {
      ++refused_;
    }
  }

  [[nodiscard]] const std::string& dir() const { return dir_; }

  // Salt for the whole cache: the global inputs a region digest does not see.
  // Library CONTENT (not path -- the same path with edited cells must miss),
  // sequential-mapping mode, DFF cell choice, assume feeding, plus a schema
  // constant bumped when the mapper's read-back changes shape.
  [[nodiscard]] static uint64_t make_salt(std::string_view library_path, bool map_register, bool map_memory,
                                          std::string_view dff_cell, bool use_proven_assume, bool use_all_assume);

private:
  std::string dir_;
  uint64_t    salt_ = 0;
  bool        dirty_ = false;
  int         hits_ = 0, misses_ = 0, stores_ = 0, refused_ = 0;

  absl::flat_hash_map<std::string, Row> rows_;  // digest hex -> row

  [[nodiscard]] hhds::GraphLibrary& lib();
};

}  // namespace livehd::abc
