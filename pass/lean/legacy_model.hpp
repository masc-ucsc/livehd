// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "certificate_ir.hpp"
#include "lean_options.hpp"
namespace lean_export {
struct LegacyEmitOptions {
  std::string        top;
  bool               emit_cert        = true;
  bool               emit_fast_bridge = false;
  LeanCertWFMode     cert_wf          = LeanCertWFMode::Skip;
  LeanCertWFFallback cert_wf_fallback = LeanCertWFFallback::Fail;
  size_t             cert_chunk_size  = 25;
  size_t             cert_chunk_limit = 0;
};
void emit_legacy_model(const DesignScan&, const CertificateIR&, const LegacyEmitOptions&, std::ostream&);
}  // namespace lean_export
