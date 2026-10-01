// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lean_options.hpp"

#include <cctype>
#include <limits>
#include <string_view>

#include "eprp_var.hpp"
namespace {
LeanCertWFMode parse_cert_wf_mode(std::string_view mode) {
  if (mode == "eval") {
    return LeanCertWFMode::Eval;
  }
  if (mode == "sorry") {
    return LeanCertWFMode::Sorry;
  }
  if (mode == "chunked") {
    return LeanCertWFMode::Chunked;
  }
  return LeanCertWFMode::Skip;
}
LeanCertWFFallback parse_cert_wf_fallback(std::string_view mode) {
  if (mode == "sorry") {
    return LeanCertWFFallback::Sorry;
  }
  if (mode == "eval") {
    return LeanCertWFFallback::Eval;
  }
  return LeanCertWFFallback::Fail;
}
size_t parse_max_width(std::string_view s, size_t dflt = 1024) {
  if (s.empty()) {
    return dflt;
  }
  std::string l(s);
  for (auto& c : l) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (l == "unlimited" || l == "inf" || l == "none" || l == "0") {
    return std::numeric_limits<size_t>::max();
  }
  try {
    size_t v = std::stoul(l);
    return v == 0 ? std::numeric_limits<size_t>::max() : v;
  } catch (...) {
    return dflt;
  }
}
}  // namespace
LeanOptions::LeanOptions(const Eprp_var& var) {
  auto s = var.get("strict");
  strict = (s == "false") ? false : true;

  auto ec   = var.get("emit_cert");
  emit_cert = (ec == "false") ? false : true;

  auto efb         = var.get("emit_fast_bridge");
  emit_fast_bridge = (efb == "true") ? true : false;

  // `mode=verified_compiler` (B1+B2 branch): emit ONLY `<Top>_designCert`, and
  // let the once-and-for-all-proved `Compiler.compileDesign` produce the model.
  // No `<Top>_comb`/`_next`/`_step`, no per-node proof scripts.
  verified_compiler = (var.get("mode") == "verified_compiler");
  if (verified_compiler) {
    emit_cert        = true;   // the DesignCert IS the certificate
    emit_fast_bridge = false;  // there is no fast model to bridge to
  }

  top              = std::string(var.get("top"));
  cert_wf          = parse_cert_wf_mode(var.get("cert_wf"));
  cert_wf_fallback = parse_cert_wf_fallback(var.get("cert_wf_fallback"));

  auto ccs = var.get("cert_chunk_size");
  if (!ccs.empty()) {
    try {
      cert_chunk_size = std::stoul(std::string(ccs));
    } catch (...) {
      cert_chunk_size = 25;
    }
  } else {
    cert_chunk_size = 25;
  }
  if (cert_chunk_size == 0) {
    cert_chunk_size = 25;
  }

  auto ccl = var.get("cert_chunk_limit");
  if (!ccl.empty()) {
    try {
      cert_chunk_limit = std::stoul(std::string(ccl));
    } catch (...) {
      cert_chunk_limit = 0;
    }
  } else {
    cert_chunk_limit = 0;
  }

  max_width = parse_max_width(var.get("max_width"));
}
