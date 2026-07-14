// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <vector>

#include "hhds/graph.hpp"

// Memory bit-blast for pass.abc `memory=true`. Each Ntype_op::Memory node is
// lowered IN PLACE into native LGraph flops + comb (address decode, write mux,
// read mux, forwarding, optional read-latency register) whose behavior matches
// ware/rtl/cgen_memory_1rd_1wr.v exactly. The result is an ordinary flop+comb
// graph that the normal pass.abc flow then technology-maps (muxes -> cells,
// storage flops -> DFF cells when register=true), so a memory becomes an array
// of DFF cells + mux logic. memory=false leaves the Memory node as a boundary.
namespace livehd::abc {

// Lower every Memory node in every graph. Returns the number lowered. Nodes that
// cannot be lowered (unsupported shape) are left intact and reported via diag.
int lower_memories(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);

}  // namespace livehd::abc
