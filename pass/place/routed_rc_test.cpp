// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "routed_rc.hpp"

#include <numeric>

#include "gtest/gtest.h"
namespace livehd::placement {
namespace {
using namespace physical;
struct Fixture {
  Technology tech;
  Floorplan  fp;
  Net        net;
  Fixture() {
    tech.routing_layers         = {"M1", "M2"};
    tech.layers["M1"].width     = .02;
    tech.layers["M2"].width     = .02;
    tech.layers["M2"].direction = "HORIZONTAL";
    tech.cut_layers["V1"].width = .01;
    tech.vias["via"].shapes = {{"M1", {-.01, -.01, .01, .01}}, {"V1", {-.005, -.005, .005, .005}}, {"M2", {-.01, -.01, .01, .01}}};
    auto& master            = tech.macros["cell"];
    master.width            = .2;
    master.height           = .2;
    master.pins["A"].shapes = {{"M1", {.09, .09, .11, .11}}};
    for (auto [name, x] : {std::pair{"a", 0}, std::pair{"b", 1000}}) {
      Ann_place p(x, 0, 200, 200);
      p.status = Status::PLACED;
      fp.components.push_back({name, "cell", p});
      net.connections.push_back({name, "A"});
    }
    net.wires.push_back({"FIXED",
                         {{"M1", {}, {}, {Route_point{100, 100, {}}, Route_via{"via"}}},
                          {"M1", {}, {}, {Route_point{1100, 100, {}}, Route_via{"via"}}},
                          {"M2", {}, {}, {Route_point{100, 100, 0}, Route_point{1100, 100, 0}}}}});
  }
};
Route_technology_rc model() {
  Route_technology_rc rc;
  rc.layers["M2"]             = {2, 3};
  rc.cut_resistance_ohm["V1"] = 7;
  return rc;
}
std::vector<Route_rc_pin> pins() { return {{"a:A", "M1", 100, 100}, {"b:A", "M1", 1100, 100}}; }
double                    capacitance(const Rc_net& net) {
  double c = 0;
  for (const auto& n : net.nodes) {
    c += n.capacitance_ff;
  }
  return c;
}
double resistance(const Rc_net& net) {
  double r = 0;
  for (const auto& e : net.edges) {
    r += e.resistance_ohm;
  }
  return r;
}
}  // namespace
TEST(RoutedRc, PathWidthsUnionWithoutDoubleCounting) {
  Fixture f;
  f.fp.nondefault_rules["wide"]      = {{"M1", 20}, {"M2", 40}};
  f.net.wires[0].paths[2].taper_rule = "wide";
  const auto rc                      = routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model());
  EXPECT_NEAR(rc.wire_length_um, 1, 1e-12);
  EXPECT_NEAR(capacitance(rc), 6, 1e-12);
  EXPECT_NEAR(resistance(rc), 15, 1e-12);
  auto ordinary = f.net.wires[0].paths[2];
  ordinary.taper_rule.clear();
  f.net.wires[0].paths.push_back(ordinary);
  EXPECT_EQ(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), rc);
}
TEST(RoutedRc, RealWireLengthAndViaResistance) {
  Fixture    f;
  const auto rc = routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model());
  EXPECT_DOUBLE_EQ(rc.wire_length_um, 1);
  EXPECT_DOUBLE_EQ(capacitance(rc), 3);
  EXPECT_DOUBLE_EQ(resistance(rc), 16);
  EXPECT_NO_THROW(physical::validate_rc_tree(rc));
}
TEST(RoutedRc, OverlappingPatchUsesUnionAndWidthDependentEstimate) {
  Fixture f;
  auto&   steps = f.net.wires[0].paths[2].steps;
  // A 0.2um section doubles in width; its resistance halves and its
  // area-equivalent effective capacitance doubles. No overlap is counted twice.
  steps.emplace_back(Route_rect{-650, -20, -450, 20});
  const auto rc = routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model());
  EXPECT_NEAR(rc.wire_length_um, 1, 1e-12);
  EXPECT_NEAR(resistance(rc), 15.8, 1e-12);
  EXPECT_NEAR(capacitance(rc), 3.6, 1e-12);
  steps.push_back(steps.back());
  EXPECT_EQ(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), rc);
  // An ordinary-width duplicate adds neither area nor capacitance.
  steps.back() = Route_rect{-650, -10, -450, 10};
  EXPECT_EQ(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), rc);
  steps.back()      = Route_rect{-650, -30, -550, 30};
  const auto nested = routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model());
  EXPECT_NEAR(nested.wire_length_um, 1, 1e-12);
  EXPECT_NEAR(resistance(nested), 14 + .8 * 2 + .1 + .1 * 2 / 3, 1e-12);
  EXPECT_NEAR(capacitance(nested), 3.9, 1e-12);
}
TEST(RoutedRc, VerticalPatchUsesLayerDirection) {
  Fixture f;
  f.tech.layers["M2"].direction    = "VERTICAL";
  f.fp.components[1].place.x       = 0;
  f.fp.components[1].place.y       = 1000;
  f.net.wires[0].paths[1].steps[0] = Route_point{100, 1100, {}};
  f.net.wires[0].paths[2].steps    = {Route_point{100, 100, 0}, Route_point{100, 1100, 0}, Route_rect{-20, -650, 20, -450}};
  auto accesses                    = pins();
  accesses[1].x                    = 100;
  accesses[1].y                    = 1100;
  const auto rc                    = routed_rc(f.tech, f.fp, f.net, "a:A", accesses, model());
  EXPECT_NEAR(rc.wire_length_um, 1, 1e-12);
  EXPECT_NEAR(resistance(rc), 15.8, 1e-12);
  EXPECT_NEAR(capacitance(rc), 3.6, 1e-12);
}
TEST(RoutedRc, ViaLandingMayTouchAdjacentWidthSection) {
  Fixture f;
  f.net.wires[0].paths[2].steps.emplace_back(Route_rect{-10, -20, 0, 20});
  const auto rc = routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model());
  EXPECT_NEAR(resistance(rc), 15.99, 1e-12);
  EXPECT_NEAR(capacitance(rc), 3.03, 1e-12);
  // Removing the centerline connection must still fail even though the
  // geometry reaches the via through its physical landing pad.
  f.net.wires[0].paths[2].steps = {Route_point{100, 100, 0}, Route_point{1095, 100, 0}};
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), std::invalid_argument);
}
TEST(RoutedRc, UnsupportedPatchDoesNotSilentlyLoseElectricalImpact) {
  Fixture f;
  auto&   steps = f.net.wires[0].paths[2].steps;
  steps.emplace_back(Route_rect{-650, -10, -450, 30});
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), std::invalid_argument);
  steps.back() = Route_rect{-650, -5, -450, 5};
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), std::invalid_argument);
  steps.back() = Route_rect{-650, -20, -450, 20};
  f.tech.layers["M2"].direction.clear();
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), std::invalid_argument);
}
TEST(RoutedRc, SplitsAtThirdPinAndDeduplicatesMetal) {
  Fixture f;
  auto    c = f.fp.components[0];
  c.name    = "c";
  c.place.x = 500;
  f.fp.components.push_back(c);
  f.net.connections.push_back({"c", "A"});
  f.net.wires[0].paths.push_back({"M1", {}, {}, {Route_point{600, 100, {}}, Route_via{"via"}}});
  auto access = pins();
  access.push_back({"c:A", "M1", 600, 100});
  f.net.wires[0].paths.push_back(f.net.wires[0].paths[2]);
  const auto rc = routed_rc(f.tech, f.fp, f.net, "a:A", access, model());
  EXPECT_DOUBLE_EQ(rc.wire_length_um, 1);
  EXPECT_DOUBLE_EQ(capacitance(rc), 3);
  EXPECT_DOUBLE_EQ(resistance(rc), 23);
  EXPECT_EQ(rc.edges.size() + 1, rc.nodes.size());
}
TEST(RoutedRc, MissingCharacterizationAndMisboundPinsReject) {
  Fixture f;
  auto    missing = model();
  missing.cut_resistance_ohm.clear();
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), missing), std::invalid_argument);
  missing = model();
  missing.layers.clear();
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), missing), std::invalid_argument);
  auto access = pins();
  access[0].x = 1100;
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", access, model()), std::invalid_argument);
}
TEST(RoutedRc, LoopDoesNotBecomeAnOptimisticSpanningTree) {
  Fixture f;
  f.net.wires[0].paths.push_back(
      {"M2", {}, {}, {Route_point{100, 100, 0}, Route_point{100, 200, 0}, Route_point{1100, 200, 0}, Route_point{1100, 100, 0}}});
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), std::invalid_argument);
}
TEST(RoutedRc, NonCenterlineContactIsNotSilentlyOmitted) {
  Fixture f;
  f.net.wires[0].paths.push_back({"M2", {}, {}, {Route_point{300, 120, 0}, Route_point{800, 120, 0}}});
  EXPECT_THROW(routed_rc(f.tech, f.fp, f.net, "a:A", pins(), model()), std::invalid_argument);
}
}  // namespace livehd::placement
