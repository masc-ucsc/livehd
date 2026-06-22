//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
//  pass.lean - emit per-design Lean theory scaffolding for graph-certificate
//  based translation proofs.  This pass intentionally mirrors the public knobs
//  of pass.isabelle so the existing DINO/CVA6 generation scripts can be ported
//  incrementally.

#include "pass_lean.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_set>

#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "perf_tracing.hpp"

static Pass_plugin pass_plugin_lean("pass_lean", Pass_lean::setup);

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

const std::unordered_set<std::string> kLeanReserved = {
    "abbrev", "axiom", "by",       "class", "def",     "deriving", "do",      "else",   "end",
    "example", "false", "for",      "fun",   "if",      "import",   "in",      "inductive",
    "instance", "let",  "match",    "mutual", "namespace", "open",  "opaque",  "partial",
    "private", "protected", "rec", "set_option", "structure", "theorem", "then", "true",
    "universe", "variable", "where", "with",
};

std::string sanitize_lean(std::string_view name) {
  std::string out;
  out.reserve(name.size() + 4);

  for (unsigned char c : name) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (ok) {
      out.push_back(static_cast<char>(c));
    } else {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "_x%02x_", c);
      out += buf;
    }
  }

  if (out.empty()) {
    out = "id";
  }
  if (out[0] >= '0' && out[0] <= '9') {
    out = "id_" + out;
  }
  if (kLeanReserved.count(out) > 0) {
    out = "id_" + out;
  }
  return out;
}

}  // namespace

Pass_lean::Pass_lean(const Eprp_var& var) : Pass("pass.lean", var) {
  auto s = var.get("strict");
  strict = (s == "false") ? false : true;

  auto n = var.get("normalize");
  normalize = (n == "false") ? false : true;

  top = std::string(var.get("top"));
  cert_wf = parse_cert_wf_mode(var.get("cert_wf"));
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

  auto mw = var.get("max_width");
  if (!mw.empty()) {
    try {
      max_width = std::stoul(std::string(mw));
    } catch (...) {
      max_width = 1024;
    }
  } else {
    max_width = 1024;
  }
}

void Pass_lean::setup() {
  Eprp_method m1("pass.lean",
                 "Emit per-design Lean theories for graph-certificate translation proofs.",
                 &Pass_lean::work);
  m1.add_label_optional("path", "Output directory for emitted Lean files.");
  m1.add_label_optional("top", "Top module name override.");
  m1.add_label_optional("strict", "true|false (default true). Abort on unsupported ops.");
  m1.add_label_optional("normalize", "true|false (default true). Normalize pre-export width artifacts.");
  m1.add_label_optional("max_width", "Hard cap on node Bits width (default 1024).");
  m1.add_label_optional("cert_wf", "skip|eval|sorry|chunked (default skip). Certificate well-formedness proof mode.");
  m1.add_label_optional("cert_wf_fallback", "fail|sorry|eval for unsupported cert_wf:chunked chunk shapes (default fail).");
  m1.add_label_optional("cert_chunk_size", "Number of node certificates per chunk for cert_wf:chunked (default 25).");
  m1.add_label_optional("cert_chunk_limit", "Emit only first N certificate chunks for proof-shape testing (default 0 = all).");
  register_pass(m1);
}

void Pass_lean::work(Eprp_var& var) {
  Pass_lean pass(var);
  for (const auto& g : var.graphs) {
    pass.emit_for_graph(g);
  }
}

void Pass_lean::emit_for_graph(const std::shared_ptr<hhds::Graph>& graph) const {
  TRACE_EVENT("pass", "LEAN_emit_for_graph");

  const std::string output_dir = (path == "/INVALID" || path.empty()) ? std::string(".") : path;

  const std::string raw_name = top.empty() ? std::string(graph->get_name()) : top;
  const std::string base_name = sanitize_lean(raw_name);
  const std::string lean_path = output_dir + "/" + base_name + "_Lgraph.lean";

  std::ofstream ofs(lean_path);
  if (!ofs) {
    livehd::diag::warn("pass.lean", "write-failed", "io").msg("could not write {}", lean_path).emit();
    return;
  }

  ofs << "/-\n";
  ofs << "  Generated by LiveHD pass.lean.\n";
  ofs << "  This is the initial Lean artifact shell.  The fast model and full\n";
  ofs << "  graph certificate are emitted by the next pass.lean implementation stage.\n";
  ofs << "-/\n\n";
  ofs << "import LeanSemanticPrimitives\n\n";
  ofs << "namespace " << base_name << "_Lgraph\n\n";
  ofs << "def " << base_name << "_sourceEnv : Nat -> BV := fun _ => mk_bv 0 0\n\n";
  ofs << "def " << base_name << "_graphCert : GraphCert :=\n";
  ofs << "  { topo := [], sources := [], nodes := fun _ => none }\n\n";
  ofs << "theorem " << base_name << "_evalGraph_correct :\n";
  ofs << "    envCorrectOn " << base_name << "_graphCert.topo\n";
  ofs << "      (evalGraph " << base_name << "_graphCert.topo " << base_name << "_graphCert " << base_name << "_sourceEnv)\n";
  ofs << "      (graphDenotation " << base_name << "_graphCert.topo " << base_name << "_graphCert " << base_name << "_sourceEnv) := by\n";
  ofs << "  exact evalGraphCorrectForCert " << base_name << "_graphCert " << base_name << "_sourceEnv\n\n";

  if (cert_wf == LeanCertWFMode::Sorry) {
    ofs << "theorem " << base_name << "_graphCert_wf : graphCertWf " << base_name << "_graphCert := by\n";
    ofs << "  simp [" << base_name << "_graphCert, graphCertWf]\n\n";
  } else {
    ofs << "-- graphCertWf proof emission is pending full graph certificate output.\n";
  }

  ofs << "end " << base_name << "_Lgraph\n";
  ofs.close();

  std::cout << "pass.lean: " << raw_name << " -> " << lean_path << " (certificate shell)\n";
}
