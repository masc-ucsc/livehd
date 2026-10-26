// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routing_connectivity.hpp"

#include "gtest/gtest.h"

namespace livehd::physical {
namespace {
struct Fixture {
  Technology tech;
  Floorplan  fp;
  Net        net;
  Fixture() {
    tech.routing_layers         = {"M1", "M2"};
    tech.layers["M1"].width     = .02;
    tech.layers["M2"].width     = .02;
    tech.cut_layers["V1"].width = .01;
    tech.vias["via"].shapes     = {
        {"M1",     {-.01, -.01, .01, .01}},
        {"V1", {-.005, -.005, .005, .005}},
        {"M2",     {-.01, -.01, .01, .01}}
    };
    auto& master            = tech.macros["cell"];
    master.width            = .2;
    master.height           = .2;
    master.pins["A"].shapes = {
        {"M1", {.09, .09, .11, .11}}
    };
    for (auto [name, x] : {
             std::pair{"a",    0},
             std::pair{"b", 1000}
    }) {
      Ann_place p(x, 0, 200, 200);
      p.status = Status::PLACED;
      fp.components.push_back({name, "cell", p});
      net.connections.push_back({name, "A"});
    }
    net.wires.push_back({
        "FIXED",
        {{"M1", {}, {}, {Route_point{100, 100, {}}, Route_via{"via"}}},
          {"M1", {}, {}, {Route_point{1100, 100, {}}, Route_via{"via"}}},
          {"M2", {}, {}, {Route_point{100, 100, 0}, Route_point{1100, 100, 0}}}}
    });
  }
};
}  // namespace
TEST(RoutingConnectivity, FullTraceAndBrokenWireOrVia) {
  Fixture f;
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  auto broken = f.net;
  broken.wires[0].paths.pop_back();
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, broken).connected);
  broken = f.net;
  broken.wires[0].paths.erase(broken.wires[0].paths.begin());
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, broken).connected);
  f.tech.vias["via"].shapes[1].rect = {.02, .02, .03, .03};
  const auto malformed              = check_route_connectivity(f.tech, f.fp, f.net);
  EXPECT_FALSE(malformed.connected);
  EXPECT_EQ(malformed.reason, "cut does not contact both via landings");
}
TEST(RoutingConnectivity, RectanglesConnectOnCurrentLayerAndPreserveAnchor) {
  Fixture f;
  f.net.wires[0].paths.pop_back();
  auto& steps = f.net.wires[0].paths[0].steps;
  // The via switches to M2; the patch must not reset the point or layer.
  steps.emplace_back(Route_rect{0, -10, 500, 10});
  steps.emplace_back(Route_rect{500, -10, 1000, 10});
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  steps.back() = Route_rect{501, -10, 1000, 10};
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  steps.back() = Route_rect{500, 10, 1000, 30};
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
}
TEST(RoutingConnectivity, ReusableIndexTracksPlacementAndRejectsForeignOrResizedFrames) {
  Fixture          f;
  Route_components index(f.fp);
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net, &index).connected);
  f.fp.components[1].place.x += 500;
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net, &index).connected);
  f.fp.components[1].place.x -= 500;
  auto copy                   = f.fp;
  EXPECT_FALSE(check_route_connectivity(f.tech, copy, f.net, &index).connected);
  f.fp.components.push_back(f.fp.components.front());
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net, &index).connected);
  EXPECT_THROW(Route_components{f.fp}, std::invalid_argument);
}
TEST(RoutingConnectivity, TrialRulesReuseTheComponentIndex) {
  Fixture                 f;
  Route_components        index(f.fp);
  const Route_width_rules trial{
      {"wide", {{"M1", 40}, {"M2", 40}}}
  };
  for (auto& path : f.net.wires[0].paths) {
    path.taper_rule = "wide";
  }
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net, &index).connected);
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net, &index, &trial).connected);
  EXPECT_TRUE(f.fp.nondefault_rules.empty());
  EXPECT_TRUE(index.matches(f.fp));
  f.fp.nondefault_rules = trial;
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net, &index).connected);
  f.fp.nondefault_rules["wide"]["M2"] = 20;
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net, &index, &trial).connected);
}

TEST(RoutingConnectivity, IslandsAndPointContactDoNotConnect) {
  Fixture f;
  f.net.wires[0].paths.push_back({
      "M2",
      {},
      {},
      {Route_point{300, 300, 0}, Route_point{400, 300, 0}}
  });
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  // Via landing corner at (1110,110) only touches this island at that point.
  f.net.wires[0].paths.back() = {
      "M2",
      {},
      {},
      {Route_point{1110, 120, 0}, Route_point{1200, 120, 0}}
  };
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  f.net.wires[0].paths.back() = {
      "M2",
      {},
      {},
      {Route_point{1090, 120, 0}, Route_point{1200, 120, 0}}
  };
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net).connected);
}
TEST(RoutingConnectivity, UnsupportedAndMissingEndpointsFailClosed) {
  Fixture f;
  f.net.connections[1].instance = "PIN";
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  f.net.connections[1].instance                     = "b";
  f.tech.macros["cell"].pins["A"].shapes[0].rect.xh = .09;
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
}
}  // namespace livehd::physical
