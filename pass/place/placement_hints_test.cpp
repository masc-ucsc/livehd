// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "placement_hints.hpp"

#include <filesystem>

#include "def_binding.hpp"
#include "gtest/gtest.h"
namespace dp = DreamPlace::native;
using namespace livehd::placement;
namespace {
dp::Design design() {
  dp::Design d;
  d.layout      = {0, 0, 200, 200, 1, 1};
  d.num_movable = 4;
  d.node_x      = {10, 30, 100, 120, 70};
  d.node_y      = {10, 40, 100, 130, 70};
  d.node_size_x = d.node_size_y = std::vector<double>(5, 10);
  d.node_macro                  = std::vector<unsigned char>(5, 1);
  d.node_halo_x = d.node_halo_y = std::vector<double>(5, 1);
  d.flat_node2pin_start         = std::vector<int>(6, 0);
  d.flat_net2pin_start          = {0};
  return d;
}
std::vector<Hint_object> objects() {
  return {
      { "g0.a", "M", true, true},
      { "g0.b", "M", true, true},
      { "g1.a", "M", true, true},
      { "g1.b", "M", true, true},
      {"fixed", "M", true, true}
  };
}
std::shared_ptr<const livehd::metadata::Bundle> bundle(std::string s) {
  return std::make_shared<const livehd::metadata::Bundle>(s);
}
std::string document(std::string hints) {
  return R"([{"class":"foreign","payload":7},{"class":"livehd.placement.hints","schema_version":1,"id":"test","design":"top","templates":[{"module":"bank","hints":)"
         + hints + "}]}]";
}
}  // namespace
TEST(PlacementHints, RepeatedInstancesNeverCrossScopes) {
  auto            d      = design();
  auto            before = d;
  Placement_hints h(bundle(document(R"([{"id":"align","kind":"align","members":["a","b"],"axis":"y"}])")),
                    "top",
                    {
                        { "top",     {}},
                        {"bank", {"g0"}},
                        {"bank", {"g1"}}
  },
                    objects(),
                    1);
  h.seed(d);
  auto pulls = h.pulls(d);
  ASSERT_EQ(pulls.size(), 4);
  EXPECT_DOUBLE_EQ(pulls[0].y, 30);
  EXPECT_DOUBLE_EQ(pulls[1].y, 30);
  EXPECT_DOUBLE_EQ(pulls[2].y, 120);
  EXPECT_DOUBLE_EQ(pulls[3].y, 120);
  EXPECT_EQ(d.node_x, before.node_x);
  EXPECT_EQ(d.pin2node, before.pin2node);
  EXPECT_EQ(d.num_movable, before.num_movable);
}
TEST(PlacementHints, CoordinateSeedsStayMovableAndRejectFixedOverlap) {
  auto            d = design();
  Placement_hints h(
      bundle(document(
          R"([{"id":"position","kind":"position","subject":"a","frame":"floorplan","anchor":"lower_left","x_um":70,"y_um":70},{"id":"good","kind":"position","subject":"b","frame":"floorplan","anchor":"lower_left","x_um":150,"y_um":150,"orientation":"FN"}])")),
      "top",
      {
          {"bank", {"g0"}}
  },
      objects(),
      1);
  h.seed(d);
  EXPECT_DOUBLE_EQ(d.node_x[0], 10);
  EXPECT_DOUBLE_EQ(d.node_y[0], 10);
  EXPECT_DOUBLE_EQ(d.node_x[1], 150);
  EXPECT_EQ(d.num_movable, 4);
  EXPECT_EQ(d.node_orientation[1], dp::Orientation::FN);
  auto file = std::filesystem::path(testing::TempDir()) / "hints-result.json";
  h.write(d, file.string());
  auto out = livehd::metadata::Bundle::read(file.string());
  EXPECT_EQ(out.select("foreign", "top").size(), 1);
  EXPECT_EQ(out.select("livehd.placement.hints", "top").size(), 1);
  auto report = out.select("livehd.placement.hint_results", "top");
  ASSERT_EQ(report.size(), 1);
  EXPECT_EQ(std::string((*report[0])["hints"][0]["status"].GetString()), "discarded");
  auto state = out.select("livehd.placement.state", "top");
  ASSERT_EQ(state.size(), 1);
  EXPECT_EQ((*state[0])["templates"][0]["hints"].Size(), 5);
  std::filesystem::remove(file);
}
TEST(PlacementHints, ContradictoryDirectionsRemainSoftAndBounded) {
  auto            d = design();
  Placement_hints h(
      bundle(document(
          R"([{"id":"e","kind":"direction","subject":"a","relative_to":"b","side":"east"},{"id":"w","kind":"direction","subject":"a","relative_to":"b","side":"west"}])")),
      "top",
      {
          {"bank", {"g0"}}
  },
      objects(),
      1);
  h.seed(d);
  auto p = h.pulls(d);
  ASSERT_EQ(p.size(), 1);
  EXPECT_LE(p[0].weight, 1);
  EXPECT_TRUE(std::isfinite(p[0].x));
  EXPECT_EQ(d.node_x[0], 10);
  EXPECT_TRUE(d.node_boxes.empty());
  dp::Options o;
  h.attach(o);
  ASSERT_TRUE(o.macro_pull_update);
  EXPECT_FALSE(o.macro_pull_update(d, 1));
  EXPECT_TRUE(o.macro_pull_update(d, 25));
}
TEST(PlacementHints, MissingNamesAndMalformedHintsDoNotAbort) {
  auto            d = design();
  Placement_hints h(
      bundle(document(
          R"([{"id":"bad","kind":"snap","subject":"missing","relative_to":"a"},true,{"id":"badpos","kind":"position","subject":"a","frame":"floorplan","anchor":"lower_left","x_um":-1,"y_um":10}])")),
      "top",
      {
          {"bank", {"g0"}}
  },
      objects(),
      1);
  EXPECT_NO_THROW(h.seed(d));
  EXPECT_TRUE(h.pulls(d).empty());
  EXPECT_EQ(d.node_x[0], 10);
}

