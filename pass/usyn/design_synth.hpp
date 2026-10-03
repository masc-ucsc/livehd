// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "arith.hpp"
#include "logical_cache.hpp"
#include "region_emit.hpp"

namespace livehd::usyn {

struct Design_options {
  Logical_options          logical;
  synth::arith::Adder_kind adder            = synth::arith::Adder_kind::rca;
  bool                     auto_sum_adder   = true;
  int                      adder_block      = 0;
  synth::arith::Mult_kind  multiplier       = synth::arith::Mult_kind::csa;
  uint64_t                 max_source_nodes = 2000000;
  partition::Flatten_mode  flatten          = partition::Flatten_mode::automatic;
  Logical_cache_options    cache{};
};

struct Design_region {
  std::string     module_name;  // current name, also on a cache hit
  Stateful_region selected;
  Logical_report  report;
  bool            cache_reused = false;
  std::string     cache_key{};
  uint64_t        search_credits = 0;  // the limit minus earlier regions' search work
  Credit_floor    search{};            // this search's consumption and credit floor (replayed on a hit)
  // Structural-allowance work of the region's mandatory synthesis steps
  // (import, logical admission and identity baseline, any structural pricing
  // or rebuild, freezing), replayed on a hit. Translation and emission are
  // charged outside it.
  uint64_t        structural_work = 0;
};

struct Logical_design {
  // Declared first so the library outlives all graph handles. Region selection
  // records own their Boolean data and source provenance, not scratch handles.
  hhds::GraphLibrary           library;
  std::shared_ptr<hhds::Graph> top;
  std::vector<Design_region>   regions;
  Logical_cache_stats          cache{};
};

struct Design_result {
  Status                          status = Status::invalid;
  std::unique_ptr<Logical_design> design;
  std::string                     reason;
};

// Native, hierarchy-preserving CMOS synthesis of a top and its definition
// closure. Keeps compact loops, native special structures and original state
// semantics. Neither SATOPT, ABC nor a prover is invoked. No Liberty needed.
// Source graphs are immutable; refusal never publishes a partial output.
// Serial graph access is required. This is the logical design seam, not a
// physical DominoLatch emitter or technology mapper. Shared private preparation
// uses source-node/work limits and process checkpoints. Partition collection,
// naming and reconstruction share work/process admission and stop after failed
// region builders. Individual bulk helpers/allocations are not yet bounded.
// `work` is the structural allowance: preparation, partition and region
// admission, translation, every region's mandatory synthesis steps (semantic
// import, logical admission and identity baseline, any structural pricing or
// rebuild, freezing) and emission
// charge it, and exhausting it refuses the design. Region searches share a
// separate search-only remainder that starts at work's entry value: region i
// gets that value minus the search work of regions before it, so structural
// work never changes a search and a low remainder degrades later searches
// (each keeps its complete incumbent, at worst its identity selection) instead
// of failing the design. The cache key excludes credits; a record is reused
// only when its credit floor reproduces it under the region's current
// credits, and a hit replays its recorded search work on the remainder and its
// structural work on `work`. Warm results equal cold results, including
// `work.remaining` and every region's search credits.
Design_result synthesize_cmos_design(const std::shared_ptr<hhds::Graph>& top, const Design_options& options, Budget& work);

}  // namespace livehd::usyn
