// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "placement_hints.hpp"

#include <filesystem>

#include "def_binding.hpp"
#include "graph/node_util.hpp"
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
Placement_hints bank_hints(std::string hints) {
  return Placement_hints(bundle(document(hints)), "top", {{"bank", {"g0"}}}, objects(), 1);
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
TEST(PlacementHints, SplitMemoryResolvesAllBanksPerInstance) {
  hhds::GraphLibrary library;
  const auto sram = library.create_io("SRAM");
  const auto gate = library.create_io("INV");
  const auto memory = library.create_io("split")->create_graph();
  const auto bank = library.create_io("bank")->create_graph();
  const auto top = library.create_io("top")->create_graph();
  auto instance = [](auto& graph, const auto& io, const char* name) {
    auto n = livehd::graph_util::create_typed_node(graph, Ntype_op::Sub);
    n.set_subnode(io);
    n.set_name(name);
  };
  memory->get_input_node().attr(livehd::attrs::memory_module).set(1);
  instance(*memory, sram, "lo");
  instance(*memory, sram, "hi");
  instance(*memory, gate, "decoder");
  instance(*bank, memory->get_io(), "storage");
  auto unnamed = livehd::graph_util::create_typed_node(*bank, Ntype_op::Sub);
  unnamed.set_subnode(memory->get_io());
  instance(*top, bank->get_io(), "g0");
  instance(*top, bank->get_io(), "g1");
  livehd::physical::Technology tech;
  tech.macros["SRAM"].klass = "BLOCK";
  tech.macros["INV"].klass = "CORE";
  const auto scopes = placement_hint_scopes(*top, tech);
  EXPECT_EQ(std::ranges::count_if(scopes, [](const Hint_scope& scope) { return scope.memory_banks.has_value(); }), 2);
  auto names = objects();
  names[0].name = "g0.storage.lo";
  names[1].name = "g0.storage.hi";
  names[2].name = "g1.storage.lo";
  names[3].name = "g1.storage.hi";
  const auto input = bundle(document(R"([{"id":"align","kind":"align","members":["storage"],"axis":"y"}])"));
  auto d = design();
  Placement_hints both(input, "top", scopes, names, 1);
  both.seed(d);
  auto pulls = both.pulls(d);
  ASSERT_EQ(pulls.size(), 4);
  EXPECT_DOUBLE_EQ(pulls[0].y, 30);
  EXPECT_DOUBLE_EQ(pulls[1].y, 30);
  EXPECT_DOUBLE_EQ(pulls[2].y, 120);
  EXPECT_DOUBLE_EQ(pulls[3].y, 120);

  names[3].name = "g1.storage.unrelated";
  Placement_hints missing(input, "top", scopes, names, 1);
  missing.seed(d);
  pulls = missing.pulls(d);
  ASSERT_EQ(pulls.size(), 2);
  EXPECT_EQ(pulls[0].node, 0);
  EXPECT_EQ(pulls[1].node, 1);
  names[3].name = "g1.storage.lo";
  Placement_hints ambiguous(input, "top", scopes, names, 1);
  ambiguous.seed(d);
  EXPECT_EQ(ambiguous.pulls(d).size(), 2);
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
  livehd::metadata::Bundle out(R"([{"class":"foreign","id":"keep"},{"class":"livehd.placement.hints","design":"top","id":"bank-hints"}])");
  h.append_results(out, d);
  EXPECT_EQ(out.select("foreign", "top").size(), 1);
  EXPECT_EQ(out.select("livehd.placement.hints", "top").size(), 1);
  auto report = out.select("livehd.placement.hint_results", "top");
  ASSERT_EQ(report.size(), 1);
  EXPECT_EQ(std::string((*report[0])["hints"][0]["status"].GetString()), "discarded");

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
TEST(PlacementHints, MeasuredClusterAndSeedDistancesFollowTheFrame) {
  auto input = bundle(R"([{"class":"livehd.placement.hints","schema_version":1,"id":"test","design":"top","templates":[
    {"module":"bank","groups":{"pair":["a","b"]},"hints":[{"id":"c","kind":"cluster","group":"pair"},
     {"id":"p","kind":"position","subject":"a","frame":"floorplan","anchor":"lower_left","x_um":5,"y_um":5}]}]}])");
  Placement_hints h(input, "top", {{"bank", {"g0"}}}, objects(), 2);
  auto d = design();
  h.seed(d);
  d.node_x[0] += 6;
  d.node_y[0] += 8;
  d.node_x[1] = d.node_x[0] + 48;
  d.node_y[1] = d.node_y[0] + 64;
  auto report = h.report(d);
  const auto& cluster = report["hints"][0];
  const auto& seed = report["hints"][1];
  EXPECT_DOUBLE_EQ(cluster["residual_um"].GetDouble(), 20);
  EXPECT_STREQ(cluster["status"].GetString(), "accepted");
  EXPECT_DOUBLE_EQ(seed["residual_um"].GetDouble(), 5);
  EXPECT_TRUE(seed["seed_applied"].GetBool());
}
TEST(PlacementHints, AutomaticSnapReportsTheSideTheSolverChose) {
  auto h = bank_hints(R"([{"id":"closest","kind":"snap","subject":"a","relative_to":"b"}])");
  auto d = design();
  d.node_x[0] = 140;
  d.node_y[0] = 69;
  d.node_x[1] = 30;
  d.node_y[1] = 53;
  d.node_size_x[1] = 100;
  h.seed(d);
  auto pulls = h.pulls(d);
  ASSERT_EQ(pulls.size(), 1);
  // The north halo gap (4 DBU) is closer than the east gap (8 DBU).
  // Snapping north also requires moving the centers 65 DBU horizontally.
  EXPECT_DOUBLE_EQ(pulls[0].x, 80);
  EXPECT_DOUBLE_EQ(pulls[0].y, 70);
  EXPECT_DOUBLE_EQ(h.report(d)["hints"][0]["residual_um"].GetDouble(), std::hypot(65., 4.));
}
TEST(PlacementHints, EditingAndClearingHintsDoesNotRestartAnIdleSolver) {
  auto d = design();
  auto h = bank_hints("[]");
  dp::Options o;
  int rebinds = 0;
  h.attach(o, [&] { ++rebinds; return objects(); }, true);
  ASSERT_EQ(o.macro_pull_interval, 25);
  EXPECT_FALSE(o.macro_pull_update(d, 25));
  EXPECT_EQ(rebinds, 0);

  h = bank_hints(R"([{"id":"group","kind":"align","members":["a","b"],"axis":"y"}])");
  auto active = o.macro_pull_update(d, 50);
  ASSERT_TRUE(active);
  EXPECT_EQ(active->size(), 2u);
  EXPECT_EQ(rebinds, 1);
  h = bank_hints("[]");
  auto clear = o.macro_pull_update(d, 75);
  ASSERT_TRUE(clear);
  EXPECT_TRUE(clear->empty());
  EXPECT_FALSE(o.macro_pull_update(d, 100));
  EXPECT_EQ(rebinds, 1);
}

