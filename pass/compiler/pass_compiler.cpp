// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_compiler.hpp"

void setup_pass_compiler() { Pass_compiler::setup(); }


void Pass_compiler::setup() {
  Eprp_method m1("pass.compiler", "lnast to lgraph compilation", &Pass_compiler::compile);
  m1.add_label_optional("path", "lgraph path", "lgdb");
  m1.add_label_optional("files", "files to process (comma separated)");

  register_pass(m1);
}

Pass_compiler::Pass_compiler(const Eprp_var &var) : Pass("pass.compiler", var) {}

void Pass_compiler::compile(Eprp_var &var) {
  Pass_compiler p(var);
  auto path = p.get_path(var);
  Lcompiler comp(path);

  if (var.lnasts.empty()) {
    auto files = p.get_files(var);
    if (files.empty()) {
      Pass::warn("nothing to compile. no files or lnast");
      return;
    }

    for (auto f : absl::StrSplit(files, ',')) {
      Pass::warn("todo: start from prp parser");
      /* comp.add(f); */
    }
  } else {
    for (const auto &lnast : var.lnasts) {
      comp.add(lnast);
    }
  }

  auto lgs = comp.wait_all();
  var.add(lgs);
}


