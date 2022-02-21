//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_bitwidth.hpp"

#include "bitwidth.hpp"
#include "lgraph.hpp"
#include "pass.hpp"

// Useful for debug
//#define PRESERVE_ATTR_NODE

static Pass_plugin sample("pass_bitwidth", Pass_bitwidth::setup);

void Pass_bitwidth::setup() {
  Eprp_method m1("pass.bitwidth", "MIT algorithm for bitwidth optimization", &Pass_bitwidth::trans);

  m1.add_label_optional("max_iterations", "maximum number of iterations to try", "3");
  m1.add_label_optional("hier", "hierarchical bitwidth", "false");

  register_pass(m1);
}

Pass_bitwidth::Pass_bitwidth(const Eprp_var &var) : Pass("pass.bitwidth", var) {
  auto miters   = var.get("max_iterations");
  auto hier_txt = var.get("hier");

  if (hier_txt != "false" && hier_txt != "0")
    hier = true;
  else
    hier = false;

  if (!miters.is_i()) {
    error("pass.bitwidth max_iterations:{} should be bigger than zero and less than 100", miters);
    return;
  }

  max_iterations = miters.to_i();

  if (max_iterations > 100 || max_iterations <= 0) {
    error("pass.bitwidth max_iterations:{} should be bigger than zero and less than 100", max_iterations);
    return;
  }
}

void Pass_bitwidth::trans(Eprp_var &var) {
  Pass_bitwidth p(var);

  Bitwidth bw(p.hier, p.max_iterations);

  std::vector<const Lgraph *> lgs;
  for (const auto &lg : var.lgs) {
    bw.do_trans(lg);
  }
}
