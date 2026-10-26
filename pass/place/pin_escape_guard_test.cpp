// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pin_escape_guard.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement {
namespace {
using namespace physical;
Technology technology() {
  Technology t;
  t.manufacturing_grid = .001;
  t.routing_layers     = {"M1", "M2"};
  for (const auto& name : t.routing_layers) {
    auto& layer         = t.layers[name];
    layer.width         = .018;
    layer.direction     = name == "M1" ? "VERTICAL" : "HORIZONTAL";
    layer.rules.loaded  = true;
    layer.rules.area    = .000666;
    layer.rules.spacing = {
        {.018, {}, {}}
    };
  }
  t.layers["M2"].rules.min_size = {
      {.037, .018}
  };
  t.layers["M2"].rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
  t.cut_layers["V1"].width                                    = .018;
  t.cut_layers["V1"].spacing                                  = {.018};
  t.vias["VIA12"].shapes                                      = {
      {"M1", {-.009, -.011, .009, .011}},
      {"M2", {-.014, -.009, .014, .009}},
      {"V1", {-.009, -.009, .009, .009}}
  };
  auto& cell     = t.macros["cell"];
  cell.klass     = "CORE";
  cell.width     = .162;
  cell.height    = .270;
  cell.pins["A"] = {"INPUT", "SIGNAL", {{"M1", {.018, .027, .036, .243}}}};
  return t;
}
Floorplan frame() {
  Floorplan f;
  f.die    = {0, 0, 3000, 3000};
  f.tracks = {
      {"Y", "M2", 1045, 6, 36}
  };
  Ann_place place(1000, 1000, 162, 270);
  place.status = Status::PLACED;
  f.components.push_back({"u0", "cell", place});
  return f;
}
const Pin_access_layers& asap7_access() { return tech_profile("asap7").route.access; }

TEST(PinEscapeGuard, ProtectClockAndSupplyPinsIncludingUnassignedAndWildcardPins) {
  for (const auto* use : {"CLOCK", "POWER", "GROUND"}) {
    for (int mode = 0; mode < 3; ++mode) {
      auto tech                         = technology();
      tech.macros["cell"].pins["A"].use = use;
      auto fp                           = frame();
      if (mode != 0) {
        Net net;
        net.name = "control";
        net.use  = use;
        net.connections.push_back({mode == 2 ? "*" : "u0", "A"});
        fp.special_nets.push_back(net);
      }
      auto             occupied = routing_occupancy(tech, fp, fp.dbu);
      Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
      ASSERT_EQ(guard.protected_pins(), 1) << use << " mode=" << mode;
      EXPECT_TRUE(guard.check({}, "unrelated").legal);
      const auto blocked = guard.check(
          {
              {"M2", {900, 1027, 1200, 1261}}
      },
          "foreign");
      EXPECT_FALSE(blocked.legal) << use << " mode=" << mode;
      EXPECT_NE(blocked.reason.find("u0:A"), std::string::npos);
    }
  }
}

TEST(PinEscapeGuard, PartialWiringDoesNotRetireUnconnectedPin) {
  auto tech = technology();
  auto fp   = frame();
  Net  net;
  net.name = "partial";
  net.connections.push_back({"u0", "A"});
  Route_path path;
  path.layer = "M2";
  path.steps.push_back(Route_point{2000, 1045});
  path.steps.push_back(Route_point{2500, 1045});
  net.wires.push_back({"FIXED", {path}});
  fp.nets.push_back(net);
  auto             occupied = routing_occupancy(tech, fp, fp.dbu);
  Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
  ASSERT_EQ(guard.protected_pins(), 1);
  EXPECT_FALSE(guard
                   .check(
                       {
                           {"M2", {900, 1027, 1200, 1261}}
  },
                       "foreign")
                   .legal);
}

TEST(PinEscapeGuard, ExhaustedAccessSearchCannotSilentlyDropProtection) {
  auto tech = technology();
  auto fp   = frame();
  fp.nets.push_back({"unrouted", {{"u0", "A"}}, {}, "CLOCK"});
  auto             occupied = routing_occupancy(tech, fp, fp.dbu);
  Pin_escape_guard bounded(tech, fp, occupied, 1, {}, asap7_access());
  const auto       trial = bounded.check({}, "foreign");
  EXPECT_FALSE(trial.legal);
  EXPECT_NE(trial.reason.find("budget"), std::string::npos);
  Pin_escape_guard sufficient(tech, fp, occupied, 1000, {}, asap7_access());
  EXPECT_TRUE(sufficient.check({}, "foreign").legal);
}

TEST(PinEscapeGuard, SupplyAttachmentRequiresActualRouteNotItsOwnPinRectangle) {
  auto tech                         = technology();
  tech.macros["cell"].pins["A"].use = "POWER";
  auto fp                           = frame();
  fp.special_nets.push_back({"VDD", {{"u0", "A"}}, {}, "POWER"});
  auto             occupied = routing_occupancy(tech, fp, fp.dbu);
  Pin_escape_guard unconnected(tech, fp, occupied, 1000, {}, asap7_access());
  ASSERT_EQ(unconnected.protected_pins(), 1);
  occupied.insert({
      {"M1", {1018, 1027, 1036, 1243}},
      "VDD",
      true
  });
  Pin_escape_guard connected(tech, fp, occupied, 1000, {}, asap7_access());
  EXPECT_EQ(connected.protected_pins(), 0);
}

TEST(PinEscapeGuard, UnsupportedControlAccessReservesItsCellButNotDistantRouting) {
  for (const auto* use : {"CLOCK", "POWER", "GROUND"}) {
    auto  tech = technology();
    auto& pin  = tech.macros["cell"].pins["A"];
    pin.use    = use;
    pin.shapes = {
        {"M2", {.018, .027, .100, .045}}
    };
    auto fp = frame();
    fp.nets.push_back({"control", {{"u0", "A"}}, {}, use});
    auto             occupied = routing_occupancy(tech, fp, fp.dbu);
    Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
    EXPECT_EQ(guard.protected_pins(), 0);
    EXPECT_EQ(guard.unresolved_pins(), 1);
    // One shared reservation must protect each queried metal/cut layer with
    // that layer's own spacing, and must still honor net ownership.
    for (const auto* layer : {"M1", "M2", "V1"}) {
      EXPECT_FALSE(guard
                       .check(
                           {
                               {layer, {1020, 1030, 1038, 1048}}
      },
                           "foreign")
                       .legal);
      EXPECT_TRUE(guard
                      .check(
                          {
                              {layer, {1020, 1030, 1038, 1048}}
      },
                          "control")
                      .legal);
      EXPECT_TRUE(guard
                      .check(
                          {
                              {layer, {2020, 2030, 2038, 2048}}
      },
                          "foreign")
                      .legal);
    }

    const auto near = guard.check(
        {
            {"M2", {1000, 1060, 1200, 1078}}
    },
        "foreign");
    EXPECT_FALSE(near.legal) << use;
    EXPECT_NE(near.reason.find("unresolved pin access: u0:A"), std::string::npos);
    if (std::string(use) != "CLOCK") {
      // A horizontal escape needs extension along X, not an isotropic halo.
      EXPECT_TRUE(guard
                      .check(
                          {
                              {"M2", {1000, 1100, 1200, 1118}}
      },
                          "foreign")
                      .legal);
      EXPECT_FALSE(guard
                       .check(
                           {
                               {"M2", {1160, 1027, 1180, 1045}}
      },
                           "foreign")
                       .legal);
    }
    EXPECT_TRUE(guard
                    .check(
                        {
                            {"M2", {2000, 2100, 2200, 2118}}
    },
                        "foreign")
                    .legal);
    if (std::string(use) == "CLOCK") {
      const std::vector<Shape> outside{
          {"M2", {1500, 1150, 1550, 1168}}
      };
      EXPECT_TRUE(guard.check(outside, "foreign").legal);
      tech.layers["M8"]              = tech.layers.at("M2");
      tech.vias["wide_upper"].shapes = {
          {"M8", {-1, -1, 1, 1}}
      };
      Pin_escape_guard upper_via(tech, fp, occupied, 1000, {}, asap7_access());
      EXPECT_TRUE(upper_via.check(outside, "foreign").legal);
      Generated_via generated;
      generated.layers      = {"M1", "V1", "M2"};
      generated.cut_size    = {18, 18};
      generated.cut_spacing = {18, 18};
      generated.enclosure   = {11, 14, 14, 11};
      generated.row_col     = std::array<int64_t, 2>{2, 3};
      generated.origin      = std::array<int64_t, 2>{400, 0};
      fp.vias.push_back({"wide", {}, generated});
      Pin_escape_guard wide_via(tech, fp, occupied, 1000, {}, asap7_access());
      EXPECT_TRUE(wide_via.check(outside, "foreign").legal);  // Unused DEF templates are not access candidates.
      tech.vias["VIA12"].shapes[1].rect = {-.4, -.009, .4, .009};
      Pin_escape_guard large_access_via(tech, fp, occupied, 1000, {}, asap7_access());
      EXPECT_FALSE(large_access_via.check(outside, "foreign").legal);
    }
    // Existing same-net routing preserves this physical supply attachment.
    if (std::string(use) != "CLOCK") {
      occupied.insert({
          {"M2", {1018, 1027, 1100, 1045}},
          "control",
          true
      });
      Pin_escape_guard attached(tech, fp, occupied, 1000, {}, asap7_access());
      EXPECT_TRUE(attached
                      .check(
                          {
                              {"M2", {1000, 1060, 1200, 1078}}
      },
                          "foreign")
                      .legal);
    }
  }
}

TEST(PinEscapeGuard, CumulativeBlocksRollbackAndLastAccess) {
  auto tech = technology();
  auto fp   = frame();
  Net  net;
  net.name = "unrouted";
  net.connections.push_back({"u0", "A"});
  fp.nets.push_back(net);
  auto               occupied = routing_occupancy(tech, fp, fp.dbu);
  Signal_metal_rules rules(tech, fp);
  auto access = enumerate_pin_access(tech, fp, fp.components[0], "A", occupied, rules, 1000, net.name, asap7_access());
  std::set<int64_t>  tracks;
  for (const auto& point : access.points) {
    tracks.insert(point.y);
  }
  ASSERT_GT(tracks.size(), 1);
  Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
  ASSERT_EQ(guard.protected_pins(), 1);
  std::vector<Shape> all;
  for (auto y : tracks) {
    all.push_back({
        "M2",
        {1000, double(y - 9), 1100, double(y + 9)}
    });
  }
  EXPECT_FALSE(guard.check(all, "blocker").legal);
  // A rejected trial must not consume any escapes. Duplicate shapes must not
  // count the same access twice; cumulative accepted routes still consume it.
  for (size_t i = 0; i + 1 < all.size(); ++i) {
    auto trial = guard.check({all[i], all[i]}, "wire" + std::to_string(i));
    ASSERT_TRUE(trial.legal) << trial.reason;
    guard.commit(trial);
    EXPECT_THROW(guard.commit(trial), std::invalid_argument);
  }
  EXPECT_FALSE(guard.check({all.back()}, "last_blocker").legal);
  EXPECT_TRUE(guard.check(all, net.name).legal);  // Its own connection may use it.
  EXPECT_FALSE(guard.check({}, "timeout", [] { return true; }).legal);
}
TEST(PinEscapeGuard, PreserveEitherDirectionalRouterEscape) {
  auto tech = technology();
  tech.layers["M2"].rules.properties["LEF58_SPACING"]
      = {"SPACING .018 ENDOFLINE .025 WITHIN .02 ENDTOEND .031 PARALLELEDGE .025 WITHIN .02 ;"};
  auto fp   = frame();
  fp.tracks = {
      {"Y", "M2", 1045, 1, 36}
  };
  Net net;
  net.name = "unrouted";
  net.connections.push_back({"u0", "A"});
  fp.nets.push_back(net);
  auto               occupied = routing_occupancy(tech, fp, fp.dbu);
  Signal_metal_rules rules(tech, fp);
  auto access = enumerate_pin_access(tech, fp, fp.components[0], "A", occupied, rules, 1000, net.name, asap7_access());
  ASSERT_EQ(access.points.size(), 1);
  const auto& point = access.points.front();
  // Short escapes clear both traces. Router probes extend 63 DBU from the
  // center and leave only a 26 DBU EOL gap, below the required 31 DBU.
  const Shape left{
      "M2",
      {double(point.x - 120), double(point.y - 9), double(point.x - 89), double(point.y + 9)}
  };
  const Shape right{
      "M2",
      {double(point.x + 89), double(point.y - 9), double(point.x + 120), double(point.y + 9)}
  };
  EXPECT_TRUE(rules.spacing(point.escape, left).legal);
  EXPECT_TRUE(rules.spacing(point.escape, right).legal);
  Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
  auto             trial = guard.check({left}, "left");
  ASSERT_TRUE(trial.legal) << trial.reason;
  guard.commit(trial);
  EXPECT_FALSE(guard.check({right}, "right").legal);
  EXPECT_TRUE(guard.check({right}, net.name).legal);
  occupied.insert({left, "existing"});
  Pin_escape_guard rebuilt(tech, fp, occupied, 1000, {}, asap7_access());
  EXPECT_EQ(rebuilt.protected_pins(), 1);
  EXPECT_FALSE(rebuilt.check({right}, "right").legal);
}
TEST(PinEscapeGuard, BareLandingEolMustSurviveAlongsideExtendedEscape) {
  auto tech = technology();
  tech.layers["M2"].rules.properties["LEF58_SPACING"]
      = {"SPACING .025 ENDOFLINE .025 WITHIN .02 ENDTOEND .031 PARALLELEDGE .025 WITHIN .02 ;"};
  // A short landing exposes an additional EOL edge which disappears when
  // the minimum-area escape extends it along the preferred direction.
  tech.vias["VIA12"].shapes[1].rect = {-.009, -.009, .009, .009};
  auto fp                           = frame();
  fp.tracks                         = {
      {"Y", "M2", 1045, 1, 36}
  };
  Net net;
  net.name = "unrouted";
  net.connections.push_back({"u0", "A"});
  fp.nets.push_back(net);
  auto               occupied = routing_occupancy(tech, fp, fp.dbu);
  Signal_metal_rules rules(tech, fp);
  auto access = enumerate_pin_access(tech, fp, fp.components[0], "A", occupied, rules, 1000, net.name, asap7_access());
  ASSERT_FALSE(access.points.empty());
  const Shape trace{
      "M2",
      {900, 1074, 1200, 1092}
  };
  for (const auto& point : access.points) {
    EXPECT_TRUE(rules.spacing(point.escape, trace).legal);
    Shape landing{
        "M2",
        {double(point.x - 9), double(point.y - 9), double(point.x + 9), double(point.y + 9)}
    };
    EXPECT_FALSE(rules.spacing(landing, trace).legal);
  }
  Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
  EXPECT_FALSE(guard.check({trace}, "foreign").legal);
  EXPECT_TRUE(guard.check({trace}, net.name).legal);
  occupied.insert({trace, "existing"});
  Pin_escape_guard rebuilt(tech, fp, occupied, 1000, {}, asap7_access());
  EXPECT_EQ(rebuilt.protected_pins(), 0);  // No initially legal bare landing.
}
TEST(PinEscapeGuard, PreserveACompatibleWholeCellCombination) {
  auto tech = technology();
  tech.layers["M2"].rules.properties["LEF58_SPACING"]
      = {"SPACING .018 ENDOFLINE .025 WITHIN .02 ENDTOEND .031 PARALLELEDGE .025 WITHIN .02 ;"};
  tech.macros["cell"].pins["B"] = {"INPUT", "SIGNAL", {{"M1", {.072, .027, .090, .243}}}};
  auto fp                       = frame();
  fp.tracks                     = {
      {"Y", "M2", 1045, 2, 72}
  };
  for (const auto* name : {"A", "B"}) {
    Net net;
    net.name = name;
    net.connections.push_back({"u0", name});
    fp.nets.push_back(net);
  }
  auto             occupied = routing_occupancy(tech, fp, fp.dbu);
  Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
  ASSERT_EQ(guard.protected_pins(), 2);
  // Both pins retain individually legal access on Y=1045. Those landings
  // face each other with only 26 DBU between ends; the rule requires 31.
  auto trial = guard.check(
      {
          {"M2", {900, 1108, 1200, 1126}}
  },
      "foreign");
  EXPECT_FALSE(trial.legal);
  EXPECT_EQ(trial.reason, "route blocks compatible cell pin access");
  EXPECT_TRUE(guard.check({}, "unrelated").legal);
  // Completing A retires its hypothetical landing; actual routed geometry
  // still participates in the normal route oracle and escape checks.
  EXPECT_TRUE(guard
                  .check(
                      {
                          {"M2", {900, 1108, 1200, 1126}}
  },
                      "A")
                  .legal);

  // A compatible combination can also be lost cumulatively across routes.
  fp.tracks = {
      {"Y", "M2", 1045, 3, 72}
  };
  auto             occupied_three = routing_occupancy(tech, fp, fp.dbu);
  Pin_escape_guard cumulative(tech, fp, occupied_three, 1000, {}, asap7_access());
  auto             first = cumulative.check(
      {
          {"M2", {900, 1180, 1200, 1198}}
  },
      "first");
  ASSERT_TRUE(first.legal) << first.reason;
  cumulative.commit(first);
  auto second = cumulative.check(
      {
          {"M2", {900, 1108, 1200, 1126}}
  },
      "second");
  EXPECT_FALSE(second.legal);
  EXPECT_EQ(second.reason, "route blocks compatible cell pin access");
  EXPECT_THROW(cumulative.commit(second), std::invalid_argument);
  // Rejected work leaves the original two-track combination intact.
  EXPECT_TRUE(cumulative.check({}, "third").legal);
  EXPECT_TRUE(cumulative
                  .check(
                      {
                          {"M2", {900, 1108, 1200, 1126}}
  },
                      "A")
                  .legal);
}
TEST(PinEscapeGuard, CutSpacingAndRetiredNet) {
  auto tech = technology();
  auto fp   = frame();
  Net  net;
  net.name = "unrouted";
  net.connections.push_back({"u0", "A"});
  fp.nets.push_back(net);
  auto               occupied = routing_occupancy(tech, fp, fp.dbu);
  Signal_metal_rules rules(tech, fp);
  auto access = enumerate_pin_access(tech, fp, fp.components[0], "A", occupied, rules, 1000, net.name, asap7_access());
  ASSERT_FALSE(access.points.empty());
  std::vector<Shape> cuts;
  for (const auto& point : access.points) {
    cuts.push_back({
        "V1",
        {double(point.x - 9), double(point.y - 9), double(point.x + 9), double(point.y + 9)}
    });
  }
  Pin_escape_guard guard(tech, fp, occupied, 1000, {}, asap7_access());
  EXPECT_FALSE(guard.check(cuts, "foreign").legal);
  auto own = guard.check(cuts, net.name);
  ASSERT_TRUE(own.legal);
  guard.commit(own);
  EXPECT_TRUE(guard.check(cuts, "foreign").legal);  // Actual wires remain in the routing oracle.
}
}  // namespace
}  // namespace livehd::placement
