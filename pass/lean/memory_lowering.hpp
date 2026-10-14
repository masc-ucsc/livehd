// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
namespace lean_export {
struct Memory;
namespace detail {
struct CertificateBuilder;
// A memory is lowered in TWO phases, for the same reason a flop is: its READ
// side is a value other nodes consume, while its WRITE side is a NEXT-STATE
// function that may read anything in the design -- including another memory's
// output. Doing both at the Memory node's own place in the topological order
// made `mem_b.din = mem_a.dout` unschedulable, because `mem_b` would then have
// to be lowered both before and after `mem_a`.
//
// Phase 1, at the Memory node's place in the order: the image source, and for
// each read port the value the port presents this cycle (a `MemRead` for an
// asynchronous port, a flop SOURCE for a registered one). Only the read ports'
// own address/enable cones -- and, under forwarding, the write cones that feed
// the read -- have to be emitted before this.
//
// Phase 2, after every node: the registered reads' next-state cones and the
// all-writes chain that becomes the next image. Like a flop's `din`, these may
// name any node in the design.
void lower_memory_reads(CertificateBuilder& builder, const Memory& memory);
void lower_memory_writes(CertificateBuilder& builder, const Memory& memory);
}  // namespace detail
}  // namespace lean_export
