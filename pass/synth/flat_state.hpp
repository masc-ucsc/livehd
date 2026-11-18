// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

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

}  // namespace livehd::synth
