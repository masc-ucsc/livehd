// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <memory>
#include <stdexcept>

#include "gtest/gtest.h"
#include "sim_compile_workers.hpp"

TEST(CompileWorkers, AnyCompletionReleasesNextJob) {
  livehd::sim::Compile_workers workers(2);
  std::promise<void>           release;
  auto                         gate   = release.get_future().share();
  auto                         first  = workers.submit([gate] { gate.wait(); });
  auto                         second = workers.submit([] { return 2; });
  EXPECT_EQ(second.get(), 2);
  // If submit waits for the first worker instead of ANY completion, this
  // launch cannot finish until the gate opens. Always open it before joining.
  auto next
      = std::async(std::launch::async, [&] { return workers.submit([value = std::make_unique<int>(3)] { return *value; }).get(); });
  EXPECT_EQ(next.wait_for(std::chrono::seconds(2)), std::future_status::ready);
  release.set_value();
  first.get();
  EXPECT_EQ(next.get(), 3);
}

TEST(CompileWorkers, OneWorkerBoundsSubmissionAndSurvivesExceptions) {
  livehd::sim::Compile_workers workers(1);
  std::promise<void>           release;
  auto                         gate  = release.get_future().share();
  auto                         first = workers.submit([gate] { gate.wait(); });
  auto                         next  = std::async(std::launch::async, [&] { return workers.submit([] { return 7; }).get(); });
  EXPECT_EQ(next.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
  release.set_value();
  first.get();
  EXPECT_EQ(next.get(), 7);
  auto failed = workers.submit([] { throw std::runtime_error("worker failure"); });
  EXPECT_THROW(failed.get(), std::runtime_error);
  EXPECT_EQ(workers.submit([] { return 9; }).get(), 9);
}
