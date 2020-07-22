//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_code_gen.hpp"

#include <strings.h>

#include <fstream>
#include <sstream>
#include <string>

#include "eprp_utils.hpp"
#include "lgedgeiter.hpp"
#include "cfg_lnast.hpp"
#include "lnast_generic_parser.hpp"

void setup_inou_code_gen() { Inou_code_gen::setup(); }

Inou_code_gen::Inou_code_gen(const Eprp_var &var) : Pass("inou_code_gen", var) { lg = 0; }

void Inou_code_gen::setup() {
  Eprp_method m2("inou.code_gen.cfg", "parse cfg_test -> build lnast -> generate cfg_text", &Inou_code_gen::to_cfg);
  m2.add_label_optional("odir", "path to put the cfg[s]", ".");

  register_inou("code_gen", m2);

  Eprp_method m3("inou.code_gen.verilog", "parse cfg_test -> build lnast -> generate verilog", &Inou_code_gen::to_verilog);
  m3.add_label_optional("odir", "path to put the verilog[s]", ".");

  register_inou("code_gen", m3);

  Eprp_method m4("inou.code_gen.prp", "parse cfg_test -> build lnast -> generate pyrope", &Inou_code_gen::to_prp);
  m4.add_label_optional("odir", "path to put the pyrope[s]", ".");

  register_inou("code_gen", m4);

  Eprp_method m5("inou.code_gen.cpp", "parse cfg_text -> build lnast -> generate cpp", &Inou_code_gen::to_cpp);
  m5.add_label_optional("odir", "path to put the cpp[s}", ".");

  register_inou("code_gen", m5);
}

void Inou_code_gen::to_xxx(Code_gen_type code_gen_type, std::shared_ptr<Lnast> lnast) {
  std::unique_ptr<Code_gen> lnast_to;
  lnast_to = std::make_unique<Code_gen>(code_gen_type, std::move(lnast), path);

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

