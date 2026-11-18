// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "preparation_budget.hpp"

namespace livehd::synth {

struct Flat_state_stats {
  uint64_t contexts         = 0;  // instances of the flat analysis copy
  uint64_t folded_bits      = 0;  // state bits replaced by a constant in some definition (copy)
  uint64_t memories_dropped = 0;  // memories none of whose read data the whole design observes, removed in a copy
  uint64_t copies           = 0;  // content-named definition copies made for instances with other verdicts
  uint64_t graphs_folded    = 0;  // definitions (or copies) whose flops were cut
  uint64_t lost_bits        = 0;  // bits the flat proof cut but whose provenance was lost (left as state)
  uint64_t outputs_folded   = 0;  // parent definitions rewired to read a folded copy's constant output
};

// The constant and unobserved state that only the WHOLE design proves (router rings: a register is constant only if
// its neighbors are), applied without flattening the synthesis copy.
//
// A private analysis copy of `top` is flattened with provenance (sweep::Flat_origin) and swept to a fixpoint exactly
// as synth.flat_sweep sweeps the real copy. Every flop bit and memory that sweep cuts out is a fact about the reachable
// behavior of the design: the bit always reads its constant, or nothing can observe it. The facts go back to the flop of
// the definition each flat bit came from, per instance. Instances of one definition whose subtree carries the same
// facts share one content-named copy (`<name>__f<hash>`); the original definitions are never edited (only `top` is,
// in place), so an instance the analysis did not see (a compact loop, another root) keeps the definition as compiled.
// The cut bits are folded into each copy and the copy is refolded; the hierarchical state sweep that follows removes
// what became dead.
//
// Returns no value when the budget refused or there is nothing to do.
std::optional<Flat_state_stats> fold_flat_state(std::vector<std::shared_ptr<hhds::Graph>>& definitions, std::string_view top,
                                                Preparation_budget* budget);

struct Const_bits_stats {
  uint64_t rounds        = 0;  // fixpoint rounds that changed something
  uint64_t output_bits   = 0;  // instance output bits a parent now reads as 0 (summed over instances)
  uint64_t input_bits    = 0;  // definition input bits now read as 0 inside the body (every instance drives 0)
  uint64_t graphs_folded = 0;  // refolds of a definition that gained masks
};

// Constant-0 bits across instance boundaries, without flattening. A definition whose output bits are provably 0 (a
// spill register whose payload flops the whole-design proof cut: 577 of 579 payload bits read 0) makes every parent
// read those bits as 0; a definition whose input bits every instance drives with 0 reads them as 0 inside its body.
// Both are plain masks (`x & keep`) that the refold folds through the logic, so a pass-through chain (FIFO -> route
// select -> output arbiter -> wormhole arbiter) learns its zeros round by round until nothing changes. Only a whole
// port's bits that hold at EVERY instance in the private copy are applied to a definition, so no copy is made here.
//
// Returns no value when the budget refused.
std::optional<Const_bits_stats> propagate_constant_bits(std::vector<std::shared_ptr<hhds::Graph>>& definitions,
                                                        std::string_view top, Preparation_budget* budget);

// Carry the per-instance-path synthesis colors (attrs::hier_color, written by pass.color over the virtual flat design)
// onto definition copies made AFTER coloring. A copy keeps its original's node ids and a re-pointed instance keeps its
// Sub node id, but every hier_color entry is keyed by the instance path's (definition gid, Sub id) steps and stored in
// the original definition: without this, every node under a re-pointed instance reads uncolored and the partitioner
// merges the whole subtree into one color-0 region. `original_of` maps each copy to the definition it was cloned from
// (a copy of a copy maps to the first original); graphs not in it are their own original.
void rehome_hier_colors(const std::vector<std::shared_ptr<hhds::Graph>>& definitions, const std::shared_ptr<hhds::Graph>& root,
                        const absl::flat_hash_map<const hhds::Graph*, const hhds::Graph*>& original_of);

}  // namespace livehd::synth
