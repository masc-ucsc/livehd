// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "route_store.hpp"

#include <cstdlib>
#include <filesystem>

#include "graph/attr_carry.hpp"
#include "graph/node_util.hpp"
#include "gtest/gtest.h"
namespace livehd::physical {
TEST(RouteStore, GraphSaveLoadPreservesGeometryAndRejectsStaleContext) {
  hhds::GraphLibrary library;
  auto               io    = library.create_io("routed");
  auto               graph = io->create_graph();
  auto               node  = graph_util::create_typed_node(*graph, Ntype_op::Or);
  node.set_name("driver");
  auto driver = node.create_driver_pin(0);
  Net  net{
      "net",
       {{"a", "Y"}, {"b", "A"}}
  };
  net.wires.push_back({
      "FIXED",
      {{"M1",
        {},
        "",
        {Route_point{10, 20, 0}, Route_via{"VIA12", Orientation::FN}, Route_point{40, 20, {}}, Route_rect{-2, -3, 4, 5}},
        "wide"},
        {"M2", {}, "", {Route_point{40, 20, 7}, Route_point{80, 20, {}}}, "wide"}}
  });
  const Route_width_rules rules{
      {"wide", {{"M1", 20}, {"M2", 40}}}
  };
  store_route(driver, 1000, net, rules, "tech1/placement1");
  auto current = net;
  current.wires.clear();
  const auto stored = load_route(driver, 1000, current, "tech1/placement1");
  ASSERT_TRUE(stored);
  EXPECT_EQ(stored->net, net);
  EXPECT_EQ(stored->rules, rules);
  EXPECT_FALSE(load_route(driver, 2000, current, "tech1/placement1"));
  EXPECT_FALSE(load_route(driver, 1000, current, "tech1/placement2"));
  current.connections[1].pin = "B";
  EXPECT_FALSE(load_route(driver, 1000, current, "tech1/placement1"));
  current.connections[1].pin = "A";
  auto dst                   = graph_util::create_typed_node(*graph, Ntype_op::Or).create_driver_pin(0);
  graph_util::carry_pin_attrs(driver, dst);
  EXPECT_FALSE(load_route(dst, 1000, current, "tech1/placement1"));
  const auto directory = (std::filesystem::path(std::getenv("TEST_TMPDIR")) / "route_graph").string();
  library.save(directory);
  hhds::GraphLibrary restored;
  restored.load(directory);
  auto loaded = restored.find_io("routed")->get_graph();
  bool found  = false;
  for (auto n : loaded->body().nodes()) {
    if (n.get_name() != "driver") {
      continue;
    }
    const auto pin   = n.create_driver_pin(0);
    const auto value = load_route(pin, 1000, current, "tech1/placement1");
    ASSERT_TRUE(value);
    EXPECT_EQ(value->net, net);
    EXPECT_EQ(value->rules, rules);
    clear_route(pin);
    EXPECT_FALSE(load_route(pin, 1000, current, "tech1/placement1"));
    found = true;
  }
  EXPECT_TRUE(found);
}
TEST(RouteStore, InvalidInputsDoNotReplaceCommittedRouteAndCorruptionFailsClosed) {
  hhds::GraphLibrary library;
  auto               io     = library.create_io("invalid");
  auto               graph  = io->create_graph();
  auto               driver = graph_util::create_typed_node(*graph, Ntype_op::Or).create_driver_pin(0);
  Net                net{
      "n",
                     {{"a", "Y"}, {"b", "A"}}
  };
  net.wires.push_back({"FIXED", {{"M2", {}, "", {Route_point{0, 0, {}}, Route_point{100, 0, {}}}}}});
  store_route(driver, 1000, net, {}, "frame1");
  auto invalid          = net;
  invalid.wires[0].type = "ROUTED";
  EXPECT_THROW(store_route(driver, 1000, invalid, {}, "frame1"), std::invalid_argument);
  ASSERT_TRUE(load_route(driver, 1000, net, "frame1"));
  auto records  = driver.attr(attrs::route_records_v1).get();
  records[0][3] = 1000000;
  driver.attr(attrs::route_records_v1).set(records);
  EXPECT_THROW(load_route(driver, 1000, net, "frame1"), std::invalid_argument);
}
TEST(RouteStore, FramePublicationAndReplayRejectStaleRoutesWithoutPartialChanges) {
  hhds::GraphLibrary library;
  auto               cell = library.create_io("cell");
  cell->add_input("A", 0);
  cell->add_output("Y", 1);
  auto      io    = library.create_io("top");
  auto      graph = io->create_graph();
  Floorplan frame;
  frame.design = "top";
  frame.die    = {0, 0, 1000, 1000};
  std::vector<hhds::Node_class> nodes;
  for (int i = 0; i < 4; ++i) {
    auto node = graph_util::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(cell);
    const auto name = std::to_string(i);
    node.set_name(name);
    Ann_place pose(i * 200, 0, 100, 100);
    pose.status = Status::LOCKED;
    frame.components.push_back({name, "cell", pose});
    nodes.push_back(node);
  }
  for (int i = 0; i < 4; i += 2) {
    nodes[i].create_driver_pin(1).connect_sink(nodes[i + 1].create_sink_pin(0));
    Net net{
        std::to_string(i),
        {{std::to_string(i), "Y"}, {std::to_string(i + 1), "A"}}
    };
    net.wires.push_back({"FIXED", {{"M2", {}, "", {Route_point{i * 200, 20, {}}, Route_point{(i + 1) * 200, 20, {}}}, "wide"}}});
    frame.nets.push_back(std::move(net));
  }
  frame.nondefault_rules["wide"]["M2"] = 40;
  EXPECT_EQ(publish_routes(*graph, frame, "technology1"), 2);
  auto duplicate = frame;
  duplicate.nets.push_back(frame.nets.front());
  duplicate.nets.back().name = "duplicate_driver";
  EXPECT_THROW(publish_routes(*graph, duplicate, "technology1"), std::invalid_argument);
  // Reject ambiguous publication before clearing the existing generation.
  EXPECT_EQ(replay_routes(*graph, frame, "technology1"), 2);
  const auto def_path = (std::filesystem::path(std::getenv("TEST_TMPDIR")) / "route_replay.def").string();
  write_def(frame, def_path);
  auto from_def = read_def(def_path);
  ASSERT_EQ(from_def.components.front().place.width, 0);
  ASSERT_EQ(from_def.components.front().place.status, Status::FIXED);
  EXPECT_EQ(replay_routes(*graph, from_def, "technology1"), 2);
  auto replay = frame;
  for (auto& net : replay.nets) {
    net.wires.clear();
  }
  replay.nondefault_rules.clear();
  replay.components[2].place.x += 1;
  EXPECT_THROW(replay_routes(*graph, replay, "technology1"), std::invalid_argument);
  EXPECT_TRUE(replay.nets[0].wires.empty());
  EXPECT_TRUE(replay.nondefault_rules.empty());
  replay.components[2].place.x -= 1;
  EXPECT_THROW(replay_routes(*graph, replay, "technology2"), std::invalid_argument);
  EXPECT_EQ(replay_routes(*graph, replay, "technology1"), 2);
  EXPECT_EQ(replay.nets, frame.nets);
  EXPECT_EQ(replay.nondefault_rules, frame.nondefault_rules);
  EXPECT_EQ(replay_routes(*graph, replay, "technology1"), 2);
  replay.nets[1].wires[0].paths[0].steps[0] = Route_point{500, 20, {}};
  EXPECT_THROW(replay_routes(*graph, replay, "technology1"), std::invalid_argument);
  for (auto& net : frame.nets) {
    net.wires.clear();
  }
  EXPECT_EQ(publish_routes(*graph, frame, "technology1"), 0);
  EXPECT_EQ(replay_routes(*graph, frame, "technology1"), 0);
}
}  // namespace livehd::physical
