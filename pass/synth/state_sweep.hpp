// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "hhds/graph.hpp"
#include "preparation_budget.hpp"

namespace livehd::synth {

// What the sweep of unobserved state did.
struct State_sweep_stats {
  uint64_t registers_constant = 0;  // registers that only ever hold one constant, replaced by it
  uint64_t constant_bits      = 0;  // their bits
  uint64_t registers_removed = 0;  // Flop/Latch cells nothing observes, deleted
  uint64_t register_bits     = 0;  // their bits
  uint64_t registers_trimmed = 0;  // registers narrowed to the highest bit anything observes
  uint64_t registers_split   = 0;  // registers replaced by one single-bit register per observed bit
  uint64_t split_bits        = 0;  // the bits those single-bit registers keep
  uint64_t trimmed_bits      = 0;  // the bits that narrowing or splitting dropped
  uint64_t cells_removed     = 0;  // combinational cells nothing observes any more
};

// Observability sweep over the private synthesis copy (synth.state_sweep, off by
// default). A register bit is observed when its value can reach a top output,
// an instance of a body-less cell, a memory or an attribute cell. The demand is
// tracked per bit through the bitwise cells, muxes, slices, concatenations, adds
// and constant left shifts (every other cell demands all its operand bits), across
// the instance hierarchy, to a fixpoint -- so a counter that only feeds itself, a
// register file nobody reads and the unread upper half of a wide status register
// are all found, which Yosys finds by flattening and `opt_clean`.
//
// A register nothing observes is deleted. A register whose highest observed bit
// is below its width is narrowed to that bit (the Q pin is restamped unsigned;
// the dropped bits are never read). Cells left without a reader are deleted too.
// Nothing observable changes, but the private copy stops carrying the dead state
// through ABC, placement and the cell count.
//
// `top` names the definition whose outputs are observed in full; a definition
// no other body instantiates is a root as well. Definitions instantiated by a
// compact loop are left alone. Returns no value when the budget refused.
std::optional<State_sweep_stats> sweep_unobserved_state(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                        std::string_view top, Preparation_budget* budget);

}  // namespace livehd::synth
