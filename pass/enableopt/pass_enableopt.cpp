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
  const auto hw      = std::max<size_t>(1, std::thread::hardware_concurrency());
  const auto count   = var.graphs.size();
  const auto workers = std::min({count, hw, size_t{16}});
  livehd::parallel_for_each(count, workers, [&] {
    return [&, cp = Enableopt{}](size_t i) mutable { cp.do_trans(var.graphs[i]); };
  });
}
