// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <set>

#include "certificate_ir.hpp"

namespace lean_export::detail {
// What phase 1 of a memory's lowering leaves for phase 2: the image source the
// write chain extends, the chains already emitted (shared so a read port whose
// forwarded set equals the whole write set reuses them), and the flop source
// standing in for each registered read port, keyed by read-port ordinal.
struct MemoryLowering {
  uint32_t                                image = 0;
  std::map<std::vector<size_t>, uint32_t> chains;
  std::map<size_t, uint32_t>              read_registers;
};
struct CertificateBuilder {
  explicit CertificateBuilder(const DesignScan& scan);
  const DesignScan&                  design;
  std::map<uint32_t, uint32_t>       input_ordinals, flop_ordinals;
  std::map<uint32_t, const Flop*>    flops;
  CertificateIR                      result;
  std::map<uint32_t, Source>         sources;
  std::map<uint64_t, uint32_t>       memory_reads;
  std::map<uint32_t, FlopDriver>     read_registers;
  std::map<uint32_t, MemoryDriver>   memory_drivers;
  std::map<uint32_t, MemoryLowering> memory_state;
  uint32_t                           next_id = 1000000000;
  // ASYNCHRONOUS flops whose reset is a combinational function of PRIMARY
  // INPUTS only (decision 2B). `SourceDesc.flopQAsync` reads its reset from one
  // input ordinal, so such a flop is read through an explicit override node,
  // `reset asserted ? resetValue : Q`, emitted once on first reference; its
  // commit side keeps `FlopDesc.resetPin`/`asyncReset`. Input-only is what makes
  // the override exact: the reset is fixed within a step, as an input is.
  std::set<uint32_t>                 input_reset_flops;
  std::map<uint32_t, uint32_t>       async_override;  // flop id -> override node id
  uint32_t                           async_read(const Flop& f);
  // Which node or memory is being lowered right now, so a dependency refusal
  // can name the REFERENCER and not only the missing operand.
  std::string                        lowering_context;
  uint32_t                           dep(const PinRef& pin, uint32_t width);
  uint32_t                           pin_width(const PinRef& pin) const;
  uint32_t                           emit(Op op, uint32_t width, std::vector<uint32_t> deps);
  // OPERAND SIGNEDNESS. LiveHD reads a pin as an INTEGER: two's complement at
  // its width when the pin is signed, unsigned otherwise; an operator works on
  // those integers. The certificate's operators read their operands UNSIGNED and
  // widen them by zero extension (LGraphModel.denote_op: Sum, Mult, EQ, ULT,
  // SHL, And/Or/Xor/Not, MuxBool/MuxN, flop and memory commits), except
  // SLT/SGT/SRA/SDiv, which read two's complement at the operand's own width.
  // `value_dep` presents an operand to a zero-extending read at `width`: a
  // signed operand narrower than that is sign-extended first (Op_Sext), so the
  // bits read are its value mod 2^width. `signed_dep` presents an operand to a
  // two's-complement read: an unsigned operand gains a zero bit (an Or resize)
  // and a non-negative constant a sign bit, so the value read is unchanged.
  uint32_t                           value_dep(const PinRef& pin, uint32_t width);
  uint32_t                           signed_dep(const PinRef& pin);
  uint32_t                           const_dep(uint32_t value);
};
}  // namespace lean_export::detail
