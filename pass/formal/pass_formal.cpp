// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_formal.hpp"

#include <string>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "cell.hpp"
#include "diag.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "prove.hpp"

using namespace livehd;
namespace gu = livehd::graph_util;

static Pass_plugin plugin("pass_formal", Pass_formal::setup);

Pass_formal::Pass_formal(const Eprp_var& var) : Pass("pass.formal", var) {}

namespace {
bool truthy(std::string_view v) { return v != "false" && v != "0" && !v.empty(); }
int  to_int(std::string_view v, int dflt) {
  std::string s{v};
  if (s.empty()) {
    return dflt;
  }
  try {
    return std::stoi(s);
  } catch (...) {
    return dflt;
  }
}
// The decoded fproperty instance-name attr ("<kind>\x1f<loc>\x1f<msg>"): the
// kind ("assert" | "assert_always" | "assume") plus the optional source
// location and user message carried for diagnostics.
struct Fprop {
  std::string kind = "assert";
  std::string loc;
  std::string msg;
};
Fprop fprop_parts(const hhds::Node_class& node) {
  Fprop f;
  auto  nm = node.attr(hhds::attrs::name);
  if (!nm.has()) {
    return f;
  }
  std::string s{nm.get()};
  auto        p1 = s.find('\x1f');
  if (p1 == std::string::npos) {
    f.kind = s;
    return f;
  }
  f.kind  = s.substr(0, p1);
  auto p2 = s.find('\x1f', p1 + 1);
  if (p2 == std::string::npos) {
    f.loc = s.substr(p1 + 1);
    return f;
  }
  f.loc = s.substr(p1 + 1, p2 - p1 - 1);
  f.msg = s.substr(p2 + 1);
  return f;
}

// A refuted property (the solver found a definitive counterexample). Per
// 2f-formal the FAIL is RECORDED but the compile CONTINUES (non-fatal .emit()),
// and the caller keeps the runtime check without ever eliding it or using it to
// optimize. The proof reasons over free module inputs (and any cut register
// state) in ISOLATION, so a different top-level instantiation may constrain them
// and flip the result -> the hint says so. A surprising refutation may instead
// be a compiler bug worth reporting (so a human / coding agent can check).
void report_refuted(std::string_view code, const std::string& what, std::string_view module, const std::string& loc,
                    const std::string& msg, const formal::Query_out& out) {
  std::string detail;
  if (!loc.empty()) {
    detail += " at " + loc;
  }
  if (!msg.empty()) {
    detail += ": " + msg;
  }
  std::string cex = out.witness.empty() ? std::string{} : (" (counterexample: " + out.witness + ")");
  std::string hint
      = out.stateful
            ? "checked over free module inputs and free (cut) register state in isolation; a different top-level "
              "instantiation, or an unreachable register state, may explain it — re-check at the intended top, or "
              "report a compiler bug if it should already hold"
            : "checked over free module inputs in isolation; a different top-level instantiation may constrain them so "
              "it holds — re-check at the intended top, or report a compiler bug if it should already hold";
  // Deferred: record + fail the build, but let the pipeline finish so cgen still
  // emits the design with the failing property kept as a runtime check.
  livehd::diag::err("pass.formal", code, "comptime")
      .msg("{} in '{}'{}{}", what, module, detail, cex)
      .hint(hint)
      .deferred()
      .emit();
}

// A property that was NOT confirmed-real (or was downgraded) is kept as a runtime
// check and reported as a LOUD `DEFERRED` warning (never silently masked) — the
// build still passes. Reasons, in priority order: the user downgraded refutations
// with on_refute=warn; a refutation in a non-top (instantiated) module = "not
// enough top"; a refutation resting on possibly-unreachable register state
// (fast/induction, needs BMC); or simply undecided (budget/unknown).
void warn_deferred(bool enabled, std::string_view code, std::string_view subject, std::string_view module,
                   const formal::Query_out& out, bool is_top, bool downgraded) {
  if (!enabled) {
    return;
  }
  std::string m{module};
  if (out.verdict == formal::Verdict::Refuted && downgraded) {
    livehd::diag::warn("pass.formal", code, "comptime")
        .msg("DEFERRED: {} in '{}' is refuted but downgraded to a warning by --set compile.formal.on_refute=warn; "
             "kept as a runtime check",
             subject, m)
        .emit();
  } else if (out.verdict == formal::Verdict::Refuted && !is_top) {
    livehd::diag::warn("pass.formal", code, "comptime")
        .msg("DEFERRED: {} in '{}' refuted only in module-isolation — its inputs are constrained by a parent "
             "instantiation (not enough top); kept as a runtime check",
             subject, m)
        .emit();
  } else if (out.verdict == formal::Verdict::Refuted) {
    livehd::diag::warn("pass.formal", code, "comptime")
        .msg("DEFERRED: {} in '{}' refuted only under a possibly-unreachable register state — kept as a runtime check "
             "(use --set compile.formal.mode=normal for a BMC check)",
             subject, m)
        .emit();
  } else {
    livehd::diag::warn("pass.formal", code, "comptime")
        .msg("DEFERRED: {} in '{}' could not be proven — kept as a runtime check", subject, m)
        .emit();
  }
}
}  // namespace

