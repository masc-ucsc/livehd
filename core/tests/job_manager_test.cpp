// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "job_manager.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include "gtest/gtest.h"
using namespace livehd;
namespace {
Host_resources roomy() { return {.cpus = 8, .load = 0, .ram = 32ull << 30, .available = 24ull << 30, .rss = 1ull << 20}; }
TEST(JobManager, MixedClassesShareOneCeiling) {
  Job_manager manager(roomy);
  manager.configure(2);
  auto first = manager.acquire(Job_class::formal_arm), second = manager.acquire(Job_class::sim_compile);
  EXPECT_FALSE(manager.try_acquire(Job_class::usyn_region));
  first.complete();
  auto third = manager.try_acquire(Job_class::usyn_region);
  EXPECT_TRUE(third);
  EXPECT_EQ(manager.stats()[0].admitted, 1u);
}
TEST(JobManager, AlwaysOneAndMeasuredMemoryAdmission) {
  auto host      = roomy();
  host.ram       = 16ull << 20;
  host.available = 8ull << 20;
  Job_manager manager([&] { return host; });
  manager.configure(8);
  auto first = manager.acquire(Job_class::formal_arm, 1, 128ull << 20);
  EXPECT_TRUE(first);
  EXPECT_FALSE(manager.try_acquire(Job_class::sim_compile));
  EXPECT_GT(manager.stats()[1].memory_waits, 0u);
  first.complete();
  EXPECT_TRUE(manager.try_acquire(Job_class::sim_compile));
}
TEST(JobManager, OwnLoadDoesNotDrainCapacityTwice) {
  auto host = roomy();
  host.cpus = 2;
  Job_manager manager([&] { return host; });
  manager.configure(2);
  auto first = manager.acquire(Job_class::formal_arm);
  host.load  = 1;
  EXPECT_TRUE(manager.try_acquire(Job_class::sim_compile));
}
TEST(JobManager, MemoryPredictionLearnsAndWaitHasDeadline) {
  Job_manager manager(roomy);
  manager.configure(1);
  auto first  = manager.acquire(Job_class::formal_arm, 64);
  auto before = manager.stats()[0].bytes_per_unit;
  EXPECT_FALSE(manager.acquire(Job_class::sim_compile, 1, 0, Job_manager::Clock::now() + std::chrono::milliseconds(10)));
  first.complete(96ull << 20);
  EXPECT_GT(manager.stats()[0].bytes_per_unit, before);
}
TEST(JobManager, YieldPreventsNestedJobDeadlock) {
  Job_manager manager(roomy);
  manager.configure(1);
  auto outer = manager.acquire(Job_class::formal_arm);
  {
    Job_manager::Yield yield(manager);
    auto               inner = manager.try_acquire(Job_class::formal_arm);
    EXPECT_TRUE(inner);
  }
  EXPECT_EQ(manager.stats()[0].active, 1u);
}
TEST(JobManager, KilledChildSlotIsRecoveredByParent) {
  Job_manager manager(roomy);
  manager.configure(1);
  auto       slot  = manager.acquire(Job_class::formal_arm);
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    if (manager.try_acquire(Job_class::sim_compile)) {
      _exit(1);
    }
    pause();
    _exit(2);
  }
  slot.adopt(child);
  kill(child, SIGKILL);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  slot.complete();
  manager.reap_tree(child);
  EXPECT_TRUE(manager.try_acquire(Job_class::sim_compile));
}
TEST(JobManager, KilledModifierCannotWedgeSharedAdmission) {
  Job_manager manager(roomy);
  manager.configure(1);
  for (int attempt = 0; attempt < 8; ++attempt) {
    const auto child = fork();
    ASSERT_GE(child, 0);
    if (!child) {
      for (;;) {
        auto slot = manager.try_acquire(Job_class::formal_arm);
      }
    }
    usleep(2000);
    kill(child, SIGKILL);
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    manager.reap_tree(child);
    auto slot = manager.acquire(Job_class::sim_compile, 1, 0, Job_manager::Clock::now() + std::chrono::milliseconds(50));
    ASSERT_TRUE(slot);
    EXPECT_EQ(manager.stats()[0].active, 0u);
  }
}

TEST(JobManager, YieldDoesNotPausePeerProcessLeases) {
  Job_manager manager(roomy);
  manager.configure(2);
  auto       peer  = manager.acquire(Job_class::formal_arm);
  const auto child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    pause();
    _exit(0);
  }
  peer.adopt(child);
  auto own = manager.acquire(Job_class::sim_compile);
  {
    Job_manager::Yield yield(manager);
    EXPECT_EQ(manager.stats()[0].active, 1u);
    EXPECT_EQ(manager.stats()[1].active, 0u);
    auto nested = manager.try_acquire(Job_class::usyn_region);
    EXPECT_TRUE(nested);
  }
  EXPECT_EQ(manager.stats()[0].active, 1u);
  EXPECT_EQ(manager.stats()[1].active, 1u);
  kill(child, SIGKILL);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  peer.complete();
}

}  // namespace
