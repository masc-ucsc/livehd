// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "lean_options.hpp"

// The knobs shared by every legacy emitter stage.
//
// This lives apart from legacy_model.hpp on purpose.  emit_legacy_cert_wf.hpp
// needs only `LegacyEmitOptions`, but including legacy_model.hpp to get it also
// drags in `emit_legacy_model` and `DesignScan` -- a dependency the cert_wf stage
// does not have.  The BUILD target papered over that by listing legacy_model.hpp
// among its own `hdrs`, declaring ownership of a header belonging to another
// target.  Splitting the struct out removes the reason for both.
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
}  // namespace lean_export