void Pass_formal::setup() {
  Eprp_method m("pass.formal",
                "Single-design formal property checks (assert / assume / Hotmux one-hotness) on the cvc5 prover",
                &Pass_formal::work);
  m.add_label_optional(
      "mode",
      "none|fast|normal — none skips; fast=induction (trusts combinational refutations, defers stateful ones); "
      "normal=BMC-intent (also trusts stateful refutations)",
      "normal");
  m.add_label_optional("on_refute",
                       "error|warn — a refutation at the top boundary fails the build (error, default) or is downgraded "
                       "to a warning (warn). A proven/passing property is always sound; only a 'fail' can be spurious.",
                       "error");
  m.add_label_optional("enabled", "true|false run the pass (opt out with false, same as mode=none)", "true");
  m.add_label_optional("budget_k", "deterministic per-query cvc5 rlimit = budget_k * cone-node-count (0 = default 256)", "0");
  m.add_label_optional("cone_max", "skip (defer to runtime) cones larger than this many nodes (0 = default 50000)", "0");
  m.add_label_optional("warn_deferred", "true|false warn whenever any obligation is deferred to runtime", "true");
  m.add_label_optional("warn_onehot", "true|false warn on a deferred Hotmux one-hot check", "true");
  m.add_label_optional("warn_assert", "true|false warn on a deferred assert", "true");
  m.add_label_optional("warn_assume", "true|false warn on a deferred assume", "true");
  register_pass(m);
}

