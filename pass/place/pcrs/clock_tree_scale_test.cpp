// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <cmath>

#include "clock_tree.hpp"
#include "gtest/gtest.h"

namespace livehd::pcrs {
TEST(PcrsClockScale, PlacedDomainsAtTenAndHundredThousandSinks) {
  const std::vector<Clock_driver> buffers{
      {"small", "A", "Y", .5, 25, 1, 90},
      {"large", "A", "Y",  1, 60, 2, 40}
  };
  Clock_limits limits;
  limits.directional_rc = std::array{40., .12, 48., .14};
  for (size_t n : {10000, 100000}) {
    std::vector<Clock_sink> sinks;
    sinks.reserve(n);
    const auto columns = static_cast<size_t>(std::ceil(std::sqrt(n)));
    for (size_t i = 0; i < n; ++i) {
      sinks.push_back({std::to_string(i), double(i % columns), double(i / columns), .4 + .1 * (i % 4)});
    }
    const auto   start   = std::chrono::steady_clock::now();
    const auto   tree    = plan_clock_tree(sinks, buffers, limits, Clock_source{});
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    RecordProperty("seconds_" + std::to_string(n), std::to_string(seconds));
    EXPECT_LT(seconds, n == 10000 ? 60 : 300);
    ASSERT_EQ(tree.arrival_ps.size(), n);
    std::vector<unsigned> visits(n);
    for (const auto& branch : tree.branches) {
      ASSERT_LE(branch.children.size(), limits.fanout);
      if (branch.leaf) {
        for (const auto id : branch.children) {
          ++visits.at(id);
        }
      }
    }
    for (size_t i = 0; i < n; ++i) {
      ASSERT_EQ(visits[i], 1u);
      ASSERT_TRUE(std::isfinite(tree.arrival_ps[i]));
      ASSERT_GT(tree.arrival_ps[i], 0);
    }
  }
}
}  // namespace livehd::pcrs
