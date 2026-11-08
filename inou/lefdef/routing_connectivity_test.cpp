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

TEST(RoutingConnectivity, RotatedDieTerminalMustTouchTheRoute) {
  Fixture  f;
  Terminal terminal;
  terminal.name        = "out";
  terminal.x           = 1100;
  terminal.y           = 150;
  terminal.status      = Status::FIXED;
  terminal.orientation = Orientation::E;
  terminal.shapes      = {
      {"M2", {40, -10, 60, 10}}
  };
  f.fp.terminals.push_back(terminal);
  f.net.connections[1] = {"PIN", "out"};
  f.net.wires[0].paths.erase(f.net.wires[0].paths.begin() + 1);
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  // Without the rotation, the pin sits above the wire and must not connect.
  f.fp.terminals[0].orientation = Orientation::N;
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  f.fp.terminals[0].orientation  = Orientation::E;
  f.fp.terminals[0].x           += 100;
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
}

TEST(RoutingConnectivity, DieTerminalRequiresUniquePlacedRectangularGeometry) {
  Fixture  f;
  Terminal terminal;
  terminal.name   = "out";
  terminal.x      = 1100;
  terminal.y      = 100;
  terminal.status = Status::FIXED;
  terminal.shapes = {
      {"M1", {-10, -10, 10, 10}}
  };
  f.fp.terminals.push_back(terminal);
  f.net.connections[1] = {"PIN", "out"};
  EXPECT_TRUE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  f.fp.terminals[0].status = Status::UNPLACED;
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  f.fp.terminals[0] = terminal;
  f.fp.terminals.push_back(terminal);
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  f.fp.terminals.pop_back();
  f.fp.terminals[0].shapes[0].rect.xh = -10;
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
  f.fp.terminals[0].shapes.clear();
  EXPECT_FALSE(check_route_connectivity(f.tech, f.fp, f.net).connected);
}

TEST(RoutingConnectivity, ConductorsRequireEveryRealIncomingComponent) {
  Fixture                  f;
  auto                     paths = f.net.wires.front().paths;
  const std::vector<Shape> incoming{
      {"M1",    {90, 90, 110, 110}},
      {"M1", {1090, 90, 1110, 110}}
  };
  EXPECT_TRUE(check_conductor_connectivity(f.tech, f.fp, paths, incoming, {}).connected);
  // Omitting a landing cannot earn credit merely because both pieces have
  // the same owner. Every immutable conductor remains an obligation.
  paths.erase(paths.begin() + 1);
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, paths, incoming, {}).connected);
  paths = f.net.wires.front().paths;
  paths.push_back({
      "M2",
      {},
      {},
      {Route_point{300, 300, 0}, Route_point{400, 300, 0}}
  });
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, paths, incoming, {}).connected);
  // The existing whole-net API still requires its original source endpoints.
  f.net.connections.clear();
  EXPECT_EQ(check_route_connectivity(f.tech, f.fp, f.net).reason, "missing endpoints or route");
}

TEST(RoutingConnectivity, ConductorLayersNeedAnActualNativeVia) {
  Fixture                  f;
  const std::vector<Shape> incoming{
      {"M1", {90, 90, 110, 110}},
      {"M2", {90, 90, 110, 110}}
  };
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, incoming, {}).connected);
  const std::vector<Route_path> via{f.net.wires.front().paths.front()};
  EXPECT_TRUE(check_conductor_connectivity(f.tech, f.fp, via, incoming, {}).connected);
  f.tech.vias["via"].shapes[1].rect = {.02, .02, .03, .03};
  EXPECT_EQ(check_conductor_connectivity(f.tech, f.fp, via, incoming, {}).reason, "cut does not contact both via landings");
}

TEST(RoutingConnectivity, OnlyActualPhysicalPinsHaveIntrinsicShapeEquivalence) {
  Fixture                  f;
  const std::vector<Shape> pieces{
      {"M1",    {90, 90, 110, 110}},
      {"M1", {1090, 90, 1110, 110}}
  };
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, pieces, {}).connected);
  const std::vector<std::vector<Shape>> one_pin{pieces};
  EXPECT_TRUE(check_conductor_connectivity(f.tech, f.fp, {}, {}, one_pin).connected);
  const std::vector<std::vector<Shape>> two_pins{{pieces[0]}, {pieces[1]}};
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, {}, two_pins).connected);
  EXPECT_TRUE(check_conductor_connectivity(f.tech, f.fp, f.net.wires.front().paths, {}, two_pins).connected);
}

TEST(RoutingConnectivity, ConductorInputMustBeNonemptyNativeMetal) {
  Fixture f;
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, {}, {}).connected);
  const std::vector<std::vector<Shape>> empty_pin(1);
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, {}, empty_pin).connected);
  std::vector<Shape> metal{
      {"M1", {10, 10, 20, 20}}
  };
  EXPECT_TRUE(check_conductor_connectivity(f.tech, f.fp, {}, metal, {}).connected);
  metal.push_back({
      "M1",
      {20, 10, 30, 20}
  });
  EXPECT_TRUE(check_conductor_connectivity(f.tech, f.fp, {}, metal, {}).connected);
  metal.back().rect = {20, 20, 30, 30};
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, metal, {}).connected);
  metal.resize(1);
  metal.front().rect.xl = 10.5;
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, metal, {}).connected);
  metal.front() = {
      "V1",
      {10, 10, 20, 20}
  };
  EXPECT_FALSE(check_conductor_connectivity(f.tech, f.fp, {}, metal, {}).connected);
}
}  // namespace livehd::physical
