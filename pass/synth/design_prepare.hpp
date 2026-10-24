// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "clock_gate.hpp"  // //graph
#include "hhds/graph.hpp"
#include "loop_cleanup.hpp"
#include "preparation_budget.hpp"
#include "reassoc.hpp"
#include "register_merge.hpp"
#include "state_sweep.hpp"

namespace livehd::synth {

// Owns the copied definition closure through mapping/read-back. Roots retain
// the caller's order (including null entries); resolve_graphs starts with those
// roots so implicit top selection never depends on library hash order.
struct Prepared_design {
  hhds::GraphLibrary                        library;
  std::vector<std::shared_ptr<hhds::Graph>> roots;
  std::vector<std::shared_ptr<hhds::Graph>> definitions;
  std::vector<std::shared_ptr<hhds::Graph>> resolve_graphs;
  Loop_preparation                          loops;
  Reassoc_stats                             reassoc;  // what `reassociate_loops` merged
  Register_merge_stats                      merged;   // what `merge_registers` merged
  State_sweep_stats                         sweep;    // what `state_sweep` removed
  clock_gate::Clock_gate_stats              gating;   // what `clock_gate` collapsed and inferred
};

// What prepare_design reassociates among the cells a loop expansion minted.
enum class Loop_reassoc {
  none,     // the chains stay as expanded
  all,      // every chain (Reassoc_trees::all)
  guarded,  // only guarded-accumulation chains (Reassoc_trees::guarded)
};

// ABC-independent private-copy and compact-loop preparation. Source graphs
// and their libraries remain untouched. Opaque callee IOs are copied as well
// as bodies, so nested blackboxes retain their declared ports. Optional budget
// refusal returns no partial design and sets budget->refused; other failures
// retain their existing diagnostics. A refused budget stays refused.
// `specialize` runs pass.specialize (state-free callees only) on the private
// copy, after pass.color decided the regions (synth.specialize, default true).
// `reassociate_loops` merges the associative chains an expanded carried loop
// leaves into balanced n-ary cells (reassoc.hpp). pass.abc asks for `all`:
// ABC maps the structure it is handed and does not rebalance a 16-link chain.
// pass.usyn asks for `guarded`: its XAG flow measured neutral to worse on
// balanced plain loop chains (lhdtrack br_arb_lru +8% delay), but a guarded
// accumulation's chain it maps linearly (Reassoc_trees::guarded).
// `state` runs the register clean-ups of the private copy, after specialization and before
// the loops are prepared: `merge_registers` (synth.merge_registers, default false) merges
// registers that are the same machine (register_merge.hpp), then `state_sweep`
// (synth.state_sweep, default false) deletes the registers and cells nothing observes and
// narrows registers to their highest observed bit (state_sweep.hpp).
struct State_options {
  bool             state_sweep     = false;
  bool             merge_registers = false;
  // synth.clock_gate: collapse the existing Clock_cells and gate registers that share enable conjuncts (clock_gate.hpp). The
  // minimum bits a gate must cover and the chain depth are synth.clock_gate_min_bits / synth.clock_gate_depth.
  bool             clock_gate          = false;
  uint32_t         clock_gate_min_bits = 16;
  uint32_t         clock_gate_depth    = 1;
  std::string_view top;  // the definition whose outputs the sweep must keep in full
};
std::unique_ptr<Prepared_design> prepare_design(std::span<const std::shared_ptr<hhds::Graph>> sources, bool unroll_carry,
                                                std::string_view from_pass, Preparation_budget* budget = nullptr,
                                                bool specialize = true, Loop_reassoc reassociate_loops = Loop_reassoc::none,
                                                const State_options& state = {});

}  // namespace livehd::synth
