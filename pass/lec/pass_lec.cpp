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
  m.add_label_optional("engine",
                       "discharge engine: bmc (default) | ind (inductive flop-cut miter) | ic3 | "
                       "auto (parallel portfolio: race ind+bmc as forked workers, take the first "
                       "trustworthy verdict — ind-Proven=PASS, bmc-Refuted=FAIL)",
                       "bmc");
  m.add_label_optional("solver",
                       "equivalence backend: cvc5 (default, in-process SMT) | bitwuzla (in-process SMT) | "
                       "lgyosys (kernel-routed yosys/lgcheck; reads Verilog directly)",
                       "cvc5");
  m.add_label_optional("bound", "BMC / induction depth bound k", "6");
  m.add_label_optional("timeout", "per-query cvc5 wall-clock seconds (0 = unbounded; default bounds the CLI so a hard miter degrades to UNKNOWN instead of freezing)", "120");
  m.add_label_optional("witness", "print the counterexample/witness on Refuted", "true");
  m.add_label_optional("phase",
                       "bmc reset phase: after_reset (default; hold reset N cycles, deassert, check "
                       "free-running) | just_reset (hold reset asserted, check during reset) | "
                       "free_toreset (reset input unconstrained) | full (just_reset AND after_reset)",
                       "after_reset");
  m.add_label_optional("reset_cycles", "after_reset phase: reset-hold prologue length before checking", "2");
  m.add_label_optional("reset", "explicit reset inputs: name[:lo|:hi], comma-separated (else auto-detect)", "");
  m.add_label_optional("match",
                       "explicit state correspondence: 'ref=impl' flop pairs (comma/newline-separated) or @FILE; "
                       "collapses differently-named registers onto one cut so the inductive miter compares them",
                       "");
  m.add_label_optional("collapse",
                       "proven-module black-box collapse: comma-separated def names forced to the sound "
                       "blackbox path even when --lib could flatten them (the bottom-up driver's proven set)",
                       "");
  m.add_label_optional("hierarchical",
                       "bottom-up hierarchical decomposition: topo-order the module-def DAG, LEC each def "
                       "leaves-first under the auto portfolio, and collapse already-proven children into "
                       "their parents (a child unprovable in isolation stays flattened)",
                       "false");
  m.add_label_optional("semdiff",
                       "structural def-diff reduction (M3): none (default) | structural. Run "
                       "pass.semdiff per module first; a def whose ref/impl are structurally identical "
                       "(and whose children are all proven) is dropped as proven with NO solver call",
                       "none");
  m.add_label_optional("cross", "also run lgcheck and assert agreement (bring-up only)", "false");
  m.add_label_optional("decompose",
                       "prove each cut/output equivalence as a separate focused query instead of one "
                       "monolithic OR-miter; isolates and reports the hard cones cvc5 cannot discharge",
                       "false");
  m.add_label_optional("strict",
                       "treat an inconclusive UNKNOWN (no counterexample, solver incomplete) as a hard "
                       "failure; default false (REFUTED fails, witness-free UNKNOWN is a deferred warning)",
                       "false");
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
  o.engine  = std::string{var.get("engine", "bmc")};
  o.solver  = std::string{var.get("solver", "cvc5")};
  o.bound   = str_tools::to_i(var.get("bound", "6"));
  o.timeout = str_tools::to_i(var.get("timeout", "120"));
  o.witness = parse_bool(var.get("witness", "true"));
  o.phase        = std::string{var.get("phase", "after_reset")};
  o.reset_cycles = str_tools::to_i(var.get("reset_cycles", "2"));
  o.reset        = std::string{var.get("reset", "")};
  o.match        = lec::parse_match_pairs(var.get("match", ""));  // inline pairs (@FILE only via `lhd lec`)
  o.decompose    = parse_bool(var.get("decompose", "false"));
  o.strict       = parse_bool(var.get("strict", "false"));
  o.semdiff      = std::string{var.get("semdiff", "none")};
  // lec.collapse: comma-separated proven-module def names to force-blackbox.
  if (std::string cs{var.get("collapse", "")}; !cs.empty()) {
    size_t pos = 0;
    while (pos < cs.size()) {
      size_t c   = cs.find(',', pos);
      size_t end = c == std::string::npos ? cs.size() : c;
      if (end > pos) {
        o.collapse.emplace_back(cs.substr(pos, end - pos));
      }
      pos = end + 1;
    }
  }

  if (auto e = lec::lec_options_range_error(o); !e.empty()) {
    livehd::diag::err("pass.lec", "bad-bound", "io").msg("pass.lec: {}", e).fatal();
    return;
  }

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
