//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_cprop.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <thread>
#include <vector>

#include "cprop.hpp"
#include "worker_pool.hpp"  // livehd::run_workers (big-stack workers)

static Pass_plugin sample("pass_cprop", Pass_cprop::setup);

void Pass_cprop::setup() {
  Eprp_method m1("pass.cprop", "in-place copy propagation", &Pass_cprop::optimize);

  register_pass(m1);
}

Pass_cprop::Pass_cprop(const Eprp_var& var) : Pass("pass.cprop", var) {}

void Pass_cprop::optimize(Eprp_var& var) {
  Pass_cprop pcp(var);

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
    Cprop cp;
    for (const auto& g : var.graphs) {
      cp.do_trans(g);
    }
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
    Cprop cp;
    while (!failed.load(std::memory_order_relaxed)) {
      const size_t i = next.fetch_add(1, std::memory_order_relaxed);
      if (i >= var.graphs.size()) {
        break;
      }
      try {
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
}
