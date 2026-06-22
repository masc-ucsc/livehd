//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_prp_writer.hpp"

#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "lnast_prp_writer.hpp"

static Pass_plugin sample("pass_prp_writer", Pass_prp_writer::setup);

void Pass_prp_writer::setup() {
  Eprp_method m1("pass.prp_writer", "emit LNAST as Pyrope 3.0 source files", &Pass_prp_writer::work);
  m1.add_label_optional("odir", "output directory for .prp files", ".");
  m1.add_label_optional("debug",
                        "emit /* TODO */ for unimplemented constructs instead of failing the compile",
                        "false");
  register_pass(m1);
}

Pass_prp_writer::Pass_prp_writer(const Eprp_var& var) : Pass("pass.prp_writer", var) {}

void Pass_prp_writer::work(Eprp_var& var) {
  Pass_prp_writer p(var);

  if (var.lnasts.empty()) {
    livehd::diag::warn("pass.prp_writer", "no-input", "io").msg("no LNASTs in pipeline — nothing written").emit();
    return;
  }

  // Read odir directly from var (optional labels may not set has_label).
  auto out_dir = std::string(var.get("odir", "."));
  if (out_dir.empty()) {
    out_dir = ".";
  }

  // `prp_writer.debug=true` downgrades an unimplemented construct from a hard
  // error to a `/* TODO */` comment in the output (for inspecting the partial
  // emission).  Default false: any unimplemented construct fails the compile so
  // it cannot silently succeed with a non-reparsable / lossy stub.
  auto debug_opt = std::string(var.get("debug", "false"));
  bool debug_on  = debug_opt == "true" || debug_opt == "1";

  if (!p.setup_directory(out_dir)) {
    livehd::diag::err("pass.prp_writer", "write-failed", "io").msg("could not create output directory: {}", out_dir).fatal();
    return;
  }

  // Cross-unit signature scan: a multi-output `comb` callee is INLINED on
  // re-compile, so `r = C(...)` is the rejected `multi-output-one-var` whole-bind
  // — the writer must destructure it.  Pre-scan every unit so each per-unit
  // writer knows which callees need that treatment (and their output names).
  std::unordered_map<std::string, std::vector<std::string>> multi_out_combs;
  for (const auto& ln : var.lnasts) {
    std::string              nm;
    std::vector<std::string> outs;
    if (Lnast_prp_writer::scan_multi_out_comb(ln, nm, outs)) {
      multi_out_combs.emplace(std::move(nm), std::move(outs));
    }
  }

  for (const auto& ln : var.lnasts) {
    auto module_name = ln->get_top_module_name();
    auto fname       = std::format("{}/{}.prp", out_dir, module_name);

    std::ofstream out(fname);
    if (!out.is_open()) {
      livehd::diag::err("pass.prp_writer", "write-failed", "io").msg("could not open output file: {}", fname).fatal();
      return;
    }

    Lnast_prp_writer writer(out, ln);
    writer.set_debug(debug_on);
    writer.set_multi_out_combs(&multi_out_combs);
    writer.write_all();
    out.close();

    if (!debug_on && writer.has_unimplemented()) {
      std::string feats;
      for (const auto& f : writer.unimplemented()) {
        feats += feats.empty() ? "" : "; ";
        feats += f;
      }
      livehd::diag::err("pass.prp_writer", "unimplemented", "unsupported")
          .msg("cannot emit Pyrope for '{}': unimplemented construct(s): {}", module_name, feats)
          .hint("the .prp was written with /* TODO */ markers; pass --set prp_writer.debug=true to keep the partial "
                "output and let the compile pass")
          .emit();
    }
  }
}
