//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_prp_writer.hpp"

#include <format>
#include <fstream>
#include <string>
#include <string_view>

#include "lnast_prp_writer.hpp"

static Pass_plugin sample("pass_prp_writer", Pass_prp_writer::setup);

void Pass_prp_writer::setup() {
  Eprp_method m1("pass.prp_writer", "emit LNAST as Pyrope 3.0 source files", &Pass_prp_writer::work);
  m1.add_label_optional("odir", "output directory for .prp files", ".");
  register_pass(m1);
}

Pass_prp_writer::Pass_prp_writer(const Eprp_var& var) : Pass("pass.prp_writer", var) {}

void Pass_prp_writer::work(Eprp_var& var) {
  Pass_prp_writer p(var);

  if (var.lnasts.empty()) {
    Pass::warn("pass.prp_writer: no LNASTs in pipeline — nothing written");
    return;
  }

  // Read odir directly from var (optional labels may not set has_label).
  auto out_dir = std::string(var.get("odir", "."));
  if (out_dir.empty()) {
    out_dir = ".";
  }

  p.setup_directory(out_dir);

  for (const auto& ln : var.lnasts) {
    auto module_name = ln->get_top_module_name();
    auto fname       = std::format("{}/{}.prp", out_dir, module_name);

    std::ofstream out(fname);
    if (!out.is_open()) {
      Pass::error("pass.prp_writer: could not open output file: {}", fname);
      return;
    }

    Lnast_prp_writer writer(out, ln);
    writer.write_all();
  }
}
