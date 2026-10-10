// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "certificate_ir.hpp"
#include "design_scan.hpp"

namespace lean_export {

// The IO/clock SIDECAR: what a runtime needs to drive a certificate that the
// certificate itself does not say.
//
// A `DesignCert` names its inputs by ORDINAL and nothing else. A caller holding
// an RTL port list has no way to build the input array from it -- which port is
// ordinal 3, how wide it is, whether the model reads it at all, and how long the
// array must be. That mapping is this file.
//
// It is derived from the SAME owned scan and certificate IR the emitter formats,
// never from the graph: a sidecar reconstructed from HHDS could disagree with
// the certificate it is supposed to describe, and the disagreement would only
// show at runtime.
struct IoPortMeta {
  std::string name;
  uint32_t    ordinal = 0;   // index into RuntimeInput / the output array
  uint32_t    width   = 0;
  // Does the emitted model actually READ this port? An input the design ignores
  // still occupies its ordinal -- dropping it would renumber every later port.
  bool        used_by_model = false;
};

struct ClockMeta {
  std::string name;
  uint32_t    ordinal  = 0;  // index into ClockEdges
  bool        implicit = false;
};

struct DesignIoMeta {
  std::string             top;
  // `Direct.inputArity`: one past the highest ordinal any source references,
  // INCLUDING a reset input reached only through `flopQAsync`. A reset-only port
  // is never an `.input` source, so an arity computed from inputs alone is too
  // small and `checkRuntime` then rejects a correctly built array.
  uint32_t                runtime_input_arity = 0;
  std::vector<IoPortMeta> inputs;   // declaration order, ordinals and GAPS kept
  std::vector<IoPortMeta> outputs;  // emission order
  std::vector<ClockMeta>  clocks;
};

DesignIoMeta build_io_meta(const DesignScan& design, const CertificateIR& cert);
std::string  io_meta_json(const DesignIoMeta& meta);

}  // namespace lean_export
