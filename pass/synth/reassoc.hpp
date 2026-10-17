// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstddef>
#include <functional>

#include "hhds/graph.hpp"
#include "preparation_budget.hpp"

namespace livehd::synth {

// Reassociation of associative Boolean chains in the synthesis private copy.
//
// A carried loop expanded for synthesis (loop_cleanup.cpp) leaves one
// two-input cell per iteration: `acc |= term` becomes the left-deep chain
// Or(Or(Or(t0, t1), t2), t3). The compile-time cprop sweep merges such
// private regions into one n-ary cell, but it runs before pass.color and,
// once colors exist, treats every colored cell as an opaque boundary -- so a
// chain minted by the post-color loop expansion reaches the blaster as a
// chain, and neither the blaster (it balances only within ONE n-ary cell)
// nor the mapper rebalances it: the critical path grows linearly with the
// trip count.
//
// reassociate_chains merges every maximal tree of And, Or or Xor cells (in
// scope, see Reassoc_scope) in
// which each absorbed cell has exactly one consumer edge, that consumer is the
// same operator, and both sit in the same synthesis region (equal color and
// equal control-cone membership, no per-cell synth policy, no native boundary
// marker) into its root as one n-ary cell. The blaster then lowers the n-ary
// cell into a balanced pairwise tree (blast.hpp). It never crosses a region
// cut, an instance, a graph port or a shared value, so the partition, every
// boundary and every other consumer see the same values. Width safety follows the blaster's bit semantics: a
// narrower intermediate is absorbed only when its truncation/extension is
// value-neutral for every bit the root reads. Wide (packed-wiring) Or cells
// are left alone because the region blaster keeps some of them native.
//
// Before merging, a guarded accumulation link -- an unrolled
// `if c { acc |= x }`, Mux(c, a, Or(a, x)) -- is rewritten in place to
// Or(a, Mux(c, 0, x)) (Xor alike, And with an all-ones identity) when both
// cells share a region, so a chain of guarded links becomes a plain chain the
// merge balances instead of a serial MUX chain.
//
// One pass, O(V + E): each cell is classified once and absorbed at most once
// (its single consumer edge names a unique root). Returns the number of cells
// absorbed; false-y budgets refuse like any other preparation step.
struct Reassoc_stats {
  size_t guarded  = 0;  // `c ? a OP x : a` links rewritten to `a OP (c ? x : id)`
  size_t roots    = 0;  // n-ary cells that absorbed at least one link
  size_t absorbed = 0;  // two-or-more-input links folded into a root
};

// The cells a rewrite may touch, as a link, as the cell a link merges into or
// as either half of a guarded link. Empty: every cell. prepare_design passes
// "minted by the loop expansion": the merge exists for the chains an expanded
// carried loop leaves, and the source's own And/Or/Xor structure (and a
// mapped netlist re-prepared for technology mapping) is the mapper's to read
// as written -- flattening it measured as a wash for ABC and a loss for usyn,
// whose region trees encode arrival order the RAW levels cannot see.
using Reassoc_scope = std::function<bool(const hhds::Node_class&)>;

[[nodiscard]] bool reassociate_chains(hhds::Graph* graph, Reassoc_stats& stats, Preparation_budget* budget = nullptr,
                                      const Reassoc_scope& scope = {});

}  // namespace livehd::synth
