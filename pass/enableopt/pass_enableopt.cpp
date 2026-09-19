//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass_enableopt.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <thread>
#include <vector>

#include "enableopt.hpp"
#include "worker_pool.hpp"  // livehd::run_workers (big-stack workers)

static Pass_plugin sample("pass_enableopt", Pass_enableopt::setup);

void Pass_enableopt::setup() {
  Eprp_method m1("pass.enableopt", "state-context enable optimization", &Pass_enableopt::optimize);

  register_pass(m1);
}

Pass_enableopt::Pass_enableopt(const Eprp_var& var) : Pass("pass.enableopt", var) {}

void Pass_enableopt::optimize(Eprp_var& var) {
  Pass_enableopt pcp(var);

  // Each worker owns a graph body and all analysis is invocation-local.
  std::atomic<size_t> next{0};
  const size_t        hw = std::max<size_t>(1, std::thread::hardware_concurrency());
  const size_t        nw = std::min({var.graphs.size(), hw, size_t{16}});
  if (nw <= 1) {
    Enableopt cp;
    for (const auto& g : var.graphs) {
      cp.do_trans(g);
    }
    return;
  }

  std::vector<std::exception_ptr> errors(var.graphs.size());
  // Stop claiming graphs after the first failure and rethrow after joining.
  std::atomic<bool>               failed{false};
  livehd::run_workers(nw, [&](size_t) {
    Enableopt cp;
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
