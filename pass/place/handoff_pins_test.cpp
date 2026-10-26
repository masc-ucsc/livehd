#include "handoff_pins.hpp"

#include "gtest/gtest.h"
#include "inou/lefdef/routing_rules.hpp"

namespace livehd::placement {
namespace ph = physical;
namespace {
ph::Technology technology() {
  ph::Technology tech;
  tech.manufacturing_grid = .001;
  for (const auto& name : {"M4", "M5"}) {
    auto& layer         = tech.layers[name];
    layer.width         = .020;
    layer.direction     = name == std::string("M4") ? "HORIZONTAL" : "VERTICAL";
    layer.rules.loaded  = true;
    layer.rules.area    = .002;
    layer.rules.spacing = {
        {.020, {}, {}}
    };
    layer.rules.properties["LEF58_RIGHTWAYONGRIDONLY"] = {"RIGHTWAYONGRIDONLY ;"};
  }
  return tech;
}
ph::Floorplan floorplan() {
  ph::Floorplan fp;
  fp.die    = {0, 0, 1000, 1000};
  fp.tracks = {
      {"Y", "M4", 10, 10, 100},
      {"X", "M5", 10, 10, 100}
  };
  fp.terminals = {
      {.name = "a", .net = "a", .x = 105, .y = 0, .status = ph::Status::PLACED},
      {.name = "b", .net = "b", .x = 105, .y = 0, .status = ph::Status::PLACED},
      {.name = "left", .net = "left", .x = 0, .y = 115, .status = ph::Status::PLACED},
      {.name = "locked", .net = "locked", .x = 950, .y = 110, .status = ph::Status::FIXED, .shapes = {{"M4", {-50, -10, 50, 10}}}}
  };
  return fp;
}
}  // namespace
TEST(HandoffPins, AlignsPreferredTracksAndAreaWithoutChangingFixedPins) {
  const auto tech = technology();
  const auto fp   = floorplan();
  const auto pins = handoff_pins(tech, fp, "M4", "M5");
  ASSERT_EQ(pins.size(), fp.terminals.size());
  EXPECT_EQ(pins[0].x, 110);
  EXPECT_EQ(pins[0].y, 50);
  EXPECT_EQ(pins[1].x, 210);
  EXPECT_EQ(pins[2].x, 50);
  EXPECT_EQ(pins[2].y, 110);
  EXPECT_EQ(pins[3], fp.terminals[3]);
  const ph::Signal_metal_rules rules(tech, fp);
  for (const auto& pin : pins) {
    ASSERT_EQ(pin.shapes.size(), 1);
    auto shape     = pin.shapes[0];
    shape.rect.xl += pin.x;
    shape.rect.xh += pin.x;
    shape.rect.yl += pin.y;
    shape.rect.yh += pin.y;
    EXPECT_TRUE(rules.rectangle(shape).legal);
    EXPECT_GE(shape.rect.xl, 0);
    EXPECT_LE(shape.rect.xh, 1000);
    EXPECT_GE(shape.rect.yl, 0);
    EXPECT_LE(shape.rect.yh, 1000);
  }
  EXPECT_EQ(fp.terminals[0].x, 105);
  EXPECT_TRUE(fp.terminals[0].shapes.empty());
}
TEST(HandoffPins, ReservesPowerAndOtherPinSpacing) {
  auto fp = floorplan();
  fp.terminals.push_back(
      {.name = "VDD", .net = "VDD", .status = ph::Status::FIXED, .shapes = {{"M5", {100, 0, 120, 200}}}, .use = "POWER"});
  const auto pins = handoff_pins(technology(), fp, "M4", "M5");
  EXPECT_EQ(pins[0].x, 10);  // Same distance as 210; deterministic lower-track tie.
  EXPECT_EQ(pins[1].x, 210);
  EXPECT_EQ(pins.back(), fp.terminals.back());
}
TEST(HandoffPins, MissingTracksAndUnsupportedRulesFailWithoutChangingInput) {
  auto fp = floorplan();
  fp.tracks.clear();
  EXPECT_THROW(handoff_pins(technology(), fp, "M4", "M5"), std::invalid_argument);
  fp        = floorplan();
  auto tech = technology();
  tech.layers["M4"].rules.unsupported.insert("unknown rule");
  EXPECT_THROW(handoff_pins(tech, fp, "M4", "M5"), std::invalid_argument);
  EXPECT_TRUE(fp.terminals[0].shapes.empty());
}
}  // namespace livehd::placement
