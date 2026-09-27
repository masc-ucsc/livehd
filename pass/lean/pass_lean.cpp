// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_lean.hpp"

#include <iostream>

#include "certificate_ir.hpp"
#include "design_scan.hpp"
#include "diag.hpp"
#include "emit_design_cert.hpp"
#include "lean_format.hpp"
#include "legacy_model.hpp"

static Pass_plugin pass_plugin_lean("pass_lean", Pass_lean::setup);
Pass_lean::Pass_lean(const Eprp_var& var) : Pass("pass.lean", var), LeanOptions(var) {}

void Pass_lean::setup() {
  Eprp_method m1("pass.lean", "Emit per-design Lean theories for graph-certificate translation proofs.", &Pass_lean::work);
  m1.add_label_optional("path", "Output directory for emitted Lean files.");
  m1.add_label_optional("top", "Top module name override.");
  m1.add_label_optional("strict",
                        "true|false. Abort on unsupported ops (formal.strict applies too; formal.lean.strict wins)",
                        "true");
  m1.add_label_optional("emit_cert", "true|false. Emit graph certificate and cert-model definitions.", "true");
  m1.add_label_optional("emit_fast_bridge", "true|false. Emit the fast-view bridge (_comb=_comb_cert, step 5).", "false");
  m1.add_label_optional("mode",
                        "legacy|verified_compiler. verified_compiler emits ONLY <Top>_designCert; the model comes from "
                        "the proved compiler Compiler.compileDesign instead of from this pass.",
                        "legacy");
  m1.add_label_optional("max_width", "Hard cap on node Bits width; 0 or 'unlimited' = no cap (default 1024).", "1024");
  m1.add_label_optional("cert_wf", "skip|eval|sorry|chunked. Certificate well-formedness proof mode.", "skip");
  m1.add_label_optional("cert_wf_fallback", "fail|sorry|eval for unsupported cert_wf:chunked chunk shapes.", "fail");
  m1.add_label_optional("cert_chunk_size", "Number of node certificates per chunk for cert_wf:chunked.", "25");
  m1.add_label_optional("cert_chunk_limit", "Emit only first N certificate chunks for proof-shape testing (0 = all).", "0");
  register_pass(m1);
}

void Pass_lean::work(Eprp_var& var) {
  Pass_lean pass(var);
  for (const auto& g : var.graphs) {
    pass.emit_for_graph(g);
  }
}

void Pass_lean::emit_for_graph(const std::shared_ptr<hhds::Graph>& graph) const {
  if (!graph) {
    livehd::diag::warn("pass.lean", "no-input", "io").msg("received a null Graph instance").emit();
    return;
  }
  const auto design      = lean_export::scan_design(*graph, {strict, max_width});
  const auto certificate = lean_export::build_certificate(design, {});
  const auto raw_name    = top.empty() ? design.name : top;
  const auto base_name   = lean_export::sanitize_lean(raw_name);
  const auto output_dir  = (path == "/INVALID" || path.empty()) ? std::string(".") : path;
  const auto lean_path   = output_dir + "/" + base_name + "_Lgraph.lean";
  if (verified_compiler) {
    lean_export::write_design_cert(base_name, certificate, lean_path);
  } else {
    const lean_export::LegacyEmitOptions
        options{top, emit_cert, emit_fast_bridge, cert_wf, cert_wf_fallback, cert_chunk_size, cert_chunk_limit};
    lean_export::write_atomic(lean_path,
                              [&](std::ostream& out) { lean_export::emit_legacy_model(design, certificate, options, out); });
  }
  std::cout << "pass.lean: " << raw_name << " -> " << lean_path << (verified_compiler ? " (verified_compiler: " : " (legacy: ")
            << certificate.sources.size() << " sources, " << certificate.nodes.size() << " nodes, " << certificate.flops.size()
            << " flops, " << certificate.memories.size() << " memories)\n";
}
