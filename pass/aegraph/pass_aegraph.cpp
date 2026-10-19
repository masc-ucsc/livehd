//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_aegraph.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "aegraph.hpp"
#include "aegraph_profile.hpp"
#include "cprop.hpp"
#include "cprop_summary.hpp"
#include "diag.hpp"
#include "loop_hoist.hpp"
#include "worker_pool.hpp"  // livehd::run_workers (big-stack workers)

static Pass_plugin sample("pass_aegraph", Pass_aegraph::setup);

void Pass_aegraph::setup() {
  Eprp_method m1("pass.aegraph", "acyclic e-graph constant propagation (alternative to pass.cprop)", &Pass_aegraph::optimize);
  // The labels below are the shared spellings of pass.cprop (`compile.cprop.*`):
  // each knob has ONE user-facing spelling and the kernel copies it here.
  m1.add_label_optional("low_lanes", "INTERNAL shared with pass.cprop: see compile.cprop.low_lanes", "true");
  m1.add_label_optional("loop_hoist", "INTERNAL shared with pass.cprop: see compile.cprop.loop_hoist", "true");
  m1.add_label_optional("engine", "INTERNAL shared with pass.cprop: see compile.cprop.engine", "aegraph");
  m1.add_label_optional("stats", "INTERNAL set by the kernel from lhd.stats: emit one cprop-summary diagnostic per graph", "false");

  // The rewrite-rule runtime (compile.aegraph.*). The rule names are in the table of
  // pass/aegraph/README.md; a list joins several names with '+' (a comma would split the --set list).
  m1.add_label_optional("rules_disable",
                        "rule names (or group:NAME) to turn off, joined with '+', e.g. flatten+group:mux; empty = all rules on",
                        "");
  m1.add_label_optional("rules_only", "keep only these rules (same syntax as rules_disable); empty = every rule", "");
  m1.add_label_optional("max_rewrites",
                        "debug bisection: stop applying rules after N rewrites, counted per graph in forward-walk order (0 = no "
                        "limit). The pass then runs on one worker and logs the last rule applied and its e-node",
                        "0");
  m1.add_label_optional("bisect_graph", "limit the max_rewrites cutoff log to the graph with this name", "");
  m1.add_label_optional("explain", "print the e-node chain behind each extracted node a rule built (rule name)", "");
  m1.add_label_optional("check",
                        "off|unions|full: runtime check that rewrites preserve values. unions evaluates both sides of each rewrite "
                        "on 8 random vectors and aborts naming the rule; full also checks every original node against the class "
                        "extracted for it on 64 vectors. Default: unions in dbg builds, off in opt",
                        "");
  m1.add_label_optional("rewrite_depth", "nested rule-built e-nodes that still meet rules (Cranelift rewrite depth)", "5");
  m1.add_label_optional("matches_limit", "alt-rule results unioned per e-node", "5");
  m1.add_label_optional("eclass_limit", "members of one e-class (the size of a union)", "5");
  m1.add_label_optional("extractor_fuel", "tree-cost saturation of the extractor (a tie keeps the original)", "500");
  m1.add_label_optional("edge_budget",
                        "rule work (operands scanned) allowed per input edge of a graph; 0 = unlimited. Keeps the pass O(V+E)",
                        "32");
  m1.add_label_optional("cost",
                        "synth|sim: the extraction cost profile label (the profiles arrive with the cost milestone)",
                        "synth");
  m1.add_label_optional("choices", "off|const|expr: whether '?' choice rules are active (no choice rules exist yet)", "off");

  register_pass(m1);
}

Pass_aegraph::Pass_aegraph(const Eprp_var& var) : Pass("pass.aegraph", var) {}

namespace {

// The lifted loop bodies among `graphs` (callees of a compact-loop Sub): the
// post steps' accumulation guard is a rolled-loop rule, as in pass.cprop.
absl::flat_hash_set<hhds::Gid> lifted_loop_bodies(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
  absl::flat_hash_set<hhds::Gid> bodies;
  for (const auto& g : graphs) {
    if (!g || !g->has_loop_subnodes()) {
      continue;
    }
    for (const auto node : g->body().nodes()) {
      if (node.is_loop_subnode()) {
        bodies.insert(node.get_subnode_gid());
      }
    }
  }
  return bodies;
}

}  // namespace

namespace livehd::aegraph {
Stats optimize_body(Cprop& cp, const std::shared_ptr<hhds::Graph>& g, const Options& options) {
  const auto stats = optimize(g, [&](const std::vector<hhds::Node_class>& cyclic) { cp.do_scalar(g, cyclic); }, options);
  livehd::aegraph_profile::Timer post(livehd::aegraph_profile::post);
  cp.do_post(g);
  return stats;
}
}  // namespace livehd::aegraph

