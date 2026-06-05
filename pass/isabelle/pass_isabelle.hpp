//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "hhds/graph.hpp"
#include "pass.hpp"

enum class CertWFMode {
  Skip,
  Eval,
  Sorry,
  Chunked
};

enum class CertWFFallback {
  Fail,
  Sorry,
  Eval
};

class Pass_isabelle : public Pass {
public:
  static void setup();
  static void work(Eprp_var &var);

  explicit Pass_isabelle(const Eprp_var &var);

  // Configuration knobs (parsed from Eprp_var):
  bool        strict;        // strict:true (default) — abort on unsupported ops
  bool        normalize;     // normalize:true (default) — fix pre-export IR width artifacts
  std::string top;           // top module name override (informational)
  CertWFMode cert_wf;        // cert_wf:skip|eval|sorry|chunked (default skip)
  CertWFFallback cert_wf_fallback; // cert_wf_fallback:fail|sorry|eval for unsupported chunk shapes
  size_t      cert_chunk_size; // cert_chunk_size:<n> for certificate chunks (default 25)
  size_t      cert_chunk_limit; // cert_chunk_limit:<n> emits only first n chunks for proof-shape testing
  size_t      max_width;     // hard cap on per-node Bits attribute (default 1024)

private:
  void emit_for_graph(const std::shared_ptr<hhds::Graph>& g) const;
};
