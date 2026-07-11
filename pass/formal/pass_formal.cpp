// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_formal.hpp"

#include <algorithm>
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

// A REACHABLE-from-reset refutation found by the shared BMC engine (mode=normal,
// 2f-formal rebase). Unlike the single-frame free-state refute, the witness is
// reachable from reset within the compile budget, so it is a genuine violation
// (no "may be unreachable" caveat). Recorded as a deferred (non-fatal) error so
// the pipeline finishes with the failing check kept at runtime.
void report_refuted_reachable(std::string_view code, const std::string& what, std::string_view module, const std::string& loc,
                              const std::string& msg, const std::string& witness, int cycle) {
  std::string detail;
  if (!loc.empty()) {
    detail += " at " + loc;
  }
  if (!msg.empty()) {
    detail += ": " + msg;
  }
  std::string cex
      = witness.empty() ? std::string{} : (" (counterexample from reset @ cycle " + std::to_string(cycle) + ": " + witness + ")");
  livehd::diag::err("pass.formal", code, "comptime")
      .msg("{} in '{}'{}{}", what, module, detail, cex)
      .hint("reachable from reset within the compile budget (BMC), so this is a genuine violation — fix the design, narrow "
            "it with a proven assume, or intend it as a runtime check; a deeper/unreachable case would not error")
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
  m.add_label_optional("bmc_bound",
                       "mode=normal BMC-from-reset unroll depth (default 4, tiny): caps how deep a REACHABLE refute is "
                       "confirmed; the 1-induction rung proves unbounded invariants regardless. Raise to catch deeper bugs",
                       "4");
  m.add_label_optional("reset",
                       "mode=normal authoritative reset input spec (name[:lo|:hi], comma-separated; else auto-detect). "
                       "When no reset is detected the BMC starts from free state, so a refute is NOT treated as reachable",
                       "");
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

  // mode=normal BMC-from-reset unroll depth (2f-formal): a TINY bound — the
  // single-frame base case plus enough free cycles that shallow reachable
  // violations surface within a compile-safe budget. The 1-induction rung proves
  // UNBOUNDED invariants regardless of it; the bound only caps how deep a
  // REACHABLE refute is confirmed (deeper bugs stay runtime checks, never a false
  // error). Raise it to confirm deeper violations at the cost of compile time.
  const int bmc_bound = std::max(1, to_int(var.get("bmc_bound", "4"), 4));

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

    // Pass 2: prove asserts / assert_always.
    //
    // The pre-rebase single-frame Prover engine, as a per-obligation proof:
    // PROVEN -> elide; (only when allow_refute_error) a trusted top-level refute
    // -> a non-fatal error + kept runtime check; else keep the runtime check + a
    // DEFERRED warning. mode=fast uses it directly (a stateful refute may be an
    // unreachable state, so trust_stateful_refute is false there); mode=normal
    // uses it ONLY to recover a free-state proof under the proven assumes that the
    // BMC engine left undecided (never hard-erroring — the BMC engine is
    // authoritative on reachability and did not confirm a reachable violation).
    auto prove_assert_prover = [&](const hhds::Node_class& node, const Fprop& parts, bool allow_refute_error) {
      auto cond = gu::get_driver_of_sink_name(node, "cond");
      if (cond.is_invalid()) {
        return;
      }
      uint32_t code = (parts.kind == "assert_always") ? gu::kFormalAssertAlways : gu::kFormalAssert;
      auto     out  = prover.is_true(cond);
      if (out.verdict == formal::Verdict::Proven) {
        gu::set_proven(node, code);  // cgen elides the runtime check
      } else if (allow_refute_error && out.verdict == formal::Verdict::Refuted && is_top
                 && (trust_stateful_refute || !out.stateful) && !downgrade_refute) {
        report_refuted("assert-refuted", parts.kind + " is refuted (a concrete input makes it false)", g->get_name(),
                       parts.loc, parts.msg, out);
        gu::set_runtime_check(node, code);  // keep: never elide a failing assert
      } else {
        gu::set_runtime_check(node, code);
        warn_deferred(warn_assert, "assert-deferred", parts.kind, g->get_name(), out, is_top, downgrade_refute);
      }
    };

    if (mode != "normal") {
      // mode=fast: single-frame induction over free (cut) state (unchanged).
      for (auto& node : props) {
        auto parts = fprop_parts(node);
        if (parts.kind != "assume") {
          prove_assert_prover(node, parts, /*allow_refute_error=*/true);
        }
      }
    } else {
      // mode=normal (2f-formal rebase): the shared BMC-from-reset + 1-induction
      // engine (lec::prove_properties) is THE assert engine. Prove every
      // obligation of g at once under a deterministic rlimit (no forks, no
      // wall-clock — reproducible across build modes), then map each verdict back
      // to its fproperty node via the encoder's occ walk and apply the stateful
      // verdict ladder:
      //   1-induction PROVEN (unbounded)          -> elide (every cycle of every bound);
      //   BMC-from-reset REFUTED in the post-reset window at a ROOT -> a genuine
      //       reachable violation -> hard (deferred) error + kept runtime check;
      //   else (bounded-only / Unknown / prologue-only / blackbox-gated / non-top)
      //       -> recover a free-state proof under the proven assumes with the
      //       single-frame Prover (never lose a pre-rebase elision), else keep + defer.
      livehd::lec::Lec_options po;
      po.engine        = "bmc";  // single strategy, in-process (never fork in compile)
      po.solver        = "cvc5";
      po.bound         = bmc_bound;  // tiny BMC depth + the 1-induction step
      po.reset_cycles  = 1;
      po.phase         = "after_reset";
      po.timeout       = 0;  // deterministic budget only
      po.rlimit        = static_cast<int>(std::min<long long>(
          static_cast<long long>(std::max(1, opts.budget_k)) * 4096, 1'000'000'000));
      po.witness       = true;
      po.partitions    = 1;      // no case-split forks
      po.split         = "none";
      po.state_pairing = false;
      po.ignore_assumes = true;  // proven assumes recovered by the Prover fallback below
      po.reset          = std::string{var.get("reset", "")};  // authoritative reset spec (else auto-detect)

      auto pres = livehd::lec::prove_properties(g, po);

      // Replay the encoder's fproperty walk to align pres.props (occ order) with
      // g's fproperty nodes. Without a sub_lib the encoder treats sub-instances as
      // opaque free boxes (not descended), so only g's own top-level fproperties
      // are emitted -> 1:1 by index. A size mismatch (unexpected hierarchy) leaves
      // the map unused and falls back to the pre-rebase Prover engine.
      std::vector<hhds::Node_class> occ_nodes;
      for (auto pn : g->forward_hier(true, false, nullptr)) {
        if (gu::type_op_of(pn) != Ntype_op::Sub) {
          continue;
        }
        auto sio = pn.get_subnode_io();
        if (sio == nullptr || sio->get_name() != gu::fproperty_module_name) {
          continue;
        }
        auto cond_pid  = sio->get_input_port_id("cond");
        bool connected = false;
        for (const auto& e : pn.inp_edges()) {
          if (e.sink.get_port_id() == cond_pid) {
            connected = true;
            break;
          }
        }
        if (connected) {
          occ_nodes.push_back(pn);
        }
      }

      if (occ_nodes.size() != pres.props.size()) {
        // Correlation failed (unexpected hierarchy / encode divergence): degrade
        // to the pre-rebase Prover engine rather than risk mis-marking a node.
        for (auto& node : props) {
          auto parts = fprop_parts(node);
          if (parts.kind != "assume") {
            prove_assert_prover(node, parts, /*allow_refute_error=*/true);
          }
        }
      } else {
        for (size_t k = 0; k < occ_nodes.size(); ++k) {
          auto& node = occ_nodes[k];
          if (node.get_graph() != g) {
            continue;  // a descended sub-instance fproperty: its own module's visit owns it
          }
          const auto& pr = pres.props[k];
          if (pr.kind == "assume") {
            continue;  // assumes are proven independently in Pass 1 (Prover)
          }
          Fprop parts;
          parts.kind    = pr.kind;
          parts.loc     = pr.loc;
          parts.msg     = pr.msg;
          uint32_t code = (pr.kind == "assert_always") ? gu::kFormalAssertAlways : gu::kFormalAssert;
          if (pr.verdict == livehd::lec::Verdict::Proven && pr.unbounded) {
            gu::set_proven(node, code);  // inductive -> holds forever -> elide
          } else if (pr.verdict == livehd::lec::Verdict::Refuted && pr.refuted_at >= pres.reset_hold && pres.reset_detected
                     && is_top && !downgrade_refute) {
            // A reset prologue actually pinned the initial state, so the BMC CEX
            // is genuinely reachable from reset -> a real violation. Without a
            // detected reset (pres.reset_detected == false) the flops start FREE,
            // so the witness may be an unreachable initial state: fall through to
            // the deferred path rather than fail a possibly-correct build.
            report_refuted_reachable("assert-refuted", pr.kind + " is refuted (a reachable state makes it false)",
                                     g->get_name(), pr.loc, pr.msg, pr.witness, pr.refuted_at);
            gu::set_runtime_check(node, code);
          } else {
            prove_assert_prover(node, parts, /*allow_refute_error=*/false);
          }
        }
      }
    }
  }
}