TEST(PlacementHints, OverlappingCoordinateRequestsBothLose) {
  auto            d   = design();
  const auto      old = d.node_x;
  Placement_hints h(
      bundle(document(
          R"([{"id":"first","kind":"position","subject":"a","frame":"floorplan","anchor":"lower_left","x_um":150,"y_um":150},{"id":"second","kind":"position","subject":"b","frame":"floorplan","anchor":"lower_left","x_um":155,"y_um":150}])")),
      "top",
      {
          {"bank", {"g0"}}
  },
      objects(),
      1);
  h.seed(d);
  EXPECT_EQ(d.node_x, old);
}
TEST(PlacementHints, TopologyRebindingUsesNames) {
  auto            d = design();
  Placement_hints h(bundle(document(R"([{"id":"align","kind":"align","members":["a","b"],"axis":"y"}])")),
                    "top",
                    {
                        {"bank", {"g0"}}
  },
                    objects(),
                    1);
  h.seed(d);
  auto names = objects();
  std::swap(names[0], names[2]);
  std::swap(d.node_x[0], d.node_x[2]);
  std::swap(d.node_y[0], d.node_y[2]);
  h.rebind(names);
  auto p = h.pulls(d);
  ASSERT_EQ(p.size(), 2);
  EXPECT_EQ(p[0].node, 1);
  EXPECT_EQ(p[1].node, 2);
  EXPECT_DOUBLE_EQ(p[0].y, 30);
  EXPECT_DOUBLE_EQ(p[1].y, 30);
}

TEST(PlacementHints, DuplicateHintIdsDoNotChooseFirst) {
  auto            d = design();
  Placement_hints h(
      bundle(document(
          R"([{"id":"same","kind":"direction","subject":"a","relative_to":"b","side":"east"},{"id":"same","kind":"direction","subject":"a","relative_to":"b","side":"west"}])")),
      "top",
      {
          {"bank", {"g0"}}
  },
      objects(),
      1);
  h.seed(d);
  EXPECT_TRUE(h.pulls(d).empty());
}

TEST(PlacementHints, ExternalDefNamesBindWithoutRenamingTimingNodes) {
  livehd::placement::Def_name_binding names;
  names.add("`bank[0]/ram`");
  names.add("bus[2]");
  names.add("__flat___sub_52.`other[0]/ram`");
  EXPECT_EQ(names.resolve(R"(other\[0\]/ram)"), "__flat___sub_52.`other[0]/ram`");
  EXPECT_EQ(names.resolve(R"(bank\[0\]/ram)"), "`bank[0]/ram`");
  EXPECT_EQ(names.resolve("`bank[0]/ram`"), "`bank[0]/ram`");
  EXPECT_EQ(names.resolve(R"(bus\[2\])"), "bus[2]");
  EXPECT_EQ(names.resolve("missing"), "missing");
  names.add("bank[0]/ram");
  EXPECT_THROW(names.resolve(R"(bank\[0\]/ram)"), std::invalid_argument);
}
