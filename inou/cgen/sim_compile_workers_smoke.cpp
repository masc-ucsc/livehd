// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <memory>
#include <stdexcept>

#include "gtest/gtest.h"
#include "sim_compile_workers.hpp"

class CompileWorkers : public ::testing::Test {
protected:
  livehd::Job_manager::Probe saved;
  void                       SetUp() override {
    auto& manager = livehd::Job_manager::global();
    saved         = manager.exchange_probe([] {
      return livehd::Host_resources{.cpus = 2, .load = 0, .ram = 16ull << 30, .available = 8ull << 30, .rss = 1ull << 20};
    });
    manager.configure(2);
  }
  void TearDown() override {
    livehd::Job_manager::global().exchange_probe(std::move(saved));
    livehd::Job_manager::global().configure(0);
  }
};

TEST_F(CompileWorkers, AnyCompletionReleasesNextJob) {
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

TEST_F(CompileWorkers, OneWorkerBoundsSubmissionAndSurvivesExceptions) {
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

TEST_F(CompileWorkers, MemoryAdmissionBlocksSubmissionBeforeQueueing) {
  auto& manager = livehd::Job_manager::global();
  manager.exchange_probe([] {
    return livehd::Host_resources{.cpus = 2, .load = 0, .ram = 128ull << 20, .available = 64ull << 20, .rss = 1ull << 20};
  });
  livehd::sim::Compile_workers workers(2);
  std::promise<void>           release;
  auto                         gate   = release.get_future().share();
  auto                         first  = workers.submit([gate] { gate.wait(); });
  auto                         submit = std::async(std::launch::async, [&] { return workers.submit([] { return 2; }); });
  EXPECT_EQ(submit.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
  release.set_value();
  first.get();
  EXPECT_EQ(submit.get().get(), 2);
}
TEST_F(CompileWorkers, WorkerReservationDoesNotBelongToSubmitter) {
  auto& manager = livehd::Job_manager::global();
  manager.configure(1);
  livehd::sim::Compile_workers workers(1);
  std::promise<void>           started, release;
  auto                         gate  = release.get_future().share();
  auto                         first = workers.submit([&started, gate] {
    started.set_value();
    gate.wait();
  });
  started.get_future().wait();
  {
    livehd::Job_manager::Yield yield(manager);
    EXPECT_EQ(manager.stats()[1].active, 1u);
    EXPECT_FALSE(manager.try_acquire(livehd::Job_class::formal_arm));
  }
  release.set_value();
  first.get();
}
