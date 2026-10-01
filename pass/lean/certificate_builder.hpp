// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "certificate_ir.hpp"

namespace lean_export::detail {
struct CertificateBuilder {
  explicit CertificateBuilder(const DesignScan& scan);
  const DesignScan&                design;
  std::map<uint32_t, uint32_t>     input_ordinals, flop_ordinals;
  std::map<uint32_t, const Flop*>  flops;
  CertificateIR                    result;
  std::map<uint32_t, Source>       sources;
  std::map<uint64_t, uint32_t>     memory_reads;
  std::map<uint32_t, FlopDriver>   read_registers;
  std::map<uint32_t, MemoryDriver> memory_drivers;
  uint32_t                         next_id = 1000000000;
  uint32_t                         dep(const PinRef& pin, uint32_t width);
  uint32_t                         pin_width(const PinRef& pin) const;
  uint32_t                         emit(Op op, uint32_t width, std::vector<uint32_t> deps);
};
}  // namespace lean_export::detail
