//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_code_gen.hpp"

#include <strings.h>

#include <fstream>
#include <sstream>
#include <string>

#include "file_utils.hpp"
#include "lgedgeiter.hpp"
/* #include "cfg_lnast.hpp" */
#include "lnast_generic_parser.hpp"

// void setup_inou_code_gen() { Inou_code_gen::setup(); }
static Pass_plugin sample("Inou_code_gen", Inou_code_gen::setup);

Inou_code_gen::Inou_code_gen(const Eprp_var &var) : Pass("inou_code_gen", var) { lg = 0; }

void Inou_code_gen::setup() {
  Eprp_method m3("inou.code_gen.verilog", "lnast -> generate verilog", &Inou_code_gen::to_verilog);
  Eprp_method m4("inou.code_gen.prp", "lnast -> generate pyrope", &Inou_code_gen::to_prp);
  Eprp_method m5("inou.code_gen.cpp", "lnast -> generate cpp", &Inou_code_gen::to_cpp);

  register_inou("code_gen", m3);
  register_inou("code_gen", m4);
  register_inou("code_gen", m5);

  m3.add_label_optional("odir", "path to put the verilog[s]", ".");
  m4.add_label_optional("odir", "path to put the pyrope[s]", ".");
  m5.add_label_optional("odir", "path to put the cpp[s}", ".");
}

void Inou_code_gen::to_xxx(Code_gen_type code_gen_type, std::shared_ptr<Lnast> lnast) {
  std::unique_ptr<Code_gen> lnast_to;
  lnast_to = std::make_unique<Code_gen>(code_gen_type, std::move(lnast), path, odir);

  lnast_to->generate();
}

void Inou_code_gen::to_verilog(Eprp_var &var) {
  Inou_code_gen p(var);
  for (const auto &l : var.lnasts) {
    p.to_xxx(Code_gen_type::Type_verilog, l);
  }
}

void Inou_code_gen::to_prp(Eprp_var &var) {
  Inou_code_gen p(var);
  for (const auto &l : var.lnasts) {
    p.to_xxx(Code_gen_type::Type_prp, l);
  }
}

void Inou_code_gen::to_cfg(Eprp_var &var) {
  Inou_code_gen p(var);
  for (const auto &l : var.lnasts) {
    p.to_xxx(Code_gen_type::Type_cfg, l);
  }
}

void Inou_code_gen::to_cpp(Eprp_var &var) {
  Inou_code_gen p(var);
  for (const auto &l : var.lnasts) {
    p.to_xxx(Code_gen_type::Type_cpp, l);
  }
}
