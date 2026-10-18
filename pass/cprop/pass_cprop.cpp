//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_cprop.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "cprop.hpp"
#include "loop_hoist.hpp"
#include "worker_pool.hpp"  // livehd::run_workers (big-stack workers)

static Pass_plugin sample("pass_cprop", Pass_cprop::setup);

void Pass_cprop::setup() {
  Eprp_method m1("pass.cprop", "in-place copy propagation", &Pass_cprop::optimize);
  m1.add_label_optional("low_lanes",
                        "true: narrow operations reading a value with known low bits, Or(Shl(H,k),L), to their H parts "
                        "(bitwidth runs after cprop in every compile)",
                        "true");
  m1.add_label_optional("loop_hoist",
                        "true: move a compact loop body's iteration-invariant computations (those reading only invariant "
                        "inputs and constants, never the ordinal, the activation or a carry) out to every loop instance's "
                        "parent, computed once per evaluation and fed through a new `__hoist<k>` body input. The compact "
                        "form is preserved; the values never change",
                        "true");

  m1.add_label_optional("engine",
                        "classic|aegraph|aegraph+classic: the constant-propagation engine the compile schedule runs in place of "
                        "(or before) pass.cprop. classic is the default and always remains the reference. At least one engine "
                        "always runs. The kernel dispatches (compile_graph_passes), so this method only ever sees classic",
                        "classic");
  m1.add_label_optional("engine_slots",
                        "both|first|second: which of the two compile cprop slots (the first, before pass.legalize, and the "
                        "second, after enableopt) follow compile.cprop.engine; the other slot stays classic",
                        "both");

  register_pass(m1);
}

Pass_cprop::Pass_cprop(const Eprp_var& var) : Pass("pass.cprop", var) {}

// Loop-invariant code motion is a library-wide rewrite (a body and all its
// parents change together), so it follows the per-graph transform serially.
static void hoist_after(Eprp_var& var) {
  if (var.get("loop_hoist", "true") == "false") {
    return;
  }
  livehd::cprop::hoist_loop_invariants(var.graphs);
}

// The lifted loop bodies among `graphs`: the callees of a compact-loop Sub
// (Subnode_loop) any of them instantiates. Only a graph with a loop Sub is
// walked (has_loop_subnodes is O(1)), so a loop-free design pays one flag read
// per graph. A body whose loop parent is outside `graphs` (a partial pass run)
// is not found and simply keeps the loop-free rules.
static absl::flat_hash_set<hhds::Gid> lifted_loop_bodies(const std::vector<std::shared_ptr<hhds::Graph>>& graphs) {
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

void Pass_cprop::optimize(Eprp_var& var) {
  Pass_cprop pcp(var);
  // The kernel swaps the method for the other engines (compile_graph_passes),
  // so a non-classic value reaching here means a caller bypassed the dispatch:
  // running classic silently would make an A/B comparison compare nothing.
  if (const auto engine = var.get("engine", "classic"); engine != "classic") {
    livehd::diag::err("pass.cprop", "bad-option", "io")
        .msg("pass.cprop runs only the classic engine, got engine='{}'; compile.cprop.engine is dispatched by the lhd kernel", engine)
        .fatal();
    return;
  }
  const auto low_lanes = var.get("low_lanes", "true") != "false";
  // The accumulation guard is a rolled-loop rewrite (Cprop::set_guard_scope):
  // every cell of a lifted loop body, nothing else.
  const auto loop_bodies = lifted_loop_bodies(var.graphs);
  const auto scoped      = [&](Cprop& cp, const std::shared_ptr<hhds::Graph>& g) {
    cp.set_guard_scope(g && loop_bodies.contains(g->get_gid()) ? Cprop::whole_graph() : Cprop::Node_scope{});
  };

  // Graph bodies are independently owned; cprop neither reads nor mutates a
  // sibling graph.  Large generated designs contain thousands of modules, so
  // walking them serially leaves most of the machine idle.  Keep publication
  // order untouched and only fan out the in-place per-body transform.  The
  // pass returns after every worker joins, before formal/save can observe the
  // graphs.  A Cprop instance is worker-local because it carries the current
  // graph while transforming it.
  std::atomic<size_t> next{0};
  const size_t        hw = std::max<size_t>(1, std::thread::hardware_concurrency());
  const size_t        nw = std::min({var.graphs.size(), hw, size_t{16}});
  if (nw <= 1) {
    Cprop cp{low_lanes};
    for (const auto& g : var.graphs) {
      scoped(cp, g);
      cp.do_trans(g);
    }
    hoist_after(var);
    return;
  }

  std::vector<std::exception_ptr> errors(var.graphs.size());
  // Serially, a throwing do_trans aborted the pass on the spot. Keep that: once
  // any worker records a failure the others stop claiming new graphs instead of
  // piling cascading diagnostics on a design that is already going to fail.
  //
  // Big-stack workers (livehd::run_workers), not std::thread: a worker's
  // default stack is 512 KiB on macOS, and cprop's recursive cone walks are as
  // deep as the design makes them.
  std::atomic<bool>               failed{false};
  livehd::run_workers(nw, [&](size_t) {
    Cprop cp{low_lanes};
    while (!failed.load(std::memory_order_relaxed)) {
      const size_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= var.graphs.size()) {
        break;
      }
      try {
        scoped(cp, var.graphs[i]);
        cp.do_trans(var.graphs[i]);
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
  hoist_after(var);
}
