// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "clock_cluster.hpp"

#include <map>

#include "gtest/gtest.h"

namespace dp = DreamPlace::native;
using namespace livehd::placement;
namespace {
dp::Design fixture() {
  dp::Design d;
  d.layout      = {0, 0, 100, 100, 1, 10};
  d.num_movable = 4;
  d.node_x      = {10, 30, 70, 90};
  d.node_y.assign(4, 10);
  d.node_size_x.assign(4, 2);
  d.node_size_y.assign(4, 2);
  d.pin2node = {0, 2};
  d.pin_offset_x.assign(2, 1);
  d.pin_offset_y.assign(2, 1);
  d.flat_net2pin       = {0, 1};
  d.flat_net2pin_start = {0, 2};
  d.net_weights        = {4};
  d.net_mask           = {1};
  d.build_maps();
  return d;
}
TEST(ClockCluster, DistinctDomainsDataCriticalityAndFences) {
  auto d = fixture();
  d.node_boxes.assign(4, {0, 0, 98, 98});
  d.node_boxes[1].xl = 29;
  const auto step    = clock_cluster_step(d,
                                          {
                                           {0, 1},
                                           {2, 3}
  },
                                       .5,
                                       .8);
  ASSERT_EQ(step.moves.size(), 3);
  EXPECT_EQ(step.data_rejections, 1);
  EXPECT_EQ(step.groups, 2);
  EXPECT_DOUBLE_EQ(step.moves[0].x, 11.25);
  EXPECT_DOUBLE_EQ(step.moves[1].x, 29);
  EXPECT_EQ(step.moves[2].node, 3);
  EXPECT_DOUBLE_EQ(step.moves[2].x, 85);
  for (const auto& m : step.moves) {
    EXPECT_EQ(m.fix_iterations, 0);
  }
  EXPECT_TRUE(clock_cluster_step(d,
                                 {
                                     {0, 1}
  },
                                 0,
                                 .8)
                  .moves.empty());
}
TEST(ClockCluster, RejectsTwoLengthenedDataNetsForOneShorterClockArm) {
  auto d     = fixture();
  d.node_x   = {40, 60, 10, 15};
  d.pin2node = {2, 0, 3, 0};
  d.pin_offset_x.assign(4, 1);
  d.pin_offset_y.assign(4, 1);
  d.flat_net2pin       = {0, 1, 2, 3};
  d.flat_net2pin_start = {0, 2, 4};
  d.net_mask           = {1, 1};
  d.net_weights        = {1, 1};
  d.build_maps();
  const auto step = clock_cluster_step(d,
                                       {
                                           {0, 1}
  },
                                       .25,
                                       .8);
  ASSERT_EQ(step.moves.size(), 1);
  EXPECT_EQ(step.moves.front().node, 1);
  EXPECT_EQ(step.data_rejections, 1);
  d.net_mask.back() = 0;
  EXPECT_EQ(clock_cluster_step(d,
                               {
                                   {0, 1}
  },
                               .25,
                               .8)
                .moves.size(),
            2);
}

TEST(ClockCluster, CrowdedDestinationRejectsInflow) {
  auto d   = fixture();
  d.layout = {0, 0, 40, 10, 1, 1};
  d.node_x = {18.95, 33, 28, 35, 20};
  d.node_y.assign(5, 0);
  d.node_size_x = {1, 1, 1, 1, 20};
  d.node_size_y = {1, 1, 1, 1, 9.9};
  d.build_maps();
  const auto step = clock_cluster_step(d,
                                       {
                                           {0, 1},
                                           {2, 3}
  },
                                       1,
                                       .9);
  EXPECT_GT(step.congested, 0);
  for (const auto& move : step.moves) {
    EXPECT_NE(move.node, 0);
  }
  d.node_size_y.back() = 1;
  const auto free      = clock_cluster_step(d,
                                            {
                                           {0, 1},
                                           {2, 3}
  },
                                       1,
                                       .9);
  EXPECT_GT(free.moves.size(), step.moves.size());
}
TEST(ClockCluster, IncludesFixedPinHotspotsEvenWithFreeCellArea) {
  auto d   = fixture();
  d.layout = {0, 0, 40, 10, 1, 1};
  d.node_x = {18.4, 33, 28, 35, 21};
  d.node_y.assign(5, 0);
  d.node_size_x     = {1, 1, 1, 1, 0};
  d.node_size_y     = {1, 1, 1, 1, 0};
  d.net_mask[0]     = 0;
  d.pin_offset_x[0] = .8;
  d.build_maps();
  ASSERT_EQ(clock_cluster_step(d,
                               {
                                   {0, 1}
  },
                               1,
                               .8)
                .moves.size(),
            2);
  for (int k = 0; k < 100; ++k) {
    d.flat_net2pin.push_back(d.pin2node.size());
    d.pin2node.push_back(4);
    d.pin_offset_x.push_back(0);
    d.pin_offset_y.push_back(0);
  }
  d.flat_net2pin_start.back() = d.pin2node.size();
  d.build_maps();
  auto step = clock_cluster_step(d,
                                 {
                                     {0, 1}
  },
                                 1,
                                 .8);
  ASSERT_EQ(step.moves.size(), 1);
  EXPECT_EQ(step.moves.front().node, 1);
  EXPECT_EQ(step.congested, 1);
}
}  // namespace