void Pass_formal::work(Eprp_var& var) {
  const std::string_view mode = var.get("mode", "normal");
  if (mode == "none" || !truthy(var.get("enabled", "true"))) {
    return;  // opt-out: --set compile.formal.mode=none (or pass.formal.enabled=false)
  }
  if (mode != "fast" && mode != "normal") {
    livehd::diag::err("pass.formal", "bad-mode", "comptime")
        .msg("pass.formal mode must be none|fast|normal, got '{}'", mode)
        .emit();
    return;
  }
  // Engine semantics (mirrors LEC's IND vs BMC), NOT a clause-budget knob:
  //   fast   = induction: the Prover cuts flops/memories as free inputs, so a
  //            PROVEN holds for every reachable state (sound) and a purely
  //            COMBINATIONAL refutation is exact. But a refutation whose cone cut
  //            a flop/memory ("stateful") may rest on an unreachable state, so
  //            fast DEFERS it to a runtime check — confirming it needs BMC.
  //   normal = BMC-intent: also trusts stateful refutations. (Today this is still
  //            the induction over-approximation — the real BMC unroll engine is
  //            not built yet — so a stateful counterexample carries the existing
  //            "may be unreachable" caveat. A future BMC mode confirms reachability.)
  // budget_k/cone_max are independent of the mode (0 = built-in default); the
  // default budget already discharges the easy combinational obligations.
  const bool trust_stateful_refute = (mode == "normal");

  // on_refute: a PROVEN property is always sound, so only a "fail" (refutation)
  // can be spurious. At the committed top boundary a refutation FAILS the build
  // (error, default); `warn` downgrades every refutation to a warning — the escape
  // hatch when a design is checked without enough top context.
  const std::string_view on_refute = var.get("on_refute", "error");
  if (on_refute != "error" && on_refute != "warn") {
    livehd::diag::err("pass.formal", "bad-on-refute", "comptime")
        .msg("pass.formal on_refute must be error|warn, got '{}'", on_refute)
        .emit();
    return;
  }
  const bool downgrade_refute = (on_refute == "warn");
  // An explicit --top names the committed design boundary, so that graph is always
  // treated as a root for the FAIL decision (even if a parent that happens to be
  // in the same compilation instantiates it).
  const std::string_view designated_top = var.get("top", "");

  formal::Prove_options opts;
  const int budget_k = to_int(var.get("budget_k", "0"), 0);
  const int cone_max = to_int(var.get("cone_max", "0"), 0);
  opts.budget_k = budget_k > 0 ? budget_k : 256;
  opts.cone_max = cone_max > 0 ? cone_max : 50000;

  const bool warn_def    = truthy(var.get("warn_deferred", "true"));
  const bool warn_onehot = warn_def && truthy(var.get("warn_onehot", "true"));

  // "Not enough top": a graph that some Sub instantiates is a SUBMODULE whose
  // inputs are constrained by its parent, so a refutation found over its free
  // ports in isolation is NOT a confirmed bug — it is reported as a loud warning,
  // never a build failure. Only a ROOT graph (nothing instantiates it — the real
  // design boundary, incl. an explicit --top) can fail the build on a refutation.
  absl::flat_hash_set<hhds::Gid> instantiated_gids;
  for (auto& gp2 : var.graphs) {
    auto* g2 = gp2.get();
    if (g2 == nullptr) {
      continue;
    }
    for (auto node : g2->forward_class()) {
      if (gu::type_op_of(node) == Ntype_op::Sub) {
        if (auto gid = node.get_subnode_gid(); gid != hhds::Gid_invalid) {
          instantiated_gids.insert(gid);
        }
      }
    }
  }

  for (auto& gp : var.graphs) {
    auto* g = gp.get();
    if (g == nullptr) {
      continue;
    }
    // is_top: the graph is the design boundary — the explicit --top (by module
    // name, tolerating a "unit." prefix) or, absent that, a root no Sub instantiates.
    std::string_view gname = g->get_name();
    auto             dot   = gname.rfind('.');
    std::string_view gmod  = (dot == std::string_view::npos) ? gname : gname.substr(dot + 1);
    const bool matches_top = !designated_top.empty() && (gname == designated_top || gmod == designated_top);
    const bool is_top      = matches_top || !instantiated_gids.contains(g->get_gid());
    formal::Prover prover(g, opts);

    // Built-in obligation: every Hotmux selector must be one-hot-or-zero. Collect
    // first so attribute writes never perturb the forward_class walk.
    std::vector<hhds::Node_class> hotmuxes;
    for (auto node : g->forward_class()) {
      if (gu::type_op_of(node) == Ntype_op::Hotmux) {
        hotmuxes.push_back(node);
      }
    }
    for (auto& node : hotmuxes) {
      hhds::Pin_class sel;
      for (const auto& e : node.inp_edges()) {
        if (e.sink.get_port_id() == 0) {  // pid 0 = selector
          sel = e.driver;
          break;
        }
      }
      if (sel.is_invalid()) {
        continue;
      }
      auto out = prover.is_onehot0(sel);
      if (out.verdict == formal::Verdict::Proven) {
        gu::set_proven(node, gu::kFormalOnehot);  // one-hotness obligation discharged
      } else if (out.verdict == formal::Verdict::Refuted && is_top && (trust_stateful_refute || !out.stateful)
                 && !downgrade_refute) {
        // FAIL: a concrete assignment sets two or more selector bits at once, in
        // a ROOT module (no missing top context). Record a (non-fatal) compile
        // error and continue; keep the runtime check and never elide it or expose
        // the selector as a don't-care to pass.abc (a refuted property is unsound
        // to optimize with).
        report_refuted("onehot-violated",
                       "Hotmux selector can have two or more bits set at once (overlapping `unique if`/`match`)",
                       g->get_name(), "", "", out);
        gu::set_runtime_check(node, gu::kFormalOnehot);
      } else {
        // Undecided, a non-top module ("not enough top"), or (fast) a stateful
        // refutation -> keep the runtime check + a loud DEFERRED warning.
        gu::set_runtime_check(node, gu::kFormalOnehot);
        warn_deferred(warn_onehot, "onehot-deferred", "Hotmux selector one-hotness", g->get_name(), out, is_top, downgrade_refute);
      }
    }

    // --- User obligations materialized by tolg: fproperty Sub-nodes ---
    // (assert / assert_always / assume). Collect first so attr writes don't
    // perturb the forward_class walk.
    std::vector<hhds::Node_class> props;
    for (auto node : g->forward_class()) {
      if (gu::type_op_of(node) != Ntype_op::Sub) {
        continue;
      }
      auto sio = node.get_subnode_io();
      if (sio && sio->get_name() == gu::fproperty_module_name) {
        props.push_back(node);
      }
    }
    const bool warn_assume = warn_def && truthy(var.get("warn_assume", "true"));
    const bool warn_assert = warn_def && truthy(var.get("warn_assert", "true"));

    // Pass 1: prove each assume INDEPENDENTLY (no hypotheses, so no circular
    // self-proof). Only PROVEN assumes become hypotheses for the asserts below
    // (sound: proven facts) and are exposed to pass.abc as don't-cares.
    std::vector<hhds::Pin_class> proven_assumes;
    for (auto& node : props) {
      auto parts = fprop_parts(node);
      if (parts.kind != "assume") {
        continue;
      }
      auto cond = gu::get_driver_of_sink_name(node, "cond");
      if (cond.is_invalid()) {
        continue;
      }
      auto out = prover.is_true(cond);
      if (out.verdict == formal::Verdict::Proven) {
        gu::set_proven(node, gu::kFormalAssume);
        proven_assumes.push_back(cond);  // only PROVEN assumes become hypotheses (sound)
      } else if (out.verdict == formal::Verdict::Refuted && is_top && (trust_stateful_refute || !out.stateful)
                 && !downgrade_refute) {
        // FAIL: the assumed contract can be false at a ROOT boundary. Record +
        // continue; keep the runtime check and do NOT add it as a hypothesis (a
        // refuted assume must never optimize, or every assert/abc query built on
        // it is unsound).
        report_refuted("assume-refuted", "assume is refuted (a concrete input makes it false)", g->get_name(),
                       parts.loc, parts.msg, out);
        gu::set_runtime_check(node, gu::kFormalAssume);
      } else {
        // Undecided, a non-top module ("not enough top"), or (fast) a stateful
        // refutation: the declared contract is kept as a runtime check (never a
        // hypothesis) + a loud DEFERRED warning.
        gu::set_runtime_check(node, gu::kFormalAssume);
        warn_deferred(warn_assume, "assume-deferred", "assume", g->get_name(), out, is_top, downgrade_refute);
      }
    }
    for (auto& c : proven_assumes) {
      prover.assume(c);  // sound hypotheses for the assert queries
    }

    // Pass 2: prove asserts / assert_always under the proven-assume hypotheses.
    // Verdict policy (2f-formal): PROVEN -> elide; REFUTED -> record a non-fatal
    // error and keep the runtime check (never elide it, never use it as a
    // hypothesis); UNKNOWN -> keep the runtime check + a deferral warning. A
    // FAIL is reported but the design still compiles (continue); the hint notes
    // that a different top-level may change the result (inputs are free here).
    for (auto& node : props) {
      auto parts = fprop_parts(node);
      if (parts.kind == "assume") {
        continue;
      }
      auto cond = gu::get_driver_of_sink_name(node, "cond");
      if (cond.is_invalid()) {
        continue;
      }
      uint32_t code = (parts.kind == "assert_always") ? gu::kFormalAssertAlways : gu::kFormalAssert;
      auto     out  = prover.is_true(cond);
      if (out.verdict == formal::Verdict::Proven) {
        gu::set_proven(node, code);  // cgen elides the runtime check
      } else if (out.verdict == formal::Verdict::Refuted && is_top && (trust_stateful_refute || !out.stateful)
                 && !downgrade_refute) {
        report_refuted("assert-refuted", parts.kind + " is refuted (a concrete input makes it false)", g->get_name(),
                       parts.loc, parts.msg, out);
        gu::set_runtime_check(node, code);  // keep: never elide a failing assert
      } else {
        // Unknown, a non-top module ("not enough top"), or (fast) a stateful
        // refutation whose witness may be an unreachable state -> keep the runtime
        // check + a loud DEFERRED warning; only a root + BMC (normal) trusts it.
        gu::set_runtime_check(node, code);
        warn_deferred(warn_assert, "assert-deferred", parts.kind, g->get_name(), out, is_top, downgrade_refute);
      }
    }
  }
}
