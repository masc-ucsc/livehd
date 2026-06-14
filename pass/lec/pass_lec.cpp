// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lec.hpp"

#include <print>
#include <string>
#include <string_view>

#include "diag.hpp"
#include "query.hpp"
#include "str_tools.hpp"

using namespace livehd;

static Pass_plugin plugin("pass_lec", Pass_lec::setup);

Pass_lec::Pass_lec(const Eprp_var& var) : Pass("pass.lec", var) {}

namespace {
bool parse_bool(std::string_view v) { return v != "false" && v != "0" && !v.empty(); }
}  // namespace

void Pass_lec::setup() {
  Eprp_method m("pass.lec",
                "Relational equivalence: prove_equal(ref=graph0, impl=graph1) under assume_equal(inputs)",
                &Pass_lec::lec);
  m.add_label_optional("engine", "discharge engine: bmc | ind (k-induction) | ic3", "ind");
  m.add_label_optional("solver", "smt-switch backend: cvc5 | bitwuzla", "cvc5");
  m.add_label_optional("bound", "BMC / induction depth bound k", "20");
  m.add_label_optional("timeout", "per-query wall-clock seconds (0 = none)", "0");
  m.add_label_optional("witness", "print the counterexample/witness on Refuted", "true");
  m.add_label_optional("cross", "also run lgcheck and assert agreement (bring-up only)", "false");
  register_pass(m);
}

void Pass_lec::lec(Eprp_var& var) {
  if (var.graphs.size() < 2) {
    livehd::diag::err("pass.lec", "lec-needs-pair", "internal")
        .msg("pass.lec needs two designs (ref, impl); got {} graph(s)", var.graphs.size())
        .fatal();
    return;
  }

  lec::Lec_options o;
  o.engine  = std::string{var.get("engine", "ind")};
  o.solver  = std::string{var.get("solver", "cvc5")};
  o.bound   = str_tools::to_i(var.get("bound", "20"));
  o.timeout = str_tools::to_i(var.get("timeout", "0"));
  o.witness = parse_bool(var.get("witness", "true"));

  auto ref  = var.graphs[0];
  auto impl = var.graphs[1];
  auto mod  = std::string{impl->get_name()};

  auto r = lec::prove_equal(ref.get(), impl.get(), o);

  switch (r.verdict) {
    case lec::Verdict::Proven : std::print("lec: '{}' PROVEN equivalent ({})\n", mod, r.detail); break;
    case lec::Verdict::Refuted: {
      std::string w = r.witness.empty() ? std::string{"(no witness)"} : r.witness;
      livehd::diag::err("pass.lec", "not-equivalent", "internal").msg("'{}' is NOT equivalent; counterexample: {}", mod, w).fatal();
      break;
    }
    case lec::Verdict::Unknown:
      livehd::diag::err("pass.lec", "lec-unknown", "unsupported").msg("'{}' equivalence is UNKNOWN ({})", mod, r.detail).fatal();
      break;
  }
}
