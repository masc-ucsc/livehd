// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "pass/route/route_tech.hpp"

#include "gtest/gtest.h"

using livehd::physical::Lef_via;
using livehd::physical::Technology;
using livehd::routing::derive_tech_stack;

namespace {
// The rules of ASAP7 M2 and M4 as its technology LEF states them, with the vias around them.
Technology asap7_like() {
  Technology t;
  t.sites["asap7sc7p5t"] = {0.054, 0.27, true};
  t.routing_layers       = {"M1", "M2", "M3", "M4", "M5", "M6", "M7", "M8"};
  const auto metal       = [&](const char* name, bool horizontal, double width, double pitch, double area) -> auto& {
    auto& l     = t.layers[name];
    l.width     = width;
    l.direction = horizontal ? "HORIZONTAL" : "VERTICAL";
    l.pitch_x = l.pitch_y = pitch;
    l.rules.loaded        = true;
    l.rules.area          = area;
    l.rules.spacing.push_back({width, {}, {}});
    return l.rules;
  };
  metal("M1", false, .018, .036, 0);
  for (const char* name : {"M2", "M3"}) {
    auto& r = metal(name, name[1] == '2', .018, .036, .000666);
    r.min_size.push_back({.037, .018});
    r.properties["LEF58_SPACING"]       = {"SPACING 0.018 ENDOFLINE 0.025 WITHIN 0.02 ENDTOEND 0.031 PARALLELEDGE 0.025 WITHIN 0.02 ;"};
    r.properties["LEF58_EOLKEEPOUT"]    = {"EOLKEEPOUT 0.025 EXTENSION 0.0 0.0125 0.031 CORNERONLY ;"};
    r.properties["LEF58_CORNERSPACING"] = {"CORNERSPACING CONVEXCORNER WIDTH 0.0 SPACING 0.02 ;"};
  }
  for (const char* name : {"M4", "M5"}) {
    auto& r = metal(name, name[1] == '4', .024, .048, .002);
    r.properties["LEF58_SPACING"]       = {"SPACING 0.024 ENDOFLINE 0.025 WITHIN 0.04 ENDTOEND 0.04 ;"};
    r.properties["LEF58_EOLKEEPOUT"]    = {"EOLKEEPOUT 0.025 EXTENSION 0.048 0.02425 0.048 CORNERONLY ;"};
    r.properties["LEF58_CORNERSPACING"] = {"CORNERSPACING CONVEXCORNER CORNERONLY 0.048 WIDTH 0.0 SPACING 0.04 ;"};
    r.parallel.push_back({{0, .025}, {0}, {{.024}, {.072}}});
  }
  for (const char* name : {"M6", "M7"}) {
    auto& r = metal(name, name[1] == '6', .032, .064, .0021875);
    r.properties["LEF58_SPACING"]       = {"SPACING 0.032 ENDOFLINE 0.0375 WITHIN 0.04 ENDTOEND 0.04 ;"};
    r.properties["LEF58_EOLKEEPOUT"]    = {"EOLKEEPOUT 0.05 EXTENSION 0.048 0.03225 0.048 CORNERONLY ;"};
    r.properties["LEF58_CORNERSPACING"] = {"CORNERSPACING CONVEXCORNER CORNERONLY 0.048 WIDTH 0.0 SPACING 0.04 ;"};
    r.parallel.push_back({{0, .033}, {0}, {{.032}, {.072}}});
  }
  metal("M8", true, .04, .08, 0);
  const auto via = [&](const char* name, const char* lower, const char* cut, const char* upper, double lx, double ly, double cx,
                       double cy, double ux, double uy, double spacing, bool table) {
    Lef_via v;
    v.is_default = true;
    v.shapes     = {{lower, {-lx, -ly, lx, ly}}, {upper, {-ux, -uy, ux, uy}}, {cut, {-cx, -cy, cx, cy}}};
    t.vias[name] = v;
    auto& c      = t.cut_layers[cut];
    if (table) {
      c.properties["LEF58_SPACINGTABLE"] = {"SPACINGTABLE DEFAULT " + std::to_string(spacing) + " CUTCLASS Vx Vx - - ;"};
    } else {
      c.spacing = {spacing};
    }
  };
  via("VIA12", "M1", "V1", "M2", .009, .011, .009, .009, .014, .009, .018, false);
  via("VIA23", "M2", "V2", "M3", .014, .009, .009, .009, .009, .014, .018, false);
  via("VIA34", "M3", "V3", "M4", .009, .017, .009, .012, .020, .012, .034, true);
  via("VIA45", "M4", "V4", "M5", .023, .012, .012, .012, .012, .023, .034, true);
  via("VIA56", "M5", "V5", "M6", .012, .027, .012, .016, .023, .016, .034, true);
  via("VIA67", "M6", "V6", "M7", .027, .016, .016, .016, .016, .027, .034, true);
  via("VIA78", "M7", "V7", "M8", .016, .034, .016, .016, .034, .020, .034, true);
  return t;
}
}  // namespace

