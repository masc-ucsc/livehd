// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "resource_budget.hpp"

#include <limits>

#include "gtest/gtest.h"

namespace livehd::usyn {
TEST(ResourceBudget, ObservationKeepsThePeakAndCountsMissingReadings) {
  Resource_observation phase;
  EXPECT_EQ(phase.sample(100), 100);
  EXPECT_EQ(phase.sample(10), 10);
  EXPECT_EQ(phase.sample(0), 0);
  EXPECT_EQ(phase.peak_bytes, 100);
  EXPECT_EQ(phase.samples, 3);
  EXPECT_EQ(phase.unavailable, 1);
  EXPECT_EQ(phase.sample(std::numeric_limits<uint64_t>::max()), std::numeric_limits<uint64_t>::max());
  EXPECT_EQ(phase.peak_bytes, std::numeric_limits<uint64_t>::max());
}
TEST(ResourceBudget, SharesBaselineAndStopsPermanently) {
  Resource_budget b;
  b.process_limit_bytes = 120;
  EXPECT_TRUE(b.admit(9, 100));
  EXPECT_FALSE(b.admit(10, 150));
  EXPECT_EQ(b.reason, "process physical-memory budget exhausted");
  EXPECT_FALSE(b.admit(0, 1));
  EXPECT_EQ(b.elapsed_ms, 10);
  EXPECT_EQ(b.peak_bytes, 150);
}
TEST(ResourceBudget, OnlyTheMeasuredProcessCeilingRefuses) {
  Resource_budget b;
  b.entry_bytes         = 1000;
  b.process_limit_bytes = 2000;
  EXPECT_TRUE(b.admit(1, 1999));  // large growth over entry is not a refusal
  EXPECT_FALSE(b.admit(2, 2001));
  EXPECT_EQ(b.reason, "process physical-memory budget exhausted");
}
TEST(ResourceBudget, MissingSamplesDoNotInventGrowth) {
  Resource_budget b;
  b.process_limit_bytes = 2000;
  EXPECT_TRUE(b.admit(0, 1000));
  EXPECT_TRUE(b.admit(1, 0));
  EXPECT_EQ(b.samples, 2);
  EXPECT_EQ(b.peak_bytes, 1000);
}
}  // namespace livehd::usyn
