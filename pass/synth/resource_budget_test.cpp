// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "resource_budget.hpp"

#include <limits>

#include "gtest/gtest.h"

namespace livehd::synth {
TEST(ResourceBudget, PhaseSamplesUseSimultaneousFootprintsAndRetainMissingReadings) {
  Resource_observation phase;
  EXPECT_EQ(phase.sample(100, 40), 140);
  EXPECT_EQ(phase.sample(10, 80), 90);
  EXPECT_EQ(phase.peak_bytes, 140);  // not 100 + 80 from different moments
  EXPECT_EQ(phase.worker_peak_bytes, 80);
  EXPECT_EQ(phase.sample(0), 0);
  EXPECT_EQ(phase.sample(10, 0), 10);
  EXPECT_EQ(phase.samples, 4);
  EXPECT_EQ(phase.parent_unavailable, 1);
  EXPECT_EQ(phase.worker_samples, 3);
  EXPECT_EQ(phase.worker_unavailable, 1);
  EXPECT_EQ(phase.sample(std::numeric_limits<uint64_t>::max(), 1), std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(phase.peak_bytes, std::numeric_limits<uint64_t>::max());
}
TEST(ResourceBudget, SharesBaselineTimeAndStopsPermanently) {
  Resource_budget b;
  b.time_limit_ms = 10;
  EXPECT_TRUE(b.admit(9, 100));
  EXPECT_FALSE(b.admit(10, 150));
  EXPECT_EQ(b.reason, "region wall-time budget exhausted");
  EXPECT_FALSE(b.admit(0, 1));
  EXPECT_EQ(b.elapsed_ms, 10);
  EXPECT_EQ(b.peak_bytes, 150);
}
TEST(ResourceBudget, GrowthAndProcessCeilingsAreDistinct) {
  Resource_budget b;
  b.entry_bytes         = 1000;
  b.growth_limit_bytes  = 100;
  b.process_limit_bytes = 2000;
  EXPECT_TRUE(b.admit(1, 1100));
  EXPECT_FALSE(b.admit(2, 1101));
  EXPECT_EQ(b.reason, "region memory-growth budget exhausted");
  Resource_budget absolute;
  absolute.entry_bytes         = 1999;
  absolute.growth_limit_bytes  = 100;
  absolute.process_limit_bytes = 2000;
  EXPECT_FALSE(absolute.admit(0, 2001));
  EXPECT_EQ(absolute.reason, "process physical-memory budget exhausted");
}
TEST(ResourceBudget, MissingSamplesDoNotInventGrowth) {
  Resource_budget b;
  b.growth_limit_bytes = 1;
  EXPECT_TRUE(b.admit(0, 1000));
  EXPECT_TRUE(b.admit(1, 0));
  EXPECT_EQ(b.samples, 2);
  EXPECT_EQ(b.peak_bytes, 1000);
}
}  // namespace livehd::synth
