//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_bitwidth.hpp"

#include "bitwidth.hpp"
#include "pass.hpp"

static Pass_plugin sample("pass_bitwidth", Pass_bitwidth::setup);

void Pass_bitwidth::setup() {
  Eprp_method m1("pass.bitwidth", "MIT algorithm for bitwidth optimization", &Pass_bitwidth::trans);

  m1.add_label_optional("max_iterations", "maximum number of iterations to try", "3");

  register_pass(m1);
}

Pass_bitwidth::Pass_bitwidth(const Eprp_var& var) : Pass("pass.bitwidth", var) {
  auto miters = var.get("max_iterations");

  if (!str_tools::is_i(miters)) {
    livehd::diag::err("pass.bitwidth", "bad-option", "io")
        .msg("max_iterations:{} should be bigger than zero and less than 100", miters)
        .fatal();
    return;
  }

  max_iterations = str_tools::to_i(miters);

  if (max_iterations > 100 || max_iterations <= 0) {
    livehd::diag::err("pass.bitwidth", "bad-option", "io")
        .msg("max_iterations:{} should be bigger than zero and less than 100", max_iterations)
        .fatal();
    return;
  }
}

void Pass_bitwidth::trans(Eprp_var& var) {
  Pass_bitwidth p(var);

  Bitwidth bw(p.max_iterations);

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    bw.do_trans(g);
  }
}
