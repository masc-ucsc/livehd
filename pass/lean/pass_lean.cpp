// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_lean.hpp"

#include <iostream>

#include "certificate_ir.hpp"
#include "design_scan.hpp"
#include "diag.hpp"
#include "design_io_meta.hpp"
#include "emit_dcert.hpp"
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
                        "true|false. Reject invalid dependency widths and undersized constants (formal.lean.strict; default true)",
                        "true");
  m1.add_label_optional("emit_cert", "true|false. Emit graph certificate and cert-model definitions.", "true");
  m1.add_label_optional("emit_fast_bridge", "true|false. Emit the fast-view bridge (_comb=_comb_cert, step 5).", "false");
  m1.add_label_optional("mode",
                        "legacy|verified_compiler. verified_compiler emits ONLY <Top>_designCert; the model comes from "
                        "the proved compiler Compiler.compileDesign instead of from this pass.",
                        "legacy");
  m1.add_label_optional("max_width",
                        "Hard cap on node Bits width; 0 or 'unlimited' = no cap (default: no cap).",
                        "unlimited");
  m1.add_label_optional("max_nodes",
                        "Hard cap on the combinational cone's node count; 0 or 'unlimited' = no cap (default 0).",
                        "0");
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
  const auto design      = lean_export::scan_design(*graph, {strict, max_width, max_nodes});
  const auto certificate = lean_export::build_certificate(design, {});
  const auto raw_name    = top.empty() ? design.name : top;
  const auto base_name   = lean_export::sanitize_lean(raw_name);
  const auto output_dir  = (path == "/INVALID" || path.empty()) ? std::string(".") : path;
  const auto lean_path   = output_dir + "/" + base_name + "_Lgraph.lean";
  const auto io_path     = output_dir + "/" + base_name + "_io.json";
  // The sidecar describes the artifact actually emitted, so it is built from the
  // SAME scan and certificate and named from the SAME `base_name` -- including a
  // `--top` override, which renames both halves together.
  //
  // Published as a PAIR: a certificate names its inputs by ordinal only, so a
  // caller needs the sidecar to drive it, and a new certificate beside a stale
  // sidecar is worse than neither. `publish_pair` keeps the previous coherent
  // pair when anything fails; see its contract for what that does and does not
  // guarantee.
  auto io_meta = lean_export::build_io_meta(design, certificate);
  io_meta.top  = base_name;
  const auto dcert_path = output_dir + "/" + base_name + ".dcert";
  if (verified_compiler) {
    // THREE artifacts, one transaction. The DCERT2 file and the Lean literal
    // describe the SAME `CertificateIR`, built once above and walked by both
    // emitters.
    //
    // WHAT THE GROUP BUYS, PRECISELY. When a publish SUCCEEDS, or fails and
    // unwinds, a reader sees three artifacts from one run or the previous
    // three -- never a certificate from this run beside a byte stream from the
    // last. What it does not buy is coherence across a CRASH: `publish_group`
    // is N renames and says so, and a crash between them leaves some targets
    // new and some old. Adding a third artifact widens that window rather than
    // closing it. The sidecar is in the same group for the reason it always
    // was.
    //
    // DCERT2 is emitted in `verified_compiler` mode only: the legacy branch
    // emits a different model whose certificate is not a `DesignCert`, and a
    // `.dcert` beside it would name a design the rest of that artifact does not
    // describe.
    lean_export::publish_group(
        {{lean_path, [&](std::ostream& out) { lean_export::emit_design_cert(base_name, certificate, out); }},
         {dcert_path, [&](std::ostream& out) { lean_export::render_dcert(certificate, out); }},
         {io_path, [&](std::ostream& out) { out << lean_export::io_meta_json(io_meta); }}});
  } else {
    const lean_export::LegacyEmitOptions
        options{top, emit_cert, emit_fast_bridge, cert_wf, cert_wf_fallback, cert_chunk_size, cert_chunk_limit};
    lean_export::publish_pair(
        lean_path, [&](std::ostream& out) { lean_export::emit_legacy_model(design, certificate, options, out); },
        io_path, [&](std::ostream& out) { out << lean_export::io_meta_json(io_meta); });
  }
  std::cout << "pass.lean: " << raw_name << " -> " << lean_path << (verified_compiler ? " + " + dcert_path : "")
            << " + " << io_path << (verified_compiler ? " (verified_compiler: " : " (legacy: ")
            << certificate.sources.size() << " sources, " << certificate.nodes.size() << " nodes, " << certificate.flops.size()
            << " flops, " << certificate.memories.size() << " memories)\n";
}
