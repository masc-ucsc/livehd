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
Busorder    make_order(const dp::Design& d) {
  const auto router = [](const std::vector<Busorder_point>& pins) {
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
                  1);
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
  order.observe(d, "before");
  EXPECT_EQ(order.samples().back().groups, 0u);  // masked nets are never measured
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
