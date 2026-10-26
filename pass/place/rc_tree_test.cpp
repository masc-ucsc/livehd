// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "rc_tree.hpp"

#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace late = livehd::placement::late;
namespace ph   = livehd::physical;

TEST(RcTree, ExactPiAndPerSinkElmoreForTwoNodes) {
  const ph::Rc_net net{
      "drv",
      {{"drv", 2}, {"load", 3}},
      {{0, 1, 5000}}
  };
  const auto result = late::analyze_rc_tree(net, 0);
  for (unsigned t = 0; t < 2; ++t) {
    EXPECT_EQ(result.total_cap_ff[t], 5);
    EXPECT_NEAR(result.pi[t].near_cap_ff, 2, 1e-12);
    EXPECT_NEAR(result.pi[t].far_cap_ff, 3, 1e-12);
    EXPECT_NEAR(result.pi[t].resistance_ohm, 5000, 1e-9);
    EXPECT_EQ(result.elmore_ps[0][t], 0);
    EXPECT_EQ(result.elmore_ps[1][t], 15);
  }
}
TEST(RcTree, BranchesMatchThreeAdmittanceMomentsWithoutLosingSinkDelays) {
  const ph::Rc_net net{
      "drv",
      {{"drv", 1}, {"a", 2}, {"b", 3}},
      {{0, 1, 5}, {0, 2, 7}}
  };
  const auto result = late::analyze_rc_tree(net, 0);
  const auto pi     = result.pi[0];
  EXPECT_NEAR(pi.near_cap_ff + pi.far_cap_ff, 6, 1e-12);
  EXPECT_NEAR(pi.resistance_ohm * pi.far_cap_ff * pi.far_cap_ff, 83, 1e-10);
  EXPECT_NEAR(pi.resistance_ohm * pi.resistance_ohm * pi.far_cap_ff * pi.far_cap_ff * pi.far_cap_ff, 1523, 1e-9);
  EXPECT_NEAR(result.elmore_ps[1][0], .010, 1e-12);
  EXPECT_NEAR(result.elmore_ps[2][0], .021, 1e-12);
}
TEST(RcTree, ExplicitTotalNodeCapsPreserveTransitionLoadsWithoutAddingWireTwice) {
  const ph::Rc_net net{
      "drv",
      {{"drv", 2}, {"load", 3}},
      {{0, 1, 1000}}
  };
  const auto result = late::analyze_rc_tree(net,
                                            0,
                                            {
                                                {2, 2},
                                                {7, 9}
  });
  EXPECT_EQ(result.total_cap_ff[0], 9);
  EXPECT_EQ(result.total_cap_ff[1], 11);
  EXPECT_EQ(result.elmore_ps[1][0], 7);
  EXPECT_EQ(result.elmore_ps[1][1], 9);
  EXPECT_NEAR(result.pi[0].near_cap_ff, 2, 1e-12);
  EXPECT_NEAR(result.pi[0].far_cap_ff, 7, 1e-12);
  EXPECT_NEAR(result.pi[1].far_cap_ff, 9, 1e-12);
}
TEST(RcTree, ZeroResistanceAndCapacitanceHaveDefinedLimitsAndCanBeRerooted) {
  ph::Rc_net net{
      "drv",
      {{"drv", 0}, {"load", 0}},
      {{0, 1, 0}}
  };
  const auto empty = late::analyze_rc_tree(net, 0);
  EXPECT_EQ(empty.pi[0].resistance_ohm, 0);
  EXPECT_EQ(empty.pi[0].near_cap_ff, 0);
  EXPECT_EQ(empty.elmore_ps[1][0], 0);
  net.nodes[0].capacitance_ff = 2;
  net.nodes[1].capacitance_ff = 3;
  const auto lumped           = late::analyze_rc_tree(net, 1);
  EXPECT_EQ(lumped.pi[0].near_cap_ff, 5);
  EXPECT_EQ(lumped.pi[0].far_cap_ff, 0);
  net.edges[0].resistance_ohm = 1000;
  const auto reversed         = late::analyze_rc_tree(net, 1);
  EXPECT_EQ(reversed.elmore_ps[0][0], 2);
  EXPECT_NEAR(reversed.pi[0].near_cap_ff, 3, 1e-12);
}
TEST(RcTree, RejectsCyclesMissingCapsAndNonfiniteValues) {
  ph::Rc_net net{
      "drv",
      {  {"drv", 1},  {"load", 1}},
      {{0, 1, 1000}, {1, 0, 1000}}
  };
  EXPECT_THROW(late::analyze_rc_tree(net, 0), std::invalid_argument);
  net.edges.pop_back();
  EXPECT_THROW(late::analyze_rc_tree(net, 2), std::invalid_argument);
  EXPECT_THROW(late::analyze_rc_tree(net,
                                     0,
                                     {
                                         {1, 1}
  }),
               std::invalid_argument);
  net.edges[0].resistance_ohm = std::numeric_limits<double>::infinity();
  EXPECT_THROW(late::analyze_rc_tree(net, 0), std::invalid_argument);
}
