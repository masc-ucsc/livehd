// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "boundary_pins.hpp"

#include <numeric>

#include "gtest/gtest.h"
#include "place.hpp"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
dp::Design fixture() {
  dp::Design d;
  d.layout             = {0, 0, 100, 100, 1, 1};
  d.num_movable        = 2;
  d.node_x             = {10, 90, 0};
  d.node_y             = {20, 20, 80};
  d.node_size_x        = {1, 1, 0};
  d.node_size_y        = {1, 1, 0};
  d.pin2node           = {0, 1};
  d.pin_offset_x       = {0, 0};
  d.pin_offset_y       = {0, 0};
  d.flat_net2pin       = {0, 1};
  d.flat_net2pin_start = {0, 2};
  d.net_weights        = {3};
  d.build_maps();
  return d;
}
const auto    boxes = Region_mesh::from_rects({
    { 0, 0,  50, 100, 1},
    {50, 0, 100, 100, 1}
});
Busorder_tree rsmt(const std::vector<Busorder_point>& points) {
  const auto    tree = steiner_tree(points);
  Busorder_tree edges;
  for (auto [a, b] : tree.edges) {
    edges.emplace_back(tree.points[a], tree.points[b]);
  }
  return edges;
}
}  // namespace

TEST(BoundaryPins, SplitsAtTheOriginalCrossingWithOneSharedTransparentTerminal) {
  const auto original = fixture();
  const auto model    = split_boundary_nets(original, boxes, rsmt);
  ASSERT_EQ(model.anchors.size(), 1u);
  EXPECT_EQ(model.split_nets, 1u);
  const auto pin = model.anchors.front();
  EXPECT_EQ(pin.x, 50);
  EXPECT_EQ(pin.y, 20);
  EXPECT_GE(pin.node, model.design.num_movable);
  EXPECT_EQ(model.design.node_size_x[pin.node], 0);
  EXPECT_EQ(model.design.node_size_y[pin.node], 0);
  EXPECT_EQ(model.design.net_weights, (std::vector<double>{3, 3}));
  EXPECT_EQ(model.parent_net, (std::vector<int>{0, 0}));
  EXPECT_EQ(model.design.flat_net2pin_start, (std::vector<int>{0, 2, 4}));
  EXPECT_EQ(std::count(model.design.pin2node.begin(), model.design.pin2node.end(), pin.node), 2);
  for (int node : {0, 1}) {
    EXPECT_EQ(std::count(model.design.pin2node.begin(), model.design.pin2node.end(), node), 1);
  }
  EXPECT_EQ(original.pin2node, (std::vector<int>{0, 1}));
}
TEST(BoundaryPins, MaskedControlNetworksAreNeverSplit) {
  auto d           = fixture();
  d.net_mask[0]    = 0;
  const auto model = split_boundary_nets(d, boxes, rsmt);
  EXPECT_TRUE(model.anchors.empty());
  EXPECT_EQ(model.design.pin2node, d.pin2node);
  EXPECT_EQ(model.design.net_mask, d.net_mask);
}
TEST(BoundaryPins, SeparateReentriesRemainSeparateLocalPieces) {
  auto d          = fixture();
  d.node_y[1]     = 80;
  const auto maze = [](const std::vector<Busorder_point>&) {
    return Busorder_tree{
        {{10, 20}, {80, 20}},
        {{80, 20}, {80, 50}},
        {{80, 50}, {20, 50}},
        {{20, 50}, {20, 80}},
        {{20, 80}, {90, 80}}
    };
  };
  const auto model = split_boundary_nets(d, boxes, maze);
  EXPECT_EQ(model.anchors.size(), 3u);
  // The two disjoint left-hand pieces must not be merged just because they
  // belong to one rectangle and one original net.
  EXPECT_EQ(model.design.net_weights.size(), 4u);
  EXPECT_EQ(model.design.pin2node.size(), 8u);
  for (int node : {0, 1}) {
    EXPECT_EQ(std::count(model.design.pin2node.begin(), model.design.pin2node.end(), node), 1);
  }
}
TEST(BoundaryPins, HandlesExteriorIOAndExactBoundaryEndpoints) {
  auto d           = fixture();
  d.num_movable    = 1;
  d.node_x[1]      = 110;
  const auto model = split_boundary_nets(d, boxes, rsmt);
  ASSERT_EQ(model.anchors.size(), 2u);
  EXPECT_EQ(model.design.net_weights.size(), 3u);
  auto on_edge          = fixture();
  on_edge.node_x[0]     = 50;
  const auto edge_model = split_boundary_nets(on_edge, boxes, rsmt);
  EXPECT_TRUE(edge_model.anchors.empty());
  EXPECT_EQ(edge_model.design.pin2node, on_edge.pin2node);
}
TEST(BoundaryPins, OnlyRealCellCoordinatesLeaveTheTemporaryModel) {
  auto                 d        = fixture();
  const auto           original = d;
  Boundary_pin_options policy;
  policy.enabled             = true;
  policy.iterations          = 20;
  policy.max_displacement_um = 5;
  Boundary_pins phase(boxes, rsmt, 1, policy);
  dp::Options   options;
  options.bins_x = options.bins_y = 4;
  options.add_fillers             = false;
  phase.refine(d, options);
  EXPECT_NE(d.node_x, original.node_x);
  EXPECT_EQ(d.node_x.size(), original.node_x.size());
  EXPECT_EQ(d.pin2node, original.pin2node);
  EXPECT_EQ(d.flat_net2pin_start, original.flat_net2pin_start);
  EXPECT_EQ(d.net_weights, original.net_weights);
  EXPECT_EQ(d.node_x[2], original.node_x[2]);
  EXPECT_EQ(d.node_y[2], original.node_y[2]);
  for (int i = 0; i < 2; ++i) {
    EXPECT_LE(std::abs(d.node_x[i] - original.node_x[i]), 5.000001);
    EXPECT_LE(std::abs(d.node_y[i] - original.node_y[i]), 5.000001);
  }
  EXPECT_TRUE(d.node_boxes.empty());
  EXPECT_TRUE(d.order_edges.empty());
  EXPECT_EQ(phase.samples().back().iteration, 20u);
  options.max_iterations = 0;
  EXPECT_TRUE(dp::place(d, options).legal);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(BoundaryPins, OrdersBusOnItsOriginalEdgeWithoutMovingRealCells) {
  auto d        = fixture();
  d.num_movable = 6;
  d.node_x      = {10, 90, 10, 90, 10, 90};
  d.node_y      = {20, 20, 80, 80, 50, 50};
  d.node_size_x.assign(6, 1);
  d.node_size_y.assign(6, 1);
  d.pin2node = {0, 1, 2, 3, 4, 5};
  d.pin_offset_x.assign(6, 0);
  d.pin_offset_y.assign(6, 0);
  d.flat_net2pin       = {0, 1, 2, 3, 4, 5};
  d.flat_net2pin_start = {0, 2, 4, 6};
  d.net_weights        = {1, 2, 3};
  d.net_mask.clear();
  d.build_maps();
  auto       model = split_boundary_nets(d, boxes, rsmt);
  const auto stats = order_boundary_pins(model,
                                         boxes,
                                         {
                                             {"data",  1},
                                             {"data",  2},
                                             {"data", 20}
  });
  EXPECT_EQ(stats.groups, 1u);
  EXPECT_EQ(stats.unordered_before, 1u);
  EXPECT_EQ(stats.unordered_after, 0u);
  EXPECT_GT(stats.moved, 0u);
  EXPECT_EQ(stats.capacity_edges, 0u);
  bool up = true, down = true;
  for (size_t i = 0; i < model.anchors.size(); ++i) {
    const auto& p = model.anchors[i];
    EXPECT_EQ(p.x, 50);
    EXPECT_GT(p.y, 0);
    EXPECT_LT(p.y, 100);
    if (i) {
      up   &= model.anchors[i - 1].y < p.y;
      down &= model.anchors[i - 1].y > p.y;
    }
  }
  EXPECT_TRUE(up || down);
  EXPECT_TRUE(model.design.order_edges.empty());
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(model.design.node_x[i], d.node_x[i]);
    EXPECT_EQ(model.design.node_y[i], d.node_y[i]);
  }
  auto crowded                     = split_boundary_nets(d, boxes, rsmt);
  crowded.design.layout.site_width = 50;
  const auto saved                 = crowded.design.node_y;
  const auto failed                = order_boundary_pins(crowded,
                                          boxes,
                                                         {
                                              {"data",  1},
                                              {"data",  2},
                                              {"data", 20}
  });
  EXPECT_EQ(failed.capacity_edges, 1u);
  EXPECT_EQ(failed.unordered_after, 1u);
  EXPECT_EQ(crowded.design.node_y, saved);
}
TEST(BoundaryPins, OrdersPinsAlongSharedTJunctionEdges) {
  auto d        = fixture();
  d.num_movable = 6;
  d.node_x      = {10, 90, 10, 90, 10, 90};
  d.node_y      = {20, 20, 40, 40, 30, 30};
  d.node_size_x.assign(6, 1);
  d.node_size_y.assign(6, 1);
  d.pin2node = {0, 1, 2, 3, 4, 5};
  d.pin_offset_x.assign(6, 0);
  d.pin_offset_y.assign(6, 0);
  d.flat_net2pin       = {0, 1, 2, 3, 4, 5};
  d.flat_net2pin_start = {0, 2, 4, 6};
  d.net_weights        = {1, 2, 3};
  d.net_mask.clear();
  d.build_maps();
  const auto mesh  = Region_mesh::from_rects({
      { 0,  0,  50, 100},
      {50,  0, 100,  50},
      {50, 50, 100, 100}
  });
  auto model = split_boundary_nets(d, mesh, rsmt);
  ASSERT_EQ(model.anchors.size(), 3u);
  const auto stats = order_boundary_pins(model,
                                         mesh,
                                         {
                                             {"data",  1},
                                             {"data",  2},
                                             {"data", 20}
  });
  EXPECT_EQ(stats.corner_pins, 0u);
  EXPECT_EQ(stats.groups, 1u);
  EXPECT_EQ(stats.unordered_before, 1u);
  EXPECT_EQ(stats.unordered_after, 0u);
  for (const auto& p : model.anchors) {
    EXPECT_GE(p.edge, 0);
    EXPECT_EQ(p.x, 50);
    EXPECT_GT(p.y, 0);
    EXPECT_LT(p.y, 50);
  }
}
TEST(BoundaryPins, AvoidsMacroInteriorsAlongTheOriginalEdge) {
  auto d           = fixture();
  // This edge crosses a macro because mesh pruning removed its horizontal cuts.
  d.node_x[2]      = 40;
  d.node_y[2]      = 10;
  d.node_size_x[2] = 20;
  d.node_size_y[2] = 30;
  d.build_maps();
  auto       model = split_boundary_nets(d, boxes, rsmt);
  const auto stats = order_boundary_pins(model,
                                         boxes,
                                         {
                                             {"data", 0}
  },
                                         {2});
  ASSERT_EQ(model.anchors.size(), 1u);
  EXPECT_EQ(stats.macro_pins_before, 1u);
  EXPECT_EQ(stats.macro_pins_after, 0u);
  EXPECT_EQ(stats.capacity_edges, 0u);
  EXPECT_EQ(model.anchors[0].x, 50);
  EXPECT_TRUE(model.anchors[0].y <= 10 || model.anchors[0].y >= 40);
  // A fully obstructed edge has no valid assignment; retain and report it.
  d.node_y[2]        = 0;
  d.node_size_y[2]   = 100;
  model              = split_boundary_nets(d, boxes, rsmt);
  const auto blocked = order_boundary_pins(model,
                                           boxes,
                                           {
                                               {"data", 0}
  },
                                           {2});
  EXPECT_EQ(blocked.capacity_edges, 1u);
  EXPECT_EQ(blocked.macro_pins_after, 1u);
  EXPECT_EQ(model.anchors[0].y, 20);
}
TEST(BoundaryPins, RepeatedBitCrossingsRemainFixedAndAreReported) {
  auto d          = fixture();
  d.node_y[1]     = 80;
  const auto maze = [](const std::vector<Busorder_point>&) {
    return Busorder_tree{
        {{10, 20}, {80, 20}},
        {{80, 20}, {80, 50}},
        {{80, 50}, {20, 50}},
        {{20, 50}, {20, 80}},
        {{20, 80}, {90, 80}}
    };
  };
  auto       model = split_boundary_nets(d, boxes, maze);
  const auto saved = model.design.node_y;
  const auto stats = order_boundary_pins(model,
                                         boxes,
                                         {
                                             {"data", 3}
  });
  EXPECT_EQ(stats.ambiguous_groups, 1u);
  EXPECT_EQ(stats.moved, 0u);
  EXPECT_EQ(model.design.node_y, saved);
}
}  // namespace livehd::placement