// The numbers the router used to carry as a table, measured with OpenROAD's checker on two-wire layouts, follow from
// the LEF: width, end-to-end, corner spacing, minimum length, extension past a via, spacing from wide metal.
TEST(RouteTech, Asap7LimitsMatchTheCalibratedTable) {
  const auto stack = derive_tech_stack(asap7_like(), 1000, [](const std::string&, bool) { return true; });
  EXPECT_EQ(stack.pin_layer, "M1");
  ASSERT_EQ(stack.layers.size(), 6u);  // the profile stops at M7
  const int expected[6][6] = {
      {18, 31, 20, 37, 19, 36},
      {18, 31, 20, 37, 19, 36},
      {24, 48, 40, 84, 23, 72},
      {24, 48, 40, 84, 27, 72},
      {32, 48, 40, 69, 27, 72},
      {32, 48, 40, 69, 27, 72}
  };
  for (size_t i = 0; i < 6; ++i) {
    const auto& l = stack.layers[i];
    EXPECT_EQ(l.name, std::string("M") + char('2' + i));
    EXPECT_EQ(l.horiz, i % 2 == 0) << l.name;
    const int got[6] = {l.w, l.e2e, l.cs, l.minlen, l.ext, l.wide};
    for (int k = 0; k < 6; ++k) {
      EXPECT_EQ(got[k], expected[i][k]) << l.name << " field " << k;
    }
    EXPECT_EQ(l.bump, 0) << l.name;  // the pads are as wide as the wires
  }
  ASSERT_EQ(stack.cuts.size(), 6u);
  const int cuts[6][3] = {{9, 9, 18}, {9, 9, 18}, {9, 12, 34}, {12, 12, 34}, {12, 16, 34}, {16, 16, 34}};
  for (size_t k = 0; k < 6; ++k) {
    const auto& c = stack.cuts[k];
    EXPECT_EQ(c.via, "VIA" + std::to_string(k + 1) + std::to_string(k + 2));
    EXPECT_EQ(c.name, "V" + std::to_string(k + 1));
    EXPECT_EQ(c.hx, cuts[k][0]) << c.via;
    EXPECT_EQ(c.hy, cuts[k][1]) << c.via;
    EXPECT_EQ(c.space, cuts[k][2]) << c.via;
  }
  EXPECT_EQ(stack.cuts[0].lower_pad, (livehd::physical::Rect{-9, -11, 9, 11}));  // the landing on the M1 pin
}

// Sky130 has no profile with a routing policy: the stack is the LEF's, pins on li1 and wires on met1 to met5.
TEST(RouteTech, Sky130StackComesFromTheLef) {
  const auto tech  = livehd::physical::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  const auto stack = derive_tech_stack(tech, 1000, [](const std::string&, bool) { return true; });
  EXPECT_EQ(stack.pin_layer, "li1");
  EXPECT_EQ(stack.grid, 5);
  // met5 is left out: its via puts a 1.18 um pad on met4, whose tracks are 0.92 um apart.
  ASSERT_EQ(stack.layers.size(), 4u);
  EXPECT_EQ(stack.unused, std::vector<std::string>{"met5"});
  const auto& met1 = stack.layers[0];
  EXPECT_EQ(met1.name, "met1");
  EXPECT_TRUE(met1.horiz);
  EXPECT_EQ(met1.w, 140);
  EXPECT_EQ(met1.spacing, 140);
  EXPECT_EQ(met1.e2e, 140);
  EXPECT_EQ(met1.cs, 0);        // tracks 340 apart leave 200 between wires
  EXPECT_EQ(met1.minlen, 595);  // 0.083 um2 over 0.14, on the 5 nm grid
  EXPECT_EQ(met1.ext, 160);     // the met1 pad of M1M2_PR along the wire
  EXPECT_EQ(met1.bump, 60);     // and beside it: 130 against half a wire of 70
  EXPECT_EQ(met1.wide, 280);
  EXPECT_EQ(stack.layers[3].name, "met4");
  EXPECT_EQ(stack.layers[3].bump, 15);
  ASSERT_EQ(stack.cuts.size(), 4u);
  EXPECT_EQ(stack.cuts[0].via, "L1M1_PR");
  EXPECT_EQ(stack.cuts[0].name, "mcon");
  EXPECT_EQ(stack.cuts[0].hx, 85);
  EXPECT_EQ(stack.cuts[0].space, 190);
  EXPECT_EQ(stack.cuts[1].via, "M1M2_PR");
  EXPECT_EQ(stack.cuts[0].lower_pad, (livehd::physical::Rect{-85, -85, 85, 85}));
  // Only layers with tracks are routed.
  const auto three = derive_tech_stack(tech, 1000, [](const std::string& layer, bool) { return layer != "met4"; });
  EXPECT_EQ(three.layers.size(), 3u);
  EXPECT_TRUE(three.unused.empty());
}
