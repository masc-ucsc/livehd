// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "boundary_pins.hpp"
#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
// Driver -> left boundary -> middle rectangle -> right boundary -> sink.
// A middle cell optionally replaces the direct local wire with two nets.
struct Fixture {
  Boundary_pin_model         m;
  std::vector<Region_rect> mesh{
      { 0, 0,  30, 100, 0},
      {30, 0,  70, 100, 0},
      {70, 0, 100, 100, 0}
  };
  std::vector<Bus_bit> bits{
      {  "data", 0},
      {"result", 0}
  };
  explicit Fixture(bool cell = false) {
    auto& d              = m.design;
    d.layout             = {0, 0, 100, 100, 1, 1};
    d.num_movable        = 3;
    d.node_x             = {10, 50, 90};
    d.node_y             = {20, 50, 60};
    d.node_size_x        = {1, 1, 1};
    d.node_size_y        = {1, 1, 1};
    d.flat_net2pin_start = {0};
    anchor(30, 20, 0);
    anchor(70, 60, cell ? 1 : 0);
    net({0, 3}, 0, 0, 0);
    if (cell) {
      net({3, 1}, 1, 0, -1);
      net({1, 4}, 1, 1, 1);
    } else {
      net({3, 4}, 1, 0, -1);
    }
    net({4, 2}, 2, cell ? 1 : 0, -1);
    d.build_maps();
  }
  int anchor(double x, double y, int original) {
    auto& d    = m.design;
    int   node = d.node_x.size();
    d.node_x.push_back(x);
    d.node_y.push_back(y);
    d.node_size_x.push_back(0);
    d.node_size_y.push_back(0);
    m.anchors.push_back({node, original, x, y, -1});
    return node;
  }
  void net(std::initializer_list<int> nodes, int box, int original, int driver) {
    auto& d = m.design;
    for (int n : nodes) {
      d.flat_net2pin.push_back(d.pin2node.size());
      d.pin2node.push_back(n);
      d.pin_offset_x.push_back(0);
      d.pin_offset_y.push_back(0);
      m.pin_is_driver.push_back(n == driver);
    }
    d.flat_net2pin_start.push_back(d.pin2node.size());
    d.net_weights.push_back(1);
    d.net_mask.push_back(1);
    m.parent_net.push_back(original);
    m.piece_region.push_back(box);
  }
  Pin_align_stats run(const std::vector<int>& macros = {}) { return align_boundary_pins(m, mesh, bits, macros); }
};
}  // namespace
TEST(Pinalign, DirectWireKeepsInputWhileOneCellMovesBothVirtualPins) {
  for (bool cell : {false, true}) {
    Fixture    f(cell);
    const auto old = f.m.design;
    auto       s   = f.run();
    EXPECT_EQ(s.direct, cell ? 0u : 1u);
    EXPECT_EQ(s.one_cell, cell ? 1u : 0u);
    ASSERT_EQ(s.aligned, 1u);
    EXPECT_EQ(s.moved, cell ? 2u : 1u);
    EXPECT_EQ(s.already_aligned, 0u);
    EXPECT_EQ(f.m.anchors[0].y, cell ? 50 : 20);
    EXPECT_EQ(f.m.anchors[1].y, cell ? 50 : 20);
    EXPECT_EQ(f.m.anchors[1].x, 70);
    for (int n = 0; n < 3; ++n) {
      EXPECT_EQ(f.m.design.node_x[n], old.node_x[n]);
      EXPECT_EQ(f.m.design.node_y[n], old.node_y[n]);
    }
    EXPECT_EQ(f.m.design.pin2node, old.pin2node);
  }
}
TEST(Pinalign, DriverDirectionAndVerticalEdges) {
  for (bool vertical : {false, true}) {
    Fixture f;
    std::fill(f.m.pin_is_driver.begin(), f.m.pin_is_driver.end(), false);
    f.m.pin_is_driver.back() = true;
    if (vertical) {
      for (auto& r : f.mesh) {
        std::swap(r.xl, r.yl);
        std::swap(r.xh, r.yh);
      }
      for (auto& a : f.m.anchors) {
        std::swap(a.x, a.y);
      }
      std::swap(f.m.design.node_x, f.m.design.node_y);
    }
    auto s = f.run();
    ASSERT_EQ(s.aligned, 1u);
    EXPECT_EQ(s.pairs[0].input, 1);
    EXPECT_EQ(s.pairs[0].output, 0);
    EXPECT_EQ(vertical ? f.m.anchors[0].x : f.m.anchors[0].y, 60);
  }
}
TEST(Pinalign, TwoCellsAdjacentSidesAndControlsAreExcluded) {
  {
    Fixture f(true);
    // The incoming piece now touches another real cell, not just the middle cell.
    f.net({3, 2, 1}, 1, 0, -1);
    f.m.design.net_mask[1] = 0;
    EXPECT_EQ(f.run().aligned, 0u);
  }
  {
    Fixture f;
    f.m.anchors[1].x = 50;
    f.m.anchors[1].y = 100;
    EXPECT_EQ(f.run().direct, 0u);
  }
  {
    Fixture f;
    std::fill(f.m.design.net_mask.begin(), f.m.design.net_mask.end(), 0);
    EXPECT_TRUE(f.run().pairs.empty());
  }
}
TEST(Pinalign, BusOrderHasPriorityOverAlignment) {
  Fixture f;
  f.bits.push_back({"data", 1});
  // Output data[0] is above data[1]; moving it from 60 to 20 reverses that order.
  f.anchor(70, 40, 2);
  auto s = f.run();
  EXPECT_EQ(s.order_conflicts, 1u);
  EXPECT_EQ(s.aligned, 0u);
  EXPECT_EQ(f.m.anchors[1].y, 60);
}
TEST(Pinalign, SpacingRangeAndMacroConflictsLeavePinsUnchanged) {
  {
    Fixture f;
    f.anchor(70, 20, -1);
    EXPECT_EQ(f.run().spacing_conflicts, 1u);
    EXPECT_EQ(f.m.anchors[1].y, 60);
  }
  {
    Fixture f;
    f.m.anchors[0].y     = .5;
    f.m.design.node_y[3] = .5;
    EXPECT_EQ(f.run().range_conflicts, 1u);
    EXPECT_EQ(f.m.anchors[1].y, 60);
  }
  {
    Fixture f;
    auto&   d     = f.m.design;
    int     macro = d.node_x.size();
    d.node_x.push_back(65);
    d.node_y.push_back(15);
    d.node_size_x.push_back(10);
    d.node_size_y.push_back(10);
    EXPECT_EQ(f.run({macro}).macro_conflicts, 1u);
    EXPECT_EQ(f.m.anchors[1].y, 60);
  }
}
TEST(Pinalign, ConnectedChainIsAlignedAtomically) {
  Fixture f;
  f.mesh = {
      { 0, 0,  30, 100, 0},
      {30, 0,  70, 100, 0},
      {70, 0,  80, 100, 0},
      {80, 0, 100, 100, 0}
  };
  int last                   = f.anchor(80, 80, 0);
  f.m.design.net_mask.back() = 0;
  f.net({4, last}, 2, 0, -1);
  f.net({last, 2}, 3, 0, -1);
  auto s = f.run();
  ASSERT_EQ(s.aligned, 2u);
  EXPECT_EQ(f.m.anchors[1].y, 20);
  EXPECT_EQ(f.m.anchors[2].y, 20);
  Fixture blocked;
  blocked.mesh                     = f.mesh;
  last                             = blocked.anchor(80, 80, 0);
  blocked.m.design.net_mask.back() = 0;
  blocked.net({4, last}, 2, 0, -1);
  blocked.net({last, 2}, 3, 0, -1);
  blocked.anchor(80, 20, -1);
  auto b = blocked.run();
  EXPECT_EQ(b.spacing_conflicts, 2u);
  EXPECT_EQ(b.aligned, 0u);
  EXPECT_EQ(blocked.m.anchors[1].y, 60);
  EXPECT_EQ(blocked.m.anchors[2].y, 80);
}
TEST(Pinalign, ConflictingTargetsForSharedCellInputSkipTheWholeGroup) {
  Fixture f(true);
  // The same boundary pin fans into two distinct cell pins with different Y.
  f.net({3, 1}, 1, 0, -1);
  f.m.design.pin_offset_y.back() = .5;
  auto s                         = f.run();
  EXPECT_EQ(s.one_cell, 2u);
  EXPECT_EQ(s.ambiguous, 2u);
  EXPECT_EQ(s.aligned, 0u);
  EXPECT_EQ(f.m.anchors[0].y, 20);
  EXPECT_EQ(f.m.anchors[1].y, 60);
}
TEST(Pinalign, OneCellUsesDistinctOrientedPhysicalPinCoordinatesOnBothAxes) {
  for (bool vertical : {false, true}) {
    for (bool mirrored : {false, true}) {
      Fixture f(true);
      auto&   d        = f.m.design;
      d.node_size_x[1] = d.node_size_y[1] = 10;
      d.node_orientation.assign(d.node_x.size(), DreamPlace::native::Orientation::N);
      d.node_orientation[1] = mirrored ? DreamPlace::native::Orientation::FN : DreamPlace::native::Orientation::N;
      // Offsets are already orientation-transformed, as supplied by LiveHD.
      double input = mirrored ? 7.5 : 2.5, output = mirrored ? 2.5 : 7.5;
      d.pin_offset_y[3] = input;
      d.pin_offset_y[4] = output;
      if (vertical) {
        for (auto& r : f.mesh) {
          std::swap(r.xl, r.yl);
          std::swap(r.xh, r.yh);
        }
        for (auto& p : f.m.anchors) {
          std::swap(p.x, p.y);
        }
        std::swap(d.node_x, d.node_y);
        std::swap(d.pin_offset_x, d.pin_offset_y);
      }
      const auto old_x = d.node_x, old_y = d.node_y;
      auto       s = f.run();
      ASSERT_EQ(s.aligned, 1u);
      EXPECT_EQ(s.moved, 2u);
      EXPECT_EQ(vertical ? f.m.anchors[0].x : f.m.anchors[0].y, 50 + input);
      EXPECT_EQ(vertical ? f.m.anchors[1].x : f.m.anchors[1].y, 50 + output);
      EXPECT_EQ(vertical ? f.m.anchors[0].y : f.m.anchors[0].x, 30);
      EXPECT_EQ(vertical ? f.m.anchors[1].y : f.m.anchors[1].x, 70);
      EXPECT_EQ(s.pairs[0].input_cell_pin, 3);
      EXPECT_EQ(s.pairs[0].output_cell_pin, 4);
      for (int n = 0; n < 3; ++n) {
        EXPECT_EQ(d.node_x[n], old_x[n]);
        EXPECT_EQ(d.node_y[n], old_y[n]);
      }
    }
  }
}
TEST(Pinalign, ConflictOnEitherCellBoundaryLeavesBothPinsUnchanged) {
  for (bool input_side : {false, true}) {
    Fixture f(true);
    f.bits.push_back({input_side ? "data" : "result", 1});
    f.anchor(input_side ? 30 : 70, input_side ? 40 : 55, 2);
    auto s = f.run();
    EXPECT_EQ(s.order_conflicts, 1u);
    EXPECT_EQ(s.aligned, 0u);
    EXPECT_EQ(f.m.anchors[0].y, 20);
    EXPECT_EQ(f.m.anchors[1].y, 60);
  }
}
TEST(Pinalign, CellOutputCoordinatePropagatesThroughADirectWireChain) {
  Fixture f(true);
  f.mesh = {
      { 0, 0,  30, 100, 0},
      {30, 0,  70, 100, 0},
      {70, 0,  80, 100, 0},
      {80, 0, 100, 100, 0}
  };
  f.m.design.pin_offset_y[3] = .25;
  f.m.design.pin_offset_y[4] = .75;
  int last                   = f.anchor(80, 80, 1);
  f.m.design.net_mask.back() = 0;
  f.net({4, last}, 2, 1, -1);
  f.net({last, 2}, 3, 1, -1);
  auto s = f.run();
  ASSERT_EQ(s.aligned, 2u);
  EXPECT_EQ(f.m.anchors[0].y, 50.25);
  EXPECT_EQ(f.m.anchors[1].y, 50.75);
  EXPECT_EQ(f.m.anchors[2].y, 50.75);
}
TEST(Pinalign, SplitMetadataRetainsOriginalDriverAndLocalRegions) {
  DreamPlace::native::Design d;
  d.layout             = {0, 0, 100, 100, 1, 1};
  d.num_movable        = 2;
  d.node_x             = {10, 90};
  d.node_y             = {20, 60};
  d.node_size_x        = {1, 1};
  d.node_size_y        = {1, 1};
  d.pin2node           = {0, 1};
  d.pin_offset_x       = {0, 0};
  d.pin_offset_y       = {0, 0};
  d.flat_net2pin       = {0, 1};
  d.flat_net2pin_start = {0, 2};
  d.net_weights        = {1};
  d.build_maps();
  Fixture f;
  f.m = split_boundary_nets(d, f.mesh, [](const std::vector<Busorder_point>&) {
    return Busorder_tree{
        {{10, 20}, {50, 20}},
        {{50, 20}, {50, 60}},
        {{50, 60}, {90, 60}}
    };
  });
  ASSERT_EQ(f.m.anchors.size(), 2u);
  ASSERT_EQ(f.m.piece_region.size(), 3u);
  EXPECT_EQ(std::count(f.m.pin_is_driver.begin(), f.m.pin_is_driver.end(), true), 1);
  EXPECT_EQ(f.run().aligned, 1u);
  for (const auto& a : f.m.anchors) {
    EXPECT_EQ(a.y, 20);
  }
  EXPECT_EQ(f.m.design.net_weights.size(), 3u);
}
TEST(Pinalign, RequiresSplitTopology) {
  Boundary_pin_options o;
  o.align = true;
  o.split = false;
  EXPECT_THROW(validate_boundary_pin_options(o), std::invalid_argument);
}
}  // namespace livehd::placement
