#include "routing_geometry.hpp"

#include <algorithm>

#include "gtest/gtest.h"

namespace livehd::physical {
namespace {
Technology technology() {
  Technology tech;
  tech.layers["M1"].width          = .018;
  tech.layers["M2"].width          = .020;
  tech.cut_layers["V1"].width      = .018;
  tech.via_rules["M2_M1"].generate = true;
  tech.vias["VIA12"].shapes        = {
      {"M1", {-.009, -.011, .009, .011}},
      {"V1", {-.009, -.009, .009, .009}},
      {"M2", {-.014, -.009, .014, .009}}
  };
  return tech;
}
Floorplan generated(int64_t cut = 18) {
  Floorplan     fp;
  Generated_via g;
  g.rule        = "M2_M1";
  g.layers      = {"M1", "V1", "M2"};
  g.cut_size    = {cut, cut};
  g.cut_spacing = {18, 18};
  g.enclosure   = {0, 0, 2, 0};
  g.row_col     = {2, 3};
  g.origin      = {4, -5};
  g.offset      = {1, 2, 3, 4};
  Via via;
  via.name      = "array12";
  via.generated = g;
  fp.vias.push_back(via);
  return fp;
}
}  // namespace

TEST(RoutingGeometry, TaperRuleUsesWidthAfterViaAndDefaultExtension) {
  Floorplan fp;
  fp.nondefault_rules["wide"] = {
      {"M1", 40},
      {"M2", 80}
  };
  Net net;
  net.wires.push_back(
      {"FIXED", {{"M1", {}, {}, {Route_point{100, 100, {}}, Route_via{"VIA12"}, Route_point{500, 100, {}}}, "wide"}}});
  const auto shapes = route_geometry(technology(), fp, net);
  ASSERT_EQ(shapes.size(), 4);
  EXPECT_EQ(shapes.back(),
            (Shape{
                "M2",
                {60, 60, 540, 140}
  }));
  fp.nondefault_rules["wide"].erase("M2");
  EXPECT_THROW(route_geometry(technology(), fp, net), std::runtime_error);
}
TEST(RoutingGeometry, TrialWidthRulesMatchCommittedGeometryWithoutChangingFrame) {
  Floorplan               fp;
  const Route_width_rules trial{
      {"wide", {{"M1", 40}, {"M2", 80}}}
  };
  Net net;
  net.wires.push_back(
      {"FIXED", {{"M1", {}, {}, {Route_point{100, 100, {}}, Route_via{"VIA12"}, Route_point{500, 100, {}}}, "wide"}}});
  EXPECT_THROW(route_geometry(technology(), fp, net), std::runtime_error);
  const auto shapes = route_geometry(technology(), fp, net, false, &trial);
  EXPECT_TRUE(fp.nondefault_rules.empty());
  fp.nondefault_rules = trial;
  EXPECT_EQ(shapes, route_geometry(technology(), fp, net));
  EXPECT_EQ(shapes, route_geometry(technology(), fp, net, false, &trial));
  fp.nondefault_rules["wide"]["M2"] = 60;
  EXPECT_THROW(route_geometry(technology(), fp, net, false, &trial), std::runtime_error);
  EXPECT_EQ(fp.nondefault_rules.at("wide").at("M2"), 60);
}

TEST(RoutingGeometry, RepeatedViaDefinitionSwitchesLayersInBothDirections) {
  auto      tech = technology();
  Floorplan fp;
  Net       net;
  net.wires.push_back({"FIXED",
                       {{"M1",
                         {},
                         {},
                         {Route_point{100, 100, {}},
                          Route_via{"VIA12"},
                          Route_point{200, 100, {}},
                          Route_via{"VIA12"},
                          Route_point{300, 100, {}}}}}});
  const auto shapes = route_geometry(tech, fp, net);
  ASSERT_EQ(shapes.size(), 8);
  EXPECT_EQ(shapes[3],
            (Shape{
                "M2",
                {90, 90, 210, 110}
  }));
  EXPECT_EQ(shapes[7],
            (Shape{
                "M1",
                {191, 91, 309, 109}
  }));
  tech.layers["M3"].width = .018;
  net.wires.front().paths.push_back({
      "M3",
      {},
      {},
      {Route_point{400, 100, {}}, Route_via{"VIA12"}}
  });
  EXPECT_THROW(route_geometry(tech, fp, net), std::runtime_error);
}
TEST(RoutingGeometry, SignalPatchUsesAnchorAndViaLayer) {
  Floorplan fp;
  Net       net;
  net.wires.push_back(
      {"FIXED",
       {{"M1",
         {},
         {},
         {Route_point{100, 200, {}}, Route_via{"VIA12"}, Route_rect{-12, -38, 12, -27}, Route_point{300, 200, {}}}}}});
  const auto shapes = route_geometry(technology(), fp, net);
  ASSERT_EQ(shapes.size(), 5);
  EXPECT_EQ(shapes[3],
            (Shape{
                "M2",
                {88, 162, 112, 173}
  }));
  EXPECT_EQ(shapes[4],
            (Shape{
                "M2",
                {90, 190, 310, 210}
  }));
  std::vector<Shape> streamed;
  visit_route_geometry(technology(), fp, net, [&](const Shape& shape) { streamed.push_back(shape); });
  EXPECT_EQ(streamed, shapes);
  size_t visits = 0;
  EXPECT_THROW(visit_route_geometry(technology(),
                                    fp,
                                    net,
                                    [&](const Shape&) {
                                      if (++visits == 2) {
                                        throw std::runtime_error("cancelled geometry visitor");
                                      }
                                    }),
               std::runtime_error);
  EXPECT_EQ(visits, 2);
  EXPECT_EQ(route_geometry(technology(), fp, net), shapes);
  net.wires[0].paths[0].steps[2] = Route_rect{0, 0, INT32_MAX, 10};
  EXPECT_THROW(route_geometry(technology(), fp, net), std::runtime_error);
  net.wires[0].paths[0].steps[2] = Route_rect{0, 0, 0, 10};
  EXPECT_THROW(route_geometry(technology(), fp, net), std::runtime_error);
}
TEST(RoutingGeometry, GeneratedArrayUsesDefPitchOriginAndOffsets) {
  const auto shapes = via_geometry(technology(), generated(), {"array12"});
  ASSERT_EQ(shapes.size(), 8);
  EXPECT_EQ(shapes[0],
            (Shape{
                "V1",
                {-41, -32, -23, -14}
  }));
  EXPECT_EQ(shapes[2],
            (Shape{
                "V1",
                {31, -32, 49, -14}
  }));
  EXPECT_EQ(shapes[3],
            (Shape{
                "V1",
                {-41, 4, -23, 22}
  }));
  EXPECT_EQ(shapes[6],
            (Shape{
                "M2",
                {-40, -28, 54, 26}
  }));
  EXPECT_EQ(shapes[7],
            (Shape{
                "M1",
                {-40, -30, 50, 24}
  }));
  const auto odd = via_geometry(technology(), generated(19), {"array12"});
  EXPECT_EQ(odd[0],
            (Shape{
                "V1",
                {-42, -33, -23, -14}
  }));
  EXPECT_EQ(odd[2],
            (Shape{
                "V1",
                {32, -33, 51, -14}
  }));
  EXPECT_EQ(odd[6],
            (Shape{
                "M2",
                {-41, -29, 56, 27}
  }));
}
TEST(RoutingGeometry, ViaUsesFloorplanUnitsAndOriginRotation) {
  auto fp           = Floorplan{};
  fp.dbu            = 2000;  // independent of the LEF technology's default 1000
  const auto shapes = via_geometry(technology(), fp, {"VIA12", Orientation::W});
  EXPECT_EQ(shapes[0],
            (Shape{
                "M1",
                {-22, -18, 22, 18}
  }));
  const auto rotated = via_geometry(technology(), generated(), {"array12", Orientation::FS});
  EXPECT_EQ(rotated[0],
            (Shape{
                "V1",
                {-41, 14, -23, 32}
  }));
}
TEST(RoutingGeometry, PathChangesLayerAtViaAndKeepsFlushExtensions) {
  Floorplan fp;
  Net       net;
  net.name = "n";
  Wire       wire;
  Route_path path;
  path.layer = "M1";
  path.steps = {
      Route_point{100, 100, 0},
      Route_point{200, 100, 2},
      Route_via{"VIA12"},
      Route_point{200, 200, 0}
  };
  wire.paths.push_back(path);
  net.wires.push_back(wire);
  const auto shapes = route_geometry(technology(), fp, net);
  ASSERT_EQ(shapes.size(), 5);
  EXPECT_EQ(shapes[0],
            (Shape{
                "M1",
                {100, 91, 202, 109}
  }));
  EXPECT_EQ(shapes[1],
            (Shape{
                "M1",
                {191, 89, 209, 111}
  }));
  EXPECT_EQ(shapes[2],
            (Shape{
                "V1",
                {191, 91, 209, 109}
  }));
  EXPECT_EQ(shapes[4],
            (Shape{
                "M2",
                {190, 98, 210, 200}
  }));
  auto& steps = net.wires[0].paths[0].steps;
  steps       = {
      Route_point{200, 100, 0},
      Route_point{100, 100, 2}
  };
  EXPECT_EQ(route_geometry(technology(), fp, net)[0],
            (Shape{
                "M1",
                {98, 91, 200, 109}
  }));
}
TEST(RoutingGeometry, SpecialWidthsAndInvalidGeometry) {
  Floorplan fp;
  Net       net;
  net.name = "VDD";
  Wire       wire;
  Route_path path;
  path.layer = "M2";
  path.width = 120;
  path.steps = {
      Route_point{   0, 0, 0},
      Route_point{1000, 0, 0}
  };
  wire.paths.push_back(path);
  net.wires.push_back(wire);
  EXPECT_EQ(route_geometry(technology(), fp, net, true)[0],
            (Shape{
                "M2",
                {0, -60, 1000, 60}
  }));
  EXPECT_THROW(route_geometry(technology(), fp, net), std::runtime_error);
  net.wires[0].paths[0].width.reset();
  EXPECT_THROW(route_geometry(technology(), fp, net, true), std::runtime_error);
  net.wires[0].paths[0].steps[1] = Route_point{1000, 1, {}};
  EXPECT_THROW(route_geometry(technology(), fp, net), std::runtime_error);
  net.wires[0].paths[0].steps = {Route_via{"VIA12"}};
  EXPECT_THROW(route_geometry(technology(), fp, net), std::runtime_error);
  auto bad                       = generated();
  bad.vias[0].generated->row_col = {1000000, 1000000};
  EXPECT_THROW(via_geometry(technology(), bad, {"array12"}), std::runtime_error);
  auto tech = technology();
  tech.vias["VIA12"].unsupported.insert("MASK");
  EXPECT_THROW(via_geometry(tech, fp, {"VIA12"}), std::runtime_error);
  EXPECT_THROW(via_geometry(technology(), fp, {"missing"}), std::runtime_error);
}
TEST(RoutingGeometry, SpecialStripeEndpointsAreFlushByDefault) {
  Floorplan fp;
  Net       net;
  net.name = "VDD";
  Route_path path;
  path.layer = "M2";
  path.width = 288;
  path.steps = {
      Route_point{ 2700, 3597, {}},
      Route_point{15552, 3597, {}}
  };
  net.wires.push_back({"ROUTED", {path}});
  // Independent OpenDB pdngen rectangle: no half-width extension in X.
  EXPECT_EQ(route_geometry(technology(), fp, net, true),
            (std::vector<Shape>{
                {"M2", {2700, 3453, 15552, 3741}}
  }));
  std::reverse(net.wires[0].paths[0].steps.begin(), net.wires[0].paths[0].steps.end());
  EXPECT_EQ(route_geometry(technology(), fp, net, true),
            (std::vector<Shape>{
                {"M2", {2700, 3453, 15552, 3741}}
  }));
}
}  // namespace livehd::physical
