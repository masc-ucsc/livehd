//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_pyrope.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "prp_lnast.hpp"

void setup_inou_pyrope() { Inou_pyrope::setup(); }

void Inou_pyrope::setup() {
  Eprp_method m1("inou.pyrope", "Parse the input file and convert to an LNAST", &Inou_pyrope::parse_to_lnast);
  m1.add_label_required("files", "pyrope files to process (comma separated)");

  register_pass(m1);
}

Inou_pyrope::Inou_pyrope(const Eprp_var &var) : Pass("inou.pyrope", var) {}

void Inou_pyrope::parse_to_lnast(Eprp_var &var) {
  Lbench      b("inou.PYROPE_parse_to_lnast");
  Inou_pyrope p(var);

  for (auto f : p.files.split(',')) {
    Prp_lnast converter;
    converter.parse_file(f.to_s());

    auto basename       = f.get_str_after_last_if_exists('/');
    auto basename_noext = basename.get_str_before_first('.');

    auto lnast = converter.prp_ast_to_lnast(basename_noext);
    var.add(std::move(lnast));
  }
}
