// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "region_mesh.hpp"

#include <filesystem>
#include <fstream>
#include <set>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace dp = DreamPlace::native;
namespace {
// 40 unit cells on a 1000x100 die around one fixed macro at x=500..520.
dp::Design fixture() {
  dp::Design d;
  d.layout      = {0, 0, 1000, 100, 1, 1};
  d.num_movable = 40;
  for (int i = 0; i < d.num_movable; ++i) {
    d.node_x.push_back(10 + 24.5 * i);
    d.node_y.push_back(10 + (i % 8) * 10);
    d.node_size_x.push_back(1);
    d.node_size_y.push_back(1);
  }
  d.node_x.push_back(500);
  d.node_y.push_back(40);
  d.node_size_x.push_back(20);
  d.node_size_y.push_back(20);
  d.node_macro.assign(d.node_x.size(), 0);
  d.node_macro.back()  = 1;
  d.flat_net2pin_start = {0};
  d.build_maps();
  return d;
}
}  // namespace

TEST(Region_mesh, MembershipPartitionsMovableCellsUnderTheCap) {
  const auto d    = fixture();
  const auto mesh = Region_mesh::build(d, {40}, {{"io[0]", 0, 50}, {"io[1]", 0, 60}}, {.max_cells = 6, .merge_um = 1}, 1);
  ASSERT_EQ(mesh.members.size(), mesh.rects.size());
  EXPECT_GE(mesh.initial_rectangles, mesh.rects.size());
  std::set<int> seen;
  for (size_t r = 0; r < mesh.rects.size(); ++r) {
    EXPECT_LE(mesh.rects[r].cells, 6u);
    EXPECT_EQ(mesh.rects[r].cells, mesh.members[r].size());
    for (int i : mesh.members[r]) {
      EXPECT_TRUE(seen.insert(i).second);
      EXPECT_EQ(mesh.rect_of(d.node_x[i] + .5, d.node_y[i] + .5), int(r));
    }
  }
  EXPECT_EQ(seen.size(), size_t(d.num_movable));
  EXPECT_FALSE(mesh.rect_of(-1, 50));
  EXPECT_FALSE(mesh.edges.empty());
  double area = 0;
  for (const auto& r : mesh.rects) {
    area += (r.xh - r.xl) * (r.yh - r.yl);
  }
  EXPECT_DOUBLE_EQ(area, 1000. * 100.);
}

TEST(Region_mesh, RejectsMovableMacrosAndBadOptions) {
  auto d = fixture();
  EXPECT_THROW(Region_mesh::build(d, {40}, {}, {.max_cells = 0}, 1), std::invalid_argument);
  EXPECT_THROW(Region_mesh::build(d, {3}, {}, {}, 1), std::invalid_argument);
  d.node_macro[0] = 1;
  EXPECT_THROW(Region_mesh::build(d, {40}, {}, {}, 1), std::invalid_argument);
}

TEST(Region_mesh, WritesOneSharedReport) {
  const auto dir  = std::filesystem::path(::testing::TempDir()) / "region_mesh";
  const auto mesh = Region_mesh::build(fixture(), {40}, {}, {.max_cells = 10}, 1);
  mesh.write(dir.string());
  std::ifstream file(dir / "region_mesh.json");
  std::string   text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  EXPECT_NE(text.find("\"rectangles\""), std::string::npos);
  EXPECT_NE(text.find("\"max_cells\":10"), std::string::npos);
}
}  // namespace livehd::placement
