// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_formal.hpp"

#include <string>
#include <string_view>
#include <vector>

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
// Parse the kind ("assert" | "assert_always" | "assume") packed in an fproperty
// Sub's instance-name attr ("<kind>\x1f<loc>\x1f<msg>").
std::string fprop_kind(const hhds::Node_class& node) {
  auto nm = node.attr(hhds::attrs::name);
  if (!nm.has()) {
    return "assert";
  }
  std::string s{nm.get()};
  auto        p = s.find('\x1f');
  return (p == std::string::npos) ? s : s.substr(0, p);
}
}  // namespace

void Pass_formal::setup() {
  Eprp_method m("pass.formal",
                "Single-design formal property checks (assert / assume / Hotmux one-hotness) on the cvc5 prover",
                &Pass_formal::work);
  m.add_label_optional("enabled", "true|false run the pass (always-on by default; opt out with false)", "true");
  m.add_label_optional("budget_k", "deterministic per-query cvc5 rlimit = budget_k * cone-node-count", "256");
  m.add_label_optional("cone_max", "skip (defer to runtime) cones larger than this many nodes", "50000");
  m.add_label_optional("warn_deferred", "true|false warn whenever any obligation is deferred to runtime", "true");
  m.add_label_optional("warn_onehot", "true|false warn on a deferred Hotmux one-hot check", "true");
  m.add_label_optional("warn_assert", "true|false warn on a deferred assert", "true");
  m.add_label_optional("warn_assume", "true|false warn on a deferred assume", "true");
  register_pass(m);
}

void Pass_formal::work(Eprp_var& var) {
  if (!truthy(var.get("enabled", "true"))) {
    return;  // opt-out: --set pass.formal.enabled=false (or compile.formal=false in the recipe loop)
  }
  formal::Prove_options opts;
  opts.budget_k = to_int(var.get("budget_k", "256"), 256);
  opts.cone_max = to_int(var.get("cone_max", "50000"), 50000);

  const bool warn_def    = truthy(var.get("warn_deferred", "true"));
  const bool warn_onehot = warn_def && truthy(var.get("warn_onehot", "true"));

  for (auto& gp : var.graphs) {
    auto* g = gp.get();
    if (g == nullptr) {
      continue;
    }
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
      } else if (out.verdict == formal::Verdict::Refuted && !out.stateful) {
        // Stateless cone + concrete falsifying assignment => a real reachable bug:
        // two arms can fire at once. Hard compile error.
        livehd::diag::err("pass.formal", "onehot-violated", "comptime")
            .msg("Hotmux selector can have two or more bits set at once in '{}' — overlapping `unique if`/`match` "
                 "conditions{}{}",
                 std::string{g->get_name()},
                 out.witness.empty() ? std::string{} : std::string{" (counterexample: "},
                 out.witness.empty() ? std::string{} : (out.witness + ")"))
            .fatal();
      } else {
        // Unknown / budget / unreachable-stateful refutation: keep a runtime check.
        gu::set_runtime_check(node, gu::kFormalOnehot);
        if (warn_onehot) {
          livehd::diag::warn("pass.formal", "onehot-deferred", "comptime")
              .msg("Hotmux selector one-hotness could not be proven in '{}' — deferring to a runtime check",
                   std::string{g->get_name()})
              .emit();
        }
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
      if (fprop_kind(node) != "assume") {
        continue;
      }
      auto cond = gu::get_driver_of_sink_name(node, "cond");
      if (cond.is_invalid()) {
        continue;
      }
      auto out = prover.is_true(cond);
      if (out.verdict == formal::Verdict::Proven) {
        gu::set_proven(node, gu::kFormalAssume);
        proven_assumes.push_back(cond);
      } else {
        gu::set_runtime_check(node, gu::kFormalAssume);  // declared contract, checked at runtime
        if (warn_assume) {
          livehd::diag::warn("pass.formal", "assume-deferred", "comptime")
              .msg("assume could not be proven in '{}' — kept as a runtime contract check", std::string{g->get_name()})
              .emit();
        }
      }
    }
    for (auto& c : proven_assumes) {
      prover.assume(c);  // sound hypotheses for the assert queries
    }

    // Pass 2: prove asserts / assert_always under the proven-assume hypotheses.
    // Refutations defer to runtime (NOT a hard error yet — a normal assert may
    // legitimately fail during reset, and reset-exclusion is a future refinement;
    // the Hotmux structural check above is the only hard-error path for now).
    for (auto& node : props) {
      std::string kind = fprop_kind(node);
      if (kind == "assume") {
        continue;
      }
      auto cond = gu::get_driver_of_sink_name(node, "cond");
      if (cond.is_invalid()) {
        continue;
      }
      uint32_t code = (kind == "assert_always") ? gu::kFormalAssertAlways : gu::kFormalAssert;
      auto     out  = prover.is_true(cond);
      if (out.verdict == formal::Verdict::Proven) {
        gu::set_proven(node, code);  // cgen elides the runtime check
      } else {
        gu::set_runtime_check(node, code);
        if (warn_assert) {
          livehd::diag::warn("pass.formal", "assert-deferred", "comptime")
              .msg("{} could not be proven in '{}' — deferring to a runtime check", kind, std::string{g->get_name()})
              .emit();
        }
      }
    }
  }
}
