//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_aegraph.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "aegraph.hpp"
#include "cprop.hpp"
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

void Pass_aegraph::optimize(Eprp_var& var) {
  Pass_aegraph pa(var);
  const auto   low_lanes   = var.get("low_lanes", "true") != "false";
  const auto   loop_bodies = lifted_loop_bodies(var.graphs);

  // One graph: the e-graph engine, then the classic post steps over what it
  // elaborated. Graph bodies are independent, so the per-body work fans out
  // exactly like pass.cprop's; publication order is untouched.
  std::mutex                 stats_mutex;
  livehd::aegraph::Stats     total;
  const auto                 one = [&](Cprop& cp, const std::shared_ptr<hhds::Graph>& g) {
    if (!g) {
      return;
    }
    cp.set_guard_scope(loop_bodies.contains(g->get_gid()) ? Cprop::whole_graph() : Cprop::Node_scope{});
    const auto stats = livehd::aegraph::optimize(g, [&](const std::vector<hhds::Node_class>& cyclic) { cp.do_scalar(g, cyclic); });
    cp.do_post(g);
    const std::lock_guard<std::mutex> lock(stats_mutex);
    total += stats;
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
        "forward=%zu kept=%zu dead=%zu consts_created=%zu\n",
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
        total.kept_twin,
        total.dead_removed,
        total.consts_created);
  };

  std::atomic<size_t> next{0};
  const size_t        hw = std::max<size_t>(1, std::thread::hardware_concurrency());
  const size_t        nw = std::min({var.graphs.size(), hw, size_t{16}});
  if (nw <= 1) {
    Cprop cp{low_lanes};
    for (const auto& g : var.graphs) {
      one(cp, g);
    }
    summary();
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
        one(cp, var.graphs[i]);
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
  hoist_after();
}
