//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "hhds/graph.hpp"
#include "pass.hpp"

enum class RocqCertWFMode {
  Skip,
  Eval,
  Sorry,
  Chunked
};

enum class RocqCertWFFallback {
  Fail,
  Sorry,
  Eval
};

// Which reduction engine closes a computational certificate goal.  Rocq is the
// only one of the three prover targets where this is a real choice, and the
// three options have genuinely different trusted bases:
//   Vm     -> vm_compute      : kernel + the bytecode VM        (default)
//   Native -> native_compute  : kernel + VM + the OCaml compiler (fastest)
//   Cbv    -> cbv             : kernel only                      (smallest TCB)
// See pass/rocq/LITERATURE_REVIEW.md section 6.
enum class RocqEvalEngine {
  Vm,
  Native,
  Cbv
};

class Pass_rocq : public Pass {
public:
  static void setup();
  static void work(Eprp_var &var);

  explicit Pass_rocq(const Eprp_var &var);

  // Configuration knobs (parsed from Eprp_var):
  bool           strict;            // strict:true (default) — abort on unsupported ops
  bool           normalize;         // normalize:true (default) — fix pre-export IR width artifacts
  bool           emit_cert;         // emit_cert:true (default) — emit graph certificate and cert model
  std::string    top;               // top module name override (informational)
  RocqCertWFMode cert_wf;           // cert_wf:skip|eval|sorry|chunked (default skip)
  RocqCertWFFallback cert_wf_fallback;  // cert_wf_fallback:fail|sorry|eval
  RocqEvalEngine     eval_engine;   // eval_engine:vm|native|cbv (default vm)
  size_t         cert_chunk_size;   // cert_chunk_size:<n> (default 25)
  size_t         cert_chunk_limit;  // cert_chunk_limit:<n> emits only first n chunks (0 = all)
  size_t         max_width;         // hard cap on per-node Bits attribute (default 1024)

private:
  void emit_for_graph(const std::shared_ptr<hhds::Graph>& g) const;
};
