// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_analyze.hpp"

#include <string>
#include <string_view>

#include "analyze.hpp"
#include "diag.hpp"
#include "node_util.hpp"

using namespace livehd;

static Pass_plugin plugin("pass_analyze", Pass_analyze::setup);

Pass_analyze::Pass_analyze(const Eprp_var& var) : Pass("pass.analyze", var) {}

namespace {
bool parse_bool(std::string_view v) { return v != "false" && v != "0" && !v.empty(); }

// `checks=loops,clocks,colors` — an explicit list REPLACES the defaults, so
// `checks=loops` runs exactly one. Empty keeps all three.
livehd::analyze::Opts parse_opts(Eprp_var& var) {
  livehd::analyze::Opts o;
  const std::string     checks{var.get("checks", "")};
  if (!checks.empty()) {
    o.loops  = checks.find("loops") != std::string::npos;
    o.clocks = checks.find("clocks") != std::string::npos;
    o.colors = checks.find("colors") != std::string::npos;
  }
  o.verbose = parse_bool(var.get("verbose", "false"));
  return o;
}
}  // namespace

void Pass_analyze::setup() {
  Eprp_method m("pass.analyze",
                "Read-only structural diagnosis over a whole lg: library: combinational loops (classified by what sits "
                "on the cycle), clock endpoints per state element, and Color_acyclic partitioning validity",
                &Pass_analyze::analyze);
  m.add_label_optional("checks", "comma list: loops,clocks,colors (default: all three)", "");
  m.add_label_optional("verbose", "report EVERY state element, not just the ones sim cannot lower", "false");
  m.add_label_optional("strict", "exit non-zero when any finding is reported", "false");
  register_pass(m);
}

void Pass_analyze::analyze(Eprp_var& var) {
  const auto opts   = parse_opts(var);
  const bool strict = parse_bool(var.get("strict", "false"));

  livehd::analyze::Report rep;
  for (const auto& g : var.graphs) {
    if (g) {
      livehd::analyze::analyze_def(g.get(), opts, rep);
    }
  }

  // Findings ride the normal diagnostic stream at `info` severity: they are NOT
  // errors, because this pass exists to survey designs the transforming passes
  // REFUSE — a survey that itself failed would be useless — and `info` is the
  // one severity that never changes an exit code. Each carries its structured
  // payload as attrs, so the whole report is `lhd pass analyze … | jq`.
  for (const auto& f : rep.loops) {
    std::string kinds;
    for (const auto k : f.kinds) {
      kinds += (kinds.empty() ? "" : ",");
      kinds += livehd::analyze::to_string(k);
    }
    livehd::diag::info("pass.analyze", "analyze-loop", "progress")
        .msg("`{}`: {} word-level cycle ({} of {} comb nodes), class {} — representative node `{}`{}", f.def,
             f.real ? "REAL" : "FALSE", f.n_on_cycle, f.n_comb, kinds, f.node,
             f.real ? " (an event-driven simulator would iterate this)"
                    : " (it dissolves once a Memory read / Sub call is treated as a boundary: the SINGLE-PASS "
                      "schedule is what cannot order it)")
        .attr("real", f.real ? "true" : "false")
        .attr("def", f.def)
        .attr("kind", livehd::analyze::to_string(f.kind))
        .attr("kinds", kinds)
        .attr("node", f.node)
        .attr("on_cycle", std::to_string(f.n_on_cycle))
        .attr("comb_nodes", std::to_string(f.n_comb))
        .emit();
  }
  for (const auto& f : rep.clocks) {
    livehd::diag::info("pass.analyze", "analyze-clock", "progress")
        .msg("`{}`: {} `{}` clock is {}{} (root `{}`, {} guard(s), chain depth {})", f.def, f.op, f.element,
             livehd::analyze::to_string(f.kind),
             livehd::analyze::sim_can_lower(f.kind) ? "" : " — inou.cgen.sim cannot lower this", f.root, f.n_guards,
             f.chain_depth)
        .attr("def", f.def)
        .attr("element", f.element)
        .attr("op", f.op)
        .attr("kind", livehd::analyze::to_string(f.kind))
        .attr("root", f.root)
        .attr("guards", std::to_string(f.n_guards))
        .attr("chain_depth", std::to_string(f.chain_depth))
        .attr("sim_ok", livehd::analyze::sim_can_lower(f.kind) ? "true" : "false")
        .emit();
  }
  for (const auto& f : rep.colors) {
    livehd::diag::info("pass.analyze", "analyze-color", "progress")
        .msg("`{}`: Color_acyclic invariant `{}` broken — {} ({} partition(s) over {} node(s))", f.def,
             livehd::analyze::to_string(f.defect), f.detail, f.n_parts, f.n_nodes)
        .attr("def", f.def)
        .attr("defect", livehd::analyze::to_string(f.defect))
        .attr("detail", f.detail)
        .attr("parts", std::to_string(f.n_parts))
        .attr("nodes", std::to_string(f.n_nodes))
        .emit();
  }
  livehd::diag::info("pass.analyze", "analyze-summary", "progress")
      .msg("{} definition(s), {} state element(s): {} loop finding(s), {} clock finding(s), {} color finding(s)",
           rep.n_defs, rep.n_state_elements, rep.loops.size(), rep.clocks.size(), rep.colors.size())
      .attr("defs", std::to_string(rep.n_defs))
      .attr("state_elements", std::to_string(rep.n_state_elements))
      .attr("loop_findings", std::to_string(rep.loops.size()))
      .attr("clock_findings", std::to_string(rep.clocks.size()))
      .attr("color_findings", std::to_string(rep.colors.size()))
      .emit();

  const size_t n = rep.loops.size() + rep.clocks.size() + rep.colors.size();
  if (n == 0) {
    return;
  }
  if (strict) {
    // DEFERRED: recorded, fails the run at the end, but does NOT halt the pass
    // pipeline. A halting error here would recreate exactly the fail-fast
    // behaviour this pass exists to replace.
    livehd::diag::err("pass.analyze", "analyze-findings", "unsupported")
        .msg("pass.analyze reported {} finding(s) over {} definition(s)", n, rep.n_defs)
        .hint("read the analyze-* records above; drop `--set pass.analyze.strict=true` to report without failing")
        .deferred()
        .emit();
  }
}
