//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// clang-format off

#include "slang_tree.hpp"
#include "inou_slang.hpp"

#include "lbench.hpp"
#include "lgraph.hpp"

// clang-format on

extern int slang_main(int argc, char **argv);  // in slang_driver.cpp

static Pass_plugin sample("inou.verilog", Inou_slang::setup);

void Inou_slang::setup() {
  Eprp_method m1("inou.verilog", "System verilog to LNAST using slang", &Inou_slang::work);

  m1.add_label_optional("files", "input verilog file paths)");
  m1.add_label_optional("includes", "comma separated include paths (otherwise, verilog paths)");
  m1.add_label_optional("defines", "comma separated defines. E.g: defines:foo=1,XXX,LALA=1");
  m1.add_label_optional("undefines", "comma separated undefines");

  register_pass(m1);
}

Inou_slang::Inou_slang(const Eprp_var &var) : Pass("pass.lec", var) {}

void Inou_slang::work(Eprp_var &var) {
  Lbench     b("inou.SLANG_verilog");
  Inou_slang p(var);

  std::vector<char *>      argv;

  argv.push_back(strdup("lgshell"));

  argv.push_back(strdup("--ignore-unknown-modules"));
  argv.push_back(strdup("--single-unit"));

  if (var.has_label("includes")) {
    auto txt = var.get("includes");
    for (const auto f : txt.split(',')) {
      argv.push_back(strdup("-I"));
      argv.push_back(strdup(f.to_s().c_str()));
    }
  }

  if (var.has_label("defines")) {
    auto txt = var.get("defines");
    for (const auto f : txt.split(',')) {
      argv.push_back(strdup("-D"));
      argv.push_back(strdup(f.to_s().c_str()));
    }
  }

  if (var.has_label("undefines")) {
    auto txt = var.get("undefines");
    for (const auto f : txt.split(',')) {
      argv.push_back(strdup("-U"));
      argv.push_back(strdup(f.to_s().c_str()));
    }
  }

  for (const auto f : p.files.split(',')) {
    argv.push_back(strdup(f.to_s().c_str()));
  }

  // --top if top: provided
  // add includes

  argv.push_back(nullptr);

  Slang_tree::setup();  // setup

  slang_main(argv.size() - 1, argv.data());  // compile to lnasts

  for (auto &ln : Slang_tree::pick_lnast()) {
    var.add(ln);
  }

  for (char *ptr : argv) {
    if (ptr)
      free(ptr);
  }
}
