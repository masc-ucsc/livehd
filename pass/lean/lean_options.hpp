// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstddef>
#include <string>
class Eprp_var;
enum class LeanCertWFMode { Skip, Eval, Sorry, Chunked };

enum class LeanCertWFFallback { Fail, Sorry, Eval };

struct LeanOptions {
  // Configuration knobs (parsed from Eprp_var):
  bool               strict;             // strict:true (default) — abort on unsupported ops
  bool               emit_cert;          // emit_cert:true (default) — emit graph certificates and cert model
  bool               emit_fast_bridge;   // emit_fast_bridge:false (default) — emit fast-view bridge (step 5)
  bool               verified_compiler;  // mode:verified_compiler — emit ONLY a DesignCert (B1+B2 branch)
  std::string        top;                // top module name override (informational)
  LeanCertWFMode     cert_wf;            // cert_wf:skip|eval|sorry|chunked (default skip)
  LeanCertWFFallback cert_wf_fallback;
  size_t             cert_chunk_size;   // cert_chunk_size:<n> (default 25)
  size_t             cert_chunk_limit;  // cert_chunk_limit:<n> emits only first n chunks
  size_t             max_width;         // hard cap on per-node Bits attribute (default 1024)

  explicit LeanOptions(const Eprp_var& var);
};
