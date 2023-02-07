//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_upass.hpp"

#include "upass_constprop.hpp"
#include "upass_runner.hpp"
#include "upass_verifier.hpp"

static Pass_plugin sample("pass.upass", Pass_upass::setup);

void Pass_upass::setup() {
  Eprp_method m1("pass.upass", "lnast micropass (upass) controller", &Pass_upass::work);
  m1.add_label_optional("verifier", "enable lnast verifier upass", "true");
  m1.add_label_optional("constprop", "enable constant propagation upass", "true");
  m1.add_label_optional("assert", "enable assert test", "true");
  register_pass(m1);
}

Pass_upass::Pass_upass(const Eprp_var &var) : Pass("pass.upass", var) {

  auto verifier_txt = var.get("verifier");
  bool do_verifier = verifier_txt != "false" && verifier_txt != "0";

  auto assert_txt = var.get("assert");
  bool do_assert = assert_txt != "false" && assert_txt != "0";

  auto constp_txt = var.get("constprop");
  bool do_constprop = constp_txt != "false" && constp_txt != "0";

  if (do_verifier) { // 1st and last pass
    upass_order.emplace_back("verifier");
  }

  if (do_constprop) {
    upass_order.emplace_back("constprop");
  }
  if (do_assert) { // last before codegen
    upass_order.emplace_back("assert");
  }

  if (do_verifier) { // 1st and last pass
    upass_order.emplace_back("verifier");
  }

  if (upass_order.empty()) {
    Pass::error("pass.upass has all the passed disabled??");
  }
}

void Pass_upass::work(Eprp_var &var) {

  Pass_upass up(var);

  for (const auto &ln : var.lnasts) {
    auto lm     = std::make_shared<upass::Lnast_manager>(ln);
    auto runner = uPass_runner(lm, up.upass_order);
    runner.run();
  }
}
