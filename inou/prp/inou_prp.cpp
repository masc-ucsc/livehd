//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_prp.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "prp2lnast.hpp"

static Pass_plugin sample("inou_prp", Inou_prp::setup);

void Inou_prp::setup() {
  Eprp_method m1("inou.prp", mmap_lib::str("Parse the input file and convert to an LNAST"), &Inou_prp::parse_to_lnast);
  m1.add_label_required("files", mmap_lib::str("prp files to process (comma separated)"));

  register_pass(m1);
}

Inou_prp::Inou_prp(const Eprp_var &var) : Pass("inou.prp", var) {}

void Inou_prp::parse_to_lnast(Eprp_var &var) {
  Lbench      b("inou.PRP_parse_to_lnast");
  Inou_prp p(var);

  for (auto f : p.files.split(',')) {
    auto basename       = f.get_str_after_last_if_exists('/');
    auto basename_noext = basename.get_str_before_first('.');

    Prp2lnast converter(f, basename_noext);

    auto lnast = converter.get_lnast();

    var.add(std::move(lnast));
  }
}
