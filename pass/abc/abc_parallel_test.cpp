// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_parallel.hpp"

#include <limits>

#include "gtest/gtest.h"

using namespace livehd::abc;

TEST(AbcParallel, CpuLimit) {
  EXPECT_EQ(synthesis_thread_limit(0, 12), 12U);
  EXPECT_EQ(synthesis_thread_limit(3, 12), 3U);
  EXPECT_EQ(synthesis_thread_limit(30, 12), 12U);
  EXPECT_EQ(synthesis_thread_limit(0, 0), 1U);
  EXPECT_EQ(synthesis_thread_limit(1, 12), 1U);
}

TEST(AbcParallel, ReservesWorkersBeforeTheyAllocate) {
  // A 64-unit machine allows strictly less than 32 units. The already
  // admitted workers have not allocated their projected 20 units yet.
  EXPECT_TRUE(admit_abc_worker(32, 4, 4, 20, 7));
  EXPECT_FALSE(admit_abc_worker(32, 4, 4, 20, 8));
  EXPECT_FALSE(admit_abc_worker(32, 4, 4, 20, 12));
}

TEST(AbcParallel, ActualMemoryDominatesAnUnderestimate) {
  EXPECT_TRUE(admit_abc_worker(32, 25, 4, 10, 6));
  EXPECT_FALSE(admit_abc_worker(32, 25, 4, 10, 7));
  EXPECT_FALSE(admit_abc_worker(32, 33, 4, 10, 1));
  EXPECT_TRUE(admit_abc_worker(32, 10, 4, 0, 20));
}

TEST(AbcParallel, UnknownMemoryAndOverflowCannotAdmit) {
  const auto max = std::numeric_limits<uint64_t>::max();
  EXPECT_FALSE(admit_abc_worker(0, 10, 4, 0, 2));
  EXPECT_FALSE(admit_abc_worker(32, 0, 0, 0, 2));
  EXPECT_FALSE(admit_abc_worker(max, 4, max - 2, 5, 1));
  EXPECT_FALSE(admit_abc_worker(32, 4, 4, 0, max));
  EXPECT_EQ(projected_abc_memory(max, max), max);
  EXPECT_GT(projected_abc_memory(100, 100), projected_abc_memory(1, 100));
  EXPECT_GT(projected_abc_memory(100, 100), projected_abc_memory(100, 0));
}
