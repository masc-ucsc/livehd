//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "worker_pool.hpp"

#include <atomic>
#include <stdexcept>
#include <vector>

#include "gtest/gtest.h"

TEST(WorkerPool, RunsEveryWorkerOnce) {
  constexpr size_t                 count = 4;
  std::vector<std::atomic<size_t>> visits(count);

  livehd::run_workers(count, [&](size_t worker) { ++visits[worker]; });

  for (const auto& visit : visits) {
    EXPECT_EQ(visit.load(), 1U);
  }
}

TEST(WorkerPool, AllowsNoWorkers) {
  bool called = false;
  livehd::run_workers(0, [&](size_t) { called = true; });
  EXPECT_FALSE(called);
}

TEST(WorkerPool, PropagatesWorkerExceptionAfterJoin) {
  EXPECT_THROW(livehd::run_workers(3,
                                   [](size_t worker) {
                                     if (worker == 1) {
                                       throw std::runtime_error("worker failed");
                                     }
                                   }),
               std::runtime_error);
}
