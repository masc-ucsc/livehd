#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "gtest/gtest.h"
#include "inou/lefdef/routing_geometry.hpp"
#include "inou/lefdef/routing_rules.hpp"
#include "power_grid.hpp"
#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"

namespace livehd::placement {
namespace {
namespace ph = physical;
ph::Technology technology() {
  ph::Technology t;
  t.sites.emplace("site", ph::Site{.054, .270, true});
  t.routing_layers           = {"M1", "M2"};
  t.layers["M1"].direction   = "VERTICAL";
  t.layers["M2"].direction   = "HORIZONTAL";
  auto& m                    = t.macros["cell"];
  m.klass                    = "CORE";
  m.site                     = "site";
  m.width                    = .108;
  m.height                   = .270;
  m.pins["VDD"]              = {"INOUT", "POWER", {{"M1", {0, .261, .108, .279}}}};
  m.pins["VSS"]              = {"INOUT", "GROUND", {{"M1", {0, -.009, .108, .009}}}};
  t.cut_layers["V1"].spacing = {.018};
  t.via_rules["M2_M1"]       = {true,
                                true,
                                {{"M1", "", std::array{0., 0.}},
                                 {"M2", "", std::array{.002, 0.}},
                                 {"V1", "", {}, {}, std::array{.036, .036}, ph::Rect{-.009, -.009, .009, .009}}}};
  return t;
}
ph::Floorplan frame() {
  ph::Floorplan f;
  f.design = "grid";
  f.die    = {0, 0, 5500, 5500};
  for (int64_t i = 0; i < 4; ++i) {
    f.rows.push_back(
        {"r" + std::to_string(i), "site", 2700, 2700 + 270 * i, 40, 54, i % 2 ? ph::Orientation::FS : ph::Orientation::N});
  }
  Ann_place loc(2916, 2700, 108, 270);
  loc.status = ph::Status::PLACED;
  f.components.push_back({"u0", "cell", loc});
  return f;
}
Pg_profile profile(const ph::Technology& t) {
  auto p = power_grid_profile("pg_asap7", t);
  p.stripes.clear();
  p.connect   = {{{"M1", "M2"}}};
  p.pin_layer = "M2";
  return p;
}
}  // namespace
TEST(PowerGridExport, ChannelTracksUseSpacingAndDeduplicateOverlappingPatches) {
  ph::Technology tech;
  tech.manufacturing_grid = .001;
  auto& layer             = tech.layers["M2"];
  layer.width             = .020;
  layer.direction         = "HORIZONTAL";
  layer.rules.loaded      = true;
  layer.rules.spacing     = {{.010, {}, {}}};
  ph::Floorplan before;
  before.die    = {0, 0, 1000, 400};
  before.tracks = {{"Y", "M2", 100, 3, 40}};
  ph::Net supply;
  supply.name = "VDD";
  supply.wires.push_back({"ROUTED", {{"M2", 100, "STRIPE", {ph::Route_point{0, 140, {}}, ph::Route_point{1000, 140, {}}}}}});
  before.special_nets.push_back(supply);
  auto after = before;
  after.special_nets.clear();
  const std::vector<Pg_channel> channels{{"M2", "skip", {0, .080, 1, .200}}};
  EXPECT_EQ(count_channel_tracks(tech, before, after, channels).at("M2"), 3);
  EXPECT_EQ(count_channel_tracks(tech, before, before, channels).at("M2"), 0);
  ph::Net signal;
  signal.name = "other";
  signal.wires.push_back({"FIXED", {{"M2", {}, {}, {ph::Route_point{0, 140, 0}, ph::Route_point{1000, 140, 0}}}}});
  after.nets.push_back(signal);
  EXPECT_EQ(count_channel_tracks(tech, before, after, channels).at("M2"), 2);
  EXPECT_EQ(count_channel_tracks(tech, before, after, {channels[0], channels[0]}).at("M2"), 2);
  after.tracks.clear();
  EXPECT_THROW(count_channel_tracks(tech, before, after, channels), std::invalid_argument);
  after.tracks = before.tracks;
  tech.layers["M2"].rules.unsupported.insert("unsupported rule");
  EXPECT_THROW(count_channel_tracks(tech, before, after, channels), std::invalid_argument);
}
TEST(PowerGridExport, EmitsConnectedLayerPairsWithoutMovingComponents) {
  const auto t      = technology();
  const auto input  = frame();
  const auto p      = profile(t);
  const auto output = make_power_grid(t, input, p);
  EXPECT_EQ(output.components, input.components);
  EXPECT_EQ(output.rows, input.rows);
  EXPECT_EQ(output.nets, input.nets);
  ASSERT_EQ(output.vias.size(), 1);
  ASSERT_EQ(output.special_nets.size(), 2);
  ASSERT_EQ(output.terminals.size(), 2);
  EXPECT_EQ(output.vias[0].generated->enclosure, (std::array<int64_t, 4>{0, 0, 2, 0}));
  for (const auto& net : output.special_nets) {
    EXPECT_EQ(net.connections.size(), 2);
    EXPECT_FALSE(ph::route_geometry(t, output, net, true).empty());
  }
  for (const auto& terminal : output.terminals) {
    EXPECT_EQ(terminal.direction, "INOUT");
    EXPECT_FALSE(terminal.shapes.empty());
  }
  const auto file = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "native_grid.def";
  ph::write_def(output, file.string());
  EXPECT_NO_THROW(ph::verify_def_roundtrip(output, ph::read_def(file.string())));
}
// A pinless soft block (CLASS BLOCK SOFT) stays PLACED through final placement as an area of cells: the export, like
// plan_power_grid, passes over it instead of requiring macro PG pins.
TEST(PowerGridExport, SoftBlocksNeedNoMacroPgPins) {
  auto  t     = technology();
  auto& soft  = t.macros["soft"];
  soft.klass  = "BLOCK SOFT";
  soft.width  = .54;
  soft.height = .54;
  auto      f = frame();
  Ann_place loc(3240, 2700, 540, 540);
  loc.status = ph::Status::PLACED;
  f.components.push_back({"blob", "soft", loc});
  const auto    p = profile(t);
  ph::Floorplan output;
  ASSERT_NO_THROW(output = make_power_grid(t, f, p));
  const auto plain = make_power_grid(t, frame(), p);
  ASSERT_EQ(output.special_nets.size(), plain.special_nets.size());
  for (size_t i = 0; i < plain.special_nets.size(); ++i) {
    EXPECT_EQ(output.special_nets[i].wires, plain.special_nets[i].wires);
  }
}
TEST(PowerGridExport, ConnectsFixedMacroPinsWithoutReemittingTheirMetal) {
  auto t = technology();
  t.routing_layers.push_back("M3");
  t.layers["M3"].direction   = "VERTICAL";
  t.cut_layers["V2"].spacing = {.018};
  t.via_rules["M3_M2"]       = {true,
                                true,
                                {{"M2", "", std::array{0., 0.}},
                                 {"M3", "", std::array{.002, 0.}},
                                 {"V2", "", {}, {}, std::array{.036, .036}, ph::Rect{-.009, -.009, .009, .009}}}};
  auto& m                    = t.macros["memory"];
  m.klass                    = "BLOCK";
  m.width = m.height = .54;
  m.pins["VDD"]      = {"INOUT", "POWER", {{"M2", {.018, .088, .522, .112}}}};
  m.pins["VSS"]      = {"INOUT", "GROUND", {{"M2", {.018, .388, .522, .412}}}};
  auto f             = frame();
  // Cut two rows around the macro body before generating PG.
  for (size_t i = 1; i <= 2; ++i) {
    auto right       = f.rows[i];
    right.name      += "_right";
    right.x          = 3780;
    right.count      = 20;
    f.rows[i].count  = 10;
    f.rows.push_back(right);
  }
  Ann_place loc(3240, 2970, 540, 540);
  loc.status = ph::Status::FIXED;
  f.components.push_back({"mem", "memory", loc});
  auto p    = profile(t);
  p.stripes = {{"M3", .12, .072, .54, .3}};
  p.connect.push_back({"M2", "M3"});
  p.macro_grids = {
      {{}, 2, {}, {{{"M2", "M3"}}}}
  };
  p.pin_layer       = "M3";
  const auto output = make_power_grid(t, f, p);
  EXPECT_EQ(output.components, f.components);
  for (const auto& net : output.special_nets) {
    EXPECT_TRUE(std::any_of(net.connections.begin(), net.connections.end(), [&](const auto& c) {
      return c.instance == "mem" && c.pin == net.name;
    }));
  }
  auto mirrored                                = f;
  mirrored.components.back().place.orientation = ph::Orientation::FS;
  const auto  reflected                        = make_power_grid(t, mirrored, p);
  const auto& power                            = reflected.special_nets.front();
  EXPECT_EQ(power.name, "VDD");
  EXPECT_TRUE(std::any_of(power.wires.front().paths.begin(), power.wires.front().paths.end(), [](const auto& path) {
    if (path.layer != "M2" || path.width != 0 || path.steps.empty()) {
      return false;
    }
    const auto* point = std::get_if<ph::Route_point>(&path.steps.front());
    return point && point->y == 3410;
  }));
  // A grazing stripe intersection cannot fit a cut. It must not prevent
  // the other, full-width crossing from connecting this same macro terminal.
  auto            grazing = t;
  const ph::Shape sliver{"M2", {.239, .130, .245, .154}};
  grazing.macros["memory"].pins["VDD"].shapes.push_back(sliver);
  EXPECT_NO_THROW(make_power_grid(grazing, f, p));
  // Skipping crossings cannot silently leave the terminal disconnected.
  grazing.macros["memory"].pins["VDD"].shapes = {sliver};
  EXPECT_THROW(make_power_grid(grazing, f, p), std::invalid_argument);
  // Each declared macro PG pin needs a usable contact, not just another pin
  // on the same net elsewhere in the design.
  auto missing_contact = t;
  missing_contact.macros["memory"].pins["VDD"].shapes.clear();
  EXPECT_THROW(make_power_grid(missing_contact, f, p), std::invalid_argument);
  auto missing_pin = t;
  missing_pin.macros["memory"].pins.erase("VSS");
  EXPECT_THROW(make_power_grid(missing_pin, f, p), std::invalid_argument);
  // A LEF that aliases power and ground geometry must not pass connectivity
  // simply because each net separately has a path to a top-level PG terminal.
  m.pins["VSS"].shapes = m.pins["VDD"].shapes;
  EXPECT_THROW(make_power_grid(t, f, p), std::invalid_argument);
}
TEST(PowerGridExport, RejectsIncompleteFramesInsteadOfExportingPartialGrid) {
  auto t = technology();
  auto f = frame();
  auto p = profile(t);
  p.connect.clear();
  EXPECT_THROW(make_power_grid(t, f, p), std::invalid_argument);
  p = profile(t);
  f.vias.push_back({"existing"});
  EXPECT_THROW(make_power_grid(t, f, p), std::invalid_argument);
  f                      = frame();
  t.macros["cell"].klass = "BLOCK";
  EXPECT_THROW(make_power_grid(t, f, p), std::invalid_argument);
}
TEST(PowerGridExport, ChannelSkipRetainsConnectionsAndCrossGridGapRejectsIslands) {
  auto t               = technology();
  t.manufacturing_grid = .001;
  t.routing_layers.push_back("M3");
  t.layers["M3"].direction     = "VERTICAL";
  t.layers["M3"].rules.loaded  = true;
  t.layers["M3"].rules.area    = .001;
  t.layers["M3"].rules.spacing = {{.018, {}, {}}};
  t.cut_layers["V2"].spacing   = {.018};
  t.via_rules["M3_M2"]         = {true,
                                  true,
                                  {{"M2", "", std::array{0., 0.}},
                                   {"M3", "", std::array{.002, 0.}},
                                   {"V2", "", {}, {}, std::array{.036, .036}, ph::Rect{-.009, -.009, .009, .009}}}};
  auto p                       = profile(t);
  p.stripes                    = {{"M3", .12, .072, .54, .3}};
  p.connect.push_back({"M2", "M3"});
  p.pin_layer          = "M3";
  const auto input     = frame();
  const auto baseline  = make_power_grid(t, input, p);
  const auto via_count = [](const auto& fp) {
    size_t count = 0;
    for (const auto& net : fp.special_nets) {
      for (const auto& wire : net.wires) {
        for (const auto& path : wire.paths) {
          for (const auto& step : path.steps) {
            count += std::holds_alternative<ph::Route_via>(step);
          }
        }
      }
    }
    return count;
  };
  p.channels = {{"M3", "skip", {2.94, 0, 3.06, 5.5}}};
  Pg_channel_stats stats;
  const auto       output = make_power_grid(t, input, p, &stats);
  EXPECT_GT(stats.removed_planned_length_um.at("M3"), 0);
  ASSERT_EQ(stats.removed_stacks.size(), 1);
  EXPECT_EQ(stats.removed_stacks.at("M2:M3"), via_count(baseline) - via_count(output));
  EXPECT_GT(stats.removed_stacks.at("M2:M3"), 0);
  EXPECT_TRUE(stats.added_stacks.empty());
  const auto removed_before = stats.removed_stacks;
  const auto length_before  = stats.removed_planned_length_um;
  const auto report_path    = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "channel_report.json";
  p.channel_max_ir_drop_v = .001;
  write_power_grid_report(output, p, report_path.string(), &stats);
  std::ifstream             report_file(report_path);
  rapidjson::IStreamWrapper stream(report_file);
  rapidjson::Document       report;
  report.ParseStream(stream);
  ASSERT_FALSE(report.HasParseError());
  const auto& accounting = report["channel_accounting"];
  EXPECT_EQ(accounting["removed_stacks_by_layer_pair"]["M2:M3"].GetUint64(), stats.removed_stacks.at("M2:M3"));
  EXPECT_DOUBLE_EQ(accounting["removed_planned_stripe_length_um"]["M3"].GetDouble(), stats.removed_planned_length_um.at("M3"));
  EXPECT_TRUE(accounting["freed_signal_tracks_by_layer"].IsNull());
  EXPECT_FALSE(report["channel_ir_drop_verified"].GetBool());
  EXPECT_EQ(report["schema_version"].GetInt(), 3);
  ASSERT_EQ(report["supplies"].Size(), 2);
  for (unsigned i = 0; i < 2; ++i) {
    EXPECT_EQ(report["supplies"][i]["net"].GetString(), p.nets[i]);
    EXPECT_EQ(report["supplies"][i]["pins"].Size(), p.pin_names[i].size());
  }
  EXPECT_DOUBLE_EQ(report["channel_max_ir_drop_v"].GetDouble(), .001);
  EXPECT_EQ(output.components, input.components);
  for (const auto& net : output.special_nets) {
    for (const auto& shape : ph::route_geometry(t, output, net, true)) {
      if (shape.layer == "M3") {
        EXPECT_TRUE(shape.rect.xh <= 2940 || shape.rect.xl >= 3060);
      }
    }
  }
  // Reserve the same slot from an actual FIXED signal, then rebuild the PG.
  // The untouched grid shorts it; deriving skips must preserve the signal and
  // still pass the PG connectivity checks.
  auto routed          = input;
  t.layers["M3"].width = .020;
  ph::Route_path signal;
  signal.layer = "M3";
  signal.steps = {ph::Route_point{3000, 2800, {}}, ph::Route_point{3000, 3400, {}}};
  routed.nets.push_back({"critical", {}, {{"FIXED", {signal}}}, "SIGNAL"});
  auto unreserved = p;
  unreserved.channels.clear();
  EXPECT_THROW(make_power_grid(t, routed, unreserved), std::invalid_argument);
  const auto reserved = reserve_power_grid_channels(t, routed, unreserved);
  ASSERT_EQ(reserved.channels.size(), 1);
  EXPECT_EQ(reserved.channels.front().layer, "M3");
  const auto protected_grid = make_power_grid(t, routed, reserved);
  EXPECT_EQ(protected_grid.nets, routed.nets);
  EXPECT_EQ(protected_grid.special_nets, output.special_nets);
  routed.nets.front().wires.front().paths.front().steps = {ph::Route_point{3075, 2800, {}}, ph::Route_point{3075, 3400, {}}};
  EXPECT_EQ(reserve_power_grid_channels(t, routed, unreserved).channels.size(), 1);
  routed.nets.front().wires.front().paths.front().steps = {ph::Route_point{3100, 2800, {}}, ph::Route_point{3100, 3400, {}}};
  EXPECT_TRUE(reserve_power_grid_channels(t, routed, unreserved).channels.empty());

  EXPECT_EQ(reserve_power_grid_channels(t, input, unreserved).channels.size(), 0);
  // Every piece still has rail vias, but there is no vertical path between
  // the top and bottom halves. Per-stripe attachment alone misses this.
  p.channels = {{"M3", "gap", {0, 3.10, 5.5, 3.20}}};
  try {
    (void)make_power_grid(t, input, p, &stats);
    FAIL() << "accepted disconnected grid";
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find("disconnected power-grid"), std::string::npos);
  }
  EXPECT_EQ(stats.removed_stacks, removed_before);
  EXPECT_EQ(stats.removed_planned_length_um, length_before);
  EXPECT_TRUE(input.special_nets.empty());
  p.channels.clear();
  (void)make_power_grid(t, input, p, &stats);
  EXPECT_TRUE(stats.removed_stacks.empty());
  EXPECT_TRUE(stats.added_stacks.empty());
  EXPECT_TRUE(stats.removed_planned_length_um.empty());
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(PowerGridExport, RetainedSignalMustClearGeneratedSupplyMetal) {
  auto tech = technology();
  for (const auto& name : tech.routing_layers) {
    auto& layer         = tech.layers.at(name);
    layer.width         = .018;
    layer.rules.loaded  = true;
    layer.rules.spacing = {{.010, {}, {}}};
  }
  auto                 input = frame();
  physical::Route_path path;
  path.layer = "M1";
  path.steps = {physical::Route_point{3000, 2680, {}}, physical::Route_point{3000, 2720, {}}};
  input.nets.push_back({"critical", {}, {{"FIXED", {path}}}, "SIGNAL"});
  const auto original = input.nets;
  EXPECT_THROW(make_power_grid(tech, input, profile(tech)), std::invalid_argument);
  EXPECT_EQ(input.nets, original);
  EXPECT_THROW(reserve_power_grid_channels(tech, input, profile(tech)), std::invalid_argument);
  // Clear of the rail geometrically, but still inside its spacing keepout.
  input.nets.front().wires.front().paths.front().steps
      = {physical::Route_point{3000, 2725, {}}, physical::Route_point{3000, 2750, {}}};
  EXPECT_THROW(make_power_grid(tech, input, profile(tech)), std::invalid_argument);
  input.nets.front().wires.front().paths.front().steps
      = {physical::Route_point{3000, 2800, {}}, physical::Route_point{3000, 2850, {}}};
  const auto output = make_power_grid(tech, input, profile(tech));
  EXPECT_EQ(output.nets, input.nets);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(PowerGridExport, PreservesSignalViasWhenGeneratedNamesCollide) {
  auto tech = technology();
  for (const auto& name : tech.routing_layers) {
    auto& layer         = tech.layers.at(name);
    layer.width         = .018;
    layer.rules.loaded  = true;
    layer.rules.spacing = {{.010, {}, {}}};
  }
  auto       input    = frame();
  const auto baseline = make_power_grid(tech, input, profile(tech));
  ASSERT_EQ(baseline.vias.size(), 1);
  // Deliberately reuse the PG generator's natural name for a different signal
  // via. Neither the retained definition nor its wire references may change.
  const auto name = baseline.vias.front().name;
  input.vias.push_back({name, {{"M1", {-9, -9, 9, 9}}, {"M2", {-9, -9, 9, 9}}, {"V1", {-4, -4, 4, 4}}}});
  ph::Route_path route;
  route.layer = "M1";
  route.steps = {ph::Route_point{1000, 1000, {}}, ph::Route_via{name}, ph::Route_point{1200, 1000, {}}};
  input.nets.push_back({"signal", {}, {{"FIXED", {route}}}, "SIGNAL"});
  const auto original = ph::route_geometry(tech, input, input.nets.front());
  const auto output   = make_power_grid(tech, input, profile(tech));
  ASSERT_EQ(output.vias.size(), 2);
  EXPECT_EQ(output.vias.front(), input.vias.front());
  EXPECT_NE(output.vias.back().name, name);
  EXPECT_EQ(output.nets, input.nets);
  EXPECT_EQ(ph::route_geometry(tech, output, output.nets.front()), original);
  for (const auto& net : output.special_nets) {
    EXPECT_FALSE(ph::route_geometry(tech, output, net, true).empty());
  }
  const auto file = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "signal_vias_grid.def";
  ph::write_def(output, file.string());
  EXPECT_NO_THROW(ph::verify_def_roundtrip(output, ph::read_def(file.string())));
  input.vias.push_back(input.vias.front());
  EXPECT_THROW(make_power_grid(tech, input, profile(tech)), std::invalid_argument);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(PowerGridExport, RegenerationChecksOrientedSignalIosWithoutMovingThem) {
  auto tech = technology();
  for (const auto& name : tech.routing_layers) {
    auto& layer         = tech.layers.at(name);
    layer.width         = .018;
    layer.rules.loaded  = true;
    layer.rules.spacing = {{.010, {}, {}}};
  }
  auto input = frame();
  input.terminals.push_back(
      {"io", "signal", "INPUT", 3000, 2680, ph::Orientation::FS, ph::Status::PLACED, {{"M1", {-9, -30, 9, -10}}}});
  // Before orientation this rectangle clears the rail. Reflection puts it
  // across the rail, so checking local coordinates would miss the short.
  EXPECT_NO_THROW(make_power_grid(tech, input, profile(tech)));
  EXPECT_THROW(make_power_grid(tech, input, profile(tech), nullptr, true), std::invalid_argument);
  input.terminals.front().use = "CLOCK";
  EXPECT_THROW(make_power_grid(tech, input, profile(tech), nullptr, true), std::invalid_argument);
  input.terminals.front().shapes.front().rect = {-9, -120, 9, -100};
  const auto output                           = make_power_grid(tech, input, profile(tech), nullptr, true);
  EXPECT_EQ(output.terminals.front(), input.terminals.front());
  input.terminals.front().status = ph::Status::UNPLACED;
  EXPECT_THROW(make_power_grid(tech, input, profile(tech), nullptr, true), std::invalid_argument);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(PowerGridExport, ContainedPinLandingDoesNotInventAnEndOfLineInsideExistingMetal) {
  auto tech               = technology();
  tech.manufacturing_grid = .001;
  for (const auto& name : tech.routing_layers) {
    auto& layer         = tech.layers.at(name);
    layer.width         = .018;
    layer.rules.loaded  = true;
    layer.rules.spacing = {
        {.018, {}, {}}
    };
  }
  tech.layers.at("M1").rules.properties["LEF58_EOLKEEPOUT"] = {"EOLKEEPOUT 0.01825 EXTENSION 0.0 0.0 0.031 ;"};
  auto& master                                              = tech.macros.at("cell");
  master.width                                              = .594;
  master.pins.at("VDD").shapes.front().rect.xh              = .594;
  master.pins.at("VSS").shapes.front().rect.xh              = .594;
  // The wide end of this L-shaped pin belongs to the LEF. A via's lower pad
  // lies inside its vertical rectangle; that pad has no independent EOL.
  master.pins["A"]                                          = {
      "INPUT",
      "SIGNAL",
      {{"M1", {.261, .225, .440, .243}}, {"M1", {.261, .183, .279, .243}}}
  };
  tech.vias["CONTACT"].shapes = {
      {"M1", {-.009, -.011, .009, .011}},
      {"M2", {-.011, -.009, .011, .009}},
      {"V1", {-.009, -.009, .009, .009}}
  };
  auto input                                 = frame();
  input.components.front().place.width       = 594;
  input.components.front().place.orientation = ph::Orientation::FS;
  for (auto& row : input.rows) {
    row.orientation = row.orientation == ph::Orientation::N ? ph::Orientation::FS : ph::Orientation::N;
  }
  input.nets.push_back({"signal", {{"u0", "A"}}, {{"ROUTED", {ph::via_path("M1", 3186, 2745, "CONTACT")}}}, "SIGNAL"});
  const ph::Signal_metal_rules rules(tech, input);
  const ph::Shape              landing{
      "M1",
      {3177, 2734, 3195, 2756}
  };
  const ph::Shape rail{
      "M1",
      {2709, 2691, 4851, 2709}
  };
  EXPECT_EQ(rules.spacing(landing, rail).reason, "LEF58 EOL keepout");
  EXPECT_NO_THROW(make_power_grid(tech, input, profile(tech)));

  auto unmatched = input;
  unmatched.nets.front().connections.clear();
  EXPECT_THROW(make_power_grid(tech, unmatched, profile(tech)), std::invalid_argument);
  // Same geometry but the rail near the pad is not pre-existing supply-pin
  // metal of this cell. Other supply pins cannot stand in for this ownership.
  auto extended_pg                                                    = tech;
  extended_pg.macros.at("cell").pins.at("VDD").shapes.front().rect.xh = .278;
  const auto rejected_signal_pg                                       = [&](const ph::Technology& candidate) {
    try {
      (void)make_power_grid(candidate, input, profile(candidate));
      FAIL() << "accepted new signal/PG spacing violation";
    } catch (const std::invalid_argument& e) {
      EXPECT_NE(std::string(e.what()).find("power grid conflicts with retained signal signal on M1"), std::string::npos);
    }
  };
  rejected_signal_pg(extended_pg);
  // Removing one DBU of the original pin leaves ordinary spacing legal but
  // makes the same landing new metal: the isolated EOL must now be rejected.
  auto uncontained                                                 = tech;
  uncontained.macros.at("cell").pins.at("A").shapes.back().rect.xl = .262;
  rejected_signal_pg(uncontained);
  for (const auto* use : {"POWER", "GROUND"}) {
    auto invalid_source                               = tech;
    invalid_source.macros.at("cell").pins.at("A").use = use;
    rejected_signal_pg(invalid_source);
  }
  for (const auto* use : {"CLOCK", "SIGNAL", ""}) {
    auto source                               = tech;
    source.macros.at("cell").pins.at("A").use = use;
    EXPECT_NO_THROW(make_power_grid(source, input, profile(source)));
  }
  auto unplaced_source                            = input;
  unplaced_source.components.front().place.status = ph::Status::UNPLACED;
  EXPECT_THROW(make_power_grid(tech, unplaced_source, profile(tech)), std::invalid_argument);
  auto virtual_source                            = tech;
  virtual_source.macros.at("cell").virtual_proxy = true;
  EXPECT_THROW(make_power_grid(virtual_source, input, profile(virtual_source)), std::invalid_argument);
  // A second row's supply pin can cover the rest of the same physical rail.
  // The coverage is a union: neither individual pin covers the whole footprint.
  auto adjacent_tech               = extended_pg;
  adjacent_tech.macros["neighbor"] = technology().macros.at("cell");
  auto adjacent                    = input;
  adjacent.rows.insert(adjacent.rows.begin(), {"below", "site", 2700, 2430, 40, 54, ph::Orientation::N});
  Ann_place neighbor(3186, 2430, 108, 270);
  neighbor.status = ph::Status::PLACED;
  adjacent.components.push_back({"u1", "neighbor", neighbor});
  EXPECT_NO_THROW(make_power_grid(adjacent_tech, adjacent, profile(adjacent_tech)));
  auto  wrong_owner              = adjacent_tech;
  auto& neighbor_pins            = wrong_owner.macros.at("neighbor").pins;
  neighbor_pins.at("VSS").shapes = neighbor_pins.at("VDD").shapes;
  neighbor_pins.erase("VDD");
  try {
    (void)make_power_grid(wrong_owner, adjacent, profile(wrong_owner));
    FAIL() << "used the other supply net to cover original PG metal";
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find("power grid conflicts with retained signal signal on M1"), std::string::npos);
  }
  // A one-DBU hole must not disappear into the supply-pin bounding hull.
  adjacent_tech.macros.at("neighbor").pins.at("VDD").shapes.front().rect.xl = .009;
  try {
    (void)make_power_grid(adjacent_tech, adjacent, profile(adjacent_tech));
    FAIL() << "accepted a gap in original supply-pin metal";
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find("power grid conflicts with retained signal signal on M1"), std::string::npos);
  }
  auto extended_landing                                      = tech;
  extended_landing.vias.at("CONTACT").shapes.front().rect.yl = -.019;
  EXPECT_THROW(make_power_grid(extended_landing, input, profile(extended_landing)), std::invalid_argument);
  // Even declared pin containment never excuses overlap or ordinary spacing.
  auto shorted                                                 = extended_landing;
  shorted.macros.at("cell").pins.at("A").shapes.back().rect.yh = .290;
  shorted.vias.at("CONTACT").shapes.front().rect.yl            = -.045;
  EXPECT_THROW(make_power_grid(shorted, input, profile(shorted)), std::invalid_argument);
  auto ordinary_spacing                          = tech;
  ordinary_spacing.layers.at("M1").rules.spacing = {
      {.026, {}, {}}
  };
  EXPECT_THROW(make_power_grid(ordinary_spacing, input, profile(ordinary_spacing)), std::invalid_argument);
  auto cut_collision                                  = tech;
  cut_collision.vias.at("CONTACT").shapes.back().rect = {-.009, -.054, .009, -.036};
  try {
    (void)make_power_grid(cut_collision, input, profile(cut_collision));
    FAIL() << "accepted signal cut at a supply cut";
  } catch (const std::invalid_argument& e) {
    EXPECT_NE(std::string(e.what()).find(" on V1 "), std::string::npos);
  }
}
}  // namespace livehd::placement
