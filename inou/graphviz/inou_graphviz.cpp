//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_graphviz.hpp"

#include "eprp_utils.hpp"
#include "graphviz.hpp"

static Pass_plugin sample("inou_graphviz", Inou_graphviz::setup);

Inou_graphviz::Inou_graphviz(const Eprp_var &var) : Pass("inou.graphviz", var) {
  if (var.has_label("bits")) {
    auto b = var.get("bits");
    bits   = b != "false" && b != "0";
  } else {
    bits = false;
  }

  auto v  = var.get("verbose");
  verbose = v != "false" && v != "0";
}

void Inou_graphviz::setup() {
  Eprp_method m1("inou.graphviz.from", "export lgraph/lnast to graphviz dot format", &Inou_graphviz::from);

  m1.add_label_optional("bits", "dump bits (true/false)", "false");
  m1.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");
  register_inou("graphviz", m1);

  Eprp_method m2("inou.graphviz.fromlg.hierarchy", "export lgraph hierarchy to graphviz dot format", &Inou_graphviz::hierarchy);
  register_inou("graphviz", m2);
}

void Inou_graphviz::from(Eprp_var &var) {
  Inou_graphviz pp(var);

  Graphviz p(pp.bits, pp.verbose, pp.get_odir(var));

  for (const auto &l : var.lgs) {
    p.do_from_lgraph(l);
  }
  for (const auto &l : var.lnasts) {
    p.do_from_lnast(l);
  }
}

void Inou_graphviz::hierarchy(Eprp_var &var) {
  Inou_graphviz pp(var);
  Graphviz      p(pp.bits, pp.verbose, pp.get_odir(var));

  for (const auto &l : var.lgs) {
    p.do_hierarchy(l);
  }
}