TEST(PlacementHints, ClearingAHintKeepsThePriorAnchorWithoutRepeatingItsJump) {
  auto d = design();
  auto h = bank_hints("[]");
  dp::Options o;
  std::vector<int> visits;
  o.macro_pull_interval = 10;
  o.macro_pull_update = [&](const dp::Design&, int iteration) -> std::optional<std::vector<dp::MacroPull>> {
    visits.push_back(iteration);
    if (iteration == 10) {
      return std::vector<dp::MacroPull>{{3, 125, 135, .5, true}};
    }
    return std::nullopt;
  };
  h.attach(o, {}, true);
  ASSERT_EQ(o.macro_pull_interval, 5);
  auto first = o.macro_pull_update(d, 10);
  ASSERT_TRUE(first && first->size() == 1 && first->front().jump);
  EXPECT_FALSE(o.macro_pull_update(d, 20));
  h = bank_hints(R"([{"id":"group","kind":"align","members":["a","b"],"axis":"y"}])");
  auto both = o.macro_pull_update(d, 25);
  ASSERT_TRUE(both && both->size() == 3);
  EXPECT_FALSE(both->back().jump);
  h = bank_hints("[]");
  auto restored = o.macro_pull_update(d, 50);
  ASSERT_TRUE(restored && restored->size() == 1);
  EXPECT_EQ(restored->front().node, 3);
  EXPECT_FALSE(restored->front().jump);
  EXPECT_FALSE(o.macro_pull_update(d, 75));
  EXPECT_EQ(visits, (std::vector<int>{10, 20, 50}));
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

TEST(PlacementHints, OrderedMatrixTransfersWithoutCoordinateEdits) {
  auto record = bundle(
      R"([{"class":"livehd.placement.hints","id":"banks","schema_version":2,"design":"top","templates":[{"module":"top","hints":[{"id":"array","kind":"matrix","members":["g1.b","g0.a","g1.a","g0.b"],"columns":2,"gap_x_um":4,"gap_y_um":8}]}]}])");
  Placement_hints hints(record,
                        "top",
                        {
                            {"top", {}}
  },
                        objects(),
                        1);
  auto            d = design();
  hints.seed(d);
  for (double width : {200., 300.}) {
    d.layout.xh = width;
    auto pulls  = hints.pulls(d);
    ASSERT_EQ(pulls.size(), 4);
    EXPECT_EQ(pulls[3].x, width / 2 - 8);
    EXPECT_EQ(pulls[0].x, width / 2 + 8);
    EXPECT_EQ(pulls[3].y, 90);
    EXPECT_EQ(pulls[2].y, 110);
    const auto preview = hints.preview(d);
    const auto& targets = preview["targets_dbu"];
    ASSERT_EQ(targets.Size(), pulls.size());
    for (size_t i = 0; i < pulls.size(); ++i) {
      EXPECT_EQ(targets[i]["name"].GetString(), objects()[pulls[i].node].name);
      EXPECT_DOUBLE_EQ(targets[i]["target_center_size"][0].GetDouble(), pulls[i].x);
      EXPECT_DOUBLE_EQ(targets[i]["target_center_size"][1].GetDouble(), pulls[i].y);
      EXPECT_DOUBLE_EQ(targets[i]["target_center_size"][2].GetDouble(), d.node_size_x[pulls[i].node]);
      EXPECT_DOUBLE_EQ(targets[i]["from_center"][0].GetDouble(), d.node_x[pulls[i].node] + d.node_size_x[pulls[i].node] / 2);
    }
  }
  d.layout.xl = 73;
  d.layout.xh = 273;
  d.layout.yl = -41;
  d.layout.yh = 159;
  d.node_size_x[3] = 30;
  auto changed = hints.pulls(d);
  ASSERT_EQ(changed.size(), 4);
  EXPECT_DOUBLE_EQ(changed[3].x, 165);
  EXPECT_DOUBLE_EQ(changed[3].y, 49);
  EXPECT_DOUBLE_EQ(changed[0].x, 191);
  EXPECT_DOUBLE_EQ(changed[2].y, 69);
  // The envelope is 48 DBU including halos and gaps. Shrink by one DBU:
  // the row cannot fit even if each member ends within one pitch of its target.
  d.layout.xh = d.layout.xl + 47;
  for (const auto& target : hints.pulls(d)) {
    d.node_x[target.node] = target.x - d.node_size_x[target.node] / 2;
    d.node_y[target.node] = target.y - d.node_size_y[target.node] / 2;
  }
  auto too_small = hints.report(d);
  const auto& hint = too_small["hints"][0];
  EXPECT_LE(hint["residual_um"].GetDouble(), 1);
  EXPECT_STREQ(hint["status"].GetString(), "relaxed");
  EXPECT_NE(std::string(hint["reason"].GetString()).find("larger than the core"), std::string::npos);
}

TEST(PlacementHints, OverrideOnlyChangesTheNamedInstance) {
  auto record
      = bundle(R"([{"class":"livehd.placement.hints","id":"banks","schema_version":2,"design":"top","templates":[{"module":"bank",
    "hints":[{"id":"edge","kind":"align","axis":"y","members":["a","b"]}],
    "overrides":[{"instance":["g0"],"id":"edge","disabled":true},{"instance":["g1"],"id":"edge","set":{"axis":"x"}}]}]}])");
  Placement_hints hints(record,
                        "top",
                        {
                            {"bank", {"g0"}},
                            {"bank", {"g1"}}
  },
                        objects(),
                        1);
  auto            d = design();
  hints.seed(d);
  const auto pulls = hints.pulls(d);
  ASSERT_EQ(pulls.size(), 2);
  EXPECT_EQ(pulls[0].node, 2);
  EXPECT_EQ(pulls[1].node, 3);
  EXPECT_DOUBLE_EQ(pulls[0].x, 115);
  EXPECT_DOUBLE_EQ(pulls[1].x, 115);
}

