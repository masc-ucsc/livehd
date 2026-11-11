// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cmath>

#include "gtest/gtest.h"
#include "wire.hpp"

using namespace livehd::timing::fast;

TEST(Elmore, SingleSegment) {
  // 1 kΩ into 10 fF: delay 10 ps; beta = 1 * (10 * 10) = 100, impulse = 200 - 100 = 100 ps^2.
  Elmore                solver;
  const std::vector<float>     cap{0, 10};
  const std::vector<Wire_edge> edges{{0, 1, 1000}};
  ASSERT_TRUE(solver.solve(cap, edges));
  EXPECT_FLOAT_EQ(solver.total_cap_ff(), 10);
  EXPECT_NEAR(solver.delay_ps(1), 10, 1e-4);
  EXPECT_NEAR(solver.impulse_ps2(1), 100, 1e-3);
  EXPECT_FLOAT_EQ(solver.delay_ps(0), 0);
}

TEST(Elmore, ChainAndBranch) {
  // 0 -R1(1k)- 1(C=2) -R2(2k)- 2(C=3), and 1 -R3(4k)- 3(C=5): loads 10 at 1, 3 at 2, 5 at 3.
  Elmore                       solver;
  const std::vector<float>     cap{0, 2, 3, 5};
  const std::vector<Wire_edge> edges{{0, 1, 1000}, {1, 2, 2000}, {3, 1, 4000}};  // an edge may point to the root side
  ASSERT_TRUE(solver.solve(cap, edges));
  EXPECT_NEAR(solver.total_cap_ff(), 10, 1e-5);
  EXPECT_NEAR(solver.delay_ps(1), 10, 1e-4);          // 1k * 10 fF
  EXPECT_NEAR(solver.delay_ps(2), 10 + 2 * 3, 1e-4);  // + 2k * 3 fF
  EXPECT_NEAR(solver.delay_ps(3), 10 + 4 * 5, 1e-4);  // + 4k * 5 fF
  // beta(1) = 1k * (2*10 + 3*16 + 5*30) fF*ps = 218; impulse(1) = 436 - 100.
  EXPECT_NEAR(solver.impulse_ps2(1), 2 * 218 - 100, 1e-2);
  EXPECT_EQ(solver.load_ff(1), 10);
}

TEST(Elmore, RootNotZeroAndRejects) {
  Elmore                       solver;
  const std::vector<float>     cap{4, 0, 6};
  const std::vector<Wire_edge> chain{{0, 1, 500}, {1, 2, 500}};
  ASSERT_TRUE(solver.solve(cap, chain, 1));  // the driver is the middle node
  EXPECT_NEAR(solver.delay_ps(0), 0.5f * 4, 1e-5);
  EXPECT_NEAR(solver.delay_ps(2), 0.5f * 6, 1e-5);
  const std::vector<Wire_edge> cycle{{0, 1, 1}, {1, 0, 1}};  // two nodes joined twice: not a tree
  EXPECT_FALSE(solver.solve(std::vector<float>{1, 1, 1}, cycle));
  const std::vector<Wire_edge> detached{{0, 1, 1}, {0, 1, 1}};  // node 2 unreachable
  EXPECT_FALSE(solver.solve(std::vector<float>{1, 1, 1}, detached));
  EXPECT_TRUE(solver.solve(std::vector<float>{}, {}));
}
