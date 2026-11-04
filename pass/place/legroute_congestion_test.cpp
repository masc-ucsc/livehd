// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "gtest/gtest.h"
#include "legroute_internal.hpp"

namespace livehd::placement {
TEST(LegrouteCongestion, DirectionalIncidentEdgesUseAdmittedMinimumAndPreserveZeroCapacity) {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  options.global_share = .95;
  lr::Design d{tech, fp, options};
  d.nets.resize(1);
  lr::Grid g;
  g.cols = g.rows = 2;
  g.tile_w        = 10;
  g.tile_h        = 20;
  lr::Global_routes routes;
  routes.trees.resize(1);
  routes.trees[0].routed = true;
  routes.trees[0].edges  = {
      {0, 1},
      {1, 3}
  };
  const std::vector<lr::Global_net> nets{
      {.net = 0, .endpoints = {0, 3}}
  };
  const auto maps = lr::route_congestion(d, g, routes, nets, {20, 10, 20, 10}, {0, 0, 0, 0});
  EXPECT_EQ(maps.limit_h, (std::vector<int>{9, 9, 0, 0}));
  EXPECT_EQ(maps.demand_h, (std::vector<size_t>{1, 0, 0, 0}));
  EXPECT_EQ(maps.horizontal, (std::vector<double>{1. / 9, 0, 1. / 9, 0}));
  EXPECT_EQ(maps.vertical, (std::vector<double>{0, 0, 2, 2}));
  EXPECT_EQ(maps.limit_v, (std::vector<int>{0, 0, 0, 0}));
}

TEST(LegrouteCongestion, MissingSearchLimitedRequestsCarrySeparateSpatialPenalty) {
  physical::Technology tech;
  physical::Floorplan  fp;
  Legroute_options     options;
  lr::Design           d{tech, fp, options};
  d.nets.resize(1);
  lr::Grid g;
  g.cols   = 3;
  g.rows   = 1;
  g.tile_w = g.tile_h = 10;
  lr::Global_routes routes;
  routes.trees.resize(1);
  routes.trees[0].search_limited = true;
  routes.unrouted = routes.search_limited = 1;
  const auto maps                         = lr::route_congestion(d,
                                                                 g,
                                                                 routes,
                                                                 {
                                                                     {.net = 0, .endpoints = {0, 1}}
  },
                                                                 {10, 10, 10},
                                                                 {10, 10, 10});
  EXPECT_EQ(maps.horizontal, (std::vector<double>{0, 0, 0}));
  EXPECT_GT(maps.unresolved_horizontal[0], 0);
  EXPECT_GT(maps.unresolved_horizontal[1], 0);
  EXPECT_EQ(maps.unresolved_horizontal[2], 0);
  EXPECT_EQ(maps.unrouted, 1u);
  EXPECT_EQ(maps.search_limited, 1u);
}

TEST(LegrouteCongestion, ResamplingPreservesPeaksAndRecordsDirectionalMeans) {
  Legroute_congestion maps;
  maps.cols = maps.rows = 2;
  maps.bounds           = {10, 20, 30, 60};
  maps.horizontal       = {1, 3, 2, 6};
  maps.vertical         = {2, 4, 6, 8};
  maps.pin_utilization = maps.unresolved_horizontal = maps.unresolved_vertical = std::vector<double>(4, 0);
  const auto whole = resample_legroute_congestion(maps, maps.bounds, 1, 1);
  EXPECT_DOUBLE_EQ(whole.horizontal[0], 6);
  EXPECT_DOUBLE_EQ(whole.mean_horizontal[0], 3);
  EXPECT_DOUBLE_EQ(whole.vertical[0], 8);
  EXPECT_DOUBLE_EQ(whole.mean_vertical[0], 5);
  const auto left = resample_legroute_congestion(maps, {10, 20, 20, 60}, 1, 2);
  EXPECT_EQ(left.horizontal, (std::vector<double>{1, 3}));
  const auto offset = resample_legroute_congestion(maps, {15, 30, 25, 50}, 1, 1);
  EXPECT_DOUBLE_EQ(offset.horizontal[0], 6);
  EXPECT_DOUBLE_EQ(offset.mean_horizontal[0], 3);
  EXPECT_TRUE(whole.demand_h.empty());
  maps.horizontal[0] = -1;
  EXPECT_THROW(resample_legroute_congestion(maps, maps.bounds, 2, 2), std::invalid_argument);
}
TEST(LegrouteCongestion, WideDestinationClampsIndicesBeforeIntegerConversion) {
  Legroute_congestion maps;
  maps.cols = maps.rows = 1;
  maps.bounds           = {0, 0, 1, 1};
  maps.horizontal       = {7};
  maps.vertical         = {3};
  maps.pin_utilization = maps.unresolved_horizontal = maps.unresolved_vertical = {0};
  const auto wide = resample_legroute_congestion(maps, {-1e12, -1e12, 1e12, 1e12}, 1, 1);
  EXPECT_EQ(wide.horizontal, (std::vector<double>{7}));
  EXPECT_EQ(wide.vertical, (std::vector<double>{3}));
  EXPECT_DOUBLE_EQ(wide.mean_horizontal[0], 7. / 4e24);
  const auto remote = resample_legroute_congestion(maps, {1e12, 1e12, 1e12 + 1, 1e12 + 1}, 1, 1);
  EXPECT_EQ(remote.horizontal, (std::vector<double>{0}));
  EXPECT_EQ(remote.vertical, (std::vector<double>{0}));
  EXPECT_THROW(resample_legroute_congestion(maps, {-1e308, 0, 1e308, 1}, 1, 1), std::invalid_argument);
}
}  // namespace livehd::placement