TEST(PlacementHints, PinFacingRespectsMasterSymmetryAndSeparatesTranslation) {
  auto members                          = objects();
  members[0].signal_pin_centers["DATA"] = {1, 5};
  auto            record = bundle(R"([{"class":"livehd.placement.hints","id":"banks","schema_version":2,"design":"top",
    "templates":[{"module":"bank","hints":[{"id":"face","kind":"pin_facing","subject":"a","pins":["DATA"],"side":"east"}]}]}])");
  auto            d      = design();
  const auto      x = d.node_x, y = d.node_y;
  Placement_hints hints(record,
                        "top",
                        {
                            {"bank", {"g0"}}
  },
                        members,
                        1);
  hints.seed(d);
  ASSERT_FALSE(d.node_orientation.empty());
  EXPECT_EQ(d.node_orientation[0], dp::Orientation::FN);
  EXPECT_EQ(x, d.node_x);
  EXPECT_EQ(y, d.node_y);
  EXPECT_TRUE(hints.pulls(d).empty());
  const auto preview = hints.preview(d);
  const auto& facing = preview["hints"][0];
  EXPECT_DOUBLE_EQ(facing["residual_um"].GetDouble(), 1);
  EXPECT_STREQ(facing["side"].GetString(), "east");
  const auto& edge = facing["pin_facing_edge_dbu"];
  ASSERT_EQ(edge.Size(), 4u);
  EXPECT_DOUBLE_EQ(edge[0].GetDouble(), d.node_x[0] + d.node_size_x[0]);
  EXPECT_DOUBLE_EQ(edge[1].GetDouble(), d.node_y[0]);
  EXPECT_DOUBLE_EQ(edge[2].GetDouble(), edge[0].GetDouble());
  EXPECT_DOUBLE_EQ(edge[3].GetDouble(), d.node_y[0] + d.node_size_y[0]);
  members[0].symmetry_y = false;
  d                     = design();
  Placement_hints held(record,
                       "top",
                       {
                           {"bank", {"g0"}}
  },
                       members,
                       1);
  held.seed(d);
  EXPECT_TRUE(d.node_orientation.empty() || d.node_orientation[0] == dp::Orientation::N);
}
