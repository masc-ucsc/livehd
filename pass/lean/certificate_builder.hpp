// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
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
  // Which node or memory is being lowered right now, so a dependency refusal
  // can name the REFERENCER and not only the missing operand.
  std::string                        lowering_context;
  uint32_t                           dep(const PinRef& pin, uint32_t width);
  uint32_t                           pin_width(const PinRef& pin) const;
  uint32_t                           emit(Op op, uint32_t width, std::vector<uint32_t> deps);
};
}  // namespace lean_export::detail
