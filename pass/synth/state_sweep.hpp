// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

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

// For each of `pins` (driver pins of `graph`), which of its declared bits something reads, with the per-bit demand of
// the sweep above computed on this graph alone (its outputs and instance inputs read in full). No value when the
// demand could not be computed.
std::optional<std::vector<std::vector<bool>>> read_bits(hhds::Graph* graph, std::span<const hhds::Pin_class> pins);

// What mask_unread_color_outputs trimmed.
struct Unread_stats {
  uint64_t values = 0;  // crossing values handed to their readers through a mask
  uint64_t bits   = 0;  // the bits those masks clear
};

// Dead code at color boundaries, on one colored graph right before it is cut into regions. A reading region asks only
// for the bits it uses, but the producing region computes every bit of the value it exports. With the same per-bit
// demand as the sweep above (this graph alone, its outputs and instance inputs read in full), an unsigned
// combinational value that a cell of another color reads and whose bits are not all read by something is handed to
// those readers as `value & read`, the And in the producer's color: the unread bits leave the producer as constant 0
// and the mapper drops the logic behind them. Returns no value when the demand could not be computed.
std::optional<Unread_stats> mask_unread_color_outputs(hhds::Graph* graph);

// Before sweep_unobserved_state: the sweep keeps a cell when ANY instance of its definition reads it. A definition
// whose instances read different outputs (a FIFO whose data one user reads and another ignores) gets a content-named
// copy (`<name>__d<hash>`) per distinct read pattern beyond the widest, appended to `definitions`, so each pattern is
// swept on its own -- what flattening gives, without flattening. Repeats (a copy's children split in the next round)
// up to `max_rounds`; at most `max_versions` copies per definition per round. Returns the copies made, or no value
// when the budget refused.
std::optional<int> split_by_demand(std::vector<std::shared_ptr<hhds::Graph>>& definitions, std::string_view top,
                                   Preparation_budget* budget, int max_rounds = 8, int max_versions = 16);

}  // namespace livehd::synth
