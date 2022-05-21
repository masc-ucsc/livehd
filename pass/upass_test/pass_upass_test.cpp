//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_upass_test.hpp"
#include "upass_runner.hpp"
#include "upass_verifier.hpp"

static Pass_plugin sample("pass_upass_test", Pass_upass_test::setup);

void Pass_upass_test::setup() {
  Eprp_method m1("pass.upass_test", "Micro-pass (uPass) test pass", &Pass_upass_test::work);
  register_pass(m1);
}

Pass_upass_test::Pass_upass_test(const Eprp_var &var) : Pass("pass.upass_test", var) {}

void Pass_upass_test::work(Eprp_var &var) {
  for (const auto &ln : var.lnasts) {
    auto lm = std::make_shared<upass::Lnast_manager>(ln);
    auto runner = uPass_runner(lm, {"verifier"});
    runner.run();
    (void)ln;
  }
}
