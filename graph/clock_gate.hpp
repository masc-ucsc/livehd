// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "hhds/graph.hpp"

namespace livehd::clock_gate {

// Optional work admission (synthesis' Preparation_budget): false stops the pass, which then returns no value. Empty admits all.
using Admit = std::function<bool(std::string_view stage, uint64_t work)>;

struct Clock_gate_options {
  // Inference is off at 0. A group of registers is gated only when the bits it clocks reach this many: an ICG is a latch and an
  // AND (or one library cell), so it pays for itself when it replaces at least that many per-bit enable muxes.
  uint32_t min_bits = 0;
  // A group is gated only when logic that feeds nothing else (the "exclusive cone" of its data inputs, up to the next register,
  // memory, instance or port) has at least this many nodes: a closed gate then skips that logic. 0 = no cone test (synthesis: the
  // gate also replaces the per-bit enable muxes, so it pays without one). The simulator sets it: a register fed straight from a
  // port gates nothing there, and its gate costs the scheduler a finer split.
  uint32_t min_cone = 0;
  // How many gates may be chained clock -> gate -> gate -> register: each level factors the next conjunct the registers below it
  // still share. 1 (siblings on the clock) is the default: pass.lec recognizes an inline ICG only on a graph-input clock, so a
  // mapped CASCADE of inferred gates cannot be proven yet, and a cascade adds clock latency a CTS has to balance. The netlists
  // are right (iverilog agrees on random stimulus); the proof is what is missing.
  uint32_t depth    = 1;
};

struct Clock_gate_stats {
  uint64_t gates      = 0;  // Clock_cells inferred
  uint64_t registers  = 0;  // registers moved onto a gated clock
  uint64_t bits       = 0;  // their bits
  uint64_t enables    = 0;  // register enables that vanished entirely (the gate carries all of them)
  uint64_t collapsed  = 0;  // existing Clock_cells removed because an earlier gate in the chain already checks their enable
  uint64_t duplicates = 0;  // existing Clock_cells merged with an identical sibling
  uint64_t redundant  = 0;  // register enable conjuncts dropped because the gated clock already implies them
};

// Clock gating on a consumer's PRIVATE copy of the design (synth.clock_gate for the mappers, the simulator's own preparation), in
// two steps per definition:
//
//  1. COLLAPSE the Clock_cells already there. A gate whose clock is another gate's output and whose enable is covered by the
//     enables of the gates above it is that outer gate (the "same check, or a subset, in a row"); a gate in a row that still has
//     something new to check becomes ONE gate on the root clock with every check in a row ANDed (a cascade adds clock latency, and
//     pass.lec cannot prove a cascade of inline gates but proves the single level); two gates on one clock with the same enable
//     are one gate. A register on a gated clock drops the enable conjuncts the gates above it already imply.
//  2. INFER gates. Registers on one clock with the same reset behavior whose enables share a conjunct (an enable is the AND of
//     its 1-bit operands) move onto a Clock_cell whose enable is the shared part, keeping only what is left of their own enable.
//     The shared part is chosen greedily by bits covered, then recursively inside each group up to `depth` levels. Factoring the
//     COMMON part into the gate is always safe; pushing it back into per-register enables later is the easy direction.
//
// A register with a synchronous reset sits behind `common | reset_active`, because the reset wins over the enable and a stopped
// clock would never load it. Negative-edge registers, pipelined registers and registers with a multi-bit enable are left alone.
// The Clock_cell is spelled structurally later (design_prepare's expand_plain_clock_cells), so the mappers see the usual
// `clk & latch(en)` and map it onto a Liberty ICG when the library has one.
//
// Returns no value when the budget refused.
std::optional<Clock_gate_stats> infer_clock_gates(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                  const Clock_gate_options& options, const Admit& admit = {});

// Step 1 alone, for any caller that wants the existing gates tidied (no gate is created).
std::optional<Clock_gate_stats> collapse_clock_gates(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                     const Admit&                                  admit = {});

}  // namespace livehd::clock_gate
