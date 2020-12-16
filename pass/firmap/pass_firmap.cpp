//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_firmap.hpp"
#include "firmap.hpp"

#include "lgraph.hpp"

// Useful for debug
//#define PRESERVE_ATTR_NODE

static Pass_plugin sample("pass_firmap", Pass_firmap::setup);

void Pass_firmap::setup() {
  Eprp_method m1("pass.firmap", "firrtl_op bitwidth inference and lgraph nodes", &Pass_firmap::trans);
  m1.add_label_optional("hier", "hierarchical firrtl map", "true");

  register_pass(m1);
}

Pass_firmap::Pass_firmap(const Eprp_var &var) : Pass("pass.firmap", var) {
  auto hier_txt = var.get("hier");

  if (hier_txt != "false" && hier_txt != "0")
    hier = true;
  else
    hier = false;
}

void Pass_firmap::trans(Eprp_var &var) {
  Pass_firmap p(var);
  Firmap fm;

  std::vector<const LGraph *> lgs;
  for (const auto &lg : var.lgs) {
    fm.do_firbits_analysis(lg);
  }
}

