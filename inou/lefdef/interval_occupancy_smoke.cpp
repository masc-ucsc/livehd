// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <limits>
#include <random>

#include "gtest/gtest.h"
#include "interval_occupancy.hpp"

namespace livehd::physical {
TEST(IntervalOccupancy, BridgesIntervalsAndPreservesGapsAndCoordinateLimits) {
  Interval_occupancy index;
  index.insert(10, 20);
  index.insert(30, 40);
  EXPECT_FALSE(index.intersects(21, 29));
  index.insert(21, 29);
  EXPECT_TRUE(index.intersects(22, 28));
  EXPECT_FALSE(index.intersects(41, 65535));
  index.insert(65534, 65536);  // Promote 16-bit storage without losing existing runs.
  EXPECT_TRUE(index.intersects(15, 15));
  EXPECT_TRUE(index.intersects(65535, 65536));
  EXPECT_FALSE(index.intersects(65537, 65538));
  index.insert(UINT32_MAX, UINT32_MAX);
  index.insert(0, 0);
  index.compact(uint64_t{1} << 32);
  EXPECT_FALSE(index.dense());
  EXPECT_FALSE(index.intersects(65537, UINT32_MAX - 1));
  EXPECT_TRUE(index.intersects(UINT32_MAX, UINT32_MAX));
  index.insert(1, UINT32_MAX - 1);
  EXPECT_TRUE(index.intersects(0, UINT32_MAX));
}
TEST(IntervalOccupancy, DenseWordsPreserveBoundaryGapsAndCopyIsolation) {
  Interval_occupancy index;
  for (uint32_t i = 0; i < 256; i += 2) {
    index.insert(i, i);
  }
  index.compact(256);
  ASSERT_TRUE(index.dense());
  for (uint32_t i = 0; i < 256; ++i) {
    EXPECT_EQ(index.intersects(i, i), i % 2 == 0);
  }
  auto copy = index;
  copy.insert(63, 193);
  for (uint32_t i = 63; i <= 193; ++i) {
    EXPECT_TRUE(copy.intersects(i, i));
  }
  EXPECT_FALSE(index.intersects(63, 63));
  EXPECT_FALSE(copy.intersects(195, 195));
  EXPECT_FALSE(copy.intersects(255, 255));
  copy.insert(255, 255);
  EXPECT_TRUE(copy.intersects(255, 255));
}
TEST(IntervalOccupancy, RandomIntervalsMatchIndependentBitOracleBeforeAndAfterCompaction) {
  std::mt19937 random(713);
  for (uint32_t extent : {256U, 4096U, 65536U, 131072U}) {
    Interval_occupancy index;
    std::vector<bool>  expected(extent);
    for (unsigned i = 0; i < 2000; ++i) {
      uint32_t a = random() % extent, b = std::min(extent - 1, a + uint32_t(random() % 12));
      index.insert(a, b);
      std::fill(expected.begin() + a, expected.begin() + b + 1, true);
      if (i == 200) {
        index.compact(extent);
      }
      for (unsigned q = 0; q < 10; ++q) {
        a = random() % extent;
        b = std::min(extent - 1, a + uint32_t(random() % 32));
        ASSERT_EQ(index.intersects(a, b),
                  std::any_of(expected.begin() + a, expected.begin() + b + 1, [](bool bit) { return bit; }));
      }
    }
  }
}
}  // namespace livehd::physical
