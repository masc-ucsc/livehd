// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "busorder.hpp"

#include <cmath>
#include <numeric>

#include "gtest/gtest.h"
#include "place.hpp"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
dp::Design fixture(const std::vector<double>& lanes = {10, 20, 40, 30}) {
  dp::Design d;
  d.layout      = {0, 0, 1000, 100, 1, 1};
  d.num_movable = 8;
  for (double x : {20., 980.}) {
    for (double y : lanes) {
      d.node_x.push_back(x);
      d.node_y.push_back(y);
      d.node_size_x.push_back(1);
      d.node_size_y.push_back(1);
    }
  }
  // A fixed physical cell and IO pin must remain untouched.
  d.node_x.insert(d.node_x.end(), {500, 1000});
  d.node_y.insert(d.node_y.end(), {80, 50});
  d.node_size_x.insert(d.node_size_x.end(), {10, 0});
  d.node_size_y.insert(d.node_size_y.end(), {10, 0});
  d.node_macro.assign(10, 0);
  d.node_macro[8]      = 1;
  d.flat_net2pin_start = {0};
  for (int bit = 0; bit < 4; ++bit) {
    d.pin2node.insert(d.pin2node.end(), {bit, bit + 4});
    d.flat_net2pin_start.push_back(d.pin2node.size());
  }
  d.pin_offset_x.assign(8, .5);
  d.pin_offset_y.assign(8, .5);
  d.flat_net2pin.resize(8);
  std::iota(d.flat_net2pin.begin(), d.flat_net2pin.end(), 0);
  d.net_weights.assign(4, 1);
  d.net_mask.assign(4, 1);
  d.build_maps();
  return d;
}
Region_mesh mesh(const dp::Design& d) { return Region_mesh::build(d, {}, {}, {.max_cells = 4, .merge_um = 0}, 1); }
Busorder    make_order(const dp::Design& d, double weight = 100) {
  Busorder_options o;
  o.enabled             = true;
  o.weight              = weight;
  o.iterations          = 40;
  o.refresh             = 20;
  o.max_displacement_um = 20;
  const auto router     = [](const std::vector<Busorder_point>& pins) {
    const auto    tree = steiner_tree(pins);
    Busorder_tree edges;
    for (auto [a, b] : tree.edges) {
      edges.emplace_back(tree.points[a], tree.points[b]);
    }
    return edges;
  };
  return Busorder(d,
                  mesh(d),
                     {
                      {"bus",  1},
                      {"bus",  2},
                      {"bus", 20},
                      {"bus", 21}
  },
                  router,
                  1,
                  o);
}
}  // namespace

TEST(Busorder, IndexedNamesAreScopedAndAllowGaps) {
  ASSERT_TRUE(indexed_bus_bit("unit/data\\[21\\]"));
  EXPECT_EQ(indexed_bus_bit("unit/data\\[21\\]")->bus, "unit/data");
  EXPECT_EQ(indexed_bus_bit("unit/data[21]")->bit, 21);
  EXPECT_FALSE(indexed_bus_bit("data[x]"));
  EXPECT_FALSE(indexed_bus_bit("data[-1]"));
  EXPECT_FALSE(indexed_bus_bit("data"));
}
TEST(Busorder, AcceptsBothDirectionsAndExcludesMaskedNets) {
  for (const auto& lanes : {
           std::vector<double>{10, 20, 30, 40},
           std::vector<double>{40, 30, 20, 10}
  }) {
    auto d     = fixture(lanes);
    auto order = make_order(d);
    order.observe(d, "before");
    ASSERT_GT(order.samples().back().groups, 0u);
    EXPECT_EQ(order.samples().back().unordered, 0u);
    for (const auto& r : mesh(d).rects) {
      EXPECT_LE(r.cells, 4u);
    }
  }
  auto d = fixture();
  d.net_mask.assign(4, 0);
  auto order = make_order(d);
  EXPECT_TRUE(order.constraints(d).empty());
}
TEST(Busorder, ConvergenceMovesRealPinsWithoutChangingConnectivityOrFixedGeometry) {
  auto       d           = fixture();
  const auto original    = d;
  auto       order       = make_order(d);
  const auto constraints = order.constraints(d);
  ASSERT_FALSE(constraints.empty());
  bool violated = false;
  for (const auto& e : constraints) {
    EXPECT_EQ(e.axis, dp::Axis::y);
    violated |= d.node_y[e.from] + d.node_size_y[e.from] + e.separation > d.node_y[e.to];
  }
  ASSERT_TRUE(violated);
  dp::Options options;
  options.bins_x = options.bins_y = 8;
  options.target_density          = .8;
  options.add_fillers             = false;
  options.density_upper_bound     = true;
  options.legalize = options.detailed_place = false;
  order.refine(d, options);
  const auto  before = order.samples().front();
  const auto& after  = order.samples().back();
  ASSERT_GT(before.initially_unordered, 0u);
  EXPECT_LT(after.penalty, before.penalty);
  EXPECT_NE(d.node_y, original.node_y);
  EXPECT_EQ(d.pin2node, original.pin2node);
  EXPECT_EQ(d.flat_net2pin, original.flat_net2pin);
  EXPECT_EQ(d.net_weights, original.net_weights);
  EXPECT_TRUE(d.order_edges.empty());
  EXPECT_TRUE(d.node_boxes.empty());
  for (int i = 0; i < 8; ++i) {
    EXPECT_LE(std::abs(d.node_x[i] - original.node_x[i]), 20.000001);
    EXPECT_LE(std::abs(d.node_y[i] - original.node_y[i]), 20.000001);
  }
  for (int i : {8, 9}) {
    EXPECT_EQ(d.node_x[i], original.node_x[i]);
    EXPECT_EQ(d.node_y[i], original.node_y[i]);
  }
  d.net_mask.assign(4, 0);
  order.observe(d, "after_legalization");
  EXPECT_EQ(order.samples().back().resolved, 0u);
  EXPECT_EQ(order.samples().back().no_longer_comparable, before.initially_unordered);
}
TEST(Busorder, CoincidentCrossingsAreNotCountedAsUntangled) {
  auto d     = fixture();
  auto order = make_order(d);
  order.observe(d, "before");
  const auto count = order.samples().back().initially_unordered;
  ASSERT_GT(count, 0u);
  for (int i = 0; i < 8; ++i) {
    d.node_y[i] = 25;
  }
  order.observe(d, "after_gp");
  EXPECT_EQ(order.samples().back().resolved, 0u);
  EXPECT_EQ(order.samples().back().ambiguous, count);
}
}  // namespace livehd::placement