void Pass_aegraph::optimize(Eprp_var& var) {
  Pass_aegraph pa(var);
  const auto   low_lanes   = var.get("low_lanes", "true") != "false";
  const auto   loop_bodies = lifted_loop_bodies(var.graphs);
  const auto   want_stats  = var.get("stats", "false") == "true";
  const auto   options     = livehd::aegraph::make_options(var.get("rules_disable", ""),
                                                       var.get("rules_only", ""),
                                                       var.get("max_rewrites", "0"),
                                                       var.get("bisect_graph", ""),
                                                       var.get("explain", ""),
                                                       var.get("check", ""),
                                                       var.get("rewrite_depth", "5"),
                                                       var.get("matches_limit", "5"),
                                                       var.get("eclass_limit", "5"),
                                                       var.get("extractor_fuel", "500"),
                                                       var.get("edge_budget", "32"),
                                                       var.get("cost", "synth"),
                                                       var.get("choices", "off"));
  // One cprop-summary record per graph (indexed like var.graphs), emitted
  // serially after the workers join so the order is the graph order.
  std::vector<livehd::cprop_summary::Record> records(want_stats ? var.graphs.size() : 0);

  // One graph: the e-graph engine, then the classic post steps over what it
  // elaborated. Graph bodies are independent, so the per-body work fans out
  // exactly like pass.cprop's; publication order is untouched.
  std::mutex                 stats_mutex;
  livehd::aegraph::Stats     total;
  const auto one = [&](Cprop& cp, size_t index) {
    const auto& g = var.graphs[index];
    if (!g) {
      return;
    }
    cp.set_guard_scope(loop_bodies.contains(g->get_gid()) ? Cprop::whole_graph() : Cprop::Node_scope{});
    const auto name = g->get_io() ? std::string{g->get_io()->get_name()} : std::string{};
    livehd::aegraph_profile::Invocation profile(name);
    livehd::cprop_summary::Probe        probe(*g, want_stats);
    const auto                          stats = livehd::aegraph::optimize_body(cp, g, options);
    if (want_stats) {
      records[index] = probe.finish(*g, name, stats.rules_fired, stats.aeg_bytes);
    }
    const std::lock_guard<std::mutex> lock(stats_mutex);
    total += stats;
  };
  const auto report = [&] {
    for (const auto& r : records) {
      if (!r.graph.empty()) {
        livehd::cprop_summary::emit("pass.aegraph", r);
      }
    }
    if (!want_stats) {
      return;
    }
    // The census: one aegraph-rule record per rule that ran, summed over the graphs.
    auto& reg = livehd::aegraph::rt::Registry::instance();
    for (size_t id = 1; id < total.rule_counts.size(); ++id) {
      const auto& c = total.rule_counts[id];
      if (c.tried == 0) {
        continue;
      }
      const auto& info = reg.rule(static_cast<livehd::aegraph::rt::Rule_id>(id));
      livehd::diag::info("pass.aegraph", "aegraph-rule", "progress")
          .msg("aegraph-rule {} tried={} matched={} applied={} extracted={}", info.name, c.tried, c.matched, c.applied, c.extracted)
          .attr("rule", info.name)
          .attr("group", info.group)
          .attr("tag", std::string{livehd::aegraph::rt::tag_name(info.tag)})
          .attr("tried", std::to_string(c.tried))
          .attr("matched", std::to_string(c.matched))
          .attr("applied", std::to_string(c.applied))
          .attr("subsumed", std::to_string(c.subsumed))
          .attr("limit_rejected", std::to_string(c.limit_rejected))
          .attr("extracted", std::to_string(c.extracted))
          .emit();
    }
  };

  const auto hoist_after = [&] {
    if (var.get("loop_hoist", "true") == "false") {
      return;
    }
    livehd::cprop::hoist_loop_invariants(var.graphs);
  };
  const auto summary = [&] {
    std::printf(
        "[pass.aegraph] graphs=%zu nodes_in=%zu nodes_out=%zu pure=%zu skeleton=%zu opaque=%zu classes=%zu cse=%zu const=%zu "
        "forward=%zu rewritten=%zu rules=%zu unions=%zu kept=%zu dead=%zu consts_created=%zu\n",
        var.graphs.size(),
        total.nodes_in,
        total.nodes_out,
        total.pure,
        total.skeleton,
        total.opaque,
        total.classes,
        total.cse_merged,
        total.const_folded,
        total.forwarded,
        total.rewritten,
        total.rules_fired,
        total.unions,
        total.kept_twin,
        total.dead_removed,
        total.consts_created);
  };

  std::atomic<size_t> next{0};
  const size_t        hw = std::max<size_t>(1, std::thread::hardware_concurrency());
  // max_rewrites bisects in forward-walk order: one worker keeps the graph order.
  const size_t        nw = options.max_rewrites != 0 ? 1 : std::min({var.graphs.size(), hw, size_t{16}});
  if (nw <= 1) {
    Cprop cp{low_lanes};
    for (size_t i = 0; i < var.graphs.size(); ++i) {
      one(cp, i);
    }
    summary();
    report();
    hoist_after();
    return;
  }

  std::vector<std::exception_ptr> errors(var.graphs.size());
  std::atomic<bool>               failed{false};
  livehd::run_workers(nw, [&](size_t) {
    Cprop cp{low_lanes};
    while (!failed.load(std::memory_order_relaxed)) {
      const size_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= var.graphs.size()) {
        break;
      }
      try {
        one(cp, i);
      } catch (...) {
        errors[i] = std::current_exception();
        failed.store(true, std::memory_order_relaxed);
      }
    }
  });
  for (const auto& error : errors) {
    if (error) {
      std::rethrow_exception(error);
    }
  }
  summary();
  report();
  hoist_after();
}
