// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "hhds/graph.hpp"

// Standalone graph-level memory bit-blast helper. The pass.abc synthesis path
// uses memory_module.hpp to lower generated RTL within a module instead.
// Each Ntype_op::Memory
// node is lowered IN PLACE into native LGraph flops + comb (address decode, one
// masksize-wide write mux per lane, read mux, forwarding, optional read-latency
// register, whole-array read_all) whose behavior matches
// ware/rtl/cgen_memory_1rd_1wr.v exactly; constant-address ports are resolved
// at build time (their entry only, no decode). The result is an ordinary flop+comb
// graph that the normal pass.abc flow then technology-maps (muxes -> cells,
// storage flops -> DFF cells when register=true), so a memory becomes an array
// of DFF cells + mux logic. memory=false leaves the Memory node as a boundary.
namespace livehd::abc {
struct Memory_satopt;

// Lower every Memory node in every graph. Returns the number lowered. Nodes that
// cannot be lowered (unsupported shape) are left intact and reported via diag,
// as is a memory whose storage (bits*size) exceeds `max_bits` (0 = no limit;
// pass.abc.memory_max_bits): one DFF cell per bit is the wrong realization for
// an SRAM-class array, and a native instance is the boundary memory=false uses.
int lower_memories(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, uint64_t max_bits,
                   const Memory_satopt* facts = nullptr);

}  // namespace livehd::abc
