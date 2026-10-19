// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "hhds/graph.hpp"
#include "preparation_budget.hpp"

namespace livehd::synth {

struct Register_merge_stats {
  uint64_t registers = 0;  // duplicate registers deleted
  uint64_t bits      = 0;  // their bits
  uint64_t rounds    = 0;  // sweeps over the definitions until nothing merged
};

// Merge registers that are the same machine (synth.merge_registers, off by default): a Flop
// with the same clock, enable, reset, polarities, reset value, width, sign and D driver as
// another in its definition holds the same value in every cycle, so its readers move to the
// survivor and it goes. Registers whose D is another duplicate's Q become identical once that
// one merges, so the sweep repeats to a fixpoint. This is Yosys's `opt_merge` on flops; LiveHD
// kept NVDLA's twin read-data registers of every SRAM wrapper (528 of partition_a's flops).
//
// Runs on the mapper's private copy. A name the source gave the deleted register is not kept
// (the survivor keeps its own). Returns no value when the budget refused.
std::optional<Register_merge_stats> merge_equal_registers(std::span<const std::shared_ptr<hhds::Graph>> definitions,
                                                          Preparation_budget*                           budget);

}  // namespace livehd::synth
