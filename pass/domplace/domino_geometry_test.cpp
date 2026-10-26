// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "inou/lefdef/lef_writer.hpp"

namespace livehd::domplace {
namespace {
namespace ph                = physical;
constexpr const char* kTlef = "inou/lefdef/tests/data/sky130hd.tlef";
constexpr const char* kLef  = "inou/lefdef/tests/data/sky130hd.lef";
constexpr const char* kInv  = "sky130_fd_sc_hd__inv_1";
constexpr const char* kDff1 = "sky130_fd_sc_hd__dfxtp_1";
// The fixture has one register drive; the X2/X4 ladder steps are synthetic
// copies with the sky130 dfxtp_2/dfxtp_4 widths.
constexpr const char* kDff2 = "test_dfxtp_2";
constexpr const char* kDff4 = "test_dfxtp_4";
const ph::Technology& sky130() {
  static const auto tech = [] {
    auto t                = ph::read_lef({kTlef, kLef});
    t.macros[kDff2]       = t.macros.at(kDff1);
    t.macros[kDff4]       = t.macros.at(kDff1);
    t.macros[kDff2].width = 8.28;
    t.macros[kDff4].width = 9.2;
    return t;
  }();
  return tech;
}
const ph::Route_policy kSky130Route{
    {"li1", "met1", "mcon"},
    "met2", "via", {}
};
// ASAP7-like M1 (vertical pins) / M2 (horizontal escape) / V1 stack as in
// pin_access_test, plus vertical M3. The 54 nm site against the 36 nm vertical
// pitch gives two site x-phases.
ph::Technology synthetic() {
  ph::Technology t;
  t.manufacturing_grid = .001;
  t.sites["core"]      = {.054, .270, true};
  t.routing_layers     = {"M1", "M2", "M3"};
  for (const auto& name : t.routing_layers) {
    auto&      layer  = t.layers[name];
    const bool across = name == "M2";
    layer.width       = .018;
    layer.direction   = across ? "HORIZONTAL" : "VERTICAL";
    layer.pitch_x = layer.pitch_y = across ? .030 : .036;
    layer.offset_x = layer.offset_y = across ? .015 : .018;
    layer.has_offset                = true;
    layer.rules.loaded              = true;
    layer.rules.area                = .000666;
    layer.rules.spacing             = {
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
  for (const auto& [name, width] : {
           std::pair{"INVx1",  .162},
           {"DFFx1",  .810},
           {"DFFx2",  .918},
           {"DFFx4", 1.026}
  }) {
    auto& m      = t.macros[name];
    m.klass      = "CORE";
    m.site       = "core";
    m.width      = width;
    m.height     = .270;
    m.symmetry_x = m.symmetry_y = true;
    m.pins["VDD"]               = {"INOUT", "POWER", {{"M1", {0, .261, width, .279}}}};
    m.pins["VSS"]               = {"INOUT", "GROUND", {{"M1", {0, -.009, width, .009}}}};
    m.pins["A"]                 = {"INPUT", "SIGNAL", {{"M1", {.018, .036, .036, .234}}}};
  }
  return t;
}
const ph::Route_policy kSyntheticRoute{
    {"M1", "M2", "V1"},
    "M3", "V2", {}
};
usyn::Physical_cell_interface pins(const std::string& base, size_t rails, int variant = 0) {
  usyn::Physical_cell_interface p{base + "_v" + std::to_string(variant) + "_t0", {}};
  for (size_t i = 0; i < rails; ++i) {
    p.rails.push_back("R" + std::to_string(i));
  }
  return p;
}
constexpr uint32_t kAux = 4;
// The named cells: the inverter and the register ladder, by LEF macro name.
Domino_reference   cells(const char* inverter, std::vector<std::string> registers, uint32_t aux = kAux) {
  Domino_reference r;
  r.inverter.name = inverter;
  for (auto& name : registers) {
    r.registers.push_back({std::move(name)});
  }
  r.aux_nonregister = aux;
  return r;
}
const Domino_reference      kSky130Cells    = cells(kInv, {kDff1, kDff2, kDff4});
const Domino_reference      kSyntheticCells = cells("INVx1", {"DFFx1", "DFFx2", "DFFx4"});
usyn::Gate_formula::Metrics pdn(uint32_t transistors) {
  usyn::Gate_formula::Metrics m;
  m.transistors = transistors;
  return m;
}
// Master shapes by (rails, N_PDN): (a&b)|(c&d), its complement realized as its
// own positive formula over the complemented rails, a binate XOR with both
// rails of each input, and AND3.
struct Master {
  const char* base;
  size_t      rails;
  uint32_t    n_pdn;
};
constexpr std::array kMasters{
    Master{"__lhd_domino_00000000000a0b0c", 4, 4},
    Master{"__lhd_domino_00000000000c0b0a", 4, 4},
    Master{"__lhd_domino_000000000000x0r2", 4, 4},
    Master{"__lhd_domino_00000000000a0nd3", 3, 3},
};
std::string temp_lef(const std::string& name, const std::string& text) {
  const auto path = std::filesystem::path(testing::TempDir()) / name;
  std::ofstream(path) << text;
  return path.string();
}
// The access check with a scratch copy of the technology, as the bundle keeps one.
Access_check check(const Geometry_inputs& in, const ph::Macro& m, const std::array<ph::Row_access, 2>& halves) {
  auto scratch = *in.tech;
  return check_domino_access(in, m, halves, scratch);
}
bool has_failure(const Access_check& c, const std::string& prefix, const std::string& text) {
  return std::any_of(c.failures.begin(), c.failures.end(), [&](const auto& f) {
    return f.starts_with(prefix) && f.find(text) != std::string::npos;
  });
}
}  // namespace
TEST(DominoGeometry, AreaWidthIsExactWholeSitesAndDriveChangesOnlyTheRegister) {
  const auto in = geometry_inputs(sky130(), kInv, kSky130Route);
  const auto x1 = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSky130Cells, 0, in);
  const auto x2 = generate_domino_macro(pins("__lhd_domino_ab", 4, 1), pdn(4), kSky130Cells, 1, in);
  const auto x4 = generate_domino_macro(pins("__lhd_domino_ab", 4, 2), pdn(4), kSky130Cells, 2, in);
  // x = 4: 4 * 3.7536 + 20.0192 = 35.0336 um^2 = exactly 14 two-row sites.
  EXPECT_DOUBLE_EQ(x1.x, 4);
  EXPECT_NEAR(x1.a_not_um2, 1.38 * 2.72, 1e-12);
  EXPECT_NEAR(x1.a_estimate_um2, 35.0336, 1e-9);
  EXPECT_EQ(x1.w_area, 14 * 460);
  EXPECT_EQ(x1.w_pin, 7 * 460);
  EXPECT_EQ(x1.width, 6440);
  EXPECT_EQ(x1.height, 5440);
  EXPECT_NEAR(x1.a_footprint_um2, 6.44 * 5.44, 1e-9);
  EXPECT_EQ(x2.width, 15 * 460);
  EXPECT_EQ(x4.width, 16 * 460);
  for (const auto* v : {&x2, &x4}) {
    // The PDN term is shared; only the register area grows with drive.
    EXPECT_EQ(v->x, x1.x);
    EXPECT_EQ(v->a_not_um2, x1.a_not_um2);
    EXPECT_GT(v->a_register_um2, x1.a_register_um2);
    EXPECT_NEAR(v->a_estimate_um2 - x1.a_estimate_um2, v->a_register_um2 - x1.a_register_um2, 1e-9);
    EXPECT_EQ(v->w_pin, x1.w_pin);
  }
  // Fractional inverter equivalents round only the total: 13.25 -> 14 sites.
  const auto odd = generate_domino_macro(pins("__lhd_domino_odd", 3), pdn(3), kSky130Cells, 0, in);
  EXPECT_DOUBLE_EQ(odd.x, 3.5);
  EXPECT_NEAR(odd.a_estimate_um2, 3.5 * 3.7536 + 20.0192, 1e-9);
  EXPECT_EQ(odd.w_area, 14 * 460);
}
TEST(DominoGeometry, RoutePolicyFallsBackToTheLefStackWithoutAProfile) {
  std::string source;
  const auto  route = domino_route_policy(sky130(), &source);
  EXPECT_EQ(source, "lef-stack");
  EXPECT_EQ(route.access.pin, kSky130Route.access.pin);
  EXPECT_EQ(route.access.escape, kSky130Route.access.escape);
  EXPECT_EQ(route.access.cut, kSky130Route.access.cut);
  EXPECT_EQ(route.branch_layer, kSky130Route.branch_layer);
  auto short_stack = synthetic();
  short_stack.routing_layers.pop_back();
  EXPECT_THROW(domino_route_policy(short_stack, &source), std::invalid_argument);
  // A registered profile's LEF takes that profile's route policy.
  ASSERT_FALSE(ph::tech_profiles().empty());
  const auto& profile = ph::tech_profiles().front();
  ASSERT_FALSE(profile.detect_sites.empty());
  auto known = synthetic();
  known.sites[profile.detect_sites.front()];
  const auto profiled = domino_route_policy(known, &source);
  EXPECT_EQ(source, "tech-profile:" + profile.name);
  EXPECT_EQ(profiled.access.pin, profile.route.access.pin);
  EXPECT_EQ(profiled.branch_layer, profile.route.branch_layer);
}
TEST(DominoGeometry, PinTermWidensWideInputMasters) {
  const auto in = geometry_inputs(sky130(), kInv, kSky130Route);
  const auto g  = generate_domino_macro(pins("__lhd_domino_wide", 14), pdn(4), kSky130Cells, 0, in);
  EXPECT_EQ(g.w_area, 14 * 460);
  EXPECT_EQ(g.w_pin, 17 * 460);
  EXPECT_EQ(g.width, 17 * 460);
  EXPECT_GT(g.a_footprint_um2, g.a_estimate_um2);
}
TEST(DominoGeometry, TwoRowCoreAbstractWithEdgeRailsAndTrackStrips) {
  const auto  in = geometry_inputs(sky130(), kInv, kSky130Route);
  const auto  g  = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSky130Cells, 0, in);
  const auto& m  = g.macro;
  EXPECT_EQ(m.klass, "CORE");
  EXPECT_EQ(m.site, "unithd");
  EXPECT_TRUE(m.symmetry_x);
  EXPECT_TRUE(m.symmetry_y);
  EXPECT_DOUBLE_EQ(m.height, 5.44);
  EXPECT_EQ(g.halves, (std::array{ph::Row_access::horizontal, ph::Row_access::vertical}));
  ASSERT_EQ(m.pins.size(), 8u);
  EXPECT_EQ(m.pins.at("CLK").use, "CLOCK");
  EXPECT_EQ(m.pins.at("CLK").direction, "INPUT");
  EXPECT_EQ(m.pins.at("Q").direction, "OUTPUT");
  EXPECT_EQ(m.pins.at("R3").use, "SIGNAL");
  double last = -1;
  for (const auto* name : {"R0", "R1", "R2", "R3", "CLK", "Q"}) {
    SCOPED_TRACE(name);
    const auto& shapes = m.pins.at(name).shapes;
    ASSERT_EQ(shapes.size(), 2u);
    // Shape 0, the RC point, is in the horizontal-access row (local row 0).
    EXPECT_GE(shapes[0].rect.yl, 0.41 - 1e-9);
    EXPECT_LE(shapes[0].rect.yh, 2.31 + 1e-9);
    EXPECT_GE(shapes[1].rect.yl, 2.72 + 0.41 - 1e-9);
    EXPECT_LE(shapes[1].rect.yh, 5.44 - 0.41 + 1e-9);
    for (const auto& shape : shapes) {
      EXPECT_EQ(shape.layer, "li1");
      const auto center = (shape.rect.xl + shape.rect.xh) / 2;
      EXPECT_NEAR(std::remainder(center - 0.23, 0.46), 0, 1e-9);  // on a met2 track
    }
    EXPECT_GT(shapes[0].rect.xl, last);
    last = shapes[0].rect.xh;
  }
  const auto rails = [&](const char* pin) {
    std::vector<double> centers;
    for (const auto& shape : m.pins.at(pin).shapes) {
      EXPECT_EQ(shape.rect.xl, 0);
      EXPECT_DOUBLE_EQ(shape.rect.xh, 6.44);
      centers.push_back((shape.rect.yl + shape.rect.yh) / 2);
    }
    return centers;
  };
  EXPECT_EQ(m.pins.at("VGND").use, "GROUND");
  const auto vgnd = rails("VGND"), vpwr = rails("VPWR");
  ASSERT_EQ(vgnd.size(), 2u);
  ASSERT_EQ(vpwr.size(), 1u);
  EXPECT_NEAR(vgnd[0], 0, 1e-9);
  EXPECT_NEAR(vgnd[1], 5.44, 1e-9);
  EXPECT_NEAR(vpwr[0], 2.72, 1e-9);
}
TEST(DominoGeometry, RegenerationIsByteIdenticalAndRoundTripsThroughTheLefReader) {
  const auto in       = geometry_inputs(sky130(), kInv, kSky130Route);
  const auto generate = [&] {
    std::map<std::string, ph::Macro> macros;
    for (const auto& master : kMasters) {
      for (int variant = 0; variant < 3; ++variant) {
        auto g = generate_domino_macro(pins(master.base, master.rails, variant), pdn(master.n_pdn), kSky130Cells, variant, in);
        macros.emplace(g.name, std::move(g.macro));
      }
    }
    return macros;
  };
  const auto macros = generate();
  ASSERT_EQ(macros.size(), kMasters.size() * 3);
  const auto text = write_lef_macros(macros, sky130().dbu);
  EXPECT_EQ(write_lef_macros(generate(), sky130().dbu), text);
  const auto back = ph::read_lef({kTlef, kLef, temp_lef("domino.lef", text)});
  for (const auto& [name, macro] : macros) {
    SCOPED_TRACE(name);
    const auto& read = back.macros.at(name);
    EXPECT_EQ(read.width, macro.width);
    EXPECT_EQ(read.height, macro.height);
    EXPECT_EQ(read.site, macro.site);
    EXPECT_TRUE(read.symmetry_x && read.symmetry_y);
    ASSERT_EQ(read.pins.size(), macro.pins.size());
    for (const auto& [pin, data] : macro.pins) {
      EXPECT_EQ(read.pins.at(pin).shapes, data.shapes) << pin;
      EXPECT_EQ(read.pins.at(pin).use, data.use) << pin;
    }
  }
}
TEST(DominoGeometry, SyntheticAccessHoldsForBothParitiesFlipsPhasesAndDrives) {
  const auto tech = synthetic();
  const auto in   = geometry_inputs(tech, "INVx1", kSyntheticRoute);
  for (const auto& master : kMasters) {
    for (int variant = 0; variant < 3; ++variant) {
      const auto g
          = generate_domino_macro(pins(master.base, master.rails, variant), pdn(master.n_pdn), kSyntheticCells, variant, in);
      SCOPED_TRACE(g.name);
      const auto c = check(in, g.macro, g.halves);
      EXPECT_TRUE(c.ok);
      EXPECT_TRUE(c.failures.empty()) << c.failures.front();
      EXPECT_TRUE(c.unsupported.empty());
      EXPECT_EQ(c.x_phases, 2u);
      ASSERT_EQ(c.cases.size(), 8u);  // start rows 0 and 1 x N, FS, FN, S
      for (const auto& k : c.cases) {
        const bool even    = k.start_row % 2 == 0;
        const bool flipped = k.orientation == ph::Orientation::FS || k.orientation == ph::Orientation::S;
        EXPECT_EQ(k.compatible, even != flipped) << k.start_row << ph::orientation_name(k.orientation);
        EXPECT_EQ(k.ok, k.compatible);
        // Rails repeat the bottom polarity at 0 and 2H: wrong on odd starts.
        EXPECT_EQ(k.pg_rows_match, k.compatible && even);
      }
    }
  }
  // A wide master is pin-limited, still accessible.
  const auto wide = generate_domino_macro(pins("__lhd_domino_wide", 13), pdn(13), kSyntheticCells, 0, in);
  EXPECT_EQ(wide.w_area, 21 * 54);
  EXPECT_EQ(wide.w_pin, 16 * 72);
  EXPECT_EQ(wide.width, 22 * 54);
  EXPECT_TRUE(check(in, wide.macro, wide.halves).ok);
}
TEST(DominoGeometry, MirroredStartsKeepVerticalAccessOnAnUnevenTrackOffset) {
  // M3 tracks 6 nm off the site grid: neither 0 nor half of gcd(54, 36), so a
  // mirrored (FN, S) placement sees other track phases than N and FS. The
  // strips must cover both.
  auto tech                     = synthetic();
  tech.layers.at("M3").offset_x = .006;
  tech.layers.at("M3").offset_y = .006;
  const auto in                 = geometry_inputs(tech, "INVx1", kSyntheticRoute);
  const auto g                  = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSyntheticCells, 0, in);
  const auto c                  = check(in, g.macro, g.halves);
  EXPECT_TRUE(c.failures.empty()) << c.failures.front();
  EXPECT_TRUE(c.ok);
  ASSERT_EQ(c.cases.size(), 8u);
  for (const auto& k : c.cases) {
    EXPECT_EQ(k.ok, k.compatible) << k.start_row << ph::orientation_name(k.orientation);
  }
}
TEST(DominoGeometry, MissingSymmetryXIsRefused) {
  const auto tech    = synthetic();
  const auto in      = geometry_inputs(tech, "INVx1", kSyntheticRoute);
  auto       g       = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSyntheticCells, 0, in);
  g.macro.symmetry_x = false;
  const auto c       = check(in, g.macro, g.halves);
  EXPECT_FALSE(c.ok);
  EXPECT_TRUE(has_failure(c, "macro:", "SYMMETRY X"));
}
TEST(DominoGeometry, TooNarrowLayoutIsRefusedNamingThePin) {
  const auto tech = synthetic();
  const auto in   = geometry_inputs(tech, "INVx1", kSyntheticRoute);
  const auto fit  = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSyntheticCells, 0, in);
  EXPECT_EQ(fit.width, 14 * 54);
  const auto narrow = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSyntheticCells, 0, in, 6 * 54);
  EXPECT_EQ(narrow.width, 6 * 54);
  const auto c = check(in, narrow.macro, narrow.halves);
  EXPECT_FALSE(c.ok);
  EXPECT_TRUE(has_failure(c, "Q:", "outside"));
  EXPECT_FALSE(has_failure(c, "R0:", "outside"));
  EXPECT_THROW(generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSyntheticCells, 0, in, 100), std::invalid_argument);
}
TEST(DominoGeometry, AccessFailuresNameThePinRowOrientationAndPhase) {
  const auto tech = synthetic();
  const auto in   = geometry_inputs(tech, "INVx1", kSyntheticRoute);
  const auto g    = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSyntheticCells, 0, in);
  {
    // Off one x-phase's vertical track in the vertical-access row.
    auto m                         = g.macro;
    m.pins.at("R1").shapes[1].rect = m.pins.at("R1").shapes[1].rect.translated(.009, 0);
    const auto c                   = check(in, m, g.halves);
    EXPECT_TRUE(has_failure(c, "R1:", "no vertical access"));
    EXPECT_TRUE(has_failure(c, "R1: start row 0 N x-phase 1:", "core row 1"));
    EXPECT_FALSE(has_failure(c, "R1: start row 0 N x-phase 0:", ""));
    EXPECT_FALSE(has_failure(c, "R0:", ""));
  }
  {
    // Escape metal blocked over the horizontal-access row of CLK.
    auto        m   = g.macro;
    const auto& clk = m.pins.at("CLK").shapes[0].rect;
    m.obstructions.push_back({
        "M2",
        {clk.xl - .05, 0, clk.xh + .05, .27}
    });
    const auto c = check(in, m, g.halves);
    EXPECT_TRUE(has_failure(c, "CLK: start row 0 N x-phase 0:", "no horizontal access in core row 0"));
    EXPECT_TRUE(has_failure(c, "CLK: start row 1 FS", "no horizontal access in core row 2"));
    EXPECT_FALSE(has_failure(c, "R0:", ""));
  }
  {
    // PG access: the middle rail is required on every start.
    auto m = g.macro;
    m.pins.at("VDD").shapes.clear();
    m.pins.at("VDD").shapes.push_back({
        "M1",
        {0, .1, .1, .12}
    });
    const auto c = check(in, m, g.halves);
    EXPECT_TRUE(has_failure(c, "PG: start row 0 N", "no rail on core row edge 1"));
    EXPECT_TRUE(has_failure(c, "PG: start row 1 FS", "no rail on core row edge 2"));
  }
}
TEST(DominoGeometry, OwnerConventionSelectsTheTemplateRows) {
  const auto                tech = synthetic();
  ph::Row_access_convention owner;
  owner.name    = "owner";
  owner.even    = ph::Row_access::vertical;
  owner.odd     = ph::Row_access::horizontal;
  const auto in = geometry_inputs(tech, "INVx1", kSyntheticRoute, owner);
  const auto g  = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSyntheticCells, 0, in);
  EXPECT_EQ(g.halves, (std::array{ph::Row_access::vertical, ph::Row_access::horizontal}));
  // Shape 0 (the RC point) moves to the horizontal-access local row 1.
  EXPECT_GE(g.macro.pins.at("R0").shapes[0].rect.yl, .27);
  EXPECT_TRUE(check(in, g.macro, g.halves).ok);
}
TEST(DominoGeometry, Sky130EngineRefusalIsUnverifiedNotPassed) {
  // The pin-access engine fails closed on sky130 mcon ENCLOSURE rules; the
  // vertical rule, outline and PG checks still run and pass.
  const auto in = geometry_inputs(sky130(), kInv, kSky130Route);
  const auto g  = generate_domino_macro(pins("__lhd_domino_ab", 4), pdn(4), kSky130Cells, 0, in);
  const auto c  = check(in, g.macro, g.halves);
  EXPECT_FALSE(c.ok);
  EXPECT_TRUE(c.failures.empty()) << c.failures.front();
  EXPECT_EQ(c.unsupported, std::vector<std::string>{"unsupported mcon cut rules"});
  EXPECT_EQ(c.x_phases, 1u);
  EXPECT_EQ(std::count_if(c.cases.begin(), c.cases.end(), [](const auto& k) { return k.compatible; }), 4);
}
TEST(DominoGeometry, InvalidInputsThrow) {
  const auto tech = synthetic();
  const auto in   = geometry_inputs(tech, "INVx1", kSyntheticRoute);
  EXPECT_THROW(generate_domino_macro(pins("m", 2), pdn(2), cells("INVx1", {"DFFx9"}), 0, in), std::invalid_argument);
  EXPECT_THROW(generate_domino_macro(pins("m", 2), pdn(0), cells("INVx1", {"DFFx1"}, 0), 0, in), std::invalid_argument);
  EXPECT_THROW(generate_domino_macro(pins("m", 0), pdn(2), kSyntheticCells, 0, in), std::invalid_argument);
  EXPECT_THROW(generate_domino_macro(pins("m", 2), pdn(2), kSyntheticCells, 3, in), std::invalid_argument);
  EXPECT_THROW(generate_domino_macro(pins("m", 2), pdn(2), cells("INVx2", {"DFFx1"}), 0, in), std::invalid_argument);
  // The footprint is resolved once: a missing or non-CORE inverter is refused there.
  EXPECT_THROW(geometry_inputs(tech, "DFFx9", kSyntheticRoute), std::invalid_argument);
  auto block                     = tech;
  block.macros.at("INVx1").klass = "BLOCK";
  EXPECT_THROW(geometry_inputs(block, "INVx1", kSyntheticRoute), std::invalid_argument);
  auto bad           = in;
  bad.vertical_layer = "M9";
  EXPECT_THROW(generate_domino_macro(pins("m", 2), pdn(2), kSyntheticCells, 0, bad), std::invalid_argument);
  auto clash     = pins("m", 2);
  clash.rails[1] = "VDD";
  EXPECT_THROW(generate_domino_macro(clash, pdn(2), kSyntheticCells, 0, in), std::invalid_argument);
}
}  // namespace livehd::domplace
