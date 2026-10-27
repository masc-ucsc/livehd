#include "place.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <print>
#include <random>
#include <set>
#include <sstream>
#include <tuple>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "inou/lefdef/lef_writer.hpp"
#include "macro_tracks.hpp"
#include "memory_proxy.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "place_test_fixture.hpp"
#include "placement_technology.hpp"
#include "preroute.hpp"
#include "rapidjson/document.h"
#include "repair.hpp"
#include "technology_rc.hpp"
namespace livehd::placement {
namespace gu = graph_util;

TEST(Placement, ExperimentalBoundaryRefinementsRunOnlyAtTheFinalStage) {
  hhds::GraphLibrary source;
  auto               io  = source.create_io("busorder_stages");
  auto               inv = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  for (int bit = 0; bit < 4; ++bit) {
    io->add_input(std::format("a[{}]", bit), bit);
    io->set_bits(std::format("a[{}]", bit), 1);
    io->add_output(std::format("y[{}]", bit), bit);
    io->set_bits(std::format("y[{}]", bit), 1);
  }
  auto graph = io->create_graph();
  for (int bit = 0; bit < 4; ++bit) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(inv);
    node.set_name(std::format("inv{}", bit));
    if (bit == 0) {
      node.attr(livehd::attrs::preroute_exclude).set();
    }
    auto input = graph->get_input_pin(std::format("a[{}]", bit));
    gu::set_bits(input, 1);
    node.create_sink_pin("A").connect_driver(input);
    auto output = node.create_driver_pin("Y");
    if (bit == 1) {
      output.attr(livehd::attrs::preroute_net_exclude).set();
    }
    gu::set_bits(output, 1);
    graph->get_output_pin(std::format("y[{}]", bit)).connect_driver(output);
  }
  auto options                    = plain_options();
  options.lefs                    = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.core                    = {0, 0, 55.2, 54.4};
  options.die                     = options.core;
  options.iterations              = 20;
  options.bins                    = 4;
  options.density                 = .5;
  options.busorder.enabled        = true;
  options.busorder.iterations     = 20;
  options.busorder.refresh        = 10;
  options.region.max_cells        = 2;
  options.boundarypins.iterations = 20;
  options.retain_preroute_design  = true;
  for (int mode : {0, 1, 2}) {
    const bool boundary          = mode != 0;
    options.busorder.enabled     = mode == 0;
    options.boundarypins.enabled = mode == 1;
    options.boundarypins.align   = mode == 2;
    for (bool terminal : {false, true}) {
      for (auto stage : {Placement_stage::macro, Placement_stage::final}) {
        hhds::GraphLibrary placed;
        options.optimization_pending = !terminal;
        options.completed_stage      = stage;
        options.output_dir = std::format("{}/busorder_stages_{}_{}_{}", std::getenv("TEST_TMPDIR"), mode, terminal, int(stage));
        std::filesystem::remove_all(options.output_dir);
        const auto result = place(*graph, placed, options);
        check_legality(physical::read_lef(options.lefs), result.floorplan);
        ASSERT_EQ(bool(result.preroute_design), stage == Placement_stage::final);
        if (result.preroute_design) {
          const auto& snapshot = *result.preroute_design;
          const auto  bindings = bind_preroute(result.floorplan, *result.parasitics, snapshot);
          EXPECT_EQ(bindings.size(), result.parasitics->nets.size());
          EXPECT_EQ(snapshot.dont_touch_pins, (std::set<std::string>{"inv0:A", "inv0:Y", "inv1:Y"}));
          EXPECT_EQ(std::count_if(bindings.begin(), bindings.end(), [](const auto& binding) { return binding.dont_touch; }), 3);
          snapshot.design.validate();
          ASSERT_EQ(snapshot.pin_labels.size(), snapshot.design.pin2node.size());
          for (size_t p = 0; p < snapshot.pin_labels.size(); ++p) {
            const auto node = snapshot.design.pin2node[p];
            const auto pos  = result.pin_locations.at(snapshot.pin_labels[p]);
            EXPECT_DOUBLE_EQ(pos.first, snapshot.design.node_x[node] + snapshot.design.pin_offset_x[p]);
            EXPECT_DOUBLE_EQ(pos.second, snapshot.design.node_y[node] + snapshot.design.pin_offset_y[p]);
          }
        }
        const auto report_path = options.output_dir + (boundary ? "/boundarypins/report.json" : "/busorder/report.json");
        EXPECT_EQ(result.floorplan.components.size(), 4u);
        EXPECT_EQ(std::filesystem::exists(report_path), stage == Placement_stage::final);
        if (stage == Placement_stage::final) {
          std::ifstream       report_file(report_path);
          const std::string   text((std::istreambuf_iterator<char>(report_file)), {});
          rapidjson::Document report;
          report.Parse(text.c_str());
          ASSERT_FALSE(report.HasParseError());
          ASSERT_GE(report["history"].Size(), 3u);
          EXPECT_STREQ(report["history"][report["history"].Size() - 1]["stage"].GetString(), "after_legalization");
        }
      }
    }
  }
}

TEST(Placement, RowLegalityAcceptsOnlyTechnologyPermittedHorizontalMirrors) {
  auto                tech = physical::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  physical::Floorplan fp;
  const auto          sw    = std::llround(tech.sites.at("unithd").width * tech.dbu);
  const auto          rh    = std::llround(tech.sites.at("unithd").height * tech.dbu);
  const auto          width = std::llround(tech.macros.at("sky130_fd_sc_hd__inv_1").width * tech.dbu);
  fp.rows.push_back({"r0", "unithd", 0, 0, 10, sw, Ann_place::Orientation::N});
  fp.rows.push_back({"r1", "unithd", 0, rh, 10, sw, Ann_place::Orientation::FS});
  for (int i = 0; i < 2; ++i) {
    physical::Component c;
    c.name              = "c" + std::to_string(i);
    c.macro             = "sky130_fd_sc_hd__inv_1";
    c.place             = Ann_place(0, i * rh, width, rh);
    c.place.status      = Ann_place::Status::PLACED;
    c.place.orientation = i ? Ann_place::Orientation::S : Ann_place::Orientation::FN;
    fp.components.push_back(c);
  }
  EXPECT_NO_THROW(check_legality(tech, fp));
  tech.macros.at("sky130_fd_sc_hd__inv_1").symmetry_y = false;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  tech.macros.at("sky130_fd_sc_hd__inv_1").symmetry_y = true;
  fp.components[0].place.orientation                  = Ann_place::Orientation::FS;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.components[0].place.orientation = Ann_place::Orientation::W;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
}
// A two-row copy of a one-row cell in PG rail template t (pg_rails.hpp): the
// cell's PG shapes restacked so that local row edge e carries the cell's
// bottom-edge rail when e + t is even and its top-edge rail otherwise. t0 fits
// starts on N rows, t1 starts on FS rows.
physical::Macro two_row_template(const physical::Macro& cell, int t) {
  auto       m    = cell;
  const auto row  = cell.height;
  m.height       *= 2;
  for (auto& [name, pin] : m.pins) {
    if (pin.use != "POWER" && pin.use != "GROUND") {
      continue;
    }
    std::vector<physical::Shape> shapes;
    for (const auto& s : pin.shapes) {
      const bool top = std::abs((s.rect.yl + s.rect.yh) / 2 - row) < std::abs((s.rect.yl + s.rect.yh) / 2);
      for (int edge = 0; edge <= 2; ++edge) {
        if (((edge + t) % 2 == 1) == top) {
          auto copy = s;
          copy.rect = copy.rect.translated(0, edge * row - (top ? row : 0));
          shapes.push_back(copy);
        }
      }
    }
    pin.shapes = std::move(shapes);
  }
  return m;
}
TEST(Placement, RowLegalityChecksEveryBandOfTwoRowCells) {
  // A two-row CORE cell (a domino abstract) on the single-height core site is
  // legal at both start parities with the start row's orientation and the
  // rail template of that parity, and every row band it spans is checked.
  auto        tech          = physical::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  const auto  sw            = std::llround(tech.sites.at("unithd").width * tech.dbu);
  const auto  rh            = std::llround(tech.sites.at("unithd").height * tech.dbu);
  const auto& inv           = tech.macros.at("sky130_fd_sc_hd__inv_1");
  tech.macros["__lhd_domino_two_row"]    = two_row_template(inv, 0);
  tech.macros["__lhd_domino_two_row_t1"] = two_row_template(inv, 1);
  const auto          width = std::llround(inv.width * tech.dbu);
  physical::Floorplan fp;
  for (int r = 0; r < 4; ++r) {
    fp.rows.push_back(
        {"r" + std::to_string(r), "unithd", 0, r * rh, 20, sw, r % 2 ? Ann_place::Orientation::FS : Ann_place::Orientation::N});
  }
  const auto add = [&](const char* master, int64_t x, int64_t y, Ann_place::Orientation o) {
    physical::Component c;
    c.name              = "c" + std::to_string(fp.components.size());
    c.macro             = master;
    c.place             = Ann_place(x, y, width, std::llround(tech.macros.at(master).height * tech.dbu));
    c.place.status      = Ann_place::Status::PLACED;
    c.place.orientation = o;
    fp.components.push_back(c);
    return fp.components.size() - 1;
  };
  const auto even = add("__lhd_domino_two_row", 0, 0, Ann_place::Orientation::N);
  const auto odd  = add("__lhd_domino_two_row_t1", 4 * sw, rh, Ann_place::Orientation::FS);
  EXPECT_NO_THROW(check_legality(tech, fp));
  // The other template on either start puts a rail on the opposite polarity.
  const auto expect_parity = [&](const char* why) {
    try {
      check_legality(tech, fp);
      ADD_FAILURE() << "wrong rail parity accepted: " << why;
    } catch (const std::runtime_error& error) {
      EXPECT_NE(std::string(error.what()).find("rail parity"), std::string::npos) << error.what();
    }
  };
  fp.components[odd].macro = "__lhd_domino_two_row";
  expect_parity("t0 on an FS start");
  fp.components[odd].macro  = "__lhd_domino_two_row_t1";
  fp.components[even].macro = "__lhd_domino_two_row_t1";
  expect_parity("t1 on an N start");
  fp.components[even].macro = "__lhd_domino_two_row";
  // A rail on one edge only is still checked; unknown row rails are refused.
  auto one_rail             = tech;
  one_rail.macros.at("__lhd_domino_two_row").pins.at("VPWR").shapes.clear();
  one_rail.macros.at("__lhd_domino_two_row").pins.at("VGND").shapes.resize(1);  // the bottom edge alone
  EXPECT_NO_THROW(check_legality(one_rail, fp));
  for (auto& [name, m] : one_rail.macros) {
    if (m.height < tech.macros.at("__lhd_domino_two_row").height) {
      m.pins.erase("VPWR");  // no one-row cell gives the row rails
    }
  }
  EXPECT_THROW(check_legality(one_rail, fp), std::runtime_error);
  // A tap-like one-row cell with one polarity on both edges says nothing of
  // the row rails: they stay known.
  auto tapped                       = tech;
  auto tap                          = inv;
  tap.pins.at("VPWR").shapes        = {};
  auto&      tap_ground             = tap.pins.at("VGND").shapes;
  const auto ground                 = tap_ground.front();
  tap_ground                        = {ground, ground};
  tap_ground[1].rect                = ground.rect.translated(0, inv.height);
  tapped.macros["tap_one_polarity"] = tap;
  EXPECT_NO_THROW(check_legality(tapped, fp));
  // A library's own multi-row master is not ours to check: the same wrong
  // template under a non-provisional name is accepted.
  tech.macros["lib_two_row_t1"] = tech.macros.at("__lhd_domino_two_row_t1");
  fp.components[even].macro     = "lib_two_row_t1";
  EXPECT_NO_THROW(check_legality(tech, fp));
  fp.components[even].macro = "__lhd_domino_two_row";
  // A single-row cell in the upper band of the even-start cell overlaps it.
  const auto upper = add("sky130_fd_sc_hd__inv_1", 0, rh, Ann_place::Orientation::FS);
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.components[upper].place.x = 10 * sw;
  EXPECT_NO_THROW(check_legality(tech, fp));
  // The orientation must be the start row's: FS on an N start is rejected.
  fp.components[even].place.orientation = Ann_place::Orientation::FS;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.components[even].place.orientation = Ann_place::Orientation::N;
  // The top band must exist: a start on the last row leaves the core.
  fp.components[even].place.y           = 3 * rh;
  fp.components[even].place.orientation = Ann_place::Orientation::FS;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
}
TEST(Placement, LegalStageSnapshotsUsePhysicalRowsAndPreserveFixedMirrors) {
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("snapshot_rows");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (unsigned i = 0; i < 4; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(inv);
    node.set_name("inv" + std::to_string(i));
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin("Y");
    gu::set_bits(signal, 1);
    if (i == 0) {
      Ann_place fixed(0, 0, 1380, 2720);
      fixed.status      = Ann_place::Status::FIXED;
      fixed.orientation = Ann_place::Orientation::FN;
      node.attr(attrs::place).set(fixed);
    }
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/snapshot_rows";
  options.iterations = 0;
  options.snapshots  = true;
  options.bins       = 2;
  options.core       = {0, 0, 2.76, 5.44};
  options.die        = {0, 0, 5.52, 10.88};
  const auto result  = place(*graph, flat, options);
  const auto tech    = physical::read_lef(options.lefs);
  for (const auto* stage : {"before_legalization.def", "after_legalization.def", "after_detailed.def"}) {
    const auto frame   = physical::read_def(options.output_dir + "/" + stage);
    unsigned   flipped = 0;
    for (const auto& cell : frame.components) {
      if (cell.name == "inv0") {
        EXPECT_EQ(cell.place.status, Ann_place::Status::FIXED);
        EXPECT_EQ(cell.place.orientation, Ann_place::Orientation::FN);
      } else if (std::string_view(stage) == "before_legalization.def") {
        EXPECT_EQ(cell.place.orientation, Ann_place::Orientation::N);
      } else {
        const auto row = static_cast<size_t>(cell.place.y / 2720);
        ASSERT_LT(row, frame.rows.size());
        EXPECT_EQ(cell.place.orientation, frame.rows[row].orientation);
        flipped += cell.place.orientation == Ann_place::Orientation::FS;
      }
    }
    if (std::string_view(stage) != "before_legalization.def") {
      EXPECT_GE(flipped, 2);
      EXPECT_NO_THROW(check_legality(tech, frame));
    }
    if (std::string_view(stage) == "after_detailed.def") {
      ASSERT_EQ(frame.components.size(), result.floorplan.components.size());
      for (const auto& cell : frame.components) {
        const auto final = std::find_if(result.floorplan.components.begin(),
                                        result.floorplan.components.end(),
                                        [&](const auto& candidate) { return candidate.name == cell.name; });
        ASSERT_NE(final, result.floorplan.components.end());
        // DEF does not carry LEF dimensions or require component order.
        EXPECT_EQ(cell.macro, final->macro);
        EXPECT_EQ(cell.place.x, final->place.x);
        EXPECT_EQ(cell.place.y, final->place.y);
        EXPECT_EQ(cell.place.orientation, final->place.orientation);
        EXPECT_EQ(cell.place.status, final->place.status);
      }
    }
  }
}
TEST(Placement, TerminalLegalizerIsLastAndPreservesItsExactOutput) {
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("terminal_rows");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (unsigned i = 0; i < 12; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(inv);
    node.set_name("inv" + std::to_string(i));
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin("Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options                 = plain_options();
  options.lefs                 = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir           = std::string(std::getenv("TEST_TMPDIR")) + "/terminal_rows";
  options.iterations           = 0;
  options.snapshots            = true;
  options.bins                 = 4;
  options.density              = .5;
  options.utilization          = .15;
  options.optimization_pending = false;
  options.terminal_legalize_options.critical_fraction = 0;
  bool later_callback                                 = false;
  options.after_stage                                 = [&](Placement_stage, const Result&) {
    later_callback = true;
    return true;
  };
  const auto result = place(*graph, flat, options);
  EXPECT_FALSE(later_callback);
  const auto tech = physical::read_lef(options.lefs);
  EXPECT_NO_THROW(check_legality(tech, result.floorplan));
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/before_legalization.def"));
  EXPECT_FALSE(std::filesystem::exists(options.output_dir + "/after_detailed.def"));
  const auto snapshot = physical::read_def(options.output_dir + "/after_legalization.def");
  ASSERT_EQ(snapshot.components.size(), result.floorplan.components.size());
  for (const auto& cell : snapshot.components) {
    const auto final = std::find_if(result.floorplan.components.begin(), result.floorplan.components.end(), [&](const auto& c) {
      return c.name == cell.name;
    });
    ASSERT_NE(final, result.floorplan.components.end());
    EXPECT_EQ(final->place.x, cell.place.x);
    EXPECT_EQ(final->place.y, cell.place.y);
    EXPECT_EQ(final->place.orientation, cell.place.orientation);
    EXPECT_EQ(final->place.status, physical::Status::PLACED);
  }
  std::ifstream       file(options.output_dir + "/terminal_legalize.json");
  std::string         text{std::istreambuf_iterator<char>(file), {}};
  rapidjson::Document report;
  report.Parse(text.c_str());
  ASSERT_FALSE(report.HasParseError());
  for (const auto& bin : report["bins"].GetArray()) {
    EXPECT_LE(bin["after_density"].GetDouble(), .8 + 1e-8);
  }
}

// A chain of sky130 cells (12 by default) in which c1, c5, c9... are a
// two-row copy of inv_1 named `master` in rail template t0, given in its own
// LEF with its t1 rail twin `master`_t1, for full placement through the
// terminal legalizer. `variants` instantiates other A/Y masters (by cell
// index) whose LEF the caller supplies.
constexpr const char* kDominoTwoRow = "__lhd_domino_two_row";
const std::string     kDominoTwin   = std::string(kDominoTwoRow) + "_t1";
struct Two_row_chain {
  std::vector<std::string>     lefs{"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  hhds::GraphLibrary           source, flat;
  std::shared_ptr<hhds::Graph> graph;
  Options                      options = plain_options();
  explicit Two_row_chain(const std::string& master, unsigned cells = 12, const std::map<unsigned, std::string>& variants = {}) {
    const auto  tech = physical::read_lef(lefs);
    const auto& inv  = tech.macros.at("sky130_fd_sc_hd__inv_1");
    const auto  dir  = std::string(std::getenv("TEST_TMPDIR")) + "/" + master;
    std::filesystem::create_directories(dir);
    std::ofstream(dir + "/two_row.lef") << physical::write_lef_macros(
        {
            {        master, two_row_template(inv, 0)},
            {master + "_t1", two_row_template(inv, 1)}
    },
        tech.dbu);
    auto io = source.create_io("two_rows");
    io->add_input("a", 0);
    io->set_bits("a", 1);
    io->add_output("y", 0);
    io->set_bits("y", 1);
    graph    = io->create_graph();
    std::map<std::string, std::shared_ptr<hhds::GraphIO>> masters;
    const auto                                            declare = [&](const std::string& name) {
      auto& cell = masters[name];
      if (!cell) {
        cell = source.create_io(name);
        cell->add_input("A", 0);
        cell->add_output("Y", 0);
      }
      return cell;
    };
    auto signal = graph->get_input_pin("a");
    gu::set_bits(signal, 1);
    for (unsigned i = 0; i < cells; ++i) {
      auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
      node.set_subnode(declare(variants.contains(i) ? variants.at(i) : i % 4 == 1 ? master : "sky130_fd_sc_hd__inv_1"));
      node.set_name("c" + std::to_string(i));
      node.create_sink_pin("A").connect_driver(signal);
      signal = node.create_driver_pin("Y");
      gu::set_bits(signal, 1);
    }
    graph->get_output_pin("y").connect_driver(signal);
    options.lefs                                        = {lefs[0], lefs[1], dir + "/two_row.lef"};
    options.output_dir                                  = dir + "/out";
    options.iterations                                  = 0;
    options.bins                                        = 4;
    options.density                                     = .5;
    options.utilization                                 = .15;
    options.optimization_pending                        = false;
    options.terminal_legalize_options.critical_fraction = 0;
  }
};

TEST(Placement, TwoRowCellsAreAnchoredBeforeTerminalLegalization) {
  // Movable two-row CORE cells (domino abstracts) go through the anchor
  // prepass, the unchanged terminal legalizer and release; the other final
  // legalizers refuse them.
  // Each start takes the PG rail template its row needs: t0 on N rows, its
  // twin t1 on FS rows, in the floorplan and the netlist alike.
  Two_row_chain chain(kDominoTwoRow);
  auto&         options                     = chain.options;
  options.row_access                        = physical::Row_access_convention{};
  options.multirow_templates[kDominoTwoRow] = {
      {physical::Row_access::horizontal, physical::Row_access::vertical},
      kDominoTwin
  };
  options.multirow_templates[kDominoTwin] = {
      {physical::Row_access::horizontal, physical::Row_access::vertical},
      kDominoTwoRow
  };
  const auto result = place(*chain.graph, chain.flat, options);
  const auto all    = physical::read_lef(options.lefs);
  EXPECT_NO_THROW(check_legality(all, result.floorplan));
  const auto rh      = std::llround(all.sites.at("unithd").height * all.dbu);
  size_t     two_row = 0;
  for (const auto& c : result.floorplan.components) {
    if (c.macro != kDominoTwoRow && c.macro != kDominoTwin) {
      continue;
    }
    ++two_row;
    EXPECT_EQ(c.place.height, 2 * rh);
    EXPECT_EQ(c.place.status, physical::Status::PLACED);
    const auto row
        = std::find_if(result.floorplan.rows.begin(), result.floorplan.rows.end(), [&](const auto& r) { return r.y == c.place.y; });
    ASSERT_NE(row, result.floorplan.rows.end()) << c.name;
    EXPECT_EQ(c.place.orientation, row->orientation) << c.name;
  }
  EXPECT_EQ(two_row, 3U);
  rapidjson::Document qor;
  qor.Parse(result.qor.c_str());
  ASSERT_TRUE(qor.HasMember("multirow"));
  EXPECT_EQ(qor["multirow"]["cells"].GetUint64(), 3U);
  EXPECT_EQ(qor["multirow"]["even_starts"].GetUint64() + qor["multirow"]["odd_starts"].GetUint64(), 3U);
  EXPECT_STREQ(qor["multirow"]["row_access"].GetString(), "stand-in");
  EXPECT_NEAR(lef_hpwl_um(all, result.floorplan), qor["hpwl_um"].GetDouble(), 1e-6);

  // An odd start: reseed the two-row cells on core row 1. With zero GP
  // iterations the seed reaches the anchor prepass, which keeps them there in
  // the row's FS orientation; their pins follow the flip.
  const auto bottom
      = std::min_element(result.floorplan.rows.begin(), result.floorplan.rows.end(), [](const auto& a, const auto& b) {
          return a.y < b.y;
        })->y;
  auto seed = result.floorplan;
  for (auto& c : seed.components) {
    if (c.macro == kDominoTwoRow || c.macro == kDominoTwin) {
      c.place.y = bottom + rh;
    }
  }
  auto odd_options          = options;
  odd_options.restart_seed  = std::make_shared<const physical::Floorplan>(seed);
  odd_options.output_dir   += "/odd";
  const auto odd            = place(*chain.graph, chain.flat, odd_options);
  EXPECT_NO_THROW(check_legality(all, odd.floorplan));
  rapidjson::Document odd_qor;
  odd_qor.Parse(odd.qor.c_str());
  EXPECT_GE(odd_qor["multirow"]["odd_starts"].GetUint64(), 1U);
  EXPECT_EQ(odd_qor["multirow"]["rail_twins"].GetUint64(), odd_qor["multirow"]["odd_starts"].GetUint64());
  std::map<std::string, std::string> instances;  // the placed netlist's masters
  for (auto node : odd.graph->body().nodes()) {
    if (const auto io = node.get_subnode_io()) {
      instances[std::string(node.get_name())] = io->get_name();
    }
  }
  size_t flipped = 0;
  for (const auto& c : odd.floorplan.components) {
    if (c.macro == kDominoTwoRow || c.macro == kDominoTwin) {
      const bool fs  = c.place.orientation == physical::Orientation::FS;
      flipped       += fs;
      EXPECT_EQ(c.macro, fs ? kDominoTwin : kDominoTwoRow) << c.name;
      EXPECT_EQ(instances.at(c.name), c.macro) << c.name;
    }
  }
  EXPECT_GE(flipped, 1U);
  // Without a twin the FS starts are refused: every anchor starts on an N row.
  auto restricted                                         = odd_options;
  restricted.multirow_templates[kDominoTwoRow].rail_twin  = "";
  restricted.output_dir                                  += "_restricted";
  const auto even_only                                    = place(*chain.graph, chain.flat, restricted);
  EXPECT_NO_THROW(check_legality(all, even_only.floorplan));
  rapidjson::Document even_qor;
  even_qor.Parse(even_only.qor.c_str());
  EXPECT_EQ(even_qor["multirow"]["odd_starts"].GetUint64(), 0U);
  EXPECT_EQ(even_qor["multirow"]["rail_twins"].GetUint64(), 0U);
  EXPECT_NEAR(lef_hpwl_um(all, odd.floorplan), odd_qor["hpwl_um"].GetDouble(), 1e-6);
  // Every single-row path refuses them by name, each with its own reason.
  const auto previous = std::make_shared<const physical::Floorplan>(result.floorplan);
  const auto h2d      = [](Options& o) {
    o.macro_mode           = "h2d";
    o.macro_legalize_cells = true;
  };
  const std::vector<std::pair<std::function<void(Options&)>, std::string>> refusals{
      {                 [](Options& o) { o.busorder.enabled = true; },                                 "region mesh"},
      {             [](Options& o) { o.boundarypins.enabled = true; },                                 "region mesh"},
      {               [](Options& o) { o.boundarypins.align = true; },                                 "region mesh"},
      {             [](Options& o) { o.optimization_pending = true; }, "native legalization outside the final stage"},
      {[](Options& o) { o.satlegalize = o.satlegalize_route = true; },             "satlegalize treats multi-height"},
      {                   [](Options& o) { o.legalize_cells = true; },              "ECO legalization is single-row"},
      {                  [](Options& o) { o.changed_cells = {"c1"}; },              "ECO legalization is single-row"},
      {                    [&](Options& o) { o.previous = previous; },              "ECO legalization is single-row"},
      {                                                           h2d,                       "macro placement stage"},
  };
  for (const auto& [mutate, expected] : refusals) {
    auto o = options;
    mutate(o);
    try {
      (void)place(*chain.graph, chain.flat, o);
      ADD_FAILURE() << "accepted two-row cells: " << expected;
    } catch (const std::invalid_argument& error) {
      const std::string what = error.what();
      EXPECT_NE(what.find("two-row"), std::string::npos) << what;
      EXPECT_NE(what.find(expected), std::string::npos) << what;
    }
  }
}

TEST(Placement, TwoRowPlacementsKeyTheCacheAndReuseItLegally) {
  const auto cache = std::string(std::getenv("TEST_TMPDIR")) + "/two_row_cache";
  const auto setup = [&](Two_row_chain& chain) {
    chain.options.row_access                        = physical::Row_access_convention{};
    chain.options.multirow_templates[kDominoTwoRow] = {
        {physical::Row_access::horizontal, physical::Row_access::vertical},
        kDominoTwin
    };
    chain.options.multirow_templates[kDominoTwin] = {
        {physical::Row_access::horizontal, physical::Row_access::vertical},
        kDominoTwoRow
    };
    chain.options.cache_dir = cache;
  };
  const auto environment = [&] {
    std::ifstream       file(cache + "/placement.json");
    const std::string   text{std::istreambuf_iterator<char>(file), {}};
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    return std::string(doc["environment"].GetString());
  };
  Two_row_chain chain(kDominoTwoRow);
  setup(chain);
  const auto all           = physical::read_lef(chain.options.lefs);
  size_t     flipped       = 0;  // two-row cells on FS starts over every run
  const auto expect_placed = [&](const Result& r, const char* what) {
    SCOPED_TRACE(what);
    EXPECT_NO_THROW(check_legality(all, r.floorplan));
    rapidjson::Document qor;
    qor.Parse(r.qor.c_str());
    EXPECT_NEAR(lef_hpwl_um(all, r.floorplan), qor["hpwl_um"].GetDouble(), 1e-6);
    std::map<std::string, std::string> instances;
    for (auto node : r.graph->body().nodes()) {
      if (const auto io = node.get_subnode_io()) {
        instances[std::string(node.get_name())] = io->get_name();
      }
    }
    for (const auto& c : r.floorplan.components) {
      if (c.macro == kDominoTwoRow || c.macro == kDominoTwin) {
        const auto row
            = std::find_if(r.floorplan.rows.begin(), r.floorplan.rows.end(), [&](const auto& x) { return x.y == c.place.y; });
        ASSERT_NE(row, r.floorplan.rows.end()) << c.name;
        EXPECT_EQ(c.place.orientation, row->orientation) << c.name;
        // A restored placement still takes the template of its start row.
        const bool fs  = row->orientation == physical::Orientation::FS;
        flipped       += fs;
        EXPECT_EQ(c.macro, fs ? kDominoTwin : kDominoTwoRow) << c.name;
        EXPECT_EQ(instances.at(c.name), c.macro) << c.name;
      }
    }
  };
  expect_placed(place(*chain.graph, chain.flat, chain.options), "cold");
  const auto key = environment();
  expect_placed(place(*chain.graph, chain.flat, chain.options), "warm");
  EXPECT_EQ(environment(), key);
  // A design edit keeps the key, but the cached reference would re-place it
  // through single-row ECO legalization: two-row cells place cold instead.
  Two_row_chain edited(kDominoTwoRow, 14);
  setup(edited);
  const auto changed = place(*edited.graph, edited.flat, edited.options);
  expect_placed(changed, "edited");
  EXPECT_EQ(environment(), key);
  rapidjson::Document qor;
  qor.Parse(changed.qor.c_str());
  EXPECT_FALSE(qor["eco"]["used"].GetBool());
  ASSERT_TRUE(qor.HasMember("multirow"));
  EXPECT_EQ(qor["multirow"]["cells"].GetUint64(), 4U);
  // The row-access convention and the templates are part of the key.
  auto renamed             = chain.options;
  renamed.row_access->name = "owner";
  expect_placed(place(*chain.graph, chain.flat, renamed), "renamed convention");
  const auto renamed_key = environment();
  EXPECT_NE(renamed_key, key);
  auto templated                                     = chain.options;
  templated.multirow_templates["__lhd_domino_other"] = {
      {physical::Row_access::vertical, physical::Row_access::horizontal}
  };
  expect_placed(place(*chain.graph, chain.flat, templated), "another template");
  const auto templated_key = environment();
  EXPECT_NE(templated_key, key);
  EXPECT_NE(templated_key, renamed_key);
  // So is the custom variant catalog domino sizing swaps within.
  auto cataloged         = chain.options;
  cataloged.custom_cells = custom_catalog({
      Custom_variant{.name = "__lhd_domino_other_v0_t0", .family = "__lhd_domino_other_t0"}
  });
  expect_placed(place(*chain.graph, chain.flat, cataloged), "custom catalog");
  EXPECT_NE(environment(), key);
  EXPECT_NE(environment(), templated_key);
}

TEST(Placement, ARestoredPlacementTakesTheRailTemplateOfItsRows) {
  // The netlist instantiates t0 only. A cold run anchors the twin on FS
  // starts; a warm run restores those poses from the cache and must derive
  // the twin again from the rows, in the floorplan and the netlist.
  Two_row_chain chain(kDominoTwoRow, 40);
  auto&         options = chain.options;
  options.row_access    = physical::Row_access_convention{};
  for (const auto& [master, twin] : {
           std::pair{std::string(kDominoTwoRow),                kDominoTwin},
           {               kDominoTwin, std::string(kDominoTwoRow)}
  }) {
    options.multirow_templates[master] = {
        {physical::Row_access::horizontal, physical::Row_access::vertical},
        twin
    };
  }
  options.utilization  = .4;
  options.output_dir  += "_restored";
  options.cache_dir    = std::string(std::getenv("TEST_TMPDIR")) + "/two_row_rail_cache";
  std::filesystem::create_directories(options.cache_dir);
  const auto all              = physical::read_lef(options.lefs);
  const auto expect_templates = [&](const Result& r) {
    EXPECT_NO_THROW(check_legality(all, r.floorplan));
    std::map<std::string, std::string> instances;
    for (auto node : r.graph->body().nodes()) {
      if (const auto io = node.get_subnode_io()) {
        instances[std::string(node.get_name())] = io->get_name();
      }
    }
    size_t flipped = 0;
    for (const auto& c : r.floorplan.components) {
      if (c.macro == kDominoTwoRow || c.macro == kDominoTwin) {
        const bool fs  = c.place.orientation == physical::Orientation::FS;
        flipped       += fs;
        EXPECT_EQ(c.macro, fs ? kDominoTwin : kDominoTwoRow) << c.name;
        EXPECT_EQ(instances.at(c.name), c.macro) << c.name;
      }
    }
    return flipped;
  };
  const auto cold = place(*chain.graph, chain.flat, options);
  ASSERT_GT(expect_templates(cold), 0U);
  const auto          warm = place(*chain.graph, chain.flat, options);
  rapidjson::Document qor;
  qor.Parse(warm.qor.c_str());
  EXPECT_TRUE(qor["incremental"]["hit"].GetBool());
  EXPECT_EQ(expect_templates(warm), expect_templates(cold));
  EXPECT_EQ(warm.floorplan.components, cold.floorplan.components);
}

TEST(Placement, TwoRowEcoSwapsVariantsAroundTheirNeighbors) {
  // c1 swaps to a wider variant of its two-row master (pins moved) around a
  // reference frame with a single-row cell just past its padding; then back.
  const std::string wide = std::string(kDominoTwoRow) + "_wide", huge = std::string(kDominoTwoRow) + "_huge";
  Two_row_chain     chain(kDominoTwoRow);
  const auto        dir   = std::filesystem::path(chain.options.output_dir).parent_path().string();
  const auto        base  = physical::read_lef(chain.options.lefs);
  const auto        write = [&](const std::string& name, unsigned sites, double pin_shift_um) {
    auto macro  = base.macros.at(kDominoTwoRow);
    macro.width = sites * base.sites.at("unithd").width;
    for (auto& [pin_name, pin] : macro.pins) {
      for (auto& shape : pin.shapes) {
        shape.rect.xl += pin_shift_um;
        shape.rect.xh += pin_shift_um;
      }
    }
    std::ofstream(dir + "/" + name + ".lef") << physical::write_lef_macros(
        {
            {name, macro}
    },
        base.dbu);
  };
  write(wide, 6, base.sites.at("unithd").width);  // twice as wide, pins one site right
  const auto setup = [&](Options& o, const std::string& out) {
    o.lefs.push_back(dir + "/" + wide + ".lef");
    o.row_access = physical::Row_access_convention{};
    for (const auto& master : {std::string(kDominoTwoRow), wide, huge}) {
      o.multirow_templates[master] = {
          {physical::Row_access::horizontal, physical::Row_access::vertical}
      };
    }
    o.output_dir = dir + "/" + out;
  };
  setup(chain.options, "cold");
  const auto cold = place(*chain.graph, chain.flat, chain.options);
  const auto all  = physical::read_lef(chain.options.lefs);
  const auto sw = std::llround(all.sites.at("unithd").width * all.dbu), rh = std::llround(all.sites.at("unithd").height * all.dbu);

  // The reference: the two-row cells at sites 2, 14 and 26 of rows 0-1; c0
  // right past c1's padding site; the other cells on rows 2 and up.
  auto reference = cold.floorplan;
  std::sort(reference.rows.begin(), reference.rows.end(), [](const auto& a, const auto& b) { return a.y < b.y; });
  ASSERT_GE(reference.rows.size(), 4U);
  ASSERT_GE(reference.rows.front().count, 32);
  ASSERT_EQ(chain.options.terminal_legalize_options.padding_sites, 1U);
  unsigned next = 0;
  for (auto& c : reference.components) {
    const auto index = std::stoi(c.name.substr(1));
    size_t     row   = 0;
    int64_t    site  = 0;
    if (c.macro == kDominoTwoRow) {
      site = 2 + 12 * (index / 4);
    } else if (index == 0) {
      site = 6;
    } else {
      row  = 2 + next / 4;
      site = 2 + 6 * (next++ % 4);
    }
    ASSERT_LT(row, reference.rows.size());
    c.place.x           = reference.rows[row].x + site * sw;
    c.place.y           = reference.rows[row].y;
    c.place.orientation = reference.rows[row].orientation;
    c.place.status      = physical::Status::PLACED;
  }
  EXPECT_NO_THROW(check_legality(all, reference));
  const auto at = [](const physical::Floorplan& fp, const std::string& name) {
    const auto it = std::find_if(fp.components.begin(), fp.components.end(), [&](const auto& c) { return c.name == name; });
    if (it == fp.components.end()) {
      throw std::out_of_range("no component " + name);
    }
    return *it;
  };

  Two_row_chain grown(kDominoTwoRow,
                      12,
                      {
                          {1, wide}
  });
  auto&         eco = grown.options;
  setup(eco, "grown");
  eco.previous      = std::make_shared<const physical::Floorplan>(reference);
  eco.changed_cells = {"c1"};
  // Without the internal flag the single-row ECO refusal is unchanged.
  try {
    (void)place(*grown.graph, grown.flat, eco);
    ADD_FAILURE() << "a two-row ECO ran without multirow_eco";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("ECO legalization is single-row"), std::string::npos) << error.what();
  }
  eco.multirow_eco = Multirow_eco_bounds{.shift_sites = 0, .max_displaced = 16};
  EXPECT_THROW((void)place(*grown.graph, grown.flat, eco), std::invalid_argument);  // needs legalize_cells
  eco.legalize_cells = true;
  const auto result  = place(*grown.graph, grown.flat, eco);
  EXPECT_NO_THROW(check_legality(all, result.floorplan));
  const auto c1 = at(result.floorplan, "c1"), c0 = at(result.floorplan, "c0");
  EXPECT_EQ(c1.macro, wide);
  EXPECT_EQ(c1.place.width, 6 * sw);
  EXPECT_EQ(c1.place.height, 2 * rh);
  EXPECT_EQ(c1.place.x, at(reference, "c1").place.x);  // grown in place
  EXPECT_EQ(c1.place.y, at(reference, "c1").place.y);
  EXPECT_EQ(c1.place.orientation, reference.rows.front().orientation);
  EXPECT_GE(c0.place.x, c1.place.x + c1.place.width + sw);  // displaced past the padding
  EXPECT_EQ(c0.place.y, c1.place.y);
  for (const auto& c : result.floorplan.components) {
    EXPECT_EQ(c.place.status, physical::Status::PLACED) << c.name;
    if (c.name != "c0" && c.name != "c1") {
      EXPECT_EQ(c.place, at(reference, c.name).place) << c.name;
    }
  }
  rapidjson::Document qor;
  qor.Parse(result.qor.c_str());
  EXPECT_NEAR(lef_hpwl_um(all, result.floorplan), qor["hpwl_um"].GetDouble(), 1e-6);  // pins from the wide LEF
  EXPECT_TRUE(qor["eco"]["used"].GetBool());
  ASSERT_TRUE(qor.HasMember("multirow_eco"));
  const auto& block = qor["multirow_eco"];
  EXPECT_EQ(block["cells"].GetUint64(), 1U);
  EXPECT_EQ(block["displaced"].GetUint64(), 1U);
  EXPECT_EQ(block["grown_sites"].GetInt64(), 6);
  EXPECT_EQ(block["displaced_cells"].GetUint64(), 1U);
  EXPECT_EQ(block["shift_sites"].GetUint(), 0U);

  // Back to the narrow variant: a down-size keeps the pose and releases the sites.
  Two_row_chain narrow(kDominoTwoRow);
  setup(narrow.options, "narrow");
  narrow.options.previous       = std::make_shared<const physical::Floorplan>(result.floorplan);
  narrow.options.changed_cells  = {"c1"};
  narrow.options.legalize_cells = true;
  narrow.options.multirow_eco   = Multirow_eco_bounds{};
  const auto back               = place(*narrow.graph, narrow.flat, narrow.options);
  EXPECT_NO_THROW(check_legality(all, back.floorplan));
  EXPECT_EQ(at(back.floorplan, "c1").place, [&] {
    auto p  = c1.place;
    p.width = 3 * sw;
    return p;
  }());
  rapidjson::Document back_qor;
  back_qor.Parse(back.qor.c_str());
  EXPECT_EQ(back_qor["multirow_eco"]["in_place"].GetUint64(), 1U);
  EXPECT_EQ(back_qor["multirow_eco"]["released_sites"].GetInt64(), 6);
  EXPECT_EQ(back_qor["multirow_eco"]["displaced_cells"].GetUint64(), 0U);
  EXPECT_NEAR(lef_hpwl_um(all, back.floorplan), back_qor["hpwl_um"].GetDouble(), 1e-6);

  // A growth that fits nowhere (a full row plus its padding) is rejected,
  // never re-placed by local_place.
  write(huge, unsigned(reference.rows.front().count), 0);
  Two_row_chain rejected(kDominoTwoRow,
                         12,
                         {
                             {1, huge}
  });
  setup(rejected.options, "rejected");
  rejected.options.lefs.push_back(dir + "/" + huge + ".lef");
  rejected.options.previous       = eco.previous;
  rejected.options.changed_cells  = {"c1"};
  rejected.options.legalize_cells = true;
  rejected.options.multirow_eco   = Multirow_eco_bounds{};
  try {
    (void)place(*rejected.graph, rejected.flat, rejected.options);
    ADD_FAILURE() << "an impossible two-row growth was placed";
  } catch (const Multirow_failure& error) {
    EXPECT_NE(std::string(error.what()).find("two-row ECO cannot place c1"), std::string::npos) << error.what();
  }

  // The ECO checks the hard bin cap on the terminal grid the reference was
  // legalized on: with automatic bins, the GP's resolved grid, never the
  // requested one (2 here).
  Two_row_chain automatic(kDominoTwoRow);
  setup(automatic.options, "automatic");
  automatic.options.automatic_bins   = true;
  automatic.options.bins             = 2;
  const auto          automatic_cold = place(*automatic.graph, automatic.flat, automatic.options);
  std::ifstream       grid_in(automatic.options.output_dir + "/terminal_legalize.json");
  rapidjson::Document grid;
  grid.Parse(std::string{std::istreambuf_iterator<char>(grid_in), {}}.c_str());
  ASSERT_TRUE(grid.IsObject() && grid.HasMember("bins_y"));
  EXPECT_GT(grid["bins_y"].GetUint64(), 2U);
  Two_row_chain again(kDominoTwoRow);
  setup(again.options, "automatic_eco");
  again.options.automatic_bins     = true;
  again.options.bins               = 2;
  again.options.previous           = std::make_shared<const physical::Floorplan>(automatic_cold.floorplan);
  again.options.changed_cells      = {"c1"};
  again.options.legalize_cells     = true;
  again.options.multirow_eco       = Multirow_eco_bounds{};
  const auto          again_result = place(*again.graph, again.flat, again.options);
  rapidjson::Document again_qor;
  again_qor.Parse(again_result.qor.c_str());
  ASSERT_TRUE(again_qor.HasMember("multirow_eco"));
  EXPECT_EQ(again_qor["multirow_eco"]["bins_x"].GetUint64(), grid["bins_x"].GetUint64());
  EXPECT_EQ(again_qor["multirow_eco"]["bins_y"].GetUint64(), grid["bins_y"].GetUint64());
}

TEST(Placement, LibraryTwoRowCellsKeepTheSingleRowPaths) {
  // Only the reserved domino masters opt into the anchor prepass: a library
  // two-row cell still reaches the single-row terminal legalizer, which
  // refuses it as before, and draws no two-row refusal.
  Two_row_chain chain("lib_two_row");
  try {
    (void)place(*chain.graph, chain.flat, chain.options);
    ADD_FAILURE() << "a library two-row cell was placed";
  } catch (const std::exception& error) {
    const std::string what = error.what();
    EXPECT_NE(what.find("terminal legalization"), std::string::npos) << what;
    EXPECT_EQ(what.find("two-row"), std::string::npos) << what;
  }
}

TEST(Placement, ResynthesisRestartSeedsCoordinatesWithoutLockingCellsOrMacros) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "restart_seed";
  std::filesystem::create_directories(root);
  std::ifstream lef_input("inou/lefdef/tests/data/sky130hd.lef");
  std::string   lef{std::istreambuf_iterator<char>(lef_input), {}};
  // Use one tiny BLOCK master and two CORE cells so both movable paths run.
  lef.replace(lef.find("CLASS CORE"), std::string("CLASS CORE").size(), "CLASS BLOCK");
  const auto lef_path = (root / "macro.lef").string();
  std::ofstream(lef_path) << lef;
  hhds::GraphLibrary source;
  auto               io = source.create_io("restart_seed");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (int i = 0; i < 3; ++i) {
    const auto master  = i ? "sky130_fd_sc_hd__buf_1" : "sky130_fd_sc_hd__inv_1";
    auto       cell_io = source.find_io(master);
    if (!cell_io) {
      cell_io = source.create_io(master);
      cell_io->add_input("A", 0);
      cell_io->add_output(i ? "X" : "Y", 0);
    }
    auto cell = gu::create_typed_node(*graph, Ntype_op::Sub);
    cell.set_subnode(cell_io);
    cell.set_name("cell" + std::to_string(i));
    cell.create_sink_pin("A").connect_driver(signal);
    signal = cell.create_driver_pin(i ? "X" : "Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", lef_path};
  options.output_dir = (root / "cold").string();
  options.core       = {0, 0, 100, 100};
  options.iterations = 0;
  options.full       = false;
  hhds::GraphLibrary cold_library;
  auto               frame = place(*graph, cold_library, options).floorplan;
  ASSERT_EQ(frame.components.size(), 3);
  for (size_t i = 0; i < frame.components.size(); ++i) {
    auto& cell        = frame.components[i];
    cell.place.x      = 10000 + i * 20000;
    cell.place.y      = 15000 + i * 10000;
    // A previous frame may have locked macros; its status is not inherited.
    cell.place.status = Ann_place::Status::FIXED;
  }
  const auto first = frame.components[0], second = frame.components[1];
  frame.components.pop_back();  // Simulates a newly created mapped cell.
  options.restart_seed = std::make_shared<physical::Floorplan>(frame);
  options.output_dir   = (root / "restart").string();
  hhds::GraphLibrary restart_library;
  const auto         seeded = place(*graph, restart_library, options);
  for (const auto& old : {first, second}) {
    const auto now = std::find_if(seeded.floorplan.components.begin(), seeded.floorplan.components.end(), [&](const auto& cell) {
      return cell.name == old.name;
    });
    ASSERT_NE(now, seeded.floorplan.components.end());
    EXPECT_EQ(now->place.x, old.place.x);
    EXPECT_EQ(now->place.y, old.place.y);
    EXPECT_NE(now->place.status, Ann_place::Status::FIXED);
    EXPECT_NE(now->place.status, Ann_place::Status::LOCKED);
  }
  std::ifstream       report_file(options.output_dir + "/restart_seed.json");
  const std::string   text{std::istreambuf_iterator<char>(report_file), {}};
  rapidjson::Document report;
  report.Parse(text.c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_TRUE(report["coordinates_only"].GetBool());
  EXPECT_EQ(report["reused_movable_cells"].GetUint64(), 2);
  EXPECT_EQ(report["reused_movable_macros"].GetUint64(), 1);
  EXPECT_EQ(report["new_movable_cells"].GetUint64(), 1);
  options.iterations = 20;
  options.output_dir = (root / "continue").string();
  hhds::GraphLibrary moving_library;
  const auto         moved = place(*graph, moving_library, options);
  for (const auto& old : {first, second}) {
    const auto now = std::find_if(moved.floorplan.components.begin(), moved.floorplan.components.end(), [&](const auto& cell) {
      return cell.name == old.name;
    });
    ASSERT_NE(now, moved.floorplan.components.end());
    EXPECT_TRUE(now->place.x != old.place.x || now->place.y != old.place.y) << old.name;
  }

  // Resynthesis can change the automatically sized die. Old edge coordinates
  // must not become interior IO seeds in the new frame.
  auto resized_seed = std::make_shared<physical::Floorplan>(*options.restart_seed);
  for (auto& terminal : resized_seed->terminals) {
    terminal.x = resized_seed->die.xh;
    terminal.y = (resized_seed->die.yl + resized_seed->die.yh) / 2;
  }
  options.restart_seed = resized_seed;
  options.iterations   = 0;
  options.core         = {0, 0, 200, 200};
  options.output_dir   = (root / "resized").string();
  hhds::GraphLibrary resized_library;
  const auto         resized = place(*graph, resized_library, options);
  ASSERT_NE(resized.floorplan.die, frame.die);
  ASSERT_FALSE(resized.floorplan.terminals.empty());
  for (const auto& terminal : resized.floorplan.terminals) {
    const auto& die = resized.floorplan.die;
    EXPECT_TRUE(terminal.x == die.xl || terminal.x == die.xh || terminal.y == die.yl || terminal.y == die.yh)
        << terminal.name << " at " << terminal.x << "," << terminal.y;
  }

  // A macro solve supersedes the seed's cells but keeps its IOs (taken before
  // planning): the report counts them.
  options.restart_seed = std::make_shared<physical::Floorplan>(frame);
  options.core         = {0, 0, 100, 100};
  options.macro_mode   = "h2d";
  options.full         = true;
  options.output_dir   = (root / "macro_solved").string();
  hhds::GraphLibrary solved_library;
  place(*graph, solved_library, options);
  std::ifstream       solved_file(options.output_dir + "/restart_seed.json");
  const std::string   solved_text{std::istreambuf_iterator<char>(solved_file), {}};
  rapidjson::Document solved;
  solved.Parse(solved_text.c_str());
  ASSERT_FALSE(solved.HasParseError()) << solved_text;
  EXPECT_TRUE(solved["superseded_by_macro_solve"].GetBool()) << solved_text;
  EXPECT_EQ(solved["reused_movable_cells"].GetUint64(), 0U);
  EXPECT_EQ(solved["reused_ios"].GetUint64(), frame.terminals.size()) << solved_text;
  EXPECT_GT(frame.terminals.size(), 0U);
}
TEST(Placement, MacroCellWorkCanBeDeferredWithoutSkippingGpTimingOrFinalLegalization) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "macro_detail";
  std::filesystem::create_directories(root);
  std::ifstream lef_input("inou/lefdef/tests/data/sky130hd.lef");
  std::string   lef{std::istreambuf_iterator<char>(lef_input), {}};
  lef.replace(lef.find("CLASS CORE"), std::string("CLASS CORE").size(), "CLASS BLOCK");
  const auto lef_path = (root / "macro.lef").string();
  std::ofstream(lef_path) << lef;
  hhds::GraphLibrary source;
  auto               io = source.create_io("macro_detail");
  io->add_input("a", 0);
  io->add_output("y", 0);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (int i = 0; i < 3; ++i) {
    const auto master  = i ? "sky130_fd_sc_hd__buf_1" : "sky130_fd_sc_hd__inv_1";
    auto       cell_io = source.find_io(master);
    if (!cell_io) {
      cell_io = source.create_io(master);
      cell_io->add_input("A", 0);
      cell_io->add_output(i ? "X" : "Y", 0);
    }
    auto cell = gu::create_typed_node(*graph, Ntype_op::Sub);
    cell.set_subnode(cell_io);
    cell.set_name("cell" + std::to_string(i));
    cell.attr(attrs::synth_color_group).set("macro_detail_group");
    cell.create_sink_pin("A").connect_driver(signal);
    signal = cell.create_driver_pin(i ? "X" : "Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  for (const auto& [detail, independent, legalize, terminal, fp] : {
           std::tuple{ true, false,  true, false, false},
           std::tuple{false, false,  true, false, false},
           std::tuple{false,  true,  true, false, false},
           std::tuple{false,  true, false, false, false},
           std::tuple{false,  true, false,  true, false},
           std::tuple{false,  true, false,  true,  true}
  }) {
    auto options                  = plain_options();
    options.lefs                  = {"inou/lefdef/tests/data/sky130hd.tlef", lef_path};
    options.output_dir            = (root
                                     / (fp            ? "fp_macro_only_terminal"
                                        : terminal    ? "macro_only_terminal"
                                        : !legalize   ? "macro_only"
                                        : detail      ? "enabled"
                                        : independent ? "independent"
                                                      : "disabled"))
                                        .string();
    options.core                  = {0, 0, 30, 30};
    options.density               = .5;
    options.macro_filler_fraction = .125;
    options.macro_fill_cell_space = true;
    options.iterations            = 20;
    const bool warp_preview       = !legalize && !terminal;
    if (warp_preview) {
      options.iterations = options.min_iterations = 100;
      options.macro_warp.enabled                  = true;
      options.macro_warp.weight                   = 1;
      options.macro_warp.start_stability          = 0;
      options.macro_warp.refresh_interval         = 20;
      options.macro_warp.bins                     = 4;
      options.macro_warp.iterations               = 1;
      options.macro_warp.seconds                  = 1;
      options.macro_warp.warmup                   = .6;
      options.macro_warp.updates                  = 2;
    }
    options.bins     = 2;
    options.fp_place = options.fp_colors = fp;
    options.fp_color_minimum             = 0;
    options.fp_iterations                = 20;
    options.fp_timing_interval           = 10;
    options.macro_cell_density_weight    = fp ? 2 : 0;
    options.local_density_bins           = 8;
    options.snapshots                    = true;
    options.final_cells                  = true;
    options.macro_detailed_place         = detail;
    options.macro_legalize_cells         = legalize;
    options.optimization_pending         = !terminal;
    options.final_independent_timing     = independent;
    options.macro_timing_interval        = 10;
    options.net_weights["cell0:Y"]       = 2;
    options.final_timing_start           = 0;  // Short fixture: exercise in-loop timing.
    bool     macro_complete = false, checked_final_weights = false;
    double   macro_weight       = 0;
    unsigned macro_timing_calls = 0;
    unsigned source_releases    = 0;
    options.fp_evaluate         = [&](const Result& checkpoint) {
      if (!macro_complete) {
        ++macro_timing_calls;
      }
      if (macro_complete && !checked_final_weights) {
        EXPECT_DOUBLE_EQ(checkpoint.net_weights.at("cell0:Y"), independent ? 2 : macro_weight);
        checked_final_weights = true;
      }
      Timing_feedback timing;
      timing.pins["cell1:A"].slack = -10;
      return timing;
    };
    physical::Floorplan macro_frame;
    options.after_stage = [&](Placement_stage stage, const Result& result) {
      EXPECT_EQ(source_releases, stage == Placement_stage::final ? 1U : 0U);
      if (stage == Placement_stage::final || (stage == Placement_stage::macro && legalize)) {
        EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), result.floorplan));
      }
      if (stage == Placement_stage::macro) {
        EXPECT_GE(macro_timing_calls, 2);  // In-loop timing remains enabled.
        EXPECT_EQ(bool(result.macro_gp_timing), !legalize);
        rapidjson::Document q;
        q.Parse(result.qor.c_str());
        EXPECT_EQ(q["cells_legalized"].GetBool(), legalize);
        EXPECT_DOUBLE_EQ(q["macro_filler_fraction"].GetDouble(), .125);
        EXPECT_TRUE(q["macro_fill_cell_space"].GetBool());
        if (warp_preview) {
          std::ifstream trace(options.output_dir + "/macro/macro_warp_stability.jsonl");
          std::string   last, line;
          while (std::getline(trace, line)) {
            last = line;
          }
          rapidjson::Document decision;
          decision.Parse(last.c_str());
          EXPECT_FALSE(decision.HasParseError());
          const bool has_fraction = decision.IsObject() && decision.HasMember("macro_stability_fraction");
          EXPECT_TRUE(has_fraction);
          // A replay recorder must not deliver a second same-step callback,
          // which would rebase the controller's window at every GP iteration.
          if (has_fraction) {
            EXPECT_TRUE(decision["macro_stability_fraction"].IsNumber());
            EXPECT_EQ(decision["iteration"].GetInt(), 80);
          }
        }
        if (!legalize && result.macro_gp_timing) {
          EXPECT_NE(result.macro_gp_timing->parasitics, nullptr);
        }
        macro_frame  = result.floorplan;
        macro_weight = result.net_weights.at("cell0:Y");
        EXPECT_GT(macro_weight, 2);
        macro_complete = true;
      }
      return false;
    };
    hhds::GraphLibrary  placed;
    const auto          result = place(*graph, placed, options, [&] { ++source_releases; });
    EXPECT_EQ(source_releases, 1U);
    rapidjson::Document final_qor;
    final_qor.Parse(result.qor.c_str());
    EXPECT_DOUBLE_EQ(final_qor["macro_filler_fraction"].GetDouble(), 0);
    EXPECT_FALSE(final_qor["macro_fill_cell_space"].GetBool());
    EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), result.floorplan));
    ASSERT_FALSE(macro_frame.components.empty());
    EXPECT_TRUE(checked_final_weights);
    EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/macro/after_legalization.def"));
    EXPECT_EQ(std::filesystem::exists(options.output_dir + "/macro/after_detailed.def"), detail);
    EXPECT_EQ(std::filesystem::exists(options.output_dir + "/after_detailed.def"), !terminal);
    EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/after_legalization.def"));
    std::ifstream seed_file(options.output_dir + "/restart_seed.json");
    ASSERT_TRUE(seed_file);
    const std::string   seed_text{std::istreambuf_iterator<char>(seed_file), {}};
    rapidjson::Document seed;
    seed.Parse(seed_text.c_str());
    ASSERT_FALSE(seed.HasParseError());
    EXPECT_TRUE(seed["coordinates_only"].GetBool());
    EXPECT_EQ(seed["reused_movable_cells"].GetUint64(), 2U);
    EXPECT_EQ(seed["reused_movable_macros"].GetUint64(), 0U);
    EXPECT_EQ(seed["new_movable_cells"].GetUint64(), 0U);
    if (fp) {
      const auto read_report = [&](const std::string& name) {
        std::ifstream       file(options.output_dir + "/" + name);
        const std::string   text{std::istreambuf_iterator<char>(file), {}};
        rapidjson::Document report;
        report.Parse(text.c_str());
        return report;
      };
      auto fp_report = read_report("macro/fp_place.json");
      ASSERT_FALSE(fp_report.HasParseError());
      EXPECT_TRUE(fp_report["fixed_targets_enabled"].GetBool());
      auto macro_gp = read_report("macro/gp_history.json");
      auto final_gp = read_report("gp_history.json");
      ASSERT_FALSE(macro_gp.HasParseError());
      ASSERT_FALSE(final_gp.HasParseError());
      EXPECT_DOUBLE_EQ(macro_gp["macro_cell_density_weight"].GetDouble(), 2);
      EXPECT_DOUBLE_EQ(final_gp["macro_cell_density_weight"].GetDouble(), 0);
    }
    const auto handoff = physical::read_def(options.output_dir + "/macro/fixed.def");
    EXPECT_EQ(handoff.components.size(), detail ? 3 : 1);
    EXPECT_EQ(result.floorplan.components.size(), 3);
    if (!detail) {
      EXPECT_TRUE(handoff.nets.empty());
    }
    const auto before = std::find_if(macro_frame.components.begin(), macro_frame.components.end(), [](const auto& c) {
      return c.name == "cell0";
    });
    const auto after  = std::find_if(result.floorplan.components.begin(), result.floorplan.components.end(), [](const auto& c) {
      return c.name == "cell0";
    });
    ASSERT_NE(before, macro_frame.components.end());
    ASSERT_NE(after, result.floorplan.components.end());
    EXPECT_EQ(before->place.x, after->place.x);
    EXPECT_EQ(before->place.y, after->place.y);
    EXPECT_EQ(before->place.orientation, after->place.orientation);
  }
}
TEST(Placement, GlobalMagnetStageExpandsMembersBeforeTheFixedMacroHandoff) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "global_magnet_stage";
  std::filesystem::create_directories(root);
  const auto    lef = root / "macros.lef";
  std::ofstream output(lef);
  output << "VERSION 5.8 ;\n";
  for (const auto* name : {"red_ram", "blue_ram"}) {
    output << "MACRO " << name << R"(
 CLASS BLOCK ; ORIGIN 0 0 ; SIZE 1.38 BY 2.72 ; SYMMETRY X Y ;
 PIN A
 DIRECTION INPUT ; PORT LAYER li1 ; RECT 0.1 0.1 1.2 2.6 ; END
 END A
 PIN Y
 DIRECTION OUTPUT ; PORT LAYER li1 ; RECT 0.1 0.1 1.2 2.6 ; END
 END Y
END )" << name
           << '\n';
  }
  output << "END LIBRARY\n";
  output.close();
  hhds::GraphLibrary source;
  auto               io = source.create_io("global_magnet_stage");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (const auto* name : {"red_ram", "blue_ram"}) {
    auto master = source.create_io(name);
    master->add_input("A", 0);
    master->add_output("Y", 0);
    auto cell = gu::create_typed_node(*graph, Ntype_op::Sub);
    cell.set_subnode(master);
    cell.set_name(name);
    cell.attr(attrs::synth_color_group).set(name);
    cell.create_sink_pin("A").connect_driver(signal);
    signal = cell.create_driver_pin("Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options                 = plain_options();
  options.lefs                 = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", lef.string()};
  options.output_dir           = (root / "out").string();
  options.core                 = {0, 0, 50, 50};
  options.iterations           = 100;
  options.final_iterations     = 20;
  options.bins                 = 8;
  options.final_cells          = true;
  options.macro_magnet.enabled = true;
  options.macro_magnet.settle_displacement = 1;
  options.macro_magnet.max_gap             = 100;  // Admit the tiny fixture's initial IO-driven separation.
  options.macro_legalize_cells = options.macro_detailed_place = false;
  options.final_independent_timing                            = true;
  options.density_upper_bound                                 = true;
  options.halo_x                                              = .46;
  options.halo_y                                              = 2.72;
  hhds::GraphLibrary placed;
  const auto         result = place(*graph, placed, options);
  EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), result.floorplan, options));
  ASSERT_EQ(result.floorplan.components.size(), 2);
  for (const auto& component : result.floorplan.components) {
    EXPECT_EQ(component.place.status, physical::Status::FIXED);
  }
  const auto handoff = physical::read_def(options.output_dir + "/macro/fixed.def");
  ASSERT_EQ(handoff.components.size(), result.floorplan.components.size());
  for (size_t i = 0; i < handoff.components.size(); ++i) {
    EXPECT_EQ(handoff.components[i].name, result.floorplan.components[i].name);
    EXPECT_EQ(handoff.components[i].macro, result.floorplan.components[i].macro);
    EXPECT_EQ(handoff.components[i].place.x, result.floorplan.components[i].place.x);
    EXPECT_EQ(handoff.components[i].place.y, result.floorplan.components[i].place.y);
    EXPECT_EQ(handoff.components[i].place.orientation, result.floorplan.components[i].place.orientation);
    EXPECT_EQ(handoff.components[i].place.status, result.floorplan.components[i].place.status);
  }
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/magnet.json"));
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/macro_animation.html"));
  rapidjson::Document qor;
  qor.Parse(result.qor.c_str());
  ASSERT_TRUE(qor.HasMember("macro_magnet"));
  EXPECT_EQ(qor["macro_magnet"]["groups"].GetUint64(), 1);
  EXPECT_EQ(qor["macro_magnet"]["members"].GetUint64(), 2);
  options.final_cells        = false;
  options.output_dir         = (root / "macro_only").string();
  options.snapshots          = false;
  options.macro_warp.enabled = true;
  int completed              = 0;
  options.after_stage        = [&](Placement_stage stage, const Result&) {
    EXPECT_EQ(stage, Placement_stage::macro);
    ++completed;
    return false;
  };
  hhds::GraphLibrary macro_only_library;
  const auto         macro_only = place(*graph, macro_only_library, options);
  EXPECT_EQ(completed, 1);
  EXPECT_FALSE(std::filesystem::exists(options.output_dir + "/macro"));
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/macro_animation.html"));
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/warp_nodes.tsv"));
  rapidjson::Document macro_report;
  macro_report.Parse(macro_only.qor.c_str());
  ASSERT_TRUE(macro_report.HasMember("macro_magnet"));
  EXPECT_EQ(macro_report["macro_magnet"]["members"].GetUint64(), 2);
  options.macro_magnet.enabled = false;
  options.optimization_pending = false;  // final-stage default must not enter a macro-only preview
  options.snapshots            = true;
  options.output_dir           = (root / "macro_only_no_magnet").string();
  std::filesystem::create_directories(options.output_dir);
  std::ofstream(options.output_dir + "/macro_warp_stability.jsonl") << "stale trace from an earlier solve\n";
  completed = 0;
  hhds::GraphLibrary ordinary_macro_library;
  const auto         ordinary_macro = place(*graph, ordinary_macro_library, options);
  EXPECT_EQ(completed, 1);
  rapidjson::Document ordinary_qor;
  ordinary_qor.Parse(ordinary_macro.qor.c_str());
  ASSERT_FALSE(ordinary_qor.HasParseError());
  EXPECT_FALSE(ordinary_qor["cells_legalized"].GetBool());
  EXPECT_FALSE(ordinary_qor.HasMember("macro_magnet"));
  std::ifstream       replay_file(options.output_dir + "/magnet.json");
  const std::string   replay_text{std::istreambuf_iterator<char>(replay_file), {}};
  rapidjson::Document replay;
  replay.Parse(replay_text.c_str());
  ASSERT_FALSE(replay.HasParseError());
  EXPECT_FALSE(replay["enabled"].GetBool());
  EXPECT_TRUE(replay["groups"].Empty());
  ASSERT_FALSE(replay["motion_history"].Empty());
  EXPECT_EQ(replay["motion_history"][0]["physical_macro_count"].GetInt(), 2);
  EXPECT_FALSE(replay["motion_history"][0]["center_step_valid"].GetBool());
  EXPECT_EQ(replay["macro_stability"]["reference_iterations"].GetInt(), 50);
  bool saw_valid_motion = false;
  for (const auto& sample : replay["motion_history"].GetArray()) {
    saw_valid_motion |= sample["center_step_valid"].GetBool();
  }
  EXPECT_TRUE(saw_valid_motion);
  EXPECT_GT(replay["frames"].Size(), 1);
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/macro_animation.html"));
}
TEST(Placement, RepulsionControlsRejectUnsupportedH2dCombination) {
  hhds::GraphLibrary source, scratch;
  auto               io      = source.create_io("repulsion_validation");
  auto               graph   = io->create_graph();
  auto               options = plain_options();
  options.macro_mode         = "h2d";
  options.automatic_bins     = true;
  EXPECT_THROW((void)place(*graph, scratch, options), std::invalid_argument);
}
TEST(Placement, TimingMaxWeightAboveNetWeightLimitIsRejectedUpFront) {
  hhds::GraphLibrary source, scratch;
  auto               io      = source.create_io("timing_weight_validation");
  auto               graph   = io->create_graph();
  auto               options = plain_options();
  options.timing_max_weight  = max_net_weight + 1;
  EXPECT_THROW((void)place(*graph, scratch, options), std::invalid_argument);
}
TEST(Placement, PartitionGridsAreValidatedUpFront) {
  hhds::GraphLibrary source, scratch;
  auto               io    = source.create_io("partition_validation");
  auto               graph = io->create_graph();
  for (const auto& edit : std::vector<std::function<void(Options&)>>{
           [](Options& o) { o.region.max_cells = 0; },  // no region consumer enabled
           [](Options& o) { o.region.merge_um = -1; },
           [](Options& o) { o.route_bins = 1; },
           [](Options& o) { o.legalize_bins = 1025; },
       }) {
    auto options = plain_options();
    edit(options);
    EXPECT_THROW((void)place(*graph, scratch, options), std::invalid_argument);
  }
}
TEST(Placement, TechnologyClockPinsAreExcludedOnlyFromPlacementCost) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "clock_cost";
  std::filesystem::create_directories(root);
  std::ifstream input("inou/lefdef/tests/data/sky130hd.lef");
  std::string   lef{std::istreambuf_iterator<char>(input), {}};
  const auto    pin = lef.find("PIN A");
  ASSERT_NE(pin, std::string::npos);
  const auto use = lef.find("USE SIGNAL", pin);
  ASSERT_NE(use, std::string::npos);
  lef.replace(use, std::string("USE SIGNAL").size(), "USE CLOCK");
  const auto path = (root / "clock.lef").string();
  std::ofstream(path) << lef;
  const auto tech = physical::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", path});
  ASSERT_EQ(tech.macros.at("sky130_fd_sc_hd__inv_1").pins.at("A").use, "CLOCK");
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("clock_cost");
  io->add_input("clk", 0);
  io->set_bits("clk", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
  node.set_name("cell");
  node.set_subnode(inv);
  gu::set_bits(graph->get_input_pin("clk"), 1);
  node.create_sink_pin("A").connect_driver(graph->get_input_pin("clk"));
  auto y = node.create_driver_pin("Y");
  gu::set_bits(y, 1);
  graph->get_output_pin("y").connect_driver(y);
  physical::Floorplan fp;
  fp.design = "clock_cost";
  fp.dbu    = tech.dbu;
  fp.die    = {0, 0, 4600, 2720};
  fp.rows.push_back({"row", tech.sites.begin()->first, 0, 0, 10, 460, Ann_place::Orientation::N});
  Ann_place cell(1380, 0, 1380, 2720);
  cell.status = Ann_place::Status::FIXED;
  fp.components.push_back({"cell", "sky130_fd_sc_hd__inv_1", cell});
  fp.terminals.push_back({"clk", "clk", "INPUT", 0, 1360, Ann_place::Orientation::N, Ann_place::Status::FIXED, {}});
  fp.terminals.push_back({"y", "y", "OUTPUT", 4600, 1360, Ann_place::Orientation::N, Ann_place::Status::FIXED, {}});
  physical::write_def(fp, (root / "input.def").string());
  auto options        = plain_options();
  options.lefs        = {"inou/lefdef/tests/data/sky130hd.tlef", path};
  options.floorplan   = (root / "input.def").string();
  options.output_dir  = (root / "placed").string();
  options.iterations  = 0;
  const auto   result = place(*graph, flat, options);
  const auto&  rect   = tech.macros.at("sky130_fd_sc_hd__inv_1").pins.at("Y").shapes.front().rect;
  const double expected
      = (std::abs(4600 - (1380 + (rect.xl + rect.xh) * .5 * tech.dbu)) + std::abs(1360 - (rect.yl + rect.yh) * .5 * tech.dbu))
        / tech.dbu;
  rapidjson::Document qor;
  qor.Parse(result.qor.c_str());
  EXPECT_NEAR(qor["hpwl_um"].GetDouble(), expected, 1e-6);
  bool preserved = false;
  for (const auto& net : result.parasitics->nets) {
    if (net.driver == "clk") {
      for (const auto& point : net.nodes) {
        preserved |= point.pin == "cell:A";
      }
    }
  }
  EXPECT_TRUE(preserved);
  EXPECT_EQ(result.floorplan.nets.size(), 2);
  // With Liberty and an explicit mapped-reset root, every inverter segment is
  // excluded, even though the ordinary LEF marks all pins SIGNAL.
  options.lefs          = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.liberty       = "inou/lefdef/tests/data/sky130hd.lib";
  options.control_ports = "clk";
  options.output_dir    = (root / "explicit_control").string();
  hhds::GraphLibrary reset_flat;
  const auto         reset = place(*graph, reset_flat, options);
  qor.Parse(reset.qor.c_str());
  EXPECT_EQ(qor["hpwl_um"].GetDouble(), 0);
  EXPECT_EQ(reset.floorplan.nets.size(), 2);
  EXPECT_EQ(reset.floorplan.components.size(), result.floorplan.components.size());
  // Automatic metadata on an internal signal works without any port override
  // and survives the private flattening used by placement.
  gu::add_signal_roles(y, attrs::signal_reset);
  options.control_ports.clear();
  options.output_dir = (root / "metadata_control").string();
  hhds::GraphLibrary metadata_flat;
  const auto         automatic = place(*graph, metadata_flat, options);
  EXPECT_TRUE(automatic.prects.pins.contains("cell:Y"));
  EXPECT_FALSE(automatic.prects.override_controls);
  qor.Parse(automatic.qor.c_str());
  EXPECT_EQ(qor["hpwl_um"].GetDouble(), 0);
  EXPECT_EQ(automatic.floorplan.nets, reset.floorplan.nets);
}
TEST(Placement, TimingCheckpointsCarryConnectivityForAutomaticResetCuts) {
  for (unsigned mode = 0; mode < 8; ++mode) {
    const bool         floorplan = mode & 1;
    hhds::GraphLibrary source, flat;
    auto               io = source.create_io("checkpoint_controls");
    for (const auto& [name, port] : std::vector<std::pair<std::string, unsigned>>{
             {    "clk", 0},
             {"trigger", 1},
             {   "data", 2}
    }) {
      io->add_input(name, port);
      io->set_bits(name, 1);
    }
    io->add_output("y", 0);
    io->set_bits("y", 1);
    auto graph = io->create_graph();
    for (const auto* name : {"clk", "trigger", "data"}) {
      gu::set_bits(graph->get_input_pin(name), 1);
    }
    const auto cell = [&](const std::string&                                          name,
                          const std::string&                                          master,
                          const std::vector<std::pair<std::string, hhds::Pin_class>>& inputs,
                          const std::string&                                          output) {
      auto type = source.create_io(master);
      for (size_t i = 0; i < inputs.size(); ++i) {
        type->add_input(inputs[i].first, i);
        type->set_bits(inputs[i].first, 1);
      }
      type->add_output(output, 0);
      type->set_bits(output, 1);
      auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
      node.set_subnode(type);
      node.set_name(name);
      for (const auto& [pin, driver] : inputs) {
        node.create_sink_pin(pin).connect_driver(driver);
      }
      auto pin = node.create_driver_pin(output);
      gu::set_bits(pin, 1);
      return pin;
    };
    auto reset = cell("reset_stage",
                      "sky130_fd_sc_hd__dfxtp_1",
                      {
                          {"CLK",     graph->get_input_pin("clk")},
                          {  "D", graph->get_input_pin("trigger")}
    },
                      "Q");
    gu::add_signal_roles(reset, attrs::signal_reset);
    auto y = cell("mix",
                  "sky130_fd_sc_hd__nand2_1",
                  {
                      {"A", graph->get_input_pin("data")},
                      {"B",                        reset}
    },
                  "Y");
    graph->get_output_pin("y").connect_driver(y);
    auto options           = plain_options();
    options.liberty        = "inou/lefdef/tests/data/sky130hd.lib";
    options.lefs           = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
    options.output_dir     = std::string(std::getenv("TEST_TMPDIR")) + (floorplan ? "/checkpoint_fp" : "/checkpoint_final");
    options.output_dir    += std::to_string(mode);
    options.control_ports  = mode & 2 ? "clk" : "";
    std::filesystem::create_directories(options.output_dir);
    const auto sdc = options.output_dir + "/timing.sdc";
    std::ofstream(sdc) << "create_clock -name clk -period 10 [get_ports clk]\n"
                          "set_input_delay -clock clk 0 [all_inputs]\nset_output_delay -clock clk 0 [all_outputs]\n";
    Eprp_var timing_options;
    timing_options.dict = {
        {"files", options.liberty + "," + sdc},
        {  "top",       "checkpoint_controls"},
        { "hier",                     "false"}
    };
    timing_options.set_stage_labels(timing_options.dict);
    opentimer::Physical_session timer(timing_options);
    const auto                  tech = physical::read_lef(options.lefs);
    options.fp_pin_adjust            = floorplan;
    options.fp_place                 = floorplan;
    options.fp_iterations            = 2;
    options.iterations               = 2;
    options.bins                     = 4;
    options.fp_resize                = false;
    options.fp_timing_interval = options.macro_timing_interval = 1;
    options.final_timing_start                                 = 0;  // Short fixture: exercise in-loop timing.
    unsigned   calls = 0, ranking_calls = 0, floorplan_stages = 0;
    const auto check_controls = [&](const Prects_networks& controls) {
      EXPECT_TRUE(controls.timing_cuts.contains("reset_stage:Q"));
      EXPECT_TRUE(controls.timing_cuts.contains("mix:B"));
      EXPECT_FALSE(controls.timing_cuts.contains("clk"));
      EXPECT_FALSE(controls.timing_cuts.contains("mix:A"));
    };
    const auto check = [&](const Result& state) {
      EXPECT_EQ(state.floorplan.nets.size(), 5);
      const auto controls = prects_networks(tech, state.floorplan, state.prects);
      check_controls(controls);
      return controls;
    };
    options.fp_evaluate = [&](const Result& state) {
      ++calls;
      const auto controls = check(state);
      auto       timing   = timer.evaluate(state.graph, state.parasitics, controls.timing_cuts);
      EXPECT_TRUE(timing.pins.at("mix:A").arrival);
      EXPECT_FALSE(timing.pins.at("mix:B").arrival);
      EXPECT_TRUE(timing.pins.at("reset_stage:CLK").clock);
      return prects_data_timing(timing, controls);
    };
    if (mode & 4) {
      options.gp_rank = [&](const Ranking_view& state) {
        ++calls;
        ++ranking_calls;
        check_controls(state.controls);
        auto timing = timer.evaluate(state.graph, state.parasitics, state.controls.timing_cuts);
        EXPECT_TRUE(timing.pins.at("mix:A").arrival);
        EXPECT_FALSE(timing.pins.at("mix:B").arrival);
        EXPECT_TRUE(timing.pins.at("reset_stage:CLK").clock);
        return prects_data_timing(timing, state.controls);
      };
    }
    options.after_stage = [&](Placement_stage stage, const Result& state) {
      check(state);
      floorplan_stages += stage == Placement_stage::floorplan;
      return false;
    };
    const auto result = place(*graph, flat, options);
    EXPECT_GT(calls, 0);
    if (mode & 4) {
      EXPECT_GT(ranking_calls, 0U);
    }
    EXPECT_EQ(floorplan_stages, floorplan ? 1 : 0);
    EXPECT_EQ(result.floorplan.nets.size(), 5);
  }
}
TEST(Placement, AlwaysTransparentLatchAliasesPreservePackedInputsAndSource) {
  // Missing gate, active-high true and active-low false all store no state.
  // Dynamic/closed gates and resettable latches must still require mapping.
  for (unsigned mode = 0; mode < 6; ++mode) {
    hhds::GraphLibrary source, flat;
    auto               io = source.create_io("top");
    io->add_input("a", 0);
    io->set_bits("a", 2);
    io->add_input("net_1", 1);  // Collides with a generated internal net name.
    io->set_bits("net_1", 1);
    io->add_input("snapshot_net_0", 2);  // Also reserve snapshot-only internal names.
    io->set_bits("snapshot_net_0", 1);
    io->add_output("y", 0);
    io->set_bits("y", 1);
    auto graph = io->create_graph();
    auto a     = graph->get_input_pin("a");
    gu::set_bits(a, 2);
    gu::set_bits(graph->get_input_pin("net_1"), 1);
    auto latch = gu::create_typed_node(*graph, Ntype_op::Latch);
    latch.set_name("combinational_reg");
    gu::setup_sink_by_name(latch, "din").connect_driver(a);
    auto q = latch.create_driver_pin(0);
    gu::set_bits(q, 2);
    gu::add_signal_roles(q, attrs::signal_reset);
    const auto constant = [&](int value) { return gu::create_const(*graph, *Dlop::create_integer(value)); };
    if (mode) {
      gu::setup_sink_by_name(latch, "enable")
          .connect_driver(mode == 3 ? gu::create_get_mask(*graph, a, 0).get_driver_pin(0)
                                    : constant(mode == 2 || mode == 4 ? 0 : 1));
    }
    if (mode == 2) {
      gu::setup_sink_by_name(latch, "posclk").connect_driver(constant(0));
    }
    if (mode == 5) {
      gu::setup_sink_by_name(latch, "reset_pin").connect_driver(gu::create_get_mask(*graph, a, 0).get_driver_pin(0));
    }
    auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
    inv->add_input("A", 0);
    inv->add_output("Y", 0);
    auto cell = gu::create_typed_node(*graph, Ntype_op::Sub);
    cell.set_subnode(inv);
    cell.set_name("invert_high_bit");
    cell.create_sink_pin("A").connect_driver(gu::create_get_mask(*graph, q, 1).get_driver_pin(0));
    auto y = cell.create_driver_pin("Y");
    gu::set_bits(y, 1);
    graph->get_output_pin("y").connect_driver(y);
    auto options       = plain_options();
    options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
    options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/transparent_" + std::to_string(mode);
    options.iterations = mode == 0 ? 0 : 2;
    options.snapshots  = true;
    options.bins       = 8;
    if (mode >= 3) {
      EXPECT_THROW((void)place(*graph, flat, options), std::runtime_error) << mode;
    } else {
      const auto result = place(*graph, flat, options);
      ASSERT_EQ(result.floorplan.components.size(), 1);
      EXPECT_TRUE(result.prects.ports.contains("a[0]"));
      EXPECT_TRUE(result.prects.ports.contains("a[1]"));
      bool connected = false;
      for (const auto& net : result.parasitics->nets) {
        if (net.driver == "a[1]") {
          for (const auto& pin : net.nodes) {
            connected |= pin.pin == "invert_high_bit:A";
          }
        }
      }
      EXPECT_TRUE(connected) << mode;
      std::set<std::string> names;
      for (const auto& net : result.floorplan.nets) {
        EXPECT_TRUE(names.insert(net.name).second);
      }
      for (const auto& terminal : result.floorplan.terminals) {
        if (terminal.direction == "INPUT") {
          EXPECT_EQ(terminal.net, terminal.name);
        }
      }
      for (const auto* stage : {"before_legalization.def", "after_legalization.def", "after_detailed.def"}) {
        const auto snapshot_path = options.output_dir + "/" + stage;
        ASSERT_TRUE(std::filesystem::is_regular_file(snapshot_path)) << snapshot_path;
        const auto            snapshot = physical::read_def(snapshot_path);
        std::set<std::string> snapshot_names;
        for (const auto& net : snapshot.nets) {
          EXPECT_FALSE(net.name.empty());
          EXPECT_TRUE(snapshot_names.insert(net.name).second) << net.name;
        }
        for (const auto& terminal : snapshot.terminals) {
          EXPECT_FALSE(terminal.net.empty()) << terminal.name;
        }
        for (const auto* unused : {"a[0]", "net_1", "snapshot_net_0"}) {
          const auto terminal = std::find_if(snapshot.terminals.begin(), snapshot.terminals.end(), [&](const auto& pin) {
            return pin.name == unused;
          });
          ASSERT_NE(terminal, snapshot.terminals.end());
          EXPECT_EQ(terminal->net, unused);
          const auto net
              = std::find_if(snapshot.nets.begin(), snapshot.nets.end(), [&](const auto& n) { return n.name == unused; });
          ASSERT_NE(net, snapshot.nets.end());
          ASSERT_EQ(net->connections.size(), 1);
          EXPECT_EQ(net->connections.front().instance, "PIN");
          EXPECT_EQ(net->connections.front().pin, unused);
        }
      }
      for (auto status : {physical::Status::PLACED, physical::Status::FIXED}) {
        auto imported = result.floorplan;
        for (auto& terminal : imported.terminals) {
          terminal.status = status;
          if (terminal.name == "a[0]" || terminal.name == "net_1" || terminal.name == "snapshot_net_0") {
            const auto old_name = terminal.net;
            terminal.net        = "imported_" + terminal.name;
            for (auto& net : imported.nets) {
              if (net.name == old_name) {
                net.name = terminal.net;
              }
            }
          }
        }
        const auto path = options.output_dir + "/imported_" + std::to_string(static_cast<unsigned>(status)) + ".def";
        physical::write_def(imported, path);
        auto repeat       = options;
        repeat.previous   = std::make_shared<physical::Floorplan>(physical::read_def(path));
        repeat.snapshots  = false;
        repeat.iterations = 0;
        repeat.output_dir = options.output_dir + "/repeat_" + std::to_string(static_cast<unsigned>(status));
        hhds::GraphLibrary repeated_library;
        const auto         repeated = place(*result.graph, repeated_library, repeat);
        for (const auto* unused : {"a[0]", "net_1", "snapshot_net_0"}) {
          const auto terminal = std::find_if(repeated.floorplan.terminals.begin(),
                                             repeated.floorplan.terminals.end(),
                                             [&](const auto& pin) { return pin.name == unused; });
          ASSERT_NE(terminal, repeated.floorplan.terminals.end());
          EXPECT_EQ(terminal->net, unused);
          const auto net = std::find_if(repeated.floorplan.nets.begin(), repeated.floorplan.nets.end(), [&](const auto& n) {
            return n.name == unused;
          });
          ASSERT_NE(net, repeated.floorplan.nets.end());
          ASSERT_EQ(net->connections.size(), 1);
          EXPECT_EQ(net->connections.front().instance, "PIN");
          EXPECT_EQ(net->connections.front().pin, unused);
        }
      }
      for (auto node : result.graph->body().nodes()) {
        EXPECT_NE(gu::type_op_of(node), Ntype_op::Latch);
      }
    }
    EXPECT_EQ(gu::type_op_of(latch), Ntype_op::Latch);
    EXPECT_EQ(gu::get_driver_of_sink_name(latch, "din"), a);
  }
}
TEST(Placement, SnapshotsPreserveUnconnectedInputNets) {
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("snapshot_ports");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_input("unused", 1);
  io->set_bits("unused", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto cell = gu::create_typed_node(*graph, Ntype_op::Sub);
  cell.set_subnode(inv);
  cell.set_name("invert");
  cell.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
  auto y = cell.create_driver_pin("Y");
  gu::set_bits(y, 1);
  graph->get_output_pin("y").connect_driver(y);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/snapshot_unconnected";
  options.iterations = 40;
  options.bins       = 8;
  options.snapshots  = true;
  ASSERT_NO_THROW(place(*graph, flat, options));
  for (const auto* name : {"before_legalization.def", "after_legalization.def", "after_detailed.def"}) {
    const auto frame = physical::read_def(options.output_dir + "/" + name);
    ASSERT_EQ(frame.terminals.size(), 3);
    for (const auto& terminal : frame.terminals) {
      ASSERT_FALSE(terminal.net.empty());
      const auto net = std::ranges::find(frame.nets, terminal.net, &physical::Net::name);
      ASSERT_NE(net, frame.nets.end());
      EXPECT_TRUE(std::ranges::any_of(net->connections, [&](const auto& endpoint) {
        return endpoint.instance == "PIN" && endpoint.pin == terminal.name;
      }));
      if (terminal.name == "unused") {
        EXPECT_EQ(net->connections.size(), 1);
      }
    }
  }
}

TEST(Placement, MappedBusGraphLegalityRcAndReuse) {
  hhds::GraphLibrary source;
  auto               io = source.create_io("top");
  io->add_input("a", 0);
  io->set_bits("a", 2);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto nand = source.create_io("sky130_fd_sc_hd__nand2_1");
  nand->add_input("A", 0);
  nand->add_input("B", 1);
  nand->add_output("Y", 0);
  auto n = graph->create_node();
  gu::set_type_op(n, Ntype_op::Sub);
  n.set_subnode(nand);
  n.set_name("logic.nand");
  auto a = graph->get_input_pin("a");
  gu::set_bits(a, 2);
  n.create_sink_pin("A").connect_driver(gu::create_get_mask(*graph, a, 0).get_driver_pin(0));
  n.create_sink_pin("B").connect_driver(gu::create_get_mask(*graph, a, 1).get_driver_pin(0));
  auto output = n.create_driver_pin("Y");
  gu::set_bits(output, 1);
  auto b = graph->create_node();
  gu::set_type_op(b, Ntype_op::Sub);
  b.set_subnode(inv);
  b.set_name("logic.inv");
  b.create_sink_pin("A").connect_driver(output);
  auto y = b.create_driver_pin("Y");
  gu::set_bits(y, 1);
  graph->get_output_pin("y").connect_driver(y);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  const auto root    = std::filesystem::path(std::getenv("TEST_TMPDIR"));
  options.output_dir = (root / "place").string();
  options.cache_dir  = (root / "cache").string();
  options.iterations = 30;
  options.bins       = 8;
  hhds::GraphLibrary first;
  const auto         placed = place(*graph, first, options);
  EXPECT_EQ(placed.floorplan.components.size(), 2);
  EXPECT_EQ(placed.floorplan.terminals.size(), 3);
  {
    // Pin-only FP still runs when no macro/hierarchy qualifies and STA is off.
    auto pins = options;
    pins.cache_dir.clear();
    pins.output_dir = (root / "pin_only").string();
    pins.fp_place = pins.fp_pin_adjust = pins.final_cells = true;
    pins.fp_colors                                        = false;
    pins.fp_pin_interval                                  = 5;
    pins.io_iterations = pins.fp_iterations = pins.iterations = 20;
    // Automatic imported PLACED terminals must remain movable when opted in.
    const auto pin_input                                      = root / "movable_pins.def";
    physical::write_def(placed.floorplan, pin_input.string());
    pins.floorplan          = pin_input.string();
    auto forbidden          = pins;
    forbidden.fp_pin_adjust = false;
    hhds::GraphLibrary rejected_library;
    EXPECT_THROW(place(*graph, rejected_library, forbidden), std::runtime_error);
    hhds::GraphLibrary  pin_library;
    const auto          result = place(*graph, pin_library, pins);
    std::ifstream       report_file(pins.output_dir + "/macro/fp_place.json");
    const std::string   text{std::istreambuf_iterator<char>(report_file), {}};
    rapidjson::Document report;
    report.Parse(text.c_str());
    ASSERT_FALSE(report.HasParseError());
    EXPECT_TRUE(report["candidates"].Empty());
    EXPECT_GT(report["fp_iterations"].GetInt(), 0);
    EXPECT_GE(report["pin_updates"].GetUint64(), 2);
    EXPECT_EQ(report["timing_sta_calls"].GetUint(), 0);
    for (const auto& terminal : result.floorplan.terminals) {
      EXPECT_EQ(terminal.status, physical::Status::PLACED);
      const auto point = std::find_if(result.parasitics->terminals.begin(), result.parasitics->terminals.end(), [&](const auto& p) {
        return p.name == terminal.name;
      });
      ASSERT_NE(point, result.parasitics->terminals.end());
      EXPECT_EQ(point->x, terminal.x);
      EXPECT_EQ(point->y, terminal.y);
    }
  }
  {
    // A supplied frame is authoritative even above the soft density target.
    auto frame = placed.floorplan;
    for (auto& terminal : frame.terminals) {
      terminal.status = physical::Status::FIXED;
    }
    const auto frame_path = root / "soft_density.def";
    physical::write_def(frame, frame_path.string());
    auto soft = options;
    soft.cache_dir.clear();
    soft.floorplan   = frame_path.string();
    soft.output_dir  = (root / "soft_density").string();
    soft.utilization = .9;
    soft.density     = .01;
    soft.iterations  = 1;
    soft.full        = false;
    hhds::GraphLibrary soft_library;
    const auto         result = place(*graph, soft_library, soft);
    EXPECT_EQ(result.floorplan.die.xh, frame.die.xh);
    EXPECT_EQ(result.floorplan.die.yh, frame.die.yh);
    EXPECT_EQ(result.floorplan.components.size(), 2);
    // Exercise an explicit filler opt-out on a very sparse fixed-IO frame.
    const auto original_rows = frame.rows;
    for (int block = 1; block < 1000; ++block) {
      for (auto row : original_rows) {
        row.name += "_" + std::to_string(block);
        row.y    += std::llround(block * frame.die.yh);
        frame.rows.push_back(std::move(row));
      }
    }
    for (auto& row : frame.rows) {
      row.count *= 1000;
    }
    frame.die.xh *= 1000;
    frame.die.yh *= 1000;
    physical::write_def(frame, frame_path.string());
    soft.output_dir      = (root / "no_virtual_fillers").string();
    soft.density         = .8;
    soft.virtual_fillers = false;
    hhds::GraphLibrary sparse_library;
    const auto         sparse = place(*graph, sparse_library, soft);
    EXPECT_EQ(sparse.floorplan.components.size(), 2);
    EXPECT_EQ(sparse.floorplan.die.xh, frame.die.xh);
    // Internal density particles can model this whitespace without creating
    // physical cells or exceeding a bounded particle count.
    soft.virtual_fillers          = true;
    soft.virtual_filler_max_count = 64;
    soft.virtual_filler_fraction  = 1.5;
    soft.output_dir               = (root / "bounded_virtual_fillers").string();
    hhds::GraphLibrary virtual_library;
    const auto         virtual_result = place(*graph, virtual_library, soft);
    EXPECT_EQ(virtual_result.floorplan.components.size(), 2);
    EXPECT_EQ(virtual_result.floorplan.die, sparse.floorplan.die);
    rapidjson::Document virtual_qor;
    virtual_qor.Parse(virtual_result.qor.c_str());
    ASSERT_FALSE(virtual_qor.HasParseError());
    EXPECT_TRUE(virtual_qor["virtual_fillers"].GetBool());
    EXPECT_EQ(std::count_if(virtual_qor.MemberBegin(),
                            virtual_qor.MemberEnd(),
                            [](const auto& member) { return member.name == "virtual_fillers"; }),
              1);
    auto invalid                     = soft;
    invalid.virtual_filler_max_count = -1;
    EXPECT_THROW(place(*graph, virtual_library, invalid), std::invalid_argument);
  }
  {
    auto core_only   = options;
    core_only.halo_x = core_only.halo_y = 1000;
    core_only.cache_dir.clear();
    core_only.output_dir = (root / "core_halo").string();
    core_only.snapshots  = true;
    hhds::GraphLibrary untouched;
    const auto         same = place(*graph, untouched, core_only);
    EXPECT_EQ(same.floorplan.components, placed.floorplan.components);
    EXPECT_EQ(same.floorplan.die, placed.floorplan.die);
    for (const auto* name : {"before_legalization.def", "after_legalization.def", "after_detailed.def"}) {
      const auto frame = physical::read_def(core_only.output_dir + "/" + name);
      EXPECT_EQ(frame.components.size(), 2);
      EXPECT_EQ(frame.terminals.size(), 3);
      EXPECT_FALSE(frame.nets.empty());
    }
    std::ifstream snapshot(root / "core_halo/gp.json");
    ASSERT_TRUE(snapshot.good());
    const std::string   text{std::istreambuf_iterator<char>(snapshot), {}};
    rapidjson::Document json;
    json.Parse(text.c_str());
    ASSERT_FALSE(json.HasParseError());
    EXPECT_STREQ(json["stage"].GetString(), "global_placement_before_legalization");
    ASSERT_EQ(json["nodes"].Size(), 2);
    std::set<std::string> snapshot_names;
    for (const auto& node : json["nodes"].GetArray()) {
      snapshot_names.insert(node[0].GetString());
      EXPECT_TRUE(std::isfinite(node[1].GetDouble()));
      EXPECT_TRUE(std::isfinite(node[2].GetDouble()));
    }
    EXPECT_EQ(snapshot_names, (std::set<std::string>{"logic.inv", "logic.nand"}));
  }
  {
    auto spread = options;
    spread.cache_dir.clear();
    spread.output_dir   = (root / "local_spread").string();
    spread.dynamic_bins = true;
    spread.bin_max = spread.local_density_bins = 32;
    spread.local_density_weight                = 8;
    spread.local_density_target                = .05;
    hhds::GraphLibrary spread_library;
    const auto         result = place(*graph, spread_library, spread);
    EXPECT_EQ(result.floorplan.components.size(), placed.floorplan.components.size());
    EXPECT_NE(result.qor.find("\"legal\":true"), std::string::npos);
    std::ifstream       spread_report(spread.output_dir + "/gp_history.json");
    const std::string   spread_text{std::istreambuf_iterator<char>(spread_report), {}};
    rapidjson::Document spread_json;
    spread_json.Parse(spread_text.c_str());
    ASSERT_FALSE(spread_json.HasParseError());
    EXPECT_EQ(spread_json["local_density_target"].GetDouble(), .05);
    EXPECT_EQ(spread_json["local_density_weight"].GetDouble(), 8);
    auto invalid                 = spread;
    invalid.local_density_target = -.2;
    EXPECT_THROW(place(*graph, spread_library, invalid), std::invalid_argument);
  }
  EXPECT_FALSE(placed.parasitics->nets.empty());
  EXPECT_TRUE(std::filesystem::exists(root / "place/out.def"));
  double cap = 0;
  for (const auto& net : placed.parasitics->nets) {
    for (const auto& node : net.nodes) {
      cap += node.capacitance_ff;
    }
  }
  EXPECT_GT(cap, 0);
  hhds::GraphLibrary second;
  const auto         warm = place(*graph, second, options);
  EXPECT_NE(warm.qor.find("\"hit\":true"), std::string::npos);
  EXPECT_EQ(placed.floorplan.components, warm.floorplan.components);
  EXPECT_EQ(placed.parasitics->identity, warm.parasitics->identity);
  // Supply a real DEF floorplan with fixed instances and IO; they must remain
  // identical while the remaining cells are placed around them.
  auto fixed                            = placed.floorplan;
  fixed.components.front().place.status = Ann_place::Status::FIXED;
  for (auto& terminal : fixed.terminals) {
    terminal.status = Ann_place::Status::FIXED;
  }
  fixed.terminals.front().x = 0;
  fixed.terminals.front().y = 1360;  // zero-area IO on the edge, halfway through a row
  physical::write_def(fixed, (root / "floorplan.def").string());
  Options imported   = options;
  imported.floorplan = (root / "floorplan.def").string();
  imported.cache_dir.clear();
  hhds::GraphLibrary with_floorplan;
  const auto         constrained = place(*graph, with_floorplan, imported);
  const auto&        original    = fixed.components.front();
  const auto         found       = std::find_if(constrained.floorplan.components.begin(),
                                                constrained.floorplan.components.end(),
                                                [&](const auto& c) { return c.name == original.name; });
  ASSERT_NE(found, constrained.floorplan.components.end());
  EXPECT_EQ(found->place, original.place);
  EXPECT_EQ(constrained.floorplan.terminals, fixed.terminals);
  auto bad           = fixed;
  bad.rows.back().y += 2720;
  physical::write_def(bad, (root / "gap.def").string());
  imported.floorplan = (root / "gap.def").string();
  hhds::GraphLibrary invalid;
  EXPECT_THROW(place(*graph, invalid, imported), std::runtime_error);
  // A DIEAREA margin around the rows: generated IO slots sit on the die edge,
  // not on the core boundary.
  auto margin = placed.floorplan;
  margin.components.clear();
  margin.terminals.clear();
  margin.nets.clear();
  for (auto& row : margin.rows) {
    row.x += 4600;
    row.y += 5440;
  }
  margin.die.xh += 2 * 4600;
  margin.die.yh += 2 * 5440;
  physical::write_def(margin, (root / "margin.def").string());
  imported.floorplan = (root / "margin.def").string();
  hhds::GraphLibrary with_margin;
  const auto         padded = place(*graph, with_margin, imported);
  ASSERT_EQ(padded.floorplan.terminals.size(), 3);
  for (const auto& t : padded.floorplan.terminals) {
    const auto& die = padded.floorplan.die;
    EXPECT_TRUE(t.x == die.xl || t.x == die.xh || t.y == die.yl || t.y == die.yh) << t.name << " at " << t.x << "," << t.y;
  }
  first.save((root / "graph").string());
  hhds::GraphLibrary reloaded;
  reloaded.load((root / "graph").string());
  auto body  = reloaded.find_io("top")->get_graph();
  int  count = 0;
  for (auto node : body->body().nodes()) {
    if (node.get_subnode_io()) {
      EXPECT_TRUE(node.attr(attrs::place).has());
      ++count;
    }
  }
  EXPECT_EQ(count, 2);
  options.full = false;
  options.cache_dir.clear();
  hhds::GraphLibrary gp;
  const auto         approximate = place(*graph, gp, options);
  EXPECT_FALSE(std::filesystem::exists(root / "place/out.def"));
  EXPECT_TRUE(approximate.parasitics->approximate);
}
TEST(Placement, SignedWiringExtendsMsb) {
  // y (2 bits) = signed a[1]: y[1] must be a[1] again, not a tie cell nor a
  // bit past a's width.
  hhds::GraphLibrary source;
  auto               io = source.create_io("top");
  io->add_input("a", 0);
  io->set_bits("a", 2);
  io->add_output("y", 0);
  io->set_bits("y", 2);
  io->add_output("z", 1);
  io->set_bits("z", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto a = graph->get_input_pin("a");
  gu::set_bits(a, 2);
  auto msb = gu::create_get_mask(*graph, a, 1).get_driver_pin(0);
  gu::set_bits(msb, 1);
  gu::set_sign(msb);
  graph->get_output_pin("y").connect_driver(msb);
  auto n = graph->create_node();
  gu::set_type_op(n, Ntype_op::Sub);
  n.set_subnode(inv);
  n.set_name("inv");
  n.create_sink_pin("A").connect_driver(gu::create_get_mask(*graph, a, 0).get_driver_pin(0));
  auto z = n.create_driver_pin("Y");
  gu::set_bits(z, 1);
  graph->get_output_pin("z").connect_driver(z);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir = (std::filesystem::path(std::getenv("TEST_TMPDIR")) / "signed").string();
  options.iterations = 30;
  options.bins       = 8;
  hhds::GraphLibrary                 flat;
  const auto                         placed = place(*graph, flat, options);
  std::map<std::string, std::string> net_of;
  for (const auto& t : placed.floorplan.terminals) {
    net_of[t.name] = t.net;
  }
  EXPECT_EQ(net_of.at("y[0]"), net_of.at("a[1]"));
  EXPECT_EQ(net_of.at("y[1]"), net_of.at("a[1]"));
  EXPECT_EQ(placed.floorplan.components.size(), 1);  // no tie cell
}
// nand(a[0], 1) -> inv -> y, plus a constant output k: the tie cell drives both
// a cell input and a top output that keeps its logical constant. Without
// constants, the same top uses a[1] for both instead (no tie cell needed).
// With unknown_k the output k is an undriven (unknown, `0ub?`) constant instead of a 1.
static std::shared_ptr<hhds::Graph> tie_graph(hhds::GraphLibrary& source, bool constants = true, bool unknown_k = false) {
  auto io = source.create_io("tie_top");
  io->add_input("a", 0);
  io->set_bits("a", 2);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  io->add_output("k", 1);
  io->set_bits("k", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto nand = source.create_io("sky130_fd_sc_hd__nand2_1");
  nand->add_input("A", 0);
  nand->add_input("B", 1);
  nand->add_output("Y", 0);
  auto a = graph->get_input_pin("a");
  gu::set_bits(a, 2);
  auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
  n.set_subnode(nand);
  n.set_name("nand");
  n.create_sink_pin("A").connect_driver(gu::create_get_mask(*graph, a, 0).get_driver_pin(0));
  const auto one = [&] {
    return constants ? gu::create_const(*graph, *Dlop::create_integer(1)) : gu::create_get_mask(*graph, a, 1).get_driver_pin(0);
  };
  n.create_sink_pin("B").connect_driver(one());
  auto mid = n.create_driver_pin("Y");
  gu::set_bits(mid, 1);
  auto b = gu::create_typed_node(*graph, Ntype_op::Sub);
  b.set_subnode(inv);
  b.set_name("inv");
  b.create_sink_pin("A").connect_driver(mid);
  auto y = b.create_driver_pin("Y");
  gu::set_bits(y, 1);
  graph->get_output_pin("y").connect_driver(y);
  graph->get_output_pin("k").connect_driver(unknown_k ? gu::create_const(*graph, *Dlop::from_pyrope("0ub?")) : one());
  return graph;
}
TEST(Placement, InfeasibleDensityHandsOffAndReportsOriginalTarget) {
  hhds::GraphLibrary source, placed;
  auto               graph = tie_graph(source, false);
  auto               o     = plain_options();
  o.lefs                   = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  o.core = o.die        = {0, 0, 5.52, 2.72};
  o.bins                = 2;
  o.density             = .1;
  o.iterations          = 200;
  o.min_iterations      = 40;
  o.post_density_factor = 4;
  o.snapshots           = true;
  o.output_dir          = std::string(std::getenv("TEST_TMPDIR")) + "/infeasible_density";
  const auto result     = place(*graph, placed, o);
  EXPECT_NO_THROW(check_legality(physical::read_lef(o.lefs), result.floorplan));
  rapidjson::Document qor;
  qor.Parse(result.qor.c_str());
  ASSERT_FALSE(qor.HasParseError());
  EXPECT_DOUBLE_EQ(qor["gp_target_density"].GetDouble(), o.density);
  EXPECT_GT(qor["gp_overflow_floor"].GetDouble(), o.stop_overflow);
  EXPECT_GT(qor["gp_overflow"].GetDouble(), o.stop_overflow);
  EXPECT_FALSE(qor["gp_converged"].GetBool());
  EXPECT_STREQ(qor["gp_stop_reason"].GetString(), "density_infeasible");
  EXPECT_GE(qor["gp_iterations"].GetInt(), o.min_iterations);
  EXPECT_LT(qor["gp_iterations"].GetInt(), o.iterations);
}

struct Tie_fixture {
  std::string                               name;
  std::vector<std::pair<std::string, bool>> outputs;
  double                                    width    = 0.92;
  bool                                      physical = true, disabled = false, geometry = true, symmetric = true;
};
static Options tie_options(const std::string& label, const std::vector<Tie_fixture>& cells) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / ("constant_" + label);
  std::filesystem::create_directories(dir);
  std::ofstream lib(dir / "ties.lib"), lef(dir / "ties.lef");
  lib << "library(test) {\n";
  lef << "VERSION 5.8 ;\n";
  for (const auto& cell : cells) {
    lib << "cell(" << cell.name << ") {\n";
    if (cell.disabled) {
      lib << "dont_use : true;\n";
    }
    if (cell.physical) {
      lef << "MACRO " << cell.name << "\n CLASS CORE ;\n SIZE " << cell.width << " BY 2.72 ;\n SYMMETRY "
          << (cell.symmetric ? "X Y" : "Y") << " ;\n SITE unithd ;\n";
    }
    for (const auto& [pin, value] : cell.outputs) {
      lib << "pin(" << pin << ") { direction:output; function:\"" << (value ? '1' : '0') << "\"; }\n";
      if (cell.physical) {
        lef << " PIN " << pin << "\n DIRECTION OUTPUT ;\n USE SIGNAL ;\n";
        if (cell.geometry) {
          lef << " PORT\n LAYER met1 ;\n RECT 0.1 0.1 0.2 0.2 ;\n END\n";
        }
        lef << " END " << pin << '\n';
      }
    }
    lib << "}\n";
    if (cell.physical) {
      lef << "END " << cell.name << '\n';
    }
  }
  lib << "}\n";
  lef << "END LIBRARY\n";
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (dir / "ties.lef").string()};
  options.liberty    = (dir / "ties.lib").string();
  options.output_dir = (dir / "out").string();
  options.iterations = 0;
  options.bins       = 8;
  return options;
}
static std::shared_ptr<hhds::Graph> both_constants(hhds::GraphLibrary& source) {
  auto graph = tie_graph(source);
  graph->get_io()->add_output("low", 2);
  graph->get_io()->set_bits("low", 1);
  graph->get_output_pin("low").connect_driver(gu::create_const(*graph, *Dlop::create_integer(0)));
  return graph;
}
TEST(Placement, ConstantDriversUseLibertyPolarityAndPhysicalCandidates) {
  hhds::GraphLibrary source, flat;
  const auto         graph   = both_constants(source);
  const auto         options = tie_options("separate",
                                           {
                                               {"disabled", {{"p", true}}, 0.46, true, true},
                                               {"absent_lef", {{"p", true}}, 0.46, false},
                                               {"absent_geometry", {{"p", true}}, 0.46, true, false, false},
                                               {"z_high", {{"p", true}}},
                                               {"a_high", {{"upper", true}}},
                                               {"low_driver", {{"lower", false}}},
  });
  const auto         placed  = place(*graph, flat, options);
  for (const auto& c : placed.floorplan.components) {
    if (c.name.starts_with("__place_tiehi") || c.name.starts_with("__place_tielo")) {
      const bool high = c.name.starts_with("__place_tiehi");
      EXPECT_EQ(c.macro, high ? "a_high" : "low_driver");
      const auto io = flat.find_io(c.macro);
      ASSERT_TRUE(io);
      ASSERT_EQ(io->get_output_pin_decls().size(), 1);
      EXPECT_EQ(io->get_output_pin_decls().front().name, high ? "upper" : "lower");
    }
  }
  // Each tie must drive both the physical DEF and the logical graph with the
  // actual library pin. Re-placement and ECO must reuse each instance once.
  for (bool eco : {false, true}) {
    auto next = options;
    if (eco) {
      next.previous = std::make_shared<physical::Floorplan>(placed.floorplan);
    }
    hhds::GraphLibrary again;
    const auto         result = place(*placed.graph, again, next);
    for (bool value : {false, true}) {
      const std::string instance = value ? "__place_tiehi" : "__place_tielo";
      EXPECT_EQ(std::count_if(result.floorplan.components.begin(),
                              result.floorplan.components.end(),
                              [&](const auto& c) { return c.name.starts_with(instance); }),
                value ? 2 : 1);
      bool connected = false;
      for (const auto& net : result.floorplan.nets) {
        for (const auto& pin : net.connections) {
          if (pin.instance.starts_with(instance)) {
            EXPECT_EQ(pin.pin, value ? "upper" : "lower");
            connected = true;
          }
        }
      }
      EXPECT_TRUE(connected);
    }
  }
  EXPECT_FALSE(source.find_io("a_high"));  // Only the private physical library changes.
}
// Imported constant masters are physical netlist cells, rather than logical
// constants. A geometry-only replay must be able to retain their shared nets.
static std::shared_ptr<hhds::Graph> shared_imported_ties(hhds::GraphLibrary& source) {
  auto io = source.create_io("shared_ties");
  for (const auto* name : {"high_a", "high_b", "low_a", "low_b", "high_loaded", "low_loaded"}) {
    io->add_output(name, io->get_output_pin_decls().size());
    io->set_bits(name, 1);
  }
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  for (bool high : {false, true}) {
    auto master = source.create_io(high ? "TIEHI" : "TIELO");
    master->add_output(high ? "H" : "L", 0);
    master->set_bits(high ? "H" : "L", 1);
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(master);
    node.set_name(high ? "shared_high" : "shared_low");
    auto driver = node.create_driver_pin(high ? "H" : "L");
    gu::set_bits(driver, 1);
    graph->get_output_pin(high ? "high_a" : "low_a").connect_driver(driver);
    graph->get_output_pin(high ? "high_b" : "low_b").connect_driver(driver);
    auto load = gu::create_typed_node(*graph, Ntype_op::Sub);
    load.set_subnode(inv);
    load.set_name(high ? "high_load" : "low_load");
    load.create_sink_pin("A").connect_driver(driver);
    auto output = load.create_driver_pin("Y");
    gu::set_bits(output, 1);
    graph->get_output_pin(high ? "high_loaded" : "low_loaded").connect_driver(output);
  }
  return graph;
}
TEST(Placement, PreserveImportedSharedTieMastersAndGraphPhysicalEndpoints) {
  hhds::GraphLibrary source;
  const auto         graph       = shared_imported_ties(source);
  auto               options     = tie_options("preserve_imported",
                                               {
                                                   {"TIEHI",  {{"H", true}}},
                                                   {"TIELO", {{"L", false}}}
  });
  options.preserve_imported_ties = true;
  options.cache_dir              = options.output_dir + "/cache";
  const std::map<std::string, std::string> expected_masters{
      {"shared_high",                  "TIEHI"},
      { "shared_low",                  "TIELO"},
      {  "high_load", "sky130_fd_sc_hd__inv_1"},
      {   "low_load", "sky130_fd_sc_hd__inv_1"}
  };
  using Endpoints = std::set<std::pair<std::string, std::string>>;
  const std::multiset<Endpoints> expected_nets{
      {{"shared_high", "H"}, {"PIN", "high_a"}, {"PIN", "high_b"}, {"high_load", "A"}},
      {{"shared_low", "L"}, {"PIN", "low_a"}, {"PIN", "low_b"}, {"low_load", "A"}},
      {{"high_load", "Y"}, {"PIN", "high_loaded"}},
      {{"low_load", "Y"}, {"PIN", "low_loaded"}}
  };
  const auto check = [&](const Result& placed) {
    std::map<std::string, std::string> masters;
    for (const auto& c : placed.floorplan.components) {
      masters.emplace(c.name, c.macro);
    }
    EXPECT_EQ(masters, expected_masters);
    std::multiset<Endpoints> nets;
    for (const auto& net : placed.floorplan.nets) {
      Endpoints endpoints;
      for (const auto& pin : net.connections) {
        endpoints.emplace(pin.instance, pin.pin);
      }
      nets.insert(endpoints);
    }
    EXPECT_EQ(nets, expected_nets);
    for (bool high : {false, true}) {
      for (const auto* output : {high ? "high_a" : "low_a", high ? "high_b" : "low_b"}) {
        const auto drivers = placed.graph->get_output_pin(output).get_driver_pins();
        ASSERT_EQ(drivers.size(), 1);
        EXPECT_EQ(drivers.front().get_master_node().get_name(), high ? "shared_high" : "shared_low");
        EXPECT_EQ(drivers.front().get_port_id(), 0);
        EXPECT_EQ(drivers.front().get_master_node().get_subnode_io()->get_name(), high ? "TIEHI" : "TIELO");
      }
    }
    for (auto node : placed.graph->body().nodes()) {
      if (node.get_name() != "high_load" && node.get_name() != "low_load") {
        continue;
      }
      for (auto pin : node.inp_pins()) {
        const auto drivers = pin.get_driver_pins();
        ASSERT_EQ(drivers.size(), 1);
        EXPECT_EQ(drivers.front().get_master_node().get_name(), node.get_name() == "high_load" ? "shared_high" : "shared_low");
      }
    }
    rapidjson::Document qor;
    qor.Parse(placed.qor.c_str());
    EXPECT_TRUE(qor["preserve_imported_ties"].GetBool());
  };
  hhds::GraphLibrary flat;
  const auto         first = place(*graph, flat, options);
  check(first);
  // Shared sources must not follow whichever single user happens to be last.
  // A timing snapshot and a warm placement retain the same exact topology.
  auto retained             = options;
  retained.previous         = std::make_shared<physical::Floorplan>(first.floorplan);
  retained.retain_placement = true;
  hhds::GraphLibrary kept;
  const auto         snapshot = place(*first.graph, kept, retained);
  check(snapshot);
  for (const auto& c : snapshot.floorplan.components) {
    const auto old = std::find_if(first.floorplan.components.begin(), first.floorplan.components.end(), [&](const auto& x) {
      return x.name == c.name;
    });
    ASSERT_NE(old, first.floorplan.components.end());
    EXPECT_EQ(c.place.x, old->place.x);
    EXPECT_EQ(c.place.y, old->place.y);
  }
  hhds::GraphLibrary warm;
  check(place(*graph, warm, options));
  const auto environment = [&] {
    std::ifstream       file(options.cache_dir + "/placement.json");
    const std::string   text{std::istreambuf_iterator<char>(file), {}};
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    return std::string(doc["environment"].GetString());
  };
  const auto preserved_key       = environment();
  options.preserve_imported_ties = false;
  hhds::GraphLibrary split_library;
  const auto         split = place(*graph, split_library, options);
  EXPECT_NE(environment(), preserved_key);
  EXPECT_EQ(split.floorplan.components.size(), 8);
  EXPECT_EQ(split.floorplan.nets.size(), 8);
  for (const auto& net : split.floorplan.nets) {
    EXPECT_EQ(net.connections.size(), 2);
  }
}
TEST(Placement, PreserveSharedTieDoesNotLocalizeItsOtherSingleLoadOutput) {
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("dual_tie");
  for (const auto* name : {"high_a", "high_b", "low"}) {
    io->add_output(name, io->get_output_pin_decls().size());
    io->set_bits(name, 1);
  }
  auto graph  = io->create_graph();
  auto master = source.create_io("DUAL_TIE");
  master->add_output("H", 0);
  master->add_output("L", 1);
  master->set_bits("H", 1);
  master->set_bits("L", 1);
  auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
  node.set_subnode(master);
  node.set_name("shared");
  auto high = node.create_driver_pin("H");
  auto low  = node.create_driver_pin("L");
  gu::set_bits(high, 1);
  gu::set_bits(low, 1);
  graph->get_output_pin("high_a").connect_driver(high);
  graph->get_output_pin("high_b").connect_driver(high);
  graph->get_output_pin("low").connect_driver(low);
  auto options                   = tie_options("preserve_dual",
                                               {
                                                   {"DUAL_TIE", {{"H", true}, {"L", false}}}
  });
  options.preserve_imported_ties = true;
  options.snapshots              = true;
  const auto result              = place(*graph, flat, options);
  ASSERT_EQ(result.floorplan.components.size(), 1);
  EXPECT_EQ(result.floorplan.components.front().name, "shared");
  EXPECT_EQ(result.floorplan.components.front().macro, "DUAL_TIE");
  const auto snapshot = physical::read_def(options.output_dir + "/after_detailed.def");
  ASSERT_EQ(snapshot.components.size(), 1);
  // No one-load localization may move the shared cell after detailed placement.
  EXPECT_EQ(result.floorplan.components.front().place.x, snapshot.components.front().place.x);
  EXPECT_EQ(result.floorplan.components.front().place.y, snapshot.components.front().place.y);
  EXPECT_EQ(result.floorplan.nets.size(), 2);
}
TEST(Placement, PreserveImportedTiesStillMaterializesLogicalConstantsPerLoad) {
  hhds::GraphLibrary source, flat;
  auto               options     = tie_options("preserve_logical",
                                               {
                                                   {"TIEHI",  {{"H", true}}},
                                                   {"TIELO", {{"L", false}}}
  });
  options.preserve_imported_ties = true;
  const auto placed              = place(*both_constants(source), flat, options);
  EXPECT_EQ(std::count_if(placed.floorplan.components.begin(),
                          placed.floorplan.components.end(),
                          [](const auto& cell) { return cell.name.starts_with("__place_tiehi"); }),
            2);
  EXPECT_EQ(std::count_if(placed.floorplan.components.begin(),
                          placed.floorplan.components.end(),
                          [](const auto& cell) { return cell.name.starts_with("__place_tielo"); }),
            1);
  for (const auto& net : placed.floorplan.nets) {
    const bool tie = std::any_of(net.connections.begin(), net.connections.end(), [](const auto& pin) {
      return pin.instance.starts_with("__place_tie");
    });
    if (tie) {
      EXPECT_EQ(net.connections.size(), 2);
    }
  }
}
TEST(Placement, RetainedOriginalNetlistRecreatesSavedTieCells) {
  hhds::GraphLibrary source, flat;
  const auto         graph   = both_constants(source);
  auto               options = tie_options("retain_ties",
                                           {
                                               {"tie_lo", {{"lo", false}}},
                                               {"tie_hi",  {{"hi", true}}}
  });
  const auto         placed  = place(*graph, flat, options);
  options.previous           = std::make_shared<physical::Floorplan>(placed.floorplan);
  options.retain_placement   = true;
  hhds::GraphLibrary retained;
  const auto         result = place(*graph, retained, options);
  ASSERT_EQ(result.floorplan.components.size(), placed.floorplan.components.size());
  for (const auto& cell : result.floorplan.components) {
    const auto old = std::find_if(placed.floorplan.components.begin(), placed.floorplan.components.end(), [&](const auto& c) {
      return c.name == cell.name;
    });
    ASSERT_NE(old, placed.floorplan.components.end());
    EXPECT_EQ(cell.macro, old->macro);
    EXPECT_EQ(cell.place.x, old->place.x);
    EXPECT_EQ(cell.place.y, old->place.y);
    EXPECT_EQ(cell.place.orientation, old->place.orientation);
  }
  auto missing = std::make_shared<physical::Floorplan>(placed.floorplan);
  std::erase_if(missing->components, [](const auto& cell) { return cell.name.starts_with("__place_tiehi"); });
  options.previous = missing;
  hhds::GraphLibrary rejected;
  EXPECT_THROW(place(*graph, rejected, options), std::runtime_error);
}
// An unknown (X) constant that reaches a mapped cell input is a synthesis don't-care
// (`assign y = 'x` in the RTL). The default refuses it by name; unknown_const_zero ties it
// low (a legal refinement) through the usual tie cell and counts what it tied.
TEST(Placement, UnknownConstantIsRefusedUnlessTiedLowByOption) {
  const auto build = [](hhds::GraphLibrary& source) {
    auto io = source.create_io("x_top");
    io->add_input("a", 0);
    io->set_bits("a", 1);
    io->add_output("y", 0);
    io->set_bits("y", 1);
    auto graph = io->create_graph();
    auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
    inv->add_input("A", 0);
    inv->add_output("Y", 0);
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(inv);
    n.set_name("inv");
    n.create_sink_pin("A").connect_driver(gu::create_const(*graph, *Dlop::from_pyrope("0ub?")));
    auto y = n.create_driver_pin("Y");
    gu::set_bits(y, 1);
    graph->get_output_pin("y").connect_driver(y);
    return graph;
  };
  const auto options = tie_options("unknown",
                                   {
                                       {"tie_lo", {{"lo", false}}},
                                       {"tie_hi",  {{"hi", true}}}
  });
  {
    hhds::GraphLibrary source, flat;
    const auto         graph = build(source);
    try {
      (void)place(*graph, flat, options);
      FAIL() << "an unknown constant reached a physical net without the opt-in";
    } catch (const std::runtime_error& e) {
      EXPECT_NE(std::string(e.what()).find("unknown constant"), std::string::npos) << e.what();
    }
  }
  hhds::GraphLibrary source, flat;
  const auto         graph = build(source);
  auto               tied  = options;
  tied.unknown_const_zero  = true;
  const auto placed        = place(*graph, flat, tied);
  EXPECT_EQ(placed.unknown_const_tied, 1);
  EXPECT_EQ(std::count_if(placed.floorplan.components.begin(),
                          placed.floorplan.components.end(),
                          [](const auto& c) { return c.name.starts_with("__place_tielo"); }),
            1);
}
// The same don't-care on a TOP-LEVEL output rather than a cell input: an undriven output port
// (the OpenROAD flow resolves it with `setundef -zero` before its tie cells). It is refused by
// default and gets the low tie cell under unknown_const_zero.
TEST(Placement, UnknownConstantOnATopOutputIsRefusedUnlessTiedLow) {
  hhds::GraphLibrary source;
  auto               graph   = tie_graph(source, true, true);
  auto               options = plain_options();
  options.liberty            = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir         = std::string(std::getenv("TEST_TMPDIR")) + "/unknown_constant_output";
  options.iterations         = 20;
  options.bins               = 8;
  hhds::GraphLibrary refused;
  try {
    (void)place(*graph, refused, options);
    ADD_FAILURE() << "an unknown constant on a physical net was accepted";
  } catch (const std::runtime_error& e) {
    EXPECT_NE(std::string(e.what()).find("unknown constant"), std::string::npos) << e.what();
  }
  options.unknown_const_zero = true;
  hhds::GraphLibrary tied;
  const auto         placed = place(*graph, tied, options);
  EXPECT_EQ(placed.unknown_const_tied, 1);
  const bool low = std::any_of(placed.floorplan.components.begin(), placed.floorplan.components.end(), [](const auto& c) {
    return c.name.starts_with("__place_tielo");
  });
  EXPECT_TRUE(low) << "the unknown output bit should be driven by the low tie cell";
}
TEST(Placement, DualConstantDriverDeclaresAndPreservesAllLibraryOutputs) {
  hhds::GraphLibrary source, flat;
  const auto         graph   = both_constants(source);
  const auto         options = tie_options("dual",
                                           {
                                               {"supply_pair", {{"plus", true}, {"minus", false}}}
  });
  const auto         placed  = place(*graph, flat, options);
  ASSERT_EQ(flat.find_io("supply_pair")->get_output_pin_decls().size(), 2);
  for (bool value : {false, true}) {
    const std::string instance  = value ? "__place_tiehi" : "__place_tielo";
    bool              connected = false;
    for (const auto& net : placed.floorplan.nets) {
      for (const auto& pin : net.connections) {
        if (pin.instance.starts_with(instance)) {
          EXPECT_EQ(pin.pin, value ? "plus" : "minus");
          connected = true;
        }
      }
    }
    EXPECT_TRUE(connected);
  }
}
TEST(Placement, MissingConstantModelNeverGuessesFromLefOrPinName) {
  for (bool empty_library : {false, true}) {
    hhds::GraphLibrary source, flat;
    const auto         graph   = both_constants(source);
    auto               options = tie_options("missing_" + std::to_string(empty_library),
                                             {
                                                 {"only_high", {{"not_a_conventional_pin", true}}}
    });
    if (empty_library) {
      options.liberty.clear();
    }
    try {
      (void)place(*graph, flat, options);
      FAIL() << "Missing constant-0 model accepted";
    } catch (const std::runtime_error& e) {
      EXPECT_NE(std::string(e.what()).find("constant-0"), std::string::npos) << e.what();
    }
  }
}
// The generated N/FS rows are decided from the mapped cells alone; a tie
// master without SYMMETRY X must not land in (or be offered) an FS row.
TEST(Placement, ConstantDriverWithoutSymmetryXAvoidsFlippedRows) {
  const auto all_n = [](const physical::Floorplan& fp) {
    return std::all_of(fp.rows.begin(), fp.rows.end(), [](const auto& r) { return r.orientation == Ann_place::Orientation::N; });
  };
  {  // Only a non-flippable tie: generated rows fall back to all-N.
    hhds::GraphLibrary source, flat;
    const auto         graph   = tie_graph(source);
    const auto         options = tie_options("flipless_only",
                                             {
                                                 {"flipless_high", {{"p", true}}, 0.46, true, false, true, false}
    });
    const auto         placed  = place(*graph, flat, options);
    EXPECT_TRUE(all_n(placed.floorplan));
    for (const auto& c : placed.floorplan.components) {
      EXPECT_EQ(c.place.orientation, Ann_place::Orientation::N) << c.name;
    }
  }
  hhds::GraphLibrary sym_source, sym_flat;
  const auto         sym_graph   = tie_graph(sym_source);
  const auto         sym_options = tie_options("flipless_smaller",
                                               {
                                                   {"flipless_high", {{"p", true}}, 0.46, true, false, true, false},
                                                   {"flippable_high", {{"p", true}}},
  });
  {  // A larger X-symmetric tie outranks a smaller non-flippable one; rows still alternate.
    const auto placed = place(*sym_graph, sym_flat, sym_options);
    EXPECT_FALSE(all_n(placed.floorplan));
    const auto tie = std::find_if(placed.floorplan.components.begin(), placed.floorplan.components.end(), [](const auto& c) {
      return c.name.starts_with("__place_tiehi");
    });
    ASSERT_NE(tie, placed.floorplan.components.end());
    EXPECT_EQ(tie->macro, "flippable_high");
    // A user floorplan with FS rows cannot take the non-flippable tie at all.
    auto frame = placed.floorplan;
    frame.components.clear();
    frame.terminals.clear();
    frame.nets.clear();
    const auto def = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "flipless_rows.def";
    physical::write_def(frame, def.string());
    hhds::GraphLibrary source, flat;
    const auto         graph   = tie_graph(source);
    auto               options = tie_options("flipless_def",
                                             {
                                                 {"flipless_high", {{"p", true}}, 0.46, true, false, true, false}
    });
    options.floorplan          = def.string();
    try {
      (void)place(*graph, flat, options);
      FAIL() << "non-X-symmetric tie accepted for FS rows";
    } catch (const std::runtime_error& e) {
      EXPECT_NE(std::string(e.what()).find("constant-1"), std::string::npos) << e.what();
    }
  }
  {  // A cached N/FS frame from a tie-free run is still regenerable: an edit that
     // now needs the non-flippable tie falls back to all-N, exactly as cold.
    auto options      = tie_options("flipless_cached",
                                    {
                                        {"flipless_high", {{"p", true}}, 0.46, true, false, true, false}
    });
    options.cache_dir = options.output_dir + "/cache";
    hhds::GraphLibrary first_source, first_flat;
    const auto         first = place(*tie_graph(first_source, false), first_flat, options);
    ASSERT_FALSE(all_n(first.floorplan));
    ASSERT_TRUE(std::filesystem::is_regular_file(options.cache_dir + "/floorplan.def"));
    hhds::GraphLibrary source, flat;
    const auto         placed = place(*tie_graph(source), flat, options);
    EXPECT_TRUE(all_n(placed.floorplan));
    bool tie = false;
    for (const auto& c : placed.floorplan.components) {
      EXPECT_EQ(c.place.orientation, Ann_place::Orientation::N) << c.name;
      tie |= c.name.starts_with("__place_tiehi");
    }
    EXPECT_TRUE(tie);
  }
}
TEST(Placement, ReplaceKeepsLocalTiesForConstantOutput) {
  hhds::GraphLibrary source;
  auto               graph   = tie_graph(source);
  auto               options = plain_options();
  options.liberty            = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir         = std::string(std::getenv("TEST_TMPDIR")) + "/tie";
  options.iterations         = 20;
  options.bins               = 8;
  hhds::GraphLibrary first;
  const auto         placed = place(*graph, first, options);
  // Timing/repair/resynth rounds retain the local sources in the placed graph.
  for (const bool eco : {false, true}) {
    auto again = options;
    if (eco) {
      again.previous = std::make_shared<physical::Floorplan>(placed.floorplan);
    }
    hhds::GraphLibrary second;
    const auto         replaced = place(*placed.graph, second, again);
    unsigned           ties     = 0;
    for (const auto& c : replaced.floorplan.components) {
      ties += c.name.starts_with("__place_tiehi");
    }
    EXPECT_EQ(ties, 2) << "eco=" << eco;
    std::string k_net;
    for (const auto& t : replaced.floorplan.terminals) {
      if (t.name == "k") {
        k_net = t.net;
      }
    }
    // Constant users retain separate local drivers across repeated/ECO runs.
    bool shared = false;
    for (const auto& net : replaced.floorplan.nets) {
      if (net.name != k_net) {
        continue;
      }
      for (const auto& c : net.connections) {
        shared |= c.instance == "nand" && c.pin == "B";
      }
    }
    EXPECT_FALSE(shared) << "eco=" << eco;
  }
}
TEST(Placement, PreviousFrameKeepsIoPositionsAndSlotsNewOnes) {
  hhds::GraphLibrary source;
  auto               graph   = tie_graph(source);
  auto               options = plain_options();
  options.liberty            = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir         = std::string(std::getenv("TEST_TMPDIR")) + "/io_frame";
  options.iterations         = 20;
  options.bins               = 8;
  hhds::GraphLibrary first;
  const auto         placed = place(*graph, first, options);
  // Previous frame (as a resynth trial passes it): swap two PLACED pins and drop
  // one, which then counts as a new top-level port.
  auto               frame  = placed.floorplan;
  ASSERT_EQ(frame.terminals.size(), 4);
  std::map<std::string, physical::Terminal*> by_name;
  for (auto& t : frame.terminals) {
    by_name[t.name] = &t;
  }
  ASSERT_NE(by_name.at("a[0]")->status, Ann_place::Status::FIXED);
  std::swap(by_name.at("a[0]")->x, by_name.at("y")->x);
  std::swap(by_name.at("a[0]")->y, by_name.at("y")->y);
  std::erase_if(frame.terminals, [](const auto& t) { return t.name == "k"; });
  options.previous = std::make_shared<physical::Floorplan>(frame);
  hhds::GraphLibrary second;
  const auto         replaced = place(*graph, second, options);
  ASSERT_EQ(replaced.floorplan.terminals.size(), 4);
  const auto& die = replaced.floorplan.die;
  for (const auto& t : replaced.floorplan.terminals) {
    const auto old = std::find_if(frame.terminals.begin(), frame.terminals.end(), [&](const auto& p) { return p.name == t.name; });
    if (old != frame.terminals.end()) {
      EXPECT_EQ(t.x, old->x) << t.name;
      EXPECT_EQ(t.y, old->y) << t.name;
      continue;
    }
    EXPECT_EQ(t.name, "k");
    EXPECT_TRUE(t.x == die.xl || t.x == die.xh || t.y == die.yl || t.y == die.yh) << t.x << "," << t.y;
    for (const auto& p : frame.terminals) {
      EXPECT_FALSE(p.x == t.x && p.y == t.y) << "new pin k on top of " << p.name;
    }
  }
}
TEST(Placement, FixedDieAndCoreSnapInwardAndKeepPinsOnTheDie) {
  hhds::GraphLibrary source;
  auto               graph   = tie_graph(source);
  auto               options = plain_options();
  options.liberty            = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir         = std::string(std::getenv("TEST_TMPDIR")) + "/fixed_frame";
  options.iterations         = 20;
  options.bins               = 8;
  // utilization/aspect are ignored once a frame is given: the 0.46 x 2.72 um site grid snaps the
  // 2..38.01 x 2..37.9 um core inward to x 2.3..37.72 (77 sites) and y 2.72..35.36 (12 rows).
  options.die                = {0, 0, 40, 40};
  options.core               = {2, 2, 38.01, 37.9};
  hhds::GraphLibrary first;
  const auto         placed = place(*graph, first, options);
  const auto&        fp     = placed.floorplan;
  EXPECT_EQ(fp.die.xl, 0);
  EXPECT_EQ(fp.die.yl, 0);
  EXPECT_EQ(fp.die.xh, 40000);
  EXPECT_EQ(fp.die.yh, 40000);
  ASSERT_EQ(fp.rows.size(), 12);
  EXPECT_EQ(fp.rows.front().x, 2300);
  EXPECT_EQ(fp.rows.front().y, 2720);
  EXPECT_EQ(fp.rows.front().count, 77);
  EXPECT_EQ(fp.rows.back().y, 2720 + 11 * 2720);
  ASSERT_EQ(fp.terminals.size(), 4);
  for (const auto& t : fp.terminals) {
    EXPECT_TRUE(t.x == fp.die.xl || t.x == fp.die.xh || t.y == fp.die.yl || t.y == fp.die.yh) << t.name;
  }
  {  // The staged macro/cell passes feed their own frame back as an imported floorplan while the
     // original die/core options stay set: the imported frame wins and the run must not refuse.
    auto frame = fp;
    frame.components.clear();
    frame.terminals.clear();
    frame.nets.clear();
    const auto def = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "fixed_frame_import.def";
    physical::write_def(frame, def.string());
    auto staged       = options;
    staged.floorplan  = def.string();
    staged.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/fixed_frame_staged";
    hhds::GraphLibrary again;
    const auto         restaged = place(*graph, again, staged);
    EXPECT_EQ(restaged.floorplan.rows.size(), fp.rows.size());
    EXPECT_EQ(restaged.floorplan.rows.front().x, fp.rows.front().x);
  }
  // A lone die doubles as the core; the same cells fit, so only the frame changes.
  options.core.clear();
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/fixed_die_only";
  hhds::GraphLibrary second;
  const auto         die_only = place(*graph, second, options);
  EXPECT_EQ(die_only.floorplan.rows.front().x, 0);
  EXPECT_EQ(die_only.floorplan.rows.front().count, 86);
  EXPECT_EQ(die_only.floorplan.rows.size(), 14);
  // The frame is validated up front: a core outside the die and a core too small for the cells.
  options.die  = {0, 0, 40, 40};
  options.core = {2, 2, 60, 38};
  hhds::GraphLibrary third;
  EXPECT_ANY_THROW(place(*graph, third, options));
  options.core = {2, 2, 3, 3};
  hhds::GraphLibrary fourth;
  EXPECT_ANY_THROW(place(*graph, fourth, options));
}
TEST(Placement, TimingSnapshotRetainsUnlegalizedGeometryAndPhysicalRc) {
  hhds::GraphLibrary source, first, imported;
  auto               graph   = tie_graph(source);
  auto               options = plain_options();
  options.liberty            = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir         = std::string(std::getenv("TEST_TMPDIR")) + "/timing_snapshot";
  options.iterations         = 20;
  options.bins               = 8;
  const auto placed          = place(*graph, first, options);
  auto       frame           = placed.floorplan;
  ASSERT_GT(frame.components.size(), 1);
  frame.components[1].place.x  = frame.components[0].place.x;
  frame.components[1].place.y  = frame.components[0].place.y;
  frame.components[0].place.x += 1;  // Deliberately off-grid: timing never repairs it.
  options.previous             = std::make_shared<physical::Floorplan>(frame);
  options.retain_placement     = true;
  const auto snapshot          = place(*placed.graph, imported, options);
  ASSERT_EQ(snapshot.floorplan.components.size(), frame.components.size());
  for (const auto& cell : snapshot.floorplan.components) {
    const auto old
        = std::find_if(frame.components.begin(), frame.components.end(), [&](const auto& c) { return c.name == cell.name; });
    ASSERT_NE(old, frame.components.end());
    EXPECT_EQ(cell.macro, old->macro);
    EXPECT_EQ(cell.place, old->place);
  }
  EXPECT_NE(snapshot.qor.find("\"mode\":\"timing_snapshot\""), std::string::npos);
  EXPECT_NE(snapshot.qor.find("\"legal\":false"), std::string::npos);
  ASSERT_TRUE(snapshot.parasitics);
  EXPECT_TRUE(std::any_of(snapshot.parasitics->nets.begin(), snapshot.parasitics->nets.end(), [](const auto& net) {
    return std::any_of(net.nodes.begin(), net.nodes.end(), [](const auto& node) { return node.capacitance_ff > 0; });
  }));
  options.previous.reset();
  hhds::GraphLibrary invalid;
  EXPECT_THROW((void)place(*placed.graph, invalid, options), std::invalid_argument);
}

TEST(Placement, MovableHardMacrosPreserveNonintegralDimensionsAndPins) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "mixed_hard_macros";
  std::filesystem::create_directories(dir);
  {
    std::ofstream lef(dir / "macros.lef");
    lef << R"(VERSION 5.8 ;
MACRO mixed_ram_a
 CLASS BLOCK ; SIZE 1.03 BY 17.01 ; SYMMETRY X Y ;
 PIN A
  DIRECTION INPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 0 0.1 0.1 0.2 ; END
 END A
 PIN Z
  DIRECTION OUTPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 0.93 0.1 1.03 0.2 ; END
 END Z
END mixed_ram_a
MACRO mixed_ram_b
 CLASS BLOCK ; SIZE 2.11 BY 16.41 ; SYMMETRY X Y ;
 PIN A
  DIRECTION INPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 0 0.1 0.1 0.2 ; END
 END A
 PIN Z
  DIRECTION OUTPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 2.01 0.1 2.11 0.2 ; END
 END Z
END mixed_ram_b
END LIBRARY
)";
  }
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("mixed");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (const auto& [master, instance] : std::vector<std::pair<std::string, std::string>>{
           {           "mixed_ram_a", "ram0"},
           {           "mixed_ram_b", "ram1"},
           {"sky130_fd_sc_hd__inv_1",  "inv"}
  }) {
    auto cell_io = source.create_io(master);
    cell_io->add_input("A", 0);
    const std::string output = instance == "inv" ? "Y" : "Z";
    cell_io->add_output(output, 0);
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(instance);
    node.set_subnode(cell_io);
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin(output);
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options = plain_options();
  options.lefs = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (dir / "macros.lef").string()};
  options.output_dir  = (dir / "out").string();
  options.cache_dir   = (dir / "cache").string();
  options.iterations  = 20;
  options.bins        = 8;
  options.utilization = 0.02;  // enough rows that a macro origin can land on an odd (FS) row
  const auto result   = place(*graph, flat, options);
  ASSERT_EQ(result.floorplan.components.size(), 3);
  for (const auto& c : result.floorplan.components) {
    if (c.name == "ram0" || c.name == "ram1") {
      const bool first = c.name == "ram0";
      EXPECT_EQ(c.place.width, first ? 1030 : 2110);
      EXPECT_EQ(c.place.height, first ? 17010 : 16410);
      EXPECT_EQ(c.place.orientation, Ann_place::Orientation::N);
      const auto pin = result.pin_locations.at(c.name + ":Z");
      EXPECT_DOUBLE_EQ(pin.first, c.place.x + (first ? 980 : 2060));
      EXPECT_DOUBLE_EQ(pin.second, c.place.y + 150);
    }
  }
  const auto tech = physical::read_lef(options.lefs);
  EXPECT_NO_THROW(check_legality(tech, result.floorplan));
  EXPECT_NO_THROW(physical::verify_def_roundtrip(result.floorplan, physical::read_def((dir / "out/out.def").string())));
  // A warm cache hit locks the macros: they must keep N (not the row's FS)
  // and the same pins as the cold run.
  hhds::GraphLibrary again;
  const auto         warm = place(*graph, again, options);
  EXPECT_NE(warm.qor.find("\"hit\":true"), std::string::npos);
  EXPECT_EQ(result.floorplan.components, warm.floorplan.components);
  for (const auto* name : {"ram0:Z", "ram1:Z"}) {
    EXPECT_EQ(result.pin_locations.at(name), warm.pin_locations.at(name)) << name;
  }
}
TEST(Placement, AutomaticHaloUsesPlacementRowHeightAndPreservesOverrides) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "automatic_halo";
  std::filesystem::create_directories(dir);
  std::ifstream     technology("inou/lefdef/tests/data/sky130hd.tlef");
  std::stringstream technology_text;
  technology_text << technology.rdbuf();
  auto       technology_lef = technology_text.str();
  const auto grid           = technology_lef.find("MANUFACTURINGGRID 0.005");
  ASSERT_NE(grid, std::string::npos);
  technology_lef.replace(grid, std::string("MANUFACTURINGGRID 0.005").size(), "MANUFACTURINGGRID 0.001");
  const auto tech_lef = dir / "tech.lef";
  std::ofstream(tech_lef) << technology_lef;
  hhds::GraphLibrary source;
  auto               io = source.create_io("automatic_halo");
  io->add_input("a", 0);
  io->add_output("y", 0);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto cell = source.create_io("halo_cell"), ram = source.create_io("halo_ram");
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (const auto& master : {ram, cell}) {
    master->add_input("A", 0);
    master->add_output("Y", 0);
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(master);
    node.set_name(std::string(master->get_name()));
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin("Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  for (const auto& [name, width, height] : {
           std::tuple{    "asap7", .054,  .270},
           std::tuple{"nangate45", .190, 1.400},
           std::tuple{ "sky130hd", .460, 2.720}
  }) {
    SCOPED_TRACE(name);
    const auto lef = dir / (std::string(name) + ".lef");
    std::ofstream(lef) << std::format(R"(VERSION 5.8 ;
SITE aaa_unused
 CLASS CORE ; SIZE 0.001 BY 0.001 ;
END aaa_unused
SITE halo_site
 CLASS CORE ; SIZE {} BY {} ; SYMMETRY Y ;
END halo_site
MACRO halo_cell
 CLASS CORE ; SITE halo_site ; SIZE {} BY {} ; SYMMETRY X Y ;
 PIN A
 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER met1 ; RECT 0 0 {} {} ; END
 END A
 PIN Y
 DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER met1 ; RECT 0 0 {} {} ; END
 END Y
END halo_cell
MACRO halo_ram
 CLASS BLOCK ; SIZE {} BY {} ; SYMMETRY X Y ;
 PIN A
 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER met1 ; RECT 0 0 {} {} ; END
 END A
 PIN Y
 DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER met1 ; RECT 0 0 {} {} ; END
 END Y
END halo_ram
END LIBRARY
)",
                                      width,
                                      height,
                                      2 * width,
                                      height,
                                      width,
                                      height,
                                      width,
                                      height,
                                      3 * height,
                                      4 * height,
                                      width,
                                      height,
                                      3 * height,
                                      4 * height);
    for (auto node : graph->body().nodes()) {
      if (node.get_subnode_io() == ram) {
        Ann_place pose(std::llround(20 * height / width) * std::llround(width * 1000),
                       std::llround(20 * height * 1000),
                       std::llround(3 * height * 1000),
                       std::llround(4 * height * 1000));
        pose.status = physical::Status::FIXED;
        node.attr(attrs::place).set(pose);
      }
    }
    auto options   = plain_options();
    options.lefs   = {tech_lef.string(), lef.string()};
    options.halo_x = options.halo_y = -1;
    options.iterations              = 0;
    options.density                 = .5;
    options.bins                    = 4;
    options.core                    = {0, 0, 60 * height, 60 * height};
    options.output_dir              = (dir / name).string();
    options.cache_dir               = (dir / (std::string(name) + "_cache")).string();
    const auto run                  = [&](const Options& run_options, double x, double y) {
      hhds::GraphLibrary  flat;
      auto                placed = place(*graph, flat, run_options);
      rapidjson::Document qor;
      qor.Parse(placed.qor.c_str());
      EXPECT_DOUBLE_EQ(qor["halo"]["x_um"].GetDouble(), x);
      EXPECT_DOUBLE_EQ(qor["halo"]["y_um"].GetDouble(), y);
      EXPECT_EQ(qor["halo"]["x_dbu"].GetInt64(), std::llround(std::ceil(x / width - 1e-9) * width * 1000));
      EXPECT_EQ(qor["halo"]["y_dbu"].GetInt64(), std::llround(std::ceil(y / height - 1e-9) * height * 1000));
      EXPECT_NO_THROW(check_placed_legality(placed, run_options));
      return qor["incremental"]["hit"].GetBool();
    };
    EXPECT_FALSE(run(options, 10 * height, 10 * height));
    EXPECT_TRUE(run(options, 10 * height, 10 * height));
    options.halo_x = options.halo_y = 0;
    EXPECT_FALSE(run(options, 0, 0));
    options.halo_x = 2 * height;
    options.halo_y = height;
    EXPECT_FALSE(run(options, 2 * height, height));
    options.halo_x = options.halo_y = -1;
    options.cell_halo               = std::pair{11 * height, 0.};
    hhds::GraphLibrary invalid;
    EXPECT_THROW(place(*graph, invalid, options), std::invalid_argument);
    options.cell_halo            = std::pair{0., 0.};
    options.final_cells          = true;
    options.optimization_pending = false;
    EXPECT_FALSE(run(options, 0, 0));
    rapidjson::Document flow;
    std::ifstream       file(options.output_dir + "/flow.json");
    std::stringstream   text;
    text << file.rdbuf();
    flow.Parse(text.str().c_str());
    ASSERT_TRUE(flow.HasMember("macro_halo_um"));
    EXPECT_DOUBLE_EQ(flow["macro_halo_um"][0].GetDouble(), 10 * height);
    EXPECT_DOUBLE_EQ(flow["macro_halo_um"][1].GetDouble(), 10 * height);
  }
}
TEST(Placement, MacroHaloSpacingPinsCacheAndEco) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "halos";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "ram.lef") << R"(VERSION 5.8 ;
MACRO halo_ram
 CLASS BLOCK ; SIZE 3.17 BY 5.19 ; SYMMETRY X Y ;
 PIN A
  DIRECTION INPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 0 0.1 0.1 0.2 ; END
 END A
 PIN Z
  DIRECTION OUTPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 3.07 0.1 3.17 0.2 ; END
 END Z
END halo_ram
END LIBRARY
)";
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("halo_top");
  io->add_input("a", 0);
  io->add_output("y", 0);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  auto ram = source.create_io("halo_ram");
  ram->add_input("A", 0);
  ram->add_output("Z", 0);
  auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  for (int i = 0; i < 4; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(std::string(i < 2 ? "ram" : "inv") + std::to_string(i));
    node.set_subnode(i < 2 ? ram : inv);
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin(i < 2 ? "Z" : "Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto o           = plain_options();
  o.lefs           = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (dir / "ram.lef").string()};
  o.output_dir     = (dir / "out").string();
  o.cache_dir      = (dir / "cache").string();
  o.utilization    = .03;
  o.iterations     = 20;
  o.bins           = 8;
  o.halo_x         = 1.1;
  o.halo_y         = .75;
  const auto tech  = physical::read_lef(o.lefs);
  const auto check = [&](const Result& r) {
    EXPECT_NO_THROW(check_legality(tech, r.floorplan, o.halo_x, o.halo_y));
    for (const auto& a : r.floorplan.components) {
      if (a.macro != "halo_ram") {
        continue;
      }
      EXPECT_EQ(a.place.width, 3170);
      EXPECT_EQ(a.place.height, 5190);
      const auto pin = r.pin_locations.at(a.name + ":Z");
      EXPECT_EQ(pin.first, a.place.x + 3120);
      EXPECT_EQ(pin.second, a.place.y + 150);
      for (const auto& b : r.floorplan.components) {
        if (a.name == b.name) {
          continue;
        }
        const int x = 1380 * (b.macro == "halo_ram" ? 2 : 1);
        const int y = 2720 * (b.macro == "halo_ram" ? 2 : 1);
        EXPECT_TRUE(a.place.x + a.place.width + x <= b.place.x || b.place.x + b.place.width + x <= a.place.x
                    || a.place.y + a.place.height + y <= b.place.y || b.place.y + b.place.height + y <= a.place.y);
      }
    }
  };
  const auto placed = place(*graph, flat, o);
  check(placed);
  EXPECT_NO_THROW(physical::verify_def_roundtrip(placed.floorplan, physical::read_def((dir / "out/out.def").string())));
  rapidjson::Document qor;
  qor.Parse(placed.qor.c_str());
  EXPECT_EQ(qor["halo"]["macros"].GetUint64(), 2);
  EXPECT_EQ(qor["halo"]["x_dbu"].GetInt64(), 1380);
  EXPECT_EQ(qor["halo"]["y_dbu"].GetInt64(), 2720);
  hhds::GraphLibrary warm_library;
  const auto         warm = place(*graph, warm_library, o);
  EXPECT_NE(warm.qor.find("\"hit\":true"), std::string::npos);
  EXPECT_EQ(placed.floorplan.components, warm.floorplan.components);
  // Changing clearance must invalidate reuse, even though mapping is unchanged.
  o.halo_x = 2.1;
  hhds::GraphLibrary changed_library;
  const auto         changed = place(*graph, changed_library, o);
  EXPECT_NE(changed.qor.find("\"hit\":false"), std::string::npos);
  EXPECT_NO_THROW(check_legality(tech, changed.floorplan, o.halo_x, o.halo_y));
  // Move a standard cell into only a macro's halo (no physical overlap).
  auto  illegal = placed.floorplan;
  auto& a   = *std::find_if(illegal.components.begin(), illegal.components.end(), [](const auto& c) { return c.name == "ram0"; });
  auto& b   = *std::find_if(illegal.components.begin(), illegal.components.end(), [](const auto& c) { return c.name == "inv2"; });
  b.place.x = a.place.x - 1380;
  b.place.y = a.place.y;
  b.place.orientation = illegal.rows.at((b.place.y - illegal.rows.front().y) / 2720).orientation;
  EXPECT_NO_THROW(check_legality(tech, illegal));
  EXPECT_THROW(check_legality(tech, illegal, 1.1, .75), std::runtime_error);
  o.previous = std::make_shared<physical::Floorplan>(illegal);
  o.cache_dir.clear();
  o.halo_x         = 1.1;
  o.legalize_cells = true;
  o.changed_cells  = {"inv2"};
  hhds::GraphLibrary eco_library;
  const auto         eco = place(*graph, eco_library, o);
  check(eco);
}
TEST(Placement, EcoBufferRemainsInSoftMaskChannel) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "soft_mask_eco";
  std::filesystem::create_directories(root);
  std::ofstream(root / "masks.lef") << R"(VERSION 5.8 ;
MACRO hard_mask
 CLASS BLOCK ; SIZE 3.68 BY 10.88 ;
END hard_mask
MACRO soft_mask
 CLASS BLOCK ; SIZE 5.52 BY 10.88 ;
END soft_mask
END LIBRARY
)";
  hhds::GraphLibrary source, placed;
  auto               io = source.create_io("mask_eco");
  io->add_input("a", 0);
  io->add_output("y", 0);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto buf   = source.create_io("sky130_fd_sc_hd__buf_1");
  buf->add_input("A", 0);
  buf->add_output("X", 0);
  physical::Floorplan frame;
  frame.design = "mask_eco";
  frame.dbu    = 1000;
  frame.die    = {0, 0, 12880, 10880};
  for (int row = 0; row < 4; ++row) {
    frame.rows.push_back({"row" + std::to_string(row),
                          "unithd",
                          0,
                          row * 2720,
                          28,
                          460,
                          row % 2 ? physical::Orientation::FS : physical::Orientation::N});
  }
  for (const auto& [name, master, x, width] : std::vector<std::tuple<std::string, std::string, int, int>>{
           {"__lhd_frame_mask_hard_0", "hard_mask",    0, 3680},
           {"__lhd_frame_mask_soft_0", "soft_mask", 3680, 5520},
           {"__lhd_frame_mask_hard_1", "hard_mask", 9200, 3680}
  }) {
    auto type = source.find_io(master);
    if (!type) {
      type = source.create_io(master);
    }
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(type);
    n.set_name(name);
    Ann_place pose(x, 0, width, 10880);
    pose.status = physical::Status::FIXED;
    n.attr(attrs::place).set(pose);
    frame.components.push_back({name, master, pose});
  }
  auto buffer = gu::create_typed_node(*graph, Ntype_op::Sub);
  buffer.set_subnode(buf);
  buffer.set_name("new_repeater");
  auto input = graph->get_input_pin("a");
  gu::set_bits(input, 1);
  buffer.create_sink_pin("A").connect_driver(input);
  auto output = buffer.create_driver_pin("X");
  gu::set_bits(output, 1);
  graph->get_output_pin("y").connect_driver(output);
  Ann_place seed(4600, 0, 1840, 2720);
  seed.status = physical::Status::PLACED;
  buffer.attr(attrs::place).set(seed);
  auto o           = plain_options();
  o.lefs           = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (root / "masks.lef").string()};
  o.output_dir     = (root / "placed").string();
  o.iterations     = 0;
  o.bins           = 2;
  o.previous       = std::make_shared<physical::Floorplan>(frame);
  o.legalize_cells = true;
  o.changed_cells  = {"new_repeater"};
  const auto result = place(*graph, placed, o);
  const auto tech   = physical::read_lef(o.lefs);
  EXPECT_NO_THROW(check_legality(tech, result.floorplan));
  const auto found = std::find_if(result.floorplan.components.begin(), result.floorplan.components.end(), [](const auto& c) {
    return c.name == "new_repeater";
  });
  ASSERT_NE(found, result.floorplan.components.end());
  EXPECT_EQ(found->place.x, seed.x);
  EXPECT_EQ(found->place.y, seed.y);
  EXPECT_NE(result.qor.find("\"single_cell\":true"), std::string::npos);
  for (const auto& old : frame.components) {
    const auto current = std::find_if(result.floorplan.components.begin(), result.floorplan.components.end(), [&](const auto& c) {
      return c.name == old.name;
    });
    ASSERT_NE(current, result.floorplan.components.end());
    EXPECT_EQ(*current, old);
  }
  auto hard = result.floorplan;
  for (auto& c : hard.components) {
    if (c.name == "__lhd_frame_mask_soft_0") {
      c.name = "__lhd_frame_mask_hard_2";
    }
  }
  EXPECT_THROW(check_legality(tech, hard), std::runtime_error);
}

TEST(Placement, MultiCellEcoGapSearchMatchesExhaustiveSites) {
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("eco_gap_search");
  io->add_input("a", 0);
  io->add_output("y", 0);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  physical::Floorplan frame;
  frame.design         = "eco_gap_search";
  frame.dbu            = 1000;
  constexpr int64_t sw = 460, rh = 2720, width = 3 * sw;
  frame.die           = {0, 0, 30 * sw, 6 * rh};
  const auto add_cell = [&](const std::string& name, const Ann_place& p) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_name(name);
    n.set_subnode(inv);
    n.attr(attrs::place).set(p);
    n.create_sink_pin("A").connect_driver(signal);
    signal = n.create_driver_pin("Y");
    gu::set_bits(signal, 1);
  };
  std::vector<Ann_place> obstacles;
  for (int row = 0; row < 6; ++row) {
    const auto orientation = row % 2 ? Ann_place::Orientation::FS : Ann_place::Orientation::N;
    frame.rows.push_back({"R" + std::to_string(row), "unithd", 0, row * rh, 30, sw, orientation});
    for (int column : {0, 8, 16}) {
      const auto name = std::format("old_{}_{}", row, column);
      Ann_place  p(column * sw, row * rh, width, rh);
      p.orientation = orientation;
      p.status      = Ann_place::Status::PLACED;
      add_cell(name, p);
      frame.components.push_back({name, "sky130_fd_sc_hd__inv_1", p});
      obstacles.push_back(p);
    }
  }
  std::map<std::string, Ann_place> expected;
  for (int i = 0; i < 7; ++i) {
    const auto name = "new_" + std::to_string(i);
    Ann_place  seed(8 * sw, 2 * rh + (i % 2 ? rh / 2 : 0), width, rh);
    seed.status = Ann_place::Status::PLACED;
    add_cell(name, seed);
    // Independent oracle: enumerate every legal site, rather than gaps.
    double    best = std::numeric_limits<double>::infinity();
    Ann_place chosen;
    for (const auto& row : frame.rows) {
      for (int64_t x = row.x; x + width <= row.x + row.count * sw; x += sw) {
        if (std::any_of(obstacles.begin(), obstacles.end(), [&](const auto& p) {
              return x < p.x + p.width && x + width > p.x && row.y < p.y + p.height && row.y + rh > p.y;
            })) {
          continue;
        }
        const double distance = std::abs(double(x - seed.x)) + std::abs(double(row.y - seed.y));
        if (distance < best) {
          best               = distance;
          chosen             = seed;
          chosen.x           = x;
          chosen.y           = row.y;
          chosen.orientation = row.orientation;
        }
      }
    }
    ASSERT_TRUE(std::isfinite(best));
    expected.emplace(name, chosen);
    obstacles.push_back(chosen);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options           = plain_options();
  options.lefs           = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir     = std::string(std::getenv("TEST_TMPDIR")) + "/eco_gap_search";
  options.previous       = std::make_shared<physical::Floorplan>(frame);
  options.legalize_cells = true;
  options.iterations     = 10;
  options.bins           = 8;
  const auto result      = place(*graph, flat, options);
  EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), result.floorplan));
  EXPECT_NE(result.qor.find("\"single_cell\":true"), std::string::npos);
  rapidjson::Document untraced_qor;
  untraced_qor.Parse(result.qor.c_str());
  ASSERT_FALSE(untraced_qor.HasParseError());
  EXPECT_FALSE(untraced_qor["eco"].HasMember("trace"));
  // Trace only exact edited names; unchanged and nonexistent names produce no
  // entries. Observation must preserve the whole deterministic legalization.
  options.eco_trace_cells  = {"new_0", "new_4", "old_0_0", "missing"};
  options.output_dir      += "_traced";
  hhds::GraphLibrary traced_library;
  const auto         traced = place(*graph, traced_library, options);
  EXPECT_EQ(traced.floorplan.components, result.floorplan.components);
  EXPECT_EQ(traced.floorplan.terminals, result.floorplan.terminals);
  rapidjson::Document traced_qor;
  traced_qor.Parse(traced.qor.c_str());
  ASSERT_FALSE(traced_qor.HasParseError());
  const auto& trace = traced_qor["eco"]["trace"];
  ASSERT_TRUE(trace.IsArray());
  ASSERT_EQ(trace.Size(), 2);
  for (rapidjson::SizeType i = 0; i < trace.Size(); ++i) {
    const int   n     = i == 0 ? 0 : 4;
    const auto  name  = "new_" + std::to_string(n);
    const auto& entry = trace[i];
    EXPECT_EQ(entry["name"].GetString(), name);
    EXPECT_STREQ(entry["master"].GetString(), "sky130_fd_sc_hd__inv_1");
    EXPECT_EQ(entry["edit_order"].GetUint64(), static_cast<uint64_t>(n));
    EXPECT_EQ(entry["seed_dbu"][0].GetInt64(), 8 * sw);
    EXPECT_EQ(entry["seed_dbu"][1].GetInt64(), 2 * rh);
    const auto& chosen = expected.at(name);
    EXPECT_EQ(entry["chosen_dbu"][0].GetInt64(), chosen.x);
    EXPECT_EQ(entry["chosen_dbu"][1].GetInt64(), chosen.y);
    EXPECT_EQ(entry["row_index"].GetUint64(), static_cast<uint64_t>(chosen.y / rh));
    EXPECT_EQ(entry["row_y_dbu"].GetInt64(), chosen.y);
    EXPECT_LE(entry["gap_dbu"][0].GetInt64(), chosen.x);
    EXPECT_GE(entry["gap_dbu"][1].GetInt64(), chosen.x + width);
    EXPECT_DOUBLE_EQ(entry["displacement_um"].GetDouble(),
                     (std::abs(double(chosen.x - 8 * sw)) + std::abs(double(chosen.y - 2 * rh))) / 1000);
  }
  for (const auto& c : result.floorplan.components) {
    if (const auto it = expected.find(c.name); it != expected.end()) {
      EXPECT_EQ(c.place, it->second) << c.name;
    } else {
      const auto original
          = std::find_if(frame.components.begin(), frame.components.end(), [&](const auto& p) { return p.name == c.name; });
      ASSERT_NE(original, frame.components.end());
      EXPECT_EQ(c.place, original->place) << c.name;
    }
  }
}

TEST(Placement, EcoConnectivityMatchesExhaustivePinHpwlAndKeepsOtherGeometry) {
  const auto directory = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "eco_connectivity";
  std::filesystem::create_directories(directory);
  const auto lef = (directory / "cells.lef").string();
  std::ofstream(lef) << R"(VERSION 5.8 ;
MACRO eco_buf
 CLASS CORE ; ORIGIN 0 0 ; SIZE 1.38 BY 2.72 ; SYMMETRY X Y ; SITE unithd ;
 PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER li1 ; RECT .09 .19 .11 .21 ; END END A
 PIN B DIRECTION INPUT ; USE SIGNAL ; PORT LAYER li1 ; RECT .99 1.99 1.01 2.01 ; END END B
 PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER li1 ; RECT 1.19 .39 1.21 .41 ; END END Y
END eco_buf
MACRO eco_clock
 CLASS CORE ; ORIGIN 0 0 ; SIZE 1.38 BY 2.72 ; SYMMETRY X Y ; SITE unithd ;
 PIN C DIRECTION INPUT ; USE CLOCK ; PORT LAYER li1 ; RECT .09 .19 .11 .21 ; END END C
END eco_clock
MACRO eco_block
 CLASS BLOCK ; ORIGIN 0 0 ; SIZE 36.8 BY 272 ; SYMMETRY X Y ;
END eco_block
END LIBRARY
)";
  auto options           = plain_options();
  options.lefs           = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", lef};
  options.iterations     = 0;
  options.output_dir     = (directory / "default").string();
  options.legalize_cells = true;
  options.net_weights    = {
      {"new_1:Y", 3}
  };
  const auto          tech = physical::read_lef(options.lefs);
  constexpr int64_t   sw = 460, rh = 2720, width = 3 * sw;
  physical::Floorplan frame;
  frame.design = "eco_cost";
  frame.dbu    = 1000;
  frame.die    = {0, 0, 100 * sw, 100 * rh};
  for (int row = 0; row < 100; ++row) {
    frame.rows.push_back({"r" + std::to_string(row),
                          "unithd",
                          0,
                          row * rh,
                          100,
                          sw,
                          row % 2 ? Ann_place::Orientation::FS : Ann_place::Orientation::N});
  }
  frame.terminals = {
      {  "a",   "a",  "INPUT", 0,       0, Ann_place::Orientation::N, Ann_place::Status::FIXED, {}},
      {"clk", "clk",  "INPUT", 0, 10 * rh, Ann_place::Orientation::N, Ann_place::Status::FIXED, {}},
      {  "y",   "y", "OUTPUT", 0, 25 * rh, Ann_place::Orientation::N, Ann_place::Status::FIXED, {}}
  };
  hhds::GraphLibrary source, default_library;
  auto               io = source.create_io("eco_cost");
  io->add_input("a", 0);
  io->add_input("clk", 1);
  io->add_output("y", 0);
  for (const auto* port : {"a", "clk", "y"}) {
    io->set_bits(port, 1);
  }
  auto graph   = io->create_graph();
  auto cell_io = source.create_io("eco_buf");
  cell_io->add_input("A", 0);
  cell_io->add_input("B", 1);
  cell_io->add_output("Y", 0);
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  std::map<std::string, Ann_place> seeds;
  const auto                       add = [&](const std::string& name, Ann_place p) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name(name);
    node.set_subnode(cell_io);
    node.attr(attrs::place).set(p);
    // Two pins on this cell share a net: excluding just one is incorrect.
    node.create_sink_pin("A").connect_driver(signal);
    node.create_sink_pin("B").connect_driver(signal);
    signal = node.create_driver_pin("Y");
    gu::set_bits(signal, 1);
    seeds.emplace(name, p);
  };
  Ann_place old(0, 0, width, rh);
  old.status = Ann_place::Status::PLACED;
  add("old", old);
  frame.components.push_back({"old", "eco_buf", old});
  for (int i = 0; i < 6; ++i) {
    Ann_place seed((i % 2 ? 50 : 47) * sw, (20 + i) * rh + (i % 2 ? rh / 2 : 0), width, rh);
    seed.status = Ann_place::Status::PLACED;
    add("new_" + std::to_string(i), seed);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto block_io = source.create_io("eco_block");
  auto block    = gu::create_typed_node(*graph, Ntype_op::Sub);
  block.set_subnode(block_io);
  block.set_name("block");
  Ann_place obstacle(10 * sw, 0, 80 * sw, 100 * rh);
  obstacle.status = Ann_place::Status::FIXED;
  block.attr(attrs::place).set(obstacle);
  frame.components.push_back({"block", "eco_block", obstacle});
  auto clock_io = source.create_io("eco_clock");
  clock_io->add_input("C", 0);
  auto clock = gu::create_typed_node(*graph, Ntype_op::Sub);
  clock.set_subnode(clock_io);
  clock.set_name("new_clock");
  Ann_place clock_seed(sw / 2, 10 * rh, width, rh);
  clock_seed.status = Ann_place::Status::PLACED;
  clock.attr(attrs::place).set(clock_seed);
  auto clk = graph->get_input_pin("clk");
  gu::set_bits(clk, 1);
  clock.create_sink_pin("C").connect_driver(clk);
  options.previous                = std::make_shared<physical::Floorplan>(frame);
  const auto default_result       = place(*graph, default_library, options);
  options.eco_connectivity_weight = 0;
  options.output_dir              = (directory / "zero").string();
  hhds::GraphLibrary zero_library;
  const auto         zero = place(*graph, zero_library, options);
  EXPECT_EQ(default_result.floorplan.components, zero.floorplan.components);
  EXPECT_EQ(default_result.floorplan.terminals, zero.floorplan.terminals);
  rapidjson::Document zero_qor;
  zero_qor.Parse(zero.qor.c_str());
  EXPECT_FALSE(zero_qor["eco"].HasMember("connectivity"));

  // Independent oracle enumerates every legal row/site and computes complete
  // pin HPWL, including candidate row reflection and both same-cell inputs.
  using End = std::pair<std::string, std::string>;
  std::vector<std::vector<End>> nets;
  for (int i = 0; i < 6; ++i) {
    const auto name = "new_" + std::to_string(i);
    nets.push_back({
        {i ? "new_" + std::to_string(i - 1) : "old", "Y"},
        {                                      name, "A"},
        {                                      name, "B"}
    });
  }
  nets.push_back({
      {"new_5", "Y"},
      {  "PIN", "y"}
  });
  const auto pin_hpwl = [&](const auto& net, const auto& poses) {
    double xl = std::numeric_limits<double>::infinity(), yl = xl, xh = -xl, yh = -xl;
    for (const auto& [name, pin] : net) {
      double x = 0, y = 25 * rh;
      if (name != "PIN") {
        const auto& p  = poses.at(name);
        const auto& m  = tech.macros.at("eco_buf");
        const auto& r  = m.pins.at(pin).shapes.front().rect;
        const auto  xy = physical::orient_point((r.xl + r.xh) / 2, (r.yl + r.yh) / 2, m.width, m.height, p.orientation);
        x              = p.x + xy.first * 1000;
        y              = p.y + xy.second * 1000;
      }
      xl = std::min(xl, x);
      xh = std::max(xh, x);
      yl = std::min(yl, y);
      yh = std::max(yh, y);
    }
    return (xh - xl + yh - yl) * (net.front().first == "new_1" ? 3 : 1);
  };
  options.eco_connectivity_weight = 2;
  options.output_dir              = (directory / "connected").string();
  hhds::GraphLibrary connected_library;
  const auto         connected = place(*graph, connected_library, options);
  EXPECT_NO_THROW(check_legality(tech, connected.floorplan));
  std::map<std::string, Ann_place> zero_poses, connected_poses;
  for (const auto& c : zero.floorplan.components) {
    zero_poses.emplace(c.name, c.place);
  }
  for (const auto& c : connected.floorplan.components) {
    connected_poses.emplace(c.name, c.place);
    if (!c.name.starts_with("new_") || c.name == "new_clock") {
      EXPECT_EQ(c.place, zero_poses.at(c.name)) << c.name;
    }
  }
  auto                   poses = seeds;
  std::vector<Ann_place> occupied{old, obstacle};
  for (int i = 0; i < 6; ++i) {
    const auto name = "new_" + std::to_string(i);
    const auto seed = seeds.at(name);
    double     best = std::numeric_limits<double>::infinity();
    for (const auto& row : frame.rows) {
      for (int64_t x = 0; x + width <= 100 * sw; x += sw) {
        if (std::any_of(occupied.begin(), occupied.end(), [&](const auto& p) {
              return x < p.x + p.width && x + width > p.x && row.y < p.y + p.height && row.y + rh > p.y;
            })) {
          continue;
        }
        auto candidate        = seed;
        candidate.x           = x;
        candidate.y           = row.y;
        candidate.orientation = row.orientation;
        poses[name]           = candidate;
        double cost           = std::abs(double(x - seed.x)) + std::abs(double(row.y - seed.y));
        for (const auto& net : nets) {
          if (std::any_of(net.begin(), net.end(), [&](const auto& p) { return p.first == name; })) {
            cost += 2 * pin_hpwl(net, poses);
          }
        }
        best = std::min(best, cost);
      }
    }
    ASSERT_TRUE(std::isfinite(best));
    // Several sites can share the same convex minimum. Check the primary
    // objective using actual prior choices instead of prescribing a tie site.
    const auto actual  = connected_poses.at(name);
    poses[name]        = actual;
    double actual_cost = std::abs(double(actual.x - seed.x)) + std::abs(double(actual.y - seed.y));
    for (const auto& net : nets) {
      if (std::any_of(net.begin(), net.end(), [&](const auto& p) { return p.first == name; })) {
        actual_cost += 2 * pin_hpwl(net, poses);
      }
    }
    EXPECT_NEAR(actual_cost, best, 1e-7) << name;
    occupied.push_back(actual);
  }
  // No active HPWL term: retain legacy round-half-up, rather than floor.
  EXPECT_EQ(connected_poses.at("new_clock").x, sw);
  EXPECT_EQ(connected_poses.at("new_clock").y, 10 * rh);
  EXPECT_EQ(connected.floorplan.terminals, zero.floorplan.terminals);
  double before = 0, after = 0;
  for (const auto& net : nets) {
    before += pin_hpwl(net, zero_poses);
    after  += pin_hpwl(net, connected_poses);
  }
  EXPECT_LT(after, before / 2);
  rapidjson::Document qor;
  qor.Parse(connected.qor.c_str());
  const auto& telemetry = qor["eco"]["connectivity"];
  EXPECT_EQ(telemetry["nets"].GetUint64(), 7);
  EXPECT_EQ(telemetry["dirty_pins"].GetUint64(), 18);
  EXPECT_GT(telemetry["candidate_sites"].GetUint64(), 0);
}

TEST(Placement, LegalityRejectsOverlapAndOffGrid) {
  auto                tech = physical::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  physical::Floorplan fp;
  fp.rows.push_back({"row", "unithd", 0, 0, 20, 460});
  Ann_place p(0, 0, 1380, 2720);
  p.status = Ann_place::Status::PLACED;
  fp.components.push_back({"a", "sky130_fd_sc_hd__inv_1", p});
  EXPECT_NO_THROW(check_legality(tech, fp));
  fp.components.push_back({"b", "sky130_fd_sc_hd__inv_1", p});
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.components.pop_back();
  fp.components[0].place.x = 1;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.components[0].place.x = 0;
  fp.rows.push_back({"row1", "unithd", 0, 2720, 20, 460, Ann_place::Orientation::FS});
  auto upper        = p;
  upper.y           = 2720;
  upper.orientation = Ann_place::Orientation::FS;
  fp.components.push_back({"c", "sky130_fd_sc_hd__inv_1", upper});  // same x, next row
  EXPECT_NO_THROW(check_legality(tech, fp));
  upper.x = 920;  // two sites into c
  fp.components.push_back({"d", "sky130_fd_sc_hd__inv_1", upper});
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
}

TEST(Placement, HardBlockageRowIndexKeepsExactVerticalGeometryAndNestedIntervals) {
  physical::Technology tech;
  tech.dbu               = 1;
  tech.sites["s"]        = {1, 10, true};
  tech.macros["m"].klass = "BLOCK";
  physical::Floorplan fp;
  fp.rows.push_back({"row", "s", -20, -10, 40, 1});
  Ann_place pose(-5, -9, 2, 2);
  pose.status = Ann_place::Status::PLACED;
  fp.components.push_back({"a", "m", pose});
  // A common row band does not imply overlap for an unaligned macro body.
  fp.blockages = {
      {false, {}, {{-5, -5, -3, -1}}}
  };
  EXPECT_NO_THROW(check_legality(tech, fp));
  // The nearest interval ends before the cell; an earlier enclosing interval
  // must still be found by the prefix maximum of all preceding right edges.
  fp.blockages.front().rectangles = {
      {-15, -10, -4, -8},
      {-10, -10, -9, -8}
  };
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.blockages.front().soft = true;
  EXPECT_NO_THROW(check_legality(tech, fp));
  fp.blockages.front().soft          = false;
  fp.components.front().place.status = Ann_place::Status::FIXED;
  EXPECT_NO_THROW(check_legality(tech, fp));
}

TEST(Placement, IncrementalWindowKeepsDistantBlocksAndSeedsNewCells) {
  hhds::GraphLibrary source;
  auto               io = source.create_io("eco");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  for (unsigned i = 0; i < 4; ++i) {
    io->add_output("y" + std::to_string(i), i);
    io->set_bits("y" + std::to_string(i), 1);
  }
  auto graph = io->create_graph();
  gu::set_bits(graph->get_input_pin("a"), 1);
  auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  physical::Floorplan frame;
  frame.design = "eco";
  frame.dbu    = 1000;
  frame.die    = {0, 0, 46000, 32640};
  for (int row = 0; row < 12; ++row) {
    frame.rows.push_back({"R" + std::to_string(row), "unithd", 0, row * 2720, 100, 460, Ann_place::Orientation::N});
  }
  std::vector<hhds::Pin_class> outputs;
  for (unsigned i = 0; i < 4; ++i) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(inv);
    n.set_name("n" + std::to_string(i));
    n.attr(attrs::color).set(i + 1);
    n.attr(attrs::placement_block).set("block" + std::to_string(i));
    Ann_place p(i ? i * 12000 : 1380, i ? i * 8160 : 0, 1380, 2720);
    p.status = Ann_place::Status::PLACED;
    // x must be a whole number of sites.
    p.x      = p.x / 460 * 460;
    n.attr(attrs::place).set(p);
    frame.components.push_back({std::string(n.get_name()), "sky130_fd_sc_hd__inv_1", p});
    n.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
    auto out = n.create_driver_pin("Y");
    gu::set_bits(out, 1);
    graph->get_output_pin("y" + std::to_string(i)).connect_driver(out);
    outputs.push_back(out);
  }
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/eco";
  options.cache_dir  = options.output_dir + "/cache";
  options.previous   = std::make_shared<physical::Floorplan>(frame);
  options.iterations = 10;
  options.bins       = 8;
  hhds::GraphLibrary initial_lib;
  const auto         initial = place(*graph, initial_lib, options);
  physical::write_def(initial.floorplan, options.output_dir + "/initial.def");
  EXPECT_EQ(initial.floorplan.components, frame.components) << initial.qor;
  auto inserted = gu::create_typed_node(*graph, Ntype_op::Sub);
  inserted.set_subnode(inv);
  inserted.set_name("inserted");
  inserted.attr(attrs::color).set(1);
  inserted.attr(attrs::placement_block).set("block0");
  inserted.create_sink_pin("A").connect_driver(outputs.front());
  auto out = inserted.create_driver_pin("Y");
  gu::set_bits(out, 1);
  gu::drop_drivers(graph->get_output_pin("y0"));
  graph->get_output_pin("y0").connect_driver(out);
  options.previous.reset();
  hhds::GraphLibrary updated_lib;
  const auto         updated = place(*graph, updated_lib, options);
  EXPECT_EQ(updated.floorplan.die, initial.floorplan.die);
  ASSERT_EQ(updated.floorplan.terminals.size(), initial.floorplan.terminals.size());
  for (size_t i = 0; i < initial.floorplan.terminals.size(); ++i) {
    const auto& before = initial.floorplan.terminals[i];
    const auto& after  = updated.floorplan.terminals[i];
    EXPECT_EQ(after.name, before.name);
    EXPECT_EQ(after.x, before.x);
    EXPECT_EQ(after.y, before.y);
    EXPECT_EQ(after.shapes, before.shapes);
    EXPECT_EQ(after.orientation, before.orientation);
    EXPECT_EQ(after.status, before.status);
  }
  for (const auto& c : updated.floorplan.components) {
    if (c.name == "n0" || c.name == "inserted") {
      continue;
    }
    const auto before = std::find_if(initial.floorplan.components.begin(), initial.floorplan.components.end(), [&](const auto& p) {
      return p.name == c.name;
    });
    ASSERT_NE(before, initial.floorplan.components.end());
    EXPECT_EQ(c.place, before->place);
  }
  rapidjson::Document json;
  json.Parse(updated.qor.c_str());
  EXPECT_TRUE(json["eco"]["used"].GetBool());
  EXPECT_EQ(json["incremental"]["reused_blocks"].GetUint(), 3);
  EXPECT_LT(json["eco"]["window_dbu"][2].GetDouble(), frame.die.xh);
  EXPECT_LE(json["eco"]["moved_cells"].GetUint(), 2);
}
TEST(Placement, IncrementalNewMemoryUnlocksCachedCellsUnderItsProxy) {
  // A memory added to a cached design packs its FIXED proxy at the lower-left
  // first fit, over cache-locked cells. The whole-core ECO window must release
  // the cache locks instead of failing with overlapping cells.
  hhds::GraphLibrary source;
  auto               io = source.create_io("eco_mem");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  gu::set_bits(graph->get_input_pin("a"), 1);
  auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto chain = graph->get_input_pin("a");
  for (int i = 0; i < 24; ++i) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(inv);
    n.set_name("c" + std::to_string(i));
    n.attr(attrs::placement_block).set("chain");
    n.create_sink_pin("A").connect_driver(chain);
    chain = n.create_driver_pin("Y");
    gu::set_bits(chain, 1);
  }
  graph->get_output_pin("y").connect_driver(chain);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.liberty    = "inou/lefdef/tests/data/sky130hd.lib";
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/eco_mem";
  options.cache_dir  = options.output_dir + "/cache";
  options.iterations = 20;
  options.bins       = 8;
  hhds::GraphLibrary initial_lib;
  const auto         initial = place(*graph, initial_lib, options);
  auto               mem     = gu::create_typed_node(*graph, Ntype_op::Memory);
  mem.set_name("ram");
  mem.attr(attrs::placement_block).set("ram");
  for (const unsigned pid : {1, 9, 6, 4, 7, 10}) {  // 1-bit x 1-entry read port, enable on
    mem.create_sink_pin(pid).connect_driver(gu::create_const(*graph, *Dlop::create_integer(1)));
  }
  mem.create_sink_pin(0).connect_driver(graph->get_input_pin("a"));
  mem.create_sink_pin(2).connect_driver(graph->get_input_pin("a"));
  hhds::GraphLibrary  updated_lib;
  const auto          updated = place(*graph, updated_lib, options);
  rapidjson::Document json;
  json.Parse(updated.qor.c_str());
  EXPECT_TRUE(json["eco"]["used"].GetBool()) << updated.qor;
  // The released cells may move, so none of the blocks counts as reused.
  EXPECT_TRUE(json["eco"]["released_locks"].GetBool()) << updated.qor;
  EXPECT_EQ(json["incremental"]["reused_blocks"].GetUint(), 0) << updated.qor;
  EXPECT_TRUE(std::any_of(updated.floorplan.components.begin(), updated.floorplan.components.end(), [](const auto& c) {
    return c.name == "ram";
  }));
  EXPECT_EQ(updated.floorplan.components.size(), initial.floorplan.components.size() + 2);  // ram + its enable tie
}
TEST(Placement, ZeroWireTimingIsIndependentOfPinGeometry) {
  std::vector<std::pair<std::string, std::pair<double, double>>> pins;
  for (int i = 0; i < 1600; ++i) {
    pins.push_back({
        "pin" + std::to_string(i),
        {double(i), double(i % 7)}
    });
  }
  bool       approximate = false;
  const auto before      = steiner_rc("pin700", pins, Wire_rc{0, 0, 0, 0, "ideal"}, 1, &approximate);
  for (auto& pin : pins) {
    pin.second = {0, 0};
  }
  const auto after = steiner_rc("pin700", pins, Wire_rc{0, 0, 0, 0, "ideal"}, 1, &approximate);
  EXPECT_FALSE(approximate);
  ASSERT_EQ(before.nodes.size(), pins.size() + 1);
  ASSERT_EQ(before.nodes.size(), after.nodes.size());
  ASSERT_EQ(before.edges.size(), pins.size());
  ASSERT_EQ(before.edges.size(), after.edges.size());
  EXPECT_EQ(before.wire_length_um, 0);
  for (size_t i = 0; i < before.nodes.size(); ++i) {
    EXPECT_EQ(before.nodes[i].pin, after.nodes[i].pin);
    EXPECT_EQ(before.nodes[i].capacitance_ff, 0);
  }
  for (size_t i = 0; i < before.edges.size(); ++i) {
    EXPECT_EQ(before.edges[i].from, after.edges[i].from);
    EXPECT_EQ(before.edges[i].to, after.edges[i].to);
    EXPECT_EQ(before.edges[i].resistance_ohm, 0);
    EXPECT_EQ(after.edges[i].resistance_ohm, 0);
  }
}
TEST(Placement, HighFanoutSteinerRcIsConnectedAcyclicAndChargesOnlyRetainedWire) {
  // Independent graph invariants: spatial FLUTE groups and their connector
  // can share several geometric junctions, so deduplicating edges is not
  // sufficient to make their union a tree.
  for (const auto& [width, height] : {
           std::pair{  32, 40},
           std::pair{  61, 18},
           std::pair{ 400,  4},
           std::pair{1201,  1},
           std::pair{ 512,  2}
  }) {
    const bool dense_visibility = width == 512;
    SCOPED_TRACE(std::format("{}x{}", width, height));
    std::vector<std::pair<std::string, std::pair<double, double>>> pins;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        pins.push_back({
            "pin" + std::to_string(pins.size()),
            {double((x + (dense_visibility && y == 0 ? 512 : 0)) * 10), double(y * 10)}
        });
      }
    }
    bool       approximate = false;
    const auto rc          = steiner_rc(pins.front().first, pins, Wire_rc{1, 2, 1, 2, "test"}, 1, &approximate);
    // A very wide, shallow grid also exposes many peers across its rows.
    EXPECT_EQ(approximate, dense_visibility || width == 400);
    ASSERT_EQ(rc.edges.size() + 1, rc.nodes.size());
    std::vector<unsigned> parent(rc.nodes.size()), degree(rc.nodes.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto root = [&](unsigned n) {
      while (parent[n] != n) {
        n = parent[n];
      }
      return n;
    };
    double wire_resistance = 0, wire_capacitance = 0;
    for (const auto& edge : rc.edges) {
      ASSERT_LT(edge.from, rc.nodes.size());
      ASSERT_LT(edge.to, rc.nodes.size());
      const auto a = root(edge.from), b = root(edge.to);
      ASSERT_NE(a, b) << "cycle at " << edge.from << " -> " << edge.to;
      parent[a] = b;
      ++degree[edge.from];
      ++degree[edge.to];
      if (edge.from >= pins.size() && edge.to >= pins.size()) {
        wire_resistance += edge.resistance_ohm;
      }
    }
    for (size_t i = 0; i < rc.nodes.size(); ++i) {
      EXPECT_EQ(root(i), root(0));
      wire_capacitance += rc.nodes[i].capacitance_ff;
      if (i < pins.size()) {
        EXPECT_EQ(rc.nodes[i].pin, pins[i].first);
        EXPECT_EQ(degree[i], 1);
        EXPECT_EQ(rc.nodes[i].capacitance_ff, 0);
      }
    }
    EXPECT_NEAR(wire_capacitance, 2 * wire_resistance, 1e-8 * wire_capacitance);
    EXPECT_NEAR(rc.wire_length_um, wire_capacitance / 2, 1e-8 * wire_capacitance);
  }
}
TEST(Placement, HierarchicalSteinerSharedJunctionCycleRegression) {
  std::mt19937                                                   rng(17);
  std::vector<std::pair<std::string, std::pair<double, double>>> pins;
  for (int sample = 0; sample <= 14; ++sample) {
    const auto count = sample == 0 ? 1501u : 1001 + rng() % 2000;
    pins.clear();
    for (unsigned i = 0; i < count; ++i) {
      const double x = sample == 0 ? 1000 * i : 1000 * (rng() % 200);
      const double y = sample == 0 ? 1000 * (i % 3) : 1000 * (rng() % 200);
      pins.push_back({
          "pin" + std::to_string(i),
          {x, y}
      });
    }
  }
  ASSERT_EQ(pins.size(), 2771);
  bool       approximate = false;
  const auto rc          = steiner_rc("pin0", pins, Wire_rc{1, 2, 1, 2, "test"}, 1000, &approximate);
  EXPECT_FALSE(approximate);
  // This placement formerly produced a cycle in the spatial group union.
  // The driver-aware tree must retain connectivity and charge accounting.
  ASSERT_EQ(rc.edges.size() + 1, rc.nodes.size());
  std::vector<unsigned> parents(rc.nodes.size()), degree(rc.nodes.size());
  std::iota(parents.begin(), parents.end(), 0);
  const auto root = [&](unsigned n) {
    while (parents[n] != n) {
      n = parents[n];
    }
    return n;
  };
  double resistance = 0, capacitance = 0;
  for (const auto& edge : rc.edges) {
    const auto a = root(edge.from), b = root(edge.to);
    ASSERT_NE(a, b);
    parents[a] = b;
    ++degree[edge.from];
    ++degree[edge.to];
    if (edge.from >= pins.size() && edge.to >= pins.size()) {
      resistance += edge.resistance_ohm;
    }
  }
  for (size_t i = 0; i < rc.nodes.size(); ++i) {
    EXPECT_EQ(root(i), root(0));
    capacitance += rc.nodes[i].capacitance_ff;
    if (i < pins.size()) {
      EXPECT_EQ(degree[i], 1);
      EXPECT_EQ(rc.nodes[i].pin, pins[i].first);
    }
  }
  EXPECT_NEAR(capacitance, 2 * resistance, 1e-8 * capacitance);
}
TEST(Placement, HighFanoutDriverAwareRcRemainsATree) {
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("top");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  gu::set_bits(graph->get_input_pin("a"), 1);
  auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->set_bits("A", 1);
  inv->add_output("Y", 0);
  inv->set_bits("Y", 1);
  auto root = gu::create_typed_node(*graph, Ntype_op::Sub);
  root.set_subnode(inv);
  root.set_name("root");
  root.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
  auto driver = root.create_driver_pin("Y");
  gu::set_bits(driver, 1);
  for (int i = 0; i < 1001; ++i) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(inv);
    n.set_name("leaf_" + std::to_string(i));
    n.create_sink_pin("A").connect_driver(driver);
    if (i == 0) {
      auto out = n.create_driver_pin("Y");
      gu::set_bits(out, 1);
      graph->get_output_pin("y").connect_driver(out);
    }
  }
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.iterations = 0;
  options.bins       = 8;
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/fanout";
  const auto result  = place(*graph, flat, options);
  EXPECT_FALSE(result.parasitics->approximate);
  bool high_fanout = false;
  for (const auto& net : result.parasitics->nets) {
    EXPECT_EQ(net.edges.size() + 1, net.nodes.size());
    high_fanout |= net.nodes.size() > 2000;
  }
  EXPECT_TRUE(high_fanout);
}
TEST(Placement, MemoryProxyPinsReachImportedTracksWithoutWidening) {
  physical::Technology tech;
  tech.dbu                   = 1000;
  tech.sites["core"]         = {0.054, 0.270, true};
  tech.layers["V"].direction = "VERTICAL";
  tech.layers["V"].width     = 0.018;
  tech.layers["V"].pitch_x = tech.layers["V"].pitch_y = 0.036;
  tech.layers["H"].direction                          = "HORIZONTAL";
  tech.layers["H"].width                              = 0.018;
  tech.layers["H"].pitch_x = tech.layers["H"].pitch_y = 0.036;
  auto& macro                                         = tech.macros["memory"];
  macro.klass                                         = "BLOCK";
  macro.virtual_proxy                                 = true;
  macro.width = macro.height = 3.361;
  for (int i = 0; i < 4; ++i) {
    const double center           = macro.width * (i + 1) / 5;
    macro.pins[std::to_string(i)] = {"OUTPUT", "SIGNAL", {{"V", {center - .009, 0, center + .009, .018}}}};
  }
  macro.pins["left"]  = {"INPUT", "SIGNAL", {{"H", {0, .364, .018, .382}}}};
  macro.pins["left2"] = {"INPUT", "SIGNAL", {{"H", {0, 1.364, .018, 1.382}}}};
  macro.pins["left3"] = {"INPUT", "SIGNAL", {{"H", {0, 2.364, .018, 2.382}}}};
  macro.pins["right"] = {"INPUT", "SIGNAL", {{"H", {3.343, .831, 3.361, .849}}}};
  physical::Floorplan fp;
  fp.dbu = tech.dbu;
  fp.die = {0, 0, 10800, 8100};
  for (int i = 0; i < 30; ++i) {
    fp.rows.push_back({std::to_string(i), "core", 0, i * 270, 200, 54, physical::Orientation::N});
  }
  // Imported pitches intentionally differ from the LEF defaults.
  fp.tracks = {
      {"X", "V",    9, 270,   40},
      {"Y", "H",  200,   5, 2000},
      {"Y", "H", 1000,   5, 2000}
  };
  align_memory_proxy_pins(tech, fp);
  Ann_place p(0, 0, 3361, 3361);
  p.status = Ann_place::Status::PLACED;
  std::vector<physical::Component> cells{
      {"ram", "memory", p}
  };
  ASSERT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, true));
  EXPECT_DOUBLE_EQ(macro.width, 3.361);
  std::vector<double> data_centers;
  for (const auto& [name, pin] : macro.pins) {
    const auto& shape = pin.shapes.front();
    const auto& r     = shape.rect;
    EXPECT_NEAR(r.xh - r.xl, .018, 1e-12);
    EXPECT_NEAR(r.yh - r.yl, .018, 1e-12);
    EXPECT_GE(r.xl, 0);
    EXPECT_GE(r.yl, 0);
    EXPECT_LE(r.xh, macro.width);
    EXPECT_LE(r.yh, macro.height);
    const bool   x          = shape.layer == "V";
    const auto   origin     = x ? cells.front().place.x : cells.front().place.y;
    const double low        = origin + (x ? r.xl : r.yl) * tech.dbu;
    const double high       = origin + (x ? r.xh : r.yh) * tech.dbu;
    bool         accessible = false;
    for (const auto& pattern : fp.tracks) {
      if (pattern.layer != shape.layer || pattern.axis != (x ? "X" : "Y")) {
        continue;
      }
      const auto index  = std::llround(((low + high) / 2 - pattern.start) / pattern.step);
      const auto track  = pattern.start + index * pattern.step;
      accessible       |= index >= 0 && index < pattern.count && track > low && track < high;
    }
    EXPECT_TRUE(accessible) << name;
    if (x) {
      data_centers.push_back((r.xl + r.xh) / 2 * tech.dbu);
    }
  }
  std::sort(data_centers.begin(), data_centers.end());
  for (size_t i = 1; i < data_centers.size(); ++i) {
    EXPECT_GE(data_centers[i] - data_centers[i - 1], 40);
  }
  cells.front().place.status = Ann_place::Status::FIXED;
  EXPECT_NO_THROW(legalize_macro_tracks(tech, fp, cells, 0, 0, false));
  fp.tracks.front().step  = std::numeric_limits<int64_t>::max();
  fp.tracks.front().start = 1;
  EXPECT_THROW(align_memory_proxy_pins(tech, fp), std::runtime_error);
}

TEST(Placement, ThreePortMemorySquareGeometryAndClockBoundaryTiming) {
  hhds::GraphLibrary source;
  auto               io        = source.create_io("memory_top");
  unsigned           next_port = 0;
  for (const auto* name : {"clk", "wa", "ra", "rb", "din", "en"}) {
    io->add_input(name, next_port++);
    io->set_bits(name,
                 std::string_view(name) == "din"                                           ? 4
                 : std::string_view(name).ends_with("a") || std::string_view(name) == "rb" ? 3
                                                                                           : 1);
  }
  io->add_output("y", 0);
  io->set_bits("y", 4);
  io->add_output("z", 1);
  io->set_bits("z", 4);
  auto graph = io->create_graph();
  for (const auto& d : io->get_input_pin_decls()) {
    gu::set_bits(graph->get_input_pin(d.name), d.bits);
  }
  auto mem = gu::create_typed_node(*graph, Ntype_op::Memory);
  mem.set_name("ram");
  const auto constant = [&](unsigned pid, int value) {
    mem.create_sink_pin(pid).connect_driver(gu::create_const(*graph, *Dlop::create_integer(value)));
  };
  constant(1, 4);
  constant(9, 8);
  constant(6, 1);
  for (unsigned port = 0; port < 3; ++port) {
    const auto base = port * Ntype::Memory_port_stride;
    mem.create_sink_pin(base).connect_driver(graph->get_input_pin(port == 0 ? "wa" : port == 1 ? "ra" : "rb"));
    mem.create_sink_pin(base + 2).connect_driver(graph->get_input_pin("clk"));
    mem.create_sink_pin(base + 4).connect_driver(port == 1 ? gu::create_const(*graph, *Dlop::create_integer(1))
                                                           : graph->get_input_pin("en"));
    constant(base + 7, 1);
    constant(base + 10, port != 0);
  }
  mem.create_sink_pin(3).connect_driver(graph->get_input_pin("din"));
  auto q = mem.create_driver_pin(1);
  gu::set_bits(q, 4);
  graph->get_output_pin("y").connect_driver(q);
  auto r = mem.create_driver_pin(2);
  gu::set_bits(r, 4);
  graph->get_output_pin("z").connect_driver(r);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.liberty    = "inou/lefdef/tests/data/sky130hd.lib";
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/memory";
  options.iterations = 20;
  options.bins       = 8;
  hhds::GraphLibrary flat;
  auto               placed = place(*graph, flat, options);
  ASSERT_EQ(placed.floorplan.components.size(), 2);
  const auto found = std::find_if(placed.floorplan.components.begin(),
                                  placed.floorplan.components.end(),
                                  [](const auto& component) { return component.name == "ram"; });
  ASSERT_NE(found, placed.floorplan.components.end());
  const auto& c    = *found;
  const auto  flop = liberty::smallest_flop_area(options.liberty);
  ASSERT_TRUE(flop);
  const auto expected = std::ceil(std::sqrt(32 * *flop * 1.1 * 1.1 * 1.1) * placed.floorplan.dbu);
  EXPECT_EQ(c.place.width, expected);
  EXPECT_EQ(c.place.height, expected);
  auto lefs = options.lefs;
  lefs.push_back(options.output_dir + "/memory_proxies.lef");
  const auto  tech  = physical::read_lef(lefs);
  const auto& macro = tech.macros.at(c.macro);
  for (const auto& [name, pin] : macro.pins) {
    const auto& rect = pin.shapes.front().rect;
    if (name.starts_with("out_") || name.starts_with("in_3[")) {
      EXPECT_EQ(rect.yl, 0);
    }
    if (name.starts_with("in_0[") || name.starts_with("in_16[") || name.starts_with("in_32[")) {
      EXPECT_EQ(rect.xl, 0);
    }
  }
  // Public placed APIs regenerate proxy geometry even though its generated LEF
  // is not an external input. The original physical frame remains unchanged.
  auto frozen = placed;
  for (auto& component : frozen.floorplan.components) {
    if (component.name == "ram") {
      component.place.status = Ann_place::Status::FIXED;
    }
  }
  auto halo_options      = options;
  halo_options.cell_halo = std::pair{0., 0.};
  halo_options           = final_cell_options(halo_options);
  EXPECT_NO_THROW(check_placed_legality(frozen, halo_options));
  auto invalid_proxy = frozen;
  for (auto& component : invalid_proxy.floorplan.components) {
    if (component.name == "ram") {
      component.place.x = -1;
    }
  }
  EXPECT_THROW(check_placed_legality(invalid_proxy, halo_options), std::runtime_error);
  const auto recovered_proxy
      = repair_buffers(std::move(frozen), halo_options, [](const Result&) { return Timing_feedback{}; }, Repair_scope::forest);
  EXPECT_EQ(recovered_proxy.placed.floorplan.components.size(), placed.floorplan.components.size());
  std::ofstream(options.output_dir + "/clock.sdc") << "create_clock -name virtual -period 10\nset_input_delay -clock virtual 2 "
                                                      "[all_inputs]\nset_output_delay -clock virtual 0 [all_outputs]\n";
  Eprp_var var;
  var.graphs            = {placed.graph};
  var.physical_rc       = placed.parasitics;
  var.dict["files"]     = options.liberty + "," + options.output_dir + "/clock.sdc";
  var.dict["top"]       = "memory_top";
  var.dict["hier"]      = "false";
  var.dict["qor"]       = options.output_dir + "/timing.json";
  var.dict["cache_dir"] = options.output_dir + "/sta_cache";
  var.set_stage_labels(var.dict);
  ASSERT_NO_THROW(Pass_opentimer::time_work(var));
  std::ifstream       timing(options.output_dir + "/timing.json");
  std::string         contents{std::istreambuf_iterator<char>(timing), {}};
  rapidjson::Document json;
  json.Parse(contents.c_str());
  ASSERT_FALSE(json.HasParseError());
  const auto& design = json["designs"][0];
  ASSERT_TRUE(design.HasMember("memory_timing_model"));
  EXPECT_STREQ(design["memory_timing_model"].GetString(), "clock_boundary_zero_internal_delay");
  EXPECT_GT(design["parasitics"]["nets"].GetUint(), 0);
  ASSERT_NO_THROW(Pass_opentimer::time_work(var));  // warm replay must validate geometry too
  for (auto node : placed.graph->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Memory) {
      auto geometry = node.attr(attrs::place).get();
      ++geometry.width;
      node.attr(attrs::place).set(geometry);
    }
  }
  EXPECT_THROW(Pass_opentimer::time_work(var), std::runtime_error);
  // Native memory launches stay at 0 despite a 2 ns [all_inputs] delay; the
  // critical path starts at an input, so check the memory output pins.
  ASSERT_TRUE(design.HasMember("physical_pins"));
  unsigned mem_outs = 0;
  for (const auto& p : design["physical_pins"].GetArray()) {
    if (std::string_view(p["pin"].GetString()).starts_with("ram:out_")) {
      ++mem_outs;
      ASSERT_TRUE(p.HasMember("arrival"));
      EXPECT_EQ(p["arrival"].GetDouble(), 0);
    }
  }
  EXPECT_EQ(mem_outs, 8);
}
TEST(Placement, MemoryReadWiderThanPortExtends) {
  // y (8 bits) = unsigned 4-bit read: y[4..7] tie low. z (6 bits) = signed
  // 4-bit read: z[4..5] repeat the read MSB. No proxy pin past the width.
  hhds::GraphLibrary source;
  auto               io  = source.create_io("wide_read");
  unsigned           pid = 0;
  for (const auto* name : {"clk", "wa", "ra", "din"}) {
    io->add_input(name, pid++);
    io->set_bits(name, std::string_view(name) == "clk" ? 1 : std::string_view(name) == "din" ? 4 : 3);
  }
  io->add_output("y", 0);
  io->set_bits("y", 8);
  io->add_output("z", 1);
  io->set_bits("z", 6);
  auto graph = io->create_graph();
  for (const auto& d : io->get_input_pin_decls()) {
    gu::set_bits(graph->get_input_pin(d.name), d.bits);
  }
  auto mem = gu::create_typed_node(*graph, Ntype_op::Memory);
  mem.set_name("ram");
  const auto constant = [&](unsigned p, int value) {
    mem.create_sink_pin(p).connect_driver(gu::create_const(*graph, *Dlop::create_integer(value)));
  };
  constant(1, 4);
  constant(9, 8);
  constant(6, 1);
  for (unsigned port = 0; port < 3; ++port) {
    const auto base = port * Ntype::Memory_port_stride;
    mem.create_sink_pin(base).connect_driver(graph->get_input_pin(port == 0 ? "wa" : "ra"));
    mem.create_sink_pin(base + 2).connect_driver(graph->get_input_pin("clk"));
    constant(base + 4, 1);
    constant(base + 7, 1);
    constant(base + 10, port != 0);
  }
  mem.create_sink_pin(3).connect_driver(graph->get_input_pin("din"));
  auto q = mem.create_driver_pin(1);
  gu::set_bits(q, 4);
  graph->get_output_pin("y").connect_driver(q);
  auto r = mem.create_driver_pin(2);
  gu::set_bits(r, 4);
  gu::set_sign(r);
  graph->get_output_pin("z").connect_driver(r);
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.liberty    = "inou/lefdef/tests/data/sky130hd.lib";
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/wide_read";
  options.iterations = 20;
  options.bins       = 8;
  hhds::GraphLibrary                 flat;
  const auto                         placed = place(*graph, flat, options);
  std::map<std::string, std::string> net_of;
  for (const auto& t : placed.floorplan.terminals) {
    net_of[t.name] = t.net;
  }
  std::map<std::string, std::vector<std::string>> members;
  for (const auto& net : placed.floorplan.nets) {
    for (const auto& c : net.connections) {
      members[net.name].push_back(c.instance + ":" + c.pin);
    }
  }
  const auto drives = [&](const std::string& terminal, const std::string& pin) {
    const auto& m = members[net_of.at(terminal)];
    return std::find(m.begin(), m.end(), pin) != m.end();
  };
  for (int b = 0; b < 4; ++b) {
    EXPECT_TRUE(drives(std::format("y[{}]", b), std::format("ram:out_1[{}]", b)));
  }
  std::set<std::string> local_lows;
  for (int b = 4; b < 8; ++b) {
    const auto& net = members.at(net_of.at(std::format("y[{}]", b)));
    ASSERT_EQ(net.size(), 2);
    EXPECT_TRUE(net.front().starts_with("__place_tielo"));
    EXPECT_TRUE(net.front().ends_with(":LO"));
    local_lows.insert(net.front());
  }
  EXPECT_EQ(local_lows.size(), 4);
  EXPECT_EQ(net_of.at("z[4]"), net_of.at("z[3]"));
  EXPECT_EQ(net_of.at("z[5]"), net_of.at("z[3]"));
  EXPECT_TRUE(drives("z[5]", "ram:out_2[3]"));
}
TEST(Placement, H2dHierarchyHaloAndMacroCache) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "h2d_hierarchy";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "ram.lef") << R"(VERSION 5.8 ;
MACRO h2d_ram
 CLASS BLOCK ; SIZE 3.17 BY 5.19 ; SYMMETRY X Y ;
 PIN A
  DIRECTION INPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 0 0.1 0.1 0.2 ; END
 END A
 PIN Z
  DIRECTION OUTPUT ; USE SIGNAL ;
  PORT LAYER met1 ; RECT 3.07 0.1 3.17 0.2 ; END
 END Z
END h2d_ram
END LIBRARY
)";
  hhds::GraphLibrary source;
  auto               ram = source.create_io("h2d_ram");
  ram->add_input("A", 0);
  ram->add_output("Z", 0);
  ram->set_bits("A", 1);
  ram->set_bits("Z", 1);
  auto branch = source.create_io("branch");
  branch->add_input("a", 0);
  branch->add_output("z", 0);
  branch->set_bits("a", 1);
  branch->set_bits("z", 1);
  auto body   = branch->create_graph();
  auto signal = body->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (int i = 0; i < 2; ++i) {
    auto node = gu::create_typed_node(*body, Ntype_op::Sub);
    node.set_name("ram" + std::to_string(i));
    node.set_subnode(ram);
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin("Z");
    gu::set_bits(signal, 1);
  }
  body->get_output_pin("z").connect_driver(signal);
  auto top = source.create_io("h2d_top");
  top->add_input("a", 0);
  top->add_output("z", 0);
  top->set_bits("a", 1);
  top->set_bits("z", 1);
  auto graph = top->create_graph();
  signal     = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (int i = 0; i < 2; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_name("u" + std::to_string(i));
    node.set_subnode(branch);
    node.create_sink_pin("a").connect_driver(signal);
    signal = node.create_driver_pin("z");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("z").connect_driver(signal);
  auto options        = plain_options();
  options.lefs        = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (dir / "ram.lef").string()};
  options.liberty     = "inou/lefdef/tests/data/sky130hd.lib";
  options.output_dir  = (dir / "out").string();
  options.cache_dir   = (dir / "cache").string();
  options.utilization = .03;
  options.density     = .5;
  options.iterations  = 10;
  options.bins        = 8;
  options.halo_x      = 1;
  options.halo_y      = 1;
  options.macro_mode  = "h2d";
  options.macro_parameters.K               = 2;
  options.macro_parameters.iters_per_outer = 5;
  options.macro_parameters.bins            = 8;
  options.macro_parameters.evals           = 4;
  options.macro_parameters.seed_sa_evals   = 4;
  hhds::GraphLibrary flat;
  const auto         first = place(*graph, flat, options);
  auto               tech  = physical::read_lef(options.lefs);
  EXPECT_NO_THROW(check_legality(tech, first.floorplan, 1, 1));
  ASSERT_EQ(first.floorplan.components.size(), 4);
  for (const auto& c : first.floorplan.components) {
    EXPECT_EQ(c.place.status, physical::Status::FIXED);
    EXPECT_EQ(c.place.width, 3170);
    EXPECT_EQ(c.place.height, 5190);
  }
  auto read = [&](const char* name) {
    std::ifstream in(options.output_dir + "/" + name);
    return std::string{std::istreambuf_iterator<char>(in), {}};
  };
  rapidjson::Document report;
  report.Parse(read("macro_topology.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_FALSE(report["cache_hit"].GetBool());
  ASSERT_EQ(report["groups"].Size(), 2);
  for (const auto& g : report["groups"].GetArray()) {
    EXPECT_EQ(g["members"].Size(), 2);
  }
  EXPECT_TRUE(std::filesystem::is_regular_file(options.output_dir + "/h2d.png"));
  hhds::GraphLibrary warm_flat;
  auto               warm = place(*graph, warm_flat, options);
  EXPECT_EQ(first.floorplan.components, warm.floorplan.components);
  report.Parse(read("macro_topology.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_TRUE(report["cache_hit"].GetBool());
  options.macro_parameters.seed += 1;
  hhds::GraphLibrary changed;
  auto               rerun = place(*graph, changed, options);
  (void)rerun;
  report.Parse(read("macro_topology.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_FALSE(report["cache_hit"].GetBool());
  // Auto's active estimator is part of the restored macro report, including
  // when a macros-only final pass has no movable cells to run GP on.
  options.route_model                        = "auto";
  options.macro_parameters.routability       = true;
  options.macro_route.policy.switch_overflow = 1000000;
  options.macro_parameters.iters_per_outer   = 25;
  options.cache_dir                          = (dir / "auto_cache").string();
  hhds::GraphLibrary auto_flat;
  const auto         auto_first = place(*graph, auto_flat, options);
  report.Parse(read("macro_topology.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  ASSERT_STREQ(report["route_model"].GetString(), "rsmt");
  rapidjson::Document route_options;
  route_options.Parse(read("route_options.json").c_str());
  ASSERT_FALSE(route_options.HasParseError());
  EXPECT_TRUE(route_options["stages"]["macro"]["feedback_enabled"].GetBool());
  EXPECT_FALSE(route_options["stages"]["final"]["feedback_enabled"].GetBool());
  const auto         route_overflow = report["rudy_overflow"].GetDouble();
  const auto         merit          = report["merit"].GetDouble();
  hhds::GraphLibrary auto_warm_flat;
  const auto         auto_warm = place(*graph, auto_warm_flat, options);
  EXPECT_EQ(auto_first.floorplan.components, auto_warm.floorplan.components);
  report.Parse(read("macro_topology.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_TRUE(report["cache_hit"].GetBool());
  EXPECT_STREQ(report["route_model"].GetString(), "rsmt");
  EXPECT_DOUBLE_EQ(report["rudy_overflow"].GetDouble(), route_overflow);
  EXPECT_DOUBLE_EQ(report["merit"].GetDouble(), merit);
  // A Rudy probe can also latch RSMT. Both the cold report and restored
  // topology must score that active model, not the requested Rudy policy.
  options.route_model                        = "rudy";
  options.macro_route.policy.probe           = true;
  options.macro_route.policy.target_overflow = 0;
  options.macro_parameters.iters_per_outer   = 100;
  options.macro_parameters.route_cap_h       = 1e-8;
  options.macro_parameters.route_cap_v       = 1e-8;
  options.density                            = .9;
  options.cache_dir                          = (dir / "probe_cache").string();
  hhds::GraphLibrary probe_flat;
  const auto         probe_first = place(*graph, probe_flat, options);
  report.Parse(read("macro_topology.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_FALSE(report["cache_hit"].GetBool());
  ASSERT_STREQ(report["route_model"].GetString(), "rsmt");
  const auto         probe_overflow = report["rudy_overflow"].GetDouble();
  const auto         probe_merit    = report["merit"].GetDouble();
  hhds::GraphLibrary probe_warm_flat;
  const auto         probe_warm = place(*graph, probe_warm_flat, options);
  EXPECT_EQ(probe_first.floorplan.components, probe_warm.floorplan.components);
  report.Parse(read("macro_topology.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  EXPECT_TRUE(report["cache_hit"].GetBool());
  EXPECT_STREQ(report["route_model"].GetString(), "rsmt");
  EXPECT_DOUBLE_EQ(report["rudy_overflow"].GetDouble(), probe_overflow);
  EXPECT_DOUBLE_EQ(report["merit"].GetDouble(), probe_merit);
  options.macro_route.policy.probe           = false;
  options.macro_route.policy.target_overflow = 1e-9;
  options.macro_parameters.iters_per_outer   = 25;
  options.macro_parameters.route_cap_h       = 0;
  options.macro_parameters.route_cap_v       = 0;
  options.density                            = .5;
  options.route_model                        = "rudy";
  options.macro_parameters.routability       = false;
  // The plain flow keeps each hierarchy instance's color separate even when
  // both instances use the same SRAM master and local color number.
  for (auto node : body->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Sub) {
      node.attr(livehd::attrs::synth_color_group).set(std::string("bank:color:1"));
    }
  }
  options.macro_mode         = "none";
  options.cache_dir          = (dir / "plain_cache").string();
  options.macro_align_weight = 25;
  options.macro_align_sigma  = .25;
  options.macro_align_start  = 0;
  options.macro_align_full   = 20;
  options.macro_snap         = true;
  options.iterations         = 40;
  options.io_iterations      = 10;
  hhds::GraphLibrary aligned_flat;
  const auto         aligned = place(*graph, aligned_flat, options);
  EXPECT_NO_THROW(check_legality(tech, aligned.floorplan, 1, 1));
  report.Parse(read("alignment.json").c_str());
  ASSERT_FALSE(report.HasParseError());
  ASSERT_EQ(report["macros"].Size(), 4);
  std::map<int, unsigned>    classes;
  std::map<int, std::string> colors;
  for (const auto& macro : report["macros"].GetArray()) {
    const int id = macro["class"].GetInt();
    ASSERT_GE(id, 0);
    ++classes[id];
    colors[id] = macro["color"].GetString();
  }
  ASSERT_EQ(classes.size(), 2);
  for (const auto& [id, count] : classes) {
    (void)id;
    EXPECT_EQ(count, 2);
  }
  EXPECT_NE(colors.begin()->second, colors.rbegin()->second);
  // Global GP alignment groups the same master across colors and hierarchy,
  // while the end-of-GP snap still retains its original hierarchy classes.
  options.macro_align_global = true;
  hhds::GraphLibrary global_flat;
  const auto         global = place(*graph, global_flat, options);
  EXPECT_NO_THROW(check_legality(tech, global.floorplan, 1, 1));
  report.Parse(read("alignment.json").c_str());
  ASSERT_TRUE(report["global_same_master"].GetBool());
  classes.clear();
  for (const auto& macro : report["macros"].GetArray()) {
    ++classes[macro["class"].GetInt()];
  }
  ASSERT_EQ(classes.size(), 1);
  EXPECT_EQ(classes.begin()->second, 4);
}

TEST(Placement, PlacementColorsUseLibertyRolesAndPinsMoveWithoutSta) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "fp_color_adapter";
  std::filesystem::create_directories(dir);
  {
    std::ofstream lef(dir / "ram.lef");
    lef << "VERSION 5.8 ;\nMACRO RAM\n CLASS BLOCK ; SIZE 5 BY 5 ; SYMMETRY X Y ;\n";
    for (const auto* pin : {"P", "X", "Y", "E", "Z"}) {
      lef << " PIN " << pin << "\n DIRECTION " << (std::string(pin) == "Z" ? "OUTPUT" : "INPUT")
          << " ; USE SIGNAL ;\n PORT LAYER met1 ; RECT 0 0.1 0.1 0.2 ; END\n END " << pin << '\n';
    }
    lef << "END RAM\nEND LIBRARY\n";
    std::ofstream lib(dir / "ram.lib");
    lib << R"(library(ram) { cell(RAM) {
      memory() { type: ram; address_width: 1; word_width: 1; }
      pin(P) { direction: input; clock: true; }
      pin(X) { direction: input; }
      pin(Y) { direction: input; memory_write() { address: X; } }
      pin(E) { direction: input; }
      pin(Z) { direction: output; memory_read() { address: X; } }
    } })";
    std::ifstream cells("inou/lefdef/tests/data/sky130hd.lib");
    lib << cells.rdbuf();
  }
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("fp_colors");
  io->add_input("clk", 1);
  io->set_bits("clk", 1);
  io->add_input("in", 0);
  io->set_bits("in", 1);
  for (int i = 0; i < 2; ++i) {
    io->add_output("out" + std::to_string(i), i);
    io->set_bits("out" + std::to_string(i), 1);
  }
  auto graph = io->create_graph();
  auto input = graph->get_input_pin("in");
  gu::set_bits(input, 1);
  auto inv_io = source.create_io("sky130_fd_sc_hd__inv_1");
  inv_io->add_input("A", 0);
  inv_io->add_output("Y", 0);
  auto ram_io = source.create_io("RAM");
  for (const auto* pin : {"P", "X", "Y", "E"}) {
    ram_io->add_input(pin, ram_io->get_input_pin_decls().size());
  }
  ram_io->add_output("Z", 0);
  const auto inverter = [&](const std::string& name, hhds::Pin_class from) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(inv_io);
    n.set_name(name);
    n.attr(attrs::synth_color_group).set("synthesis-kept");
    n.create_sink_pin("A").connect_driver(from);
    auto output = n.create_driver_pin("Y");
    gu::set_bits(output, 1);
    return output;
  };
  // The clock must have a distinct source; a data branch of the same physical
  // buffered clock is intentionally excluded from all placement edges.
  auto clock_input = graph->get_input_pin("clk");
  gu::set_bits(clock_input, 1);
  const auto clock = inverter("clock_logic", clock_input), enable = inverter("enable_logic", input);
  const auto constant_data
      = inverter("constant_buffer2", inverter("constant_buffer1", gu::create_const(*graph, *Dlop::create_integer(0))));
  for (int i = 0; i < 2; ++i) {
    const auto name = "ram" + std::to_string(i);
    auto       pre  = inverter(name + "_input", input);
    auto       ram  = gu::create_typed_node(*graph, Ntype_op::Sub);
    ram.set_subnode(ram_io);
    ram.set_name(name);
    ram.attr(attrs::synth_color_group).set("synthesis-kept");
    ram.create_sink_pin("P").connect_driver(clock);
    ram.create_sink_pin("E").connect_driver(enable);
    ram.create_sink_pin("X").connect_driver(pre);
    ram.create_sink_pin("Y").connect_driver(constant_data);
    auto q = ram.create_driver_pin("Z");
    gu::set_bits(q, 1);
    auto post = inverter(name + "_output", q);
    graph->get_output_pin("out" + std::to_string(i)).connect_driver(post);
  }
  auto o       = plain_options();
  o.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (dir / "ram.lef").string()};
  o.liberty    = (dir / "ram.lib").string();
  o.output_dir = (dir / "out").string();
  o.fp_place = o.fp_colors = o.fp_pin_adjust = true;
  o.macro_pin_adjust                         = true;
  o.macro_cell_density_weight                = 2;
  o.macro_density_pull_weight                = .1;
  o.local_density_bins                       = 32;
  o.macro_pin_interval                       = 5;
  for (const auto* color_source : {"placement", "memory_first"}) {
    o.fp_anchor_macros_only = true;
    o.fp_color_source       = color_source;
    o.fp_color_minimum      = 0;
    o.fp_iterations         = 40;
    o.iterations            = 20;
    o.io_iterations         = 10;
    o.fp_pin_interval       = 5;
    o.bins                  = 8;
    o.utilization           = .05;
    hhds::GraphLibrary run_flat;
    const auto         result = place(*graph, run_flat, o);
    EXPECT_NO_THROW(check_legality(physical::read_lef(o.lefs), result.floorplan));
    const auto read_json = [&](const std::string& name) {
      std::ifstream       file(dir / "out" / name);
      const std::string   text{std::istreambuf_iterator<char>(file), {}};
      rapidjson::Document doc;
      doc.Parse(text.c_str());
      return doc;
    };
    auto colors = read_json("fp_placement_colors.json");
    ASSERT_FALSE(colors.HasParseError());
    ASSERT_EQ(colors["colors"].Size(), 2);
    for (const auto& c : colors["colors"].GetArray()) {
      EXPECT_EQ(c["macros"].GetInt(), 1);
      EXPECT_EQ(c["members"].GetUint64(), o.fp_color_source == "memory_first" ? 2 : 3);
    }
    auto report = read_json("fp_place.json");
    ASSERT_FALSE(report.HasParseError());
    EXPECT_TRUE(report["distance_attraction"].GetBool());
    EXPECT_TRUE(report["fixed_anchor_macros_only"].GetBool());
    EXPECT_EQ(report["attraction_outside"].GetDouble(), 4);
    EXPECT_GT(report["fp_attraction_coefficient"].GetDouble(), 0);
    EXPECT_GT(report["macro_attraction_coefficient"].GetDouble(), 0);
    EXPECT_EQ(report["macro_attraction_calibration_iteration"].GetInt(), 0);
    EXPECT_GE(report["pin_updates"].GetUint64(), 2);
    EXPECT_GE(report["macro_pin_updates"].GetUint64(), 1);
    EXPECT_GT(report["fp_iterations"].GetInt(), 0);
    EXPECT_EQ(report["fp_macro_cell_density_seconds"].GetDouble(), 0);
    EXPECT_EQ(report["fp_macro_cell_density_force"].GetDouble(), 0);
    EXPECT_EQ(report["fp_macro_density_pull_seconds"].GetDouble(), 0);
    EXPECT_EQ(report["fp_macro_density_pull_force"].GetDouble(), 0);
    auto macro_history = read_json("gp_history.json");
    ASSERT_FALSE(macro_history.HasParseError());
    EXPECT_EQ(macro_history["macro_cell_density_weight"].GetDouble(), o.macro_cell_density_weight);
    EXPECT_EQ(macro_history["macro_cell_density_reach"].GetDouble(), 2);
    EXPECT_GT(macro_history["macro_cell_density_screening_length_dbu"].GetDouble(), 0);
    EXPECT_GT(macro_history["macro_cell_density_coefficient"].GetDouble(), 0);
    EXPECT_GT(macro_history["macro_cell_density_seconds"].GetDouble(), 0);
    EXPECT_GT(macro_history["macro_density_pull_seconds"].GetDouble(), 0);
    for (auto node : graph->body().nodes()) {
      if (auto a = node.attr(attrs::synth_color_group); a.has()) {
        EXPECT_EQ(a.get(), "synthesis-kept");
      }
    }
    for (const auto& t : result.floorplan.terminals) {
      const auto it = std::find_if(result.parasitics->terminals.begin(), result.parasitics->terminals.end(), [&](const auto& p) {
        return p.name == t.name;
      });
      ASSERT_NE(it, result.parasitics->terminals.end());
      EXPECT_EQ(it->x, t.x);
      EXPECT_EQ(it->y, t.y);
    }
  }
  // Exercise both stage controls independently without a timing callback.
  std::optional<physical::Floorplan> off_frame;
  for (unsigned mask : {0U, 1U, 2U, 3U, 0U}) {
    auto warped               = o;
    warped.fp_warp.enabled    = mask & 1;
    warped.macro_warp.enabled = mask & 2;
    warped.fp_warp.bins = warped.macro_warp.bins = 4;
    warped.fp_warp.iterations = warped.macro_warp.iterations = 1;
    warped.macro_warp.updates                                = 2;
    warped.fp_snap_enabled                                   = false;  // shifted unsnapped targets must survive handoff
    warped.output_dir                                        = (dir / ("warp_" + std::to_string(mask))).string();
    hhds::GraphLibrary warped_flat;
    const auto         result = place(*graph, warped_flat, warped);
    EXPECT_NO_THROW(check_legality(physical::read_lef(warped.lefs), result.floorplan));
    EXPECT_EQ(result.floorplan.nets.size(), result.parasitics->nets.size());
    EXPECT_EQ(std::filesystem::exists(warped.output_dir + "/fp_warp_field.json"), bool(mask & 1));
    EXPECT_EQ(std::filesystem::exists(warped.output_dir + "/macro_warp_summary.json"), bool(mask & 2));
    if (mask & 1) {
      std::ifstream targets(warped.output_dir + "/fp_warp_targets.tsv");
      std::string   line;
      std::getline(targets, line);
      while (std::getline(targets, line)) {
        std::istringstream row(line);
        std::string        name;
        double             bx, by, wx, wy, sx, sy;
        ASSERT_TRUE(bool(row >> name >> bx >> by >> wx >> wy >> sx >> sy));
        EXPECT_DOUBLE_EQ(wx, sx);
        EXPECT_DOUBLE_EQ(wy, sy);
      }
    }

    if (!mask) {
      if (!off_frame) {
        off_frame = result.floorplan;
      } else {
        ASSERT_EQ(off_frame->components.size(), result.floorplan.components.size());
        for (size_t n = 0; n < off_frame->components.size(); ++n) {
          EXPECT_EQ(off_frame->components[n].place, result.floorplan.components[n].place);
        }
      }
    }
  }
  // Automatic IO uses its plain preview, independently of macro GP pressure.
  {
    auto io_options                      = o;
    io_options.fp_place                  = false;
    io_options.macro_pin_adjust          = false;
    io_options.output_dir                = (dir / "io_without_pressure").string();
    io_options.macro_cell_density_weight = io_options.macro_density_pull_weight = 0;
    hhds::GraphLibrary plain_flat;
    const auto         plain_io          = place(*graph, plain_flat, io_options);
    io_options.output_dir                = (dir / "io_with_pressure").string();
    io_options.macro_cell_density_weight = o.macro_cell_density_weight;
    io_options.macro_density_pull_weight = o.macro_density_pull_weight;
    hhds::GraphLibrary pressure_flat;
    const auto         pressure_io = place(*graph, pressure_flat, io_options);
    ASSERT_EQ(plain_io.floorplan.terminals.size(), pressure_io.floorplan.terminals.size());
    for (size_t i = 0; i < plain_io.floorplan.terminals.size(); ++i) {
      const auto& plain    = plain_io.floorplan.terminals[i];
      const auto& pressure = pressure_io.floorplan.terminals[i];
      EXPECT_EQ(plain.name, pressure.name);
      EXPECT_EQ(plain.x, pressure.x);
      EXPECT_EQ(plain.y, pressure.y);
    }
  }
  // Without FP, macro GP still owns IO movement. A fixed macro/IO handoff
  // suppresses that callback in the final cell solve, even with the flag set.
  o.fp_place = o.fp_colors = o.fp_pin_adjust = false;
  o.output_dir                               = (dir / "macro_only").string();
  o.floorplan                                = (dir / "out/out.def").string();
  const auto macro_only                      = place(*graph, flat, o);
  EXPECT_TRUE(std::filesystem::exists(dir / "macro_only/macro_pins.jsonl"));
  EXPECT_FALSE(std::filesystem::exists(dir / "macro_only/fp_pins.jsonl"));
  auto       frame = macro_only.floorplan;
  const auto tech  = physical::read_lef(o.lefs);
  for (auto& c : frame.components) {
    if (!tech.macros.at(c.macro).klass.starts_with("CORE")) {
      c.place.status = physical::Status::FIXED;
    }
  }
  for (auto& t : frame.terminals) {
    t.status = physical::Status::FIXED;
  }
  o.floorplan = (dir / "fixed.def").string();
  physical::write_def(frame, o.floorplan);
  o.output_dir = (dir / "final").string();
  hhds::GraphLibrary final_flat;
  const auto         final = place(*graph, final_flat, o);
  EXPECT_FALSE(std::filesystem::exists(dir / "final/macro_pins.jsonl"));
  {
    std::ifstream       history_file(dir / "final/gp_history.json");
    const std::string   text{std::istreambuf_iterator<char>(history_file), {}};
    rapidjson::Document report;
    report.Parse(text.c_str());
    ASSERT_FALSE(report.HasParseError());
    EXPECT_EQ(report["macro_cell_density_weight"].GetDouble(), 0);
    EXPECT_EQ(report["macro_cell_density_seconds"].GetDouble(), 0);
    EXPECT_EQ(report["macro_density_pull_weight"].GetDouble(), 0);
    EXPECT_EQ(report["macro_density_pull_seconds"].GetDouble(), 0);
    EXPECT_EQ(report["local_density_grid_seconds"].GetDouble(), 0);
  }
  for (const auto& t : final.floorplan.terminals) {
    const auto it = std::find_if(frame.terminals.begin(), frame.terminals.end(), [&](const auto& p) { return p.name == t.name; });
    ASSERT_NE(it, frame.terminals.end());
    EXPECT_EQ(t.x, it->x);
    EXPECT_EQ(t.y, it->y);
    EXPECT_EQ(t.status, physical::Status::FIXED);
  }
  o.floorplan.clear();
  o.fp_place = o.fp_colors = true;
  o.analyze_colors_only    = true;
  o.final_cells            = false;
  o.fp_evaluate            = [](const Result&) -> Timing_feedback { throw std::runtime_error("analysis invoked STA"); };
  o.output_dir             = (dir / "analysis").string();
  hhds::GraphLibrary analysis_flat;
  const auto         analysis = place(*graph, analysis_flat, o);
  EXPECT_TRUE(std::filesystem::exists(dir / "analysis/fp_placement_colors.json"));
  EXPECT_FALSE(std::filesystem::exists(dir / "analysis/fp_gp.json"));
  EXPECT_FALSE(std::filesystem::exists(dir / "analysis/gp_history.json"));
  EXPECT_GE(analysis.floorplan.components.size(), 6);
}
TEST(Placement, ProxyMemoryColorsExcludeSharedClockAndResetLogic) {
  hhds::GraphLibrary source, flat;
  const auto         io = source.create_io("proxy_colors");
  io->add_input("in", 0);
  io->set_bits("in", 1);
  for (unsigned i = 0; i < 2; ++i) {
    io->add_output("out" + std::to_string(i), i);
    io->set_bits("out" + std::to_string(i), 1);
  }
  auto graph = io->create_graph();
  auto input = graph->get_input_pin("in");
  gu::set_bits(input, 1);
  const auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  const auto control = [&](const char* name) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(inv);
    n.set_name(name);
    n.create_sink_pin("A").connect_driver(input);
    auto pin = n.create_driver_pin("Y");
    gu::set_bits(pin, 1);
    return pin;
  };
  const auto clock = control("shared_clock"), reset = control("shared_reset");
  for (unsigned i = 0; i < 2; ++i) {
    auto mem = gu::create_typed_node(*graph, Ntype_op::Memory);
    mem.set_name("ram" + std::to_string(i));
    for (auto [pid, value] : std::vector<std::pair<unsigned, int>>{
             { 1, 1},
             { 9, 2},
             { 4, 1},
             { 7, 1},
             {10, 1}
    }) {
      mem.create_sink_pin(pid).connect_driver(gu::create_const(*graph, *Dlop::create_integer(value)));
    }
    mem.create_sink_pin(0).connect_driver(input);
    mem.create_sink_pin(2).connect_driver(clock);
    mem.create_sink_pin(14).connect_driver(reset);
    auto q = mem.create_driver_pin(0);
    gu::set_bits(q, 1);
    graph->get_output_pin("out" + std::to_string(i)).connect_driver(q);
  }
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.liberty    = "inou/lefdef/tests/data/sky130hd.lib";
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/proxy_colors";
  options.fp_place = options.fp_colors = true;
  options.fp_color_source              = "placement";
  options.fp_color_minimum             = 0;
  options.bins                         = 8;
  options.fp_iterations = options.iterations = options.io_iterations = 20;
  options.utilization                                                = .05;
  const auto          placed                                         = place(*graph, flat, options);
  std::ifstream       input_report(options.output_dir + "/fp_placement_colors.json");
  const std::string   text{std::istreambuf_iterator<char>(input_report), {}};
  rapidjson::Document report;
  report.Parse(text.c_str());
  ASSERT_FALSE(report.HasParseError());
  ASSERT_EQ(report["colors"].Size(), 2);
  for (const auto& c : report["colors"].GetArray()) {
    EXPECT_EQ(c["members"].GetUint64(), 1);
    EXPECT_EQ(c["macros"].GetInt(), 1);
  }
  EXPECT_EQ(placed.floorplan.components.size(), 6);  // Two macros, two controls, and one enable tie per macro.
  EXPECT_EQ(std::count_if(placed.floorplan.components.begin(),
                          placed.floorplan.components.end(),
                          [](const auto& c) { return c.macro == "sky130_fd_sc_hd__inv_1"; }),
            2);
}
TEST(Placement, TimingPrioritySurvivesAbacusAndDetail) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout             = {0, 0, 12, 2, 1, 1};
  d.num_movable        = 3;
  d.node_x             = {4, 4, 8};
  d.node_y             = {0, 0, 1};
  d.node_size_x        = {2, 2, 2};
  d.node_size_y        = {1, 1, 1};
  d.pin2node           = {0, 2, 1, 2};
  d.pin_offset_x       = {1, 1, 1, 1};
  d.pin_offset_y       = {.5, .5, .5, .5};
  d.flat_net2pin       = {0, 1, 2, 3};
  d.flat_net2pin_start = {0, 2, 4};
  d.build_maps();
  d.node_legalize_weights  = {100, 1, 1};
  d.node_legalize_anchor_x = d.node_x;
  d.node_legalize_anchor_y = d.node_y;
  dp::detail::legalize(d, 2, 2);
  ASSERT_TRUE(dp::is_legal(d));
  EXPECT_DOUBLE_EQ(d.node_x[0], 4);
  EXPECT_DOUBLE_EQ(d.node_y[0], 0);
  dp::detail::detailed_place(d, 2, 2, 1);
  EXPECT_TRUE(dp::is_legal(d));
  EXPECT_DOUBLE_EQ(d.node_x[0], 4);
  EXPECT_DOUBLE_EQ(d.node_y[0], 0);
}

TEST(Placement, TimingLegalizationRejectsInvalidWeightArrays) {
  DreamPlace::native::Design d;
  d.layout             = {0, 0, 4, 1, 1, 1};
  d.num_movable        = 1;
  d.node_x             = {0};
  d.node_y             = {0};
  d.node_size_x        = {1};
  d.node_size_y        = {1};
  d.flat_net2pin_start = {0};
  d.build_maps();
  d.node_legalize_weights = {1, 2};
  EXPECT_THROW(d.validate(), std::invalid_argument);
  d.node_legalize_weights = {std::numeric_limits<double>::infinity()};
  EXPECT_THROW(d.validate(), std::invalid_argument);
}

}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, SharedCharacterizedTiesSplitEveryPhysicalBitAndPersist) {
  hhds::GraphLibrary source, flat;
  auto               graph  = both_constants(source);
  auto               tie_io = source.create_io("source_cell");
  tie_io->add_output("power", 0);
  tie_io->set_bits("power", 1);
  auto tie = gu::create_typed_node(*graph, Ntype_op::Sub);
  tie.set_subnode(tie_io);
  tie.set_name("ordinary_imported_instance");
  auto high = tie.create_driver_pin("power");
  gu::set_bits(high, 1);
  for (auto node : graph->body().nodes()) {
    if (node.get_name() == "nand") {
      for (const auto& name : {"A", "B"}) {
        auto sink = node.create_sink_pin(name);
        gu::drop_drivers(sink);
        sink.connect_driver(high);
      }
    }
  }
  graph->get_io()->set_bits("k", 2);
  gu::drop_drivers(graph->get_output_pin("k"));
  graph->get_output_pin("k").connect_driver(gu::create_const(*graph, *Dlop::from_pyrope("0ub11")));
  auto options      = tie_options("local_shared_bits",
                                  {
                                      {"source_cell",   {{"power", true}}},
                                      {"ground_cell", {{"ground", false}}}
  });
  options.cache_dir = options.output_dir + "/cache";
  const auto first  = place(*graph, flat, options);
  const auto check  = [&](const Result& placed) {
    unsigned              high_count = 0, low_count = 0;
    std::set<std::string> names;
    for (const auto& cell : placed.floorplan.components) {
      if (cell.macro != "source_cell" && cell.macro != "ground_cell") {
        continue;
      }
      names.insert(cell.name);
      high_count    += cell.macro == "source_cell";
      low_count     += cell.macro == "ground_cell";
      unsigned nets  = 0;
      for (const auto& net : placed.floorplan.nets) {
        if (!net.connections.empty() && net.connections.front().instance == cell.name) {
          ++nets;
          EXPECT_EQ(net.connections.size(), 2) << cell.name;
          EXPECT_EQ(net.connections.front().pin, cell.macro == "source_cell" ? "power" : "ground");
        }
      }
      EXPECT_EQ(nets, 1) << cell.name;
    }
    EXPECT_EQ(high_count, 4);
    EXPECT_EQ(low_count, 1);
    check_legality(physical::read_lef(options.lefs), placed.floorplan);
    return names;
  };
  const auto names = check(first);
  for (bool cached_original : {false, true}) {
    hhds::GraphLibrary again;
    auto               next = options;
    if (!cached_original) {
      next.previous       = std::make_shared<physical::Floorplan>(first.floorplan);
      next.legalize_cells = true;
    }
    const auto repeated = place(cached_original ? *graph : *first.graph, again, next);
    EXPECT_EQ(check(repeated), names);
  }
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, TimingPriorityHandlesDenseFragmentedRowsAndFixedObstacles) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout      = {0, 0, 12, 2, 1, 1};
  d.num_movable = 6;
  d.node_x      = {4, 4, 4, 4, 4, 4, 2, 6, 10, 2, 6, 10};
  d.node_y      = {0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1};
  d.node_size_x.assign(d.node_x.size(), 2);
  d.node_size_y.assign(d.node_x.size(), 1);
  d.flat_net2pin_start = {0};
  d.build_maps();
  d.node_legalize_weights.assign(d.node_x.size(), 1);
  d.node_legalize_weights[0] = 100;
  d.node_legalize_anchor_x   = d.node_x;
  d.node_legalize_anchor_y   = d.node_y;
  const auto before_x = d.node_x, before_y = d.node_y;
  dp::detail::legalize(d, 2, 2);
  EXPECT_TRUE(dp::is_legal(d));
  EXPECT_DOUBLE_EQ(d.node_x[0], 4);
  EXPECT_DOUBLE_EQ(d.node_y[0], 0);
  for (size_t i = d.num_movable; i < d.node_x.size(); ++i) {
    EXPECT_EQ(d.node_x[i], before_x[i]);
    EXPECT_EQ(d.node_y[i], before_y[i]);
  }
}

TEST(Placement, BeforeLegalizeReceivesPhysicalHaloCoordinatesAndPreservesAnchors) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout      = {0, 0, 12, 4, 1, 1};
  d.num_movable = 1;
  d.node_x      = {5, 1};
  d.node_y      = {1, 1};
  d.node_size_x = {1, 2};
  d.node_size_y = {1, 1};
  d.node_macro  = {0, 1};
  d.node_halo_x = d.node_halo_y = {0, 1};
  d.flat_net2pin_start          = {0};
  d.build_maps();
  dp::Options options;
  options.max_iterations = 0;
  options.add_fillers    = false;
  options.detailed_place = false;
  options.bins_x = options.bins_y = 2;
  unsigned calls                  = 0;
  options.before_legalize         = [&](dp::Design& snapshot) {
    ++calls;
    EXPECT_EQ(snapshot.node_x[1], 1);
    EXPECT_EQ(snapshot.node_y[1], 1);
    EXPECT_EQ(snapshot.node_size_x[1], 2);
    EXPECT_EQ(snapshot.node_size_y[1], 1);
    snapshot.node_legalize_weights  = {10, 1};
    snapshot.node_legalize_anchor_x = snapshot.node_x;
    snapshot.node_legalize_anchor_y = snapshot.node_y;
  };
  const auto result = dp::place(d, options);
  EXPECT_TRUE(result.legal);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(d.node_legalize_anchor_x, (std::vector<double>{5, 1}));
  EXPECT_EQ(d.node_legalize_anchor_y, (std::vector<double>{1, 1}));
  EXPECT_EQ(d.node_x[1], 1);
  EXPECT_EQ(d.node_y[1], 1);
}

TEST(Placement, MacroRankingUpdatesOnlyAtDensityMilestones) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "macro_ranking_milestones";
  std::filesystem::create_directories(root);
  std::ifstream input("inou/lefdef/tests/data/sky130hd.lef");
  std::string   lef{std::istreambuf_iterator<char>(input), {}};
  lef.replace(lef.find("CLASS CORE"), std::string("CLASS CORE").size(), "CLASS BLOCK");
  const auto path = (root / "macro.lef").string();
  std::ofstream(path) << lef;
  hhds::GraphLibrary source, placed_library;
  auto               graph   = tie_graph(source, false);
  auto               options = plain_options();
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", path};
  options.liberty            = "inou/lefdef/tests/data/sky130hd.lib";
  options.output_dir         = (root / "out").string();
  options.core               = {0, 0, 30, 30};
  options.bins               = 2;
  options.iterations = options.min_iterations = 120;
  options.macro_timing_interval               = 10;
  unsigned ranking_calls                      = 0;
  options.fp_evaluate                         = [](const Result&) { return Timing_feedback{}; };
  options.gp_rank                             = [&](const Ranking_view&) {
    ++ranking_calls;
    Timing_feedback feedback;
    feedback.ranking_only        = true;
    feedback.pins["inv:A"].slack = -10;
    return feedback;
  };
  const auto placed = place(*graph, placed_library, options);
  EXPECT_GT(ranking_calls, 0U);
  EXPECT_LE(ranking_calls, 2U);
  EXPECT_NO_THROW(check_legality(options.technology().lef, placed.floorplan));
  std::ifstream trace(options.output_dir + "/macro_timing.jsonl");
  ASSERT_TRUE(trace);
  unsigned samples = 0;
  for (std::string line; std::getline(trace, line);) {
    rapidjson::Document sample;
    sample.Parse(line.c_str());
    ASSERT_FALSE(sample.HasParseError());
    EXPECT_TRUE(sample["ranking_only"].GetBool());
    EXPECT_LE(sample["overflow"].GetDouble(), .2);
    ++samples;
  }
  EXPECT_EQ(samples, ranking_calls);

  options.output_dir           += "_handoff";
  options.macro_legalize_cells  = false;
  options.after_stage           = [](Placement_stage, const Result&) { return false; };
  hhds::GraphLibrary handoff_library;
  const auto         handoff = place(*graph, handoff_library, options);
  ASSERT_TRUE(handoff.macro_gp_timing);
  EXPECT_FALSE(handoff.macro_gp_timing->pin_locations.empty());
  EXPECT_NE(handoff.macro_gp_timing->parasitics, nullptr);
}

TEST(Placement, ObservationalGpSamplingPreservesPlacementAndTimingSchedule) {
  hhds::GraphLibrary source, baseline_library, observed_library;
  auto               graph   = tie_graph(source, false);
  auto               options = plain_options();
  options.liberty            = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir         = std::string(std::getenv("TEST_TMPDIR")) + "/gp_observation_baseline";
  options.iterations = options.min_iterations = 40;
  options.bins                                = 4;
  options.threads                             = 1;
  options.final_timing_start                  = 20;
  options.macro_timing_interval               = 10;
  unsigned timing_calls                       = 0;
  options.fp_evaluate                         = [&](const Result&) {
    ++timing_calls;
    Timing_feedback timing;
    timing.time_unit_ps = 1;
    timing.delay        = 10;
    timing.wns          = -1;
    timing.tns          = -2;
    return timing;
  };
  const auto baseline           = place(*graph, baseline_library, options);
  const auto baseline_calls     = timing_calls;
  timing_calls                  = 0;
  options.output_dir           += "_observed";
  options.gp_observer_interval  = 10;
  std::vector<int> iterations;
  options.gp_observer = [&](const Result& snapshot, const Gp_observation& sample) {
    iterations.push_back(sample.iteration);
    EXPECT_TRUE(snapshot.parasitics);
    EXPECT_FALSE(snapshot.floorplan.components.empty());
    EXPECT_DOUBLE_EQ(sample.macro_scale, 1);
    EXPECT_DOUBLE_EQ(sample.route_overflow, sample.opaque_route_overflow);
    EXPECT_GE(sample.hpwl_um, 0);
  };
  const auto observed = place(*graph, observed_library, options);
  EXPECT_EQ(iterations, (std::vector<int>{10, 20, 30, 40}));
  EXPECT_EQ(timing_calls, baseline_calls);
  EXPECT_EQ(observed.net_weights, baseline.net_weights);
  ASSERT_EQ(observed.floorplan.components.size(), baseline.floorplan.components.size());
  for (size_t i = 0; i < baseline.floorplan.components.size(); ++i) {
    const auto& a = baseline.floorplan.components[i];
    const auto& b = observed.floorplan.components[i];
    EXPECT_EQ(a.name, b.name);
    EXPECT_EQ(a.place.x, b.place.x);
    EXPECT_EQ(a.place.y, b.place.y);
    EXPECT_EQ(a.place.orientation, b.place.orientation);
  }
  options.macro_transparency.cycles = 1;
  options.macro_transparency.warmup = 10;
  options.macro_transparency.period = 10;
  std::vector<double> scales;
  options.gp_observer = [&](const Result&, const Gp_observation& sample) { scales.push_back(sample.macro_scale); };
  hhds::GraphLibrary cycled_library;
  place(*graph, cycled_library, options);
  EXPECT_EQ(scales, (std::vector<double>{1, 0, 1, 1}));
  options.macro_transparency = {};
  struct Stop {};
  iterations.clear();
  options.gp_observer = [&](const Result&, const Gp_observation& sample) {
    iterations.push_back(sample.iteration);
    if (sample.iteration == 20) {
      throw Stop{};
    }
  };
  hhds::GraphLibrary stopped;
  EXPECT_THROW(place(*graph, stopped, options), Stop);
  EXPECT_EQ(iterations, (std::vector<int>{10, 20}));
}

TEST(Placement, OriginalDefBlockagesRemainPhysicalConstraintsWithoutSignalInstances) {
  hhds::GraphLibrary source, initial_library, placed_library;
  auto               graph   = tie_graph(source, false);
  auto               options = plain_options();
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir         = std::string(std::getenv("TEST_TMPDIR")) + "/original_blockages_seed";
  options.iterations         = 0;
  options.bins               = 4;
  options.core = options.die = {0, 0, 18.4, 10.88};
  auto seed                  = place(*graph, initial_library, options);
  ASSERT_EQ(seed.floorplan.components.size(), 2);
  const auto   tech   = physical::read_lef(options.lefs);
  const double middle = 9200, top = 10880, row_height = 2720;
  for (auto& c : seed.floorplan.components) {
    if (c.name == "nand") {
      c.place.x           = middle;
      c.place.y           = 0;
      c.place.orientation = physical::Orientation::N;
      c.place.status      = physical::Status::FIXED;
    }
  }
  seed.floorplan.blockages = {
      {false,     {},                    {{0, 0, middle, top}}},
      { true, "nand", {{middle, top - row_height, 18400, top}}}
  };
  const auto path = std::string(std::getenv("TEST_TMPDIR")) + "/original_blockages.def";
  physical::write_def(seed.floorplan, path);
  options.floorplan   = path;
  options.output_dir += "_gp";
  options.snapshots   = true;
  options.iterations = options.min_iterations = 20;
  options.gp_observer_interval                = 10;
  options.fp_pin_adjust = options.macro_pin_adjust = true;
  unsigned observations                            = 0;
  options.gp_observer                              = [&](const Result& snapshot, const Gp_observation&) {
    ++observations;
    EXPECT_EQ(snapshot.floorplan.blockages, seed.floorplan.blockages);
    EXPECT_EQ(snapshot.floorplan.components.size(), 2);
    EXPECT_EQ(snapshot.floorplan.terminals.size(), seed.floorplan.terminals.size());
    EXPECT_EQ(snapshot.parasitics->cells.size(), 2);
    for (const auto& c : snapshot.floorplan.components) {
      EXPECT_TRUE(c.name == "inv" || c.name == "nand");
    }
  };
  const auto placed = place(*graph, placed_library, options);
  EXPECT_EQ(observations, 2);
  rapidjson::Document gp;
  const auto          gp_path = options.output_dir + "/gp.json";
  std::ifstream       gp_file(gp_path);
  const std::string   gp_text((std::istreambuf_iterator<char>(gp_file)), {});
  gp.Parse(gp_text.c_str());
  ASSERT_FALSE(gp.HasParseError());
  ASSERT_EQ(gp["nodes"].Size(), 2);
  EXPECT_EQ(placed.floorplan.blockages, seed.floorplan.blockages);
  ASSERT_EQ(placed.floorplan.components.size(), 2);
  EXPECT_NO_THROW(check_legality(tech, placed.floorplan));
  for (const auto& c : placed.floorplan.components) {
    EXPECT_GE(c.place.x, middle);
  }
  auto       changed = placed.floorplan;
  const auto moving
      = std::find_if(changed.components.begin(), changed.components.end(), [](const auto& c) { return c.name == "inv"; });
  ASSERT_NE(moving, changed.components.end());
  moving->place.x           = 0;
  moving->place.y           = 0;
  moving->place.orientation = physical::Orientation::N;
  EXPECT_THROW(check_legality(tech, changed), std::runtime_error);
  moving->place.x           = middle;
  moving->place.y           = top - row_height;
  moving->place.orientation = physical::Orientation::FS;
  EXPECT_NO_THROW(check_legality(tech, changed));  // Soft restrictions allow final row packing/repair.
  auto       eco      = options;
  auto       previous = std::make_shared<physical::Floorplan>(placed.floorplan);
  const auto edited
      = std::find_if(previous->components.begin(), previous->components.end(), [](const auto& c) { return c.name == "inv"; });
  ASSERT_NE(edited, previous->components.end());
  edited->place.x = edited->place.y  = 0;
  edited->place.orientation          = physical::Orientation::N;
  eco.previous                       = previous;
  eco.changed_cells                  = {"inv"};
  eco.legalize_cells                 = true;
  eco.gp_observer                    = {};
  eco.output_dir                    += "_eco";
  hhds::GraphLibrary eco_library;
  const auto         repaired = place(*graph, eco_library, eco);
  EXPECT_NO_THROW(check_legality(tech, repaired.floorplan));
  EXPECT_EQ(repaired.floorplan.blockages, placed.floorplan.blockages);
  for (const auto& c : repaired.floorplan.components) {
    EXPECT_GE(c.place.x, middle);
  }
  options.macro_transparency.always = true;
  hhds::GraphLibrary transparent_library;
  EXPECT_THROW(place(*graph, transparent_library, options), std::invalid_argument);
}

TEST(Placement, SegmentedImportedRowsConstrainPlacementSnapshotsAndEco) {
  hhds::GraphLibrary source, seed_library;
  auto               graph   = tie_graph(source, false);
  auto               options = plain_options();
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  const auto root            = std::string(std::getenv("TEST_TMPDIR")) + "/segmented_import";
  options.density            = .5;
  options.output_dir         = root + "/seed";
  options.iterations         = 0;
  options.bins               = 4;
  options.core = options.die = {0, 0, 18.4, 10.88};
  auto       seed            = place(*graph, seed_library, options);
  const auto tech            = physical::read_lef(options.lefs);
  auto       frame           = seed.floorplan;
  const auto first           = frame.rows.front();
  auto       left = first, right = first;
  left.name        = "left";
  left.count       = 10;
  right.name       = "right";
  right.x         += 30 * first.step;
  right.count      = 10;
  // A hole within one row and a completely missing row both remain forbidden.
  frame.rows       = {right, frame.rows[3], left, frame.rows[2]};
  frame.blockages  = {
      {false, {}, {{13800, 8160, 18400, 10880}}}
  };
  for (auto& terminal : frame.terminals) {
    terminal.status = physical::Status::FIXED;
  }
  options.floorplan = root + "/segments.def";
  physical::write_def(frame, options.floorplan);
  options.iterations = options.min_iterations = 20;
  options.snapshots                           = true;
  options.gp_observer_interval                = 10;
  unsigned observed                           = 0;
  options.gp_observer                         = [&](const Result& snapshot, const Gp_observation&) {
    ++observed;
    EXPECT_EQ(snapshot.floorplan.rows, frame.rows);
    EXPECT_EQ(snapshot.floorplan.blockages, frame.blockages);
    EXPECT_EQ(snapshot.floorplan.components.size(), 2);
    EXPECT_EQ(snapshot.parasitics->cells.size(), 2);
  };
  for (bool terminal : {false, true}) {
    hhds::GraphLibrary placed_library;
    options.optimization_pending = !terminal;
    options.output_dir           = root + (terminal ? "/terminal" : "/native");
    auto placed                  = place(*graph, placed_library, options);
    EXPECT_NO_THROW(check_legality(tech, placed.floorplan));
    EXPECT_EQ(placed.floorplan.rows, frame.rows);
    EXPECT_EQ(placed.floorplan.blockages, frame.blockages);
    EXPECT_EQ(placed.floorplan.components.size(), 2);
    for (const auto& file : {"out.def", "before_legalization.def", "after_legalization.def"}) {
      auto exported = physical::read_def(options.output_dir + "/" + file);
      EXPECT_EQ(exported.rows, frame.rows);
      EXPECT_EQ(exported.blockages, frame.blockages);
    }
    if (terminal) {
      auto previous = std::make_shared<physical::Floorplan>(placed.floorplan);
      for (auto& cell : previous->components) {
        cell.place.x = 9200;
        cell.place.y = 2720;
      }
      auto eco     = options;
      eco.previous = previous;
      eco.floorplan.clear();
      eco.gp_observer    = {};
      eco.changed_cells  = {"inv", "nand"};
      eco.legalize_cells = true;
      eco.output_dir     = root + "/eco";
      hhds::GraphLibrary eco_library;
      const auto         repaired = place(*graph, eco_library, eco);
      EXPECT_NO_THROW(check_legality(tech, repaired.floorplan));
      EXPECT_EQ(repaired.floorplan.rows, frame.rows);
      EXPECT_EQ(repaired.floorplan.blockages, frame.blockages);
    }
  }
  EXPECT_GT(observed, 0);
}

TEST(Placement, TieLocalizationRespectsHardDefExclusionsAfterNativeLegalization) {
  hhds::GraphLibrary source, seed_library, placed_library;
  auto               graph = tie_graph(source);
  for (auto node : graph->body().nodes()) {
    if (node.get_name() == "nand") {
      Ann_place fixed(9200, 0, 1840, 2720);
      fixed.status = physical::Status::FIXED;
      node.attr(attrs::place).set(fixed);
    }
  }
  auto options       = plain_options();
  options.liberty    = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/tie_hard_mask_seed";
  options.core = options.die = {0, 0, 55.2, 5.44};
  options.iterations         = 0;
  options.bins               = 4;
  auto seed                  = place(*graph, seed_library, options);
  seed.floorplan.blockages   = {
      {false, {}, {{0, 0, 36800, 2720}}}
  };
  for (auto& port : seed.floorplan.terminals) {
    port.status = physical::Status::FIXED;
  }
  const auto path = std::string(std::getenv("TEST_TMPDIR")) + "/tie_hard_mask.def";
  physical::write_def(seed.floorplan, path);
  options.floorplan   = path;
  options.output_dir += "_blocked";
  const auto placed   = place(*graph, placed_library, options);
  const auto tech     = physical::read_lef(options.lefs);
  EXPECT_NO_THROW(check_legality(tech, placed.floorplan));
  unsigned ties = 0;
  for (const auto& cell : placed.floorplan.components) {
    if (cell.macro == "sky130_fd_sc_hd__conb_1") {
      ++ties;
      EXPECT_TRUE(cell.place.y >= 2720 || cell.place.x >= 36800);
    }
  }
  EXPECT_GT(ties, 0);
}

TEST(Placement, FreshLegalizationTimingRanksPositiveSlackPaths) {
  hhds::GraphLibrary source, flat;
  auto               graph          = tie_graph(source, false);
  auto               options        = plain_options();
  options.liberty                   = "inou/lefdef/tests/data/sky130hd.lib";
  options.lefs                      = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir                = std::string(std::getenv("TEST_TMPDIR")) + "/positive_legalization_slack";
  options.iterations                = 0;
  options.snapshots                 = true;
  options.macro_timing_interval     = 1;
  options.final_timing_start        = 0;  // Short fixture: exercise in-loop timing.
  options.legalize_timing_strength  = 20;
  options.legalize_timing_threshold = .5;
  unsigned calls                    = 0;
  options.fp_evaluate               = [&](const Result& snapshot) {
    ++calls;
    if (calls == 1) {
      // The saved zero-GP frame must precede fresh E/F timing feedback, so
      // controls can replay exactly the coordinates that the legalizer sees.
      const auto path = options.output_dir + "/before_legalization.def";
      EXPECT_TRUE(std::filesystem::is_regular_file(path));
      if (std::filesystem::is_regular_file(path)) {
        const auto saved = physical::read_def(path);
        EXPECT_EQ(saved.components.size(), snapshot.floorplan.components.size());
        for (size_t i = 0; i < std::min(saved.components.size(), snapshot.floorplan.components.size()); ++i) {
          EXPECT_EQ(saved.components[i].name, snapshot.floorplan.components[i].name);
          EXPECT_EQ(saved.components[i].place.x, snapshot.floorplan.components[i].place.x);
          EXPECT_EQ(saved.components[i].place.y, snapshot.floorplan.components[i].place.y);
        }
      }
    }
    EXPECT_TRUE(snapshot.parasitics);
    EXPECT_FALSE(snapshot.pin_locations.empty());
    Timing_feedback timing;
    timing.pins["nand:B"].slack     = .1;
    timing.pins["inv:A"].slack      = .9;
    timing.pins["nand:clock"].slack = -100;
    timing.pins["nand:clock"].clock = true;
    timing.wns                      = .1;
    timing.tns                      = 0;
    return timing;
  };
  const auto result = place(*graph, flat, options);
  EXPECT_GE(calls, 3);  // Exact final-GP snapshot, post-legalization and post-detail.
  EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), result.floorplan));
  std::ifstream report(options.output_dir + "/legalize_priorities.tsv");
  ASSERT_TRUE(report);
  std::string header, name;
  std::getline(report, header);
  std::map<std::string, double> weights;
  double                        slack, criticality, weight, x, y;
  while (report >> name >> slack >> criticality >> weight >> x >> y) {
    weights[name] = weight;
  }
  EXPECT_EQ(weights.at("nand"), 21);
  EXPECT_EQ(weights.at("inv"), 1);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, ConstantSourcesLegalizeAfterTheirFunctionalUsers) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout             = {0, 0, 8, 1, 1, 1};
  d.num_movable        = 2;
  d.node_x             = {2, 2};
  d.node_y             = {0, 0};
  d.node_size_x        = {2, 2};
  d.node_size_y        = {1, 1};
  d.flat_net2pin_start = {0};
  d.build_maps();
  // The tie deliberately precedes its user in the node array, and no timing
  // weights are supplied. Function semantics still give the user first choice.
  d.node_legalize_last = {1, 0};
  dp::detail::legalize(d, 2, 2);
  EXPECT_TRUE(dp::is_legal(d));
  // Abacus can settle the cluster while retaining the spatial order chosen
  // when the user claimed the original competing site before the source.
  EXPECT_GT(d.node_x[1], d.node_x[0]);
  d.node_legalize_last = {1};
  EXPECT_THROW(d.validate(), std::invalid_argument);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, RemovingFirstConstantUserKeepsOtherTieIdentities) {
  hhds::GraphLibrary source, flat;
  auto               graph   = tie_graph(source);
  auto               options = tie_options("stable_removed_user",
                                           {
                                               {"high_cell", {{"high", true}}}
  });
  options.cache_dir          = options.output_dir + "/cache";
  const auto first           = place(*graph, flat, options);
  const auto nand_source     = [](const Result& placed) {
    for (const auto& net : placed.floorplan.nets) {
      if (net.connections.size() == 2 && net.connections[1].instance == "nand" && net.connections[1].pin == "B") {
        return net.connections[0].instance;
      }
    }
    return std::string{};
  };
  const auto before = nand_source(first);
  ASSERT_FALSE(before.empty());
  auto output = graph->get_output_pin("k");
  gu::drop_drivers(output);
  output.connect_driver(gu::create_get_mask(*graph, graph->get_input_pin("a"), 0).get_driver_pin(0));
  hhds::GraphLibrary edited;
  const auto         result = place(*graph, edited, options);
  EXPECT_EQ(nand_source(result), before);
  EXPECT_EQ(result.floorplan.components.size(), 3);
}

TEST(Placement, DeferredLegalizationMetadataSurvivesFillerTimingCheckpoints) {
  namespace dp = DreamPlace::native;
  dp::Design d;
  d.layout      = {0, 0, 20, 4, 1, 1};
  d.num_movable = 2;
  d.node_x      = {2, 10};
  d.node_y      = {0, 2};
  d.node_size_x = d.node_size_y = {1, 1};
  d.node_legalize_last          = {1, 0};
  d.pin2node                    = {0, 1};
  d.pin_offset_x = d.pin_offset_y = {.5, .5};
  d.flat_net2pin                  = {0, 1};
  d.flat_net2pin_start            = {0, 2};
  d.build_maps();
  dp::Options options;
  options.max_iterations = 2;
  options.bins_x = options.bins_y = 2;
  options.add_fillers             = true;
  options.target_density          = .5;
  options.detailed_place          = false;
  options.checkpoint_interval     = 1;
  unsigned calls                  = 0;
  options.checkpoint              = [&](const dp::Design& snapshot, int) {
    ++calls;
    EXPECT_NO_THROW(snapshot.validate());
    EXPECT_EQ(snapshot.node_x.size(), 2);
    EXPECT_EQ(snapshot.node_legalize_last, (std::vector<uint8_t>{1, 0}));
    return dp::GpUpdate{};
  };
  const auto result = dp::place(d, options);
  EXPECT_GT(calls, 0);
  EXPECT_GT(result.filler_count, 0);
  EXPECT_TRUE(result.legal);
  EXPECT_EQ(d.node_legalize_last, (std::vector<uint8_t>{1, 0}));
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, UnplacedImportedPinsInitializeAndMoveInFpAndMacroStages) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "unplaced_io_stages";
  std::filesystem::create_directories(root);
  {
    std::ofstream lef(root / "macro.lef");
    lef << "VERSION 5.8 ;\nMACRO io_macro\n CLASS BLOCK ; SIZE 4 BY 4 ; SYMMETRY X Y ;\n";
    for (const auto* name : {"A", "B", "Y"}) {
      lef << " PIN " << name << "\n DIRECTION " << (std::string_view(name) == "Y" ? "OUTPUT" : "INPUT")
          << " ; USE SIGNAL ;\n PORT LAYER met1 ; RECT 0.1 0.1 0.2 0.2 ; END\n END " << name << '\n';
    }
    lef << "END io_macro\nEND LIBRARY\n";
  }
  hhds::GraphLibrary source, initial_library;
  auto               graph = tie_graph(source, false);
  const auto         macro = source.create_io("io_macro");
  macro->add_input("A", 0);
  macro->add_input("B", 1);
  macro->add_output("Y", 0);
  for (auto node : graph->body().nodes()) {
    if (node.get_name() == "nand") {
      node.set_subnode(macro);
    }
  }
  auto options = plain_options();
  options.lefs = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (root / "macro.lef").string()};
  options.output_dir          = (root / "initial").string();
  options.iterations          = 0;
  options.bins                = 8;
  options.utilization         = .1;
  const auto         baseline = place(*graph, initial_library, options);
  auto               frame    = baseline.floorplan;
  physical::Terminal fixed;
  for (auto& pin : frame.terminals) {
    if (pin.name == "y") {
      pin.status = physical::Status::FIXED;
      fixed      = pin;
    } else {
      pin.status = physical::Status::PLACED;
      pin.shapes.clear();
    }
  }
  const auto positioned_def = root / "positioned.def";
  const auto unplaced_def   = root / "unplaced.def";
  physical::write_def(frame, positioned_def.string());
  {
    std::ifstream input(positioned_def);
    std::ofstream output(unplaced_def);
    std::string   line;
    bool          pins = false;
    while (std::getline(input, line)) {
      if (line.starts_with("PINS ")) {
        pins = true;
      }
      if (line == "END PINS") {
        pins = false;
      }
      if (pins && line.starts_with("- ")) {
        if (const auto placement = line.find(" + PLACED "); placement != std::string::npos) {
          line.erase(placement);
          line += " ;";
        }
      }
      output << line << '\n';
    }
  }
  const auto imported = physical::read_def(unplaced_def.string());
  for (const auto& pin : imported.terminals) {
    if (pin.name != "y") {
      ASSERT_EQ(pin.status, physical::Status::UNPLACED);
      ASSERT_TRUE(pin.shapes.empty());
    }
  }
  options.floorplan = unplaced_def.string();
  {
    hhds::GraphLibrary refused;
    EXPECT_THROW(place(*graph, refused, options), std::runtime_error);
  }
  for (bool macro_stage : {false, true}) {
    auto run       = options;
    run.output_dir = (root / (macro_stage ? "macro" : "fp")).string();
    run.fp_place = run.fp_pin_adjust = !macro_stage;
    run.macro_pin_adjust             = macro_stage;
    run.iterations = run.fp_iterations = run.io_iterations = 2;
    run.fp_pin_interval = run.macro_pin_interval = 1;
    hhds::GraphLibrary placed_library;
    const auto         result = place(*graph, placed_library, run);
    EXPECT_NO_THROW(check_legality(physical::read_lef(run.lefs), result.floorplan));
    EXPECT_EQ(result.floorplan.nets, baseline.floorplan.nets);
    for (const auto& pin : result.floorplan.terminals) {
      if (pin.name == "y") {
        EXPECT_EQ(pin.x, fixed.x);
        EXPECT_EQ(pin.y, fixed.y);
        EXPECT_EQ(pin.status, physical::Status::FIXED);
      } else {
        EXPECT_EQ(pin.status, physical::Status::PLACED);
        EXPECT_FALSE(pin.shapes.empty());
        EXPECT_TRUE(pin.x == result.floorplan.die.xl || pin.x == result.floorplan.die.xh || pin.y == result.floorplan.die.yl
                    || pin.y == result.floorplan.die.yh);
      }
    }
    std::ifstream trace(run.output_dir + (macro_stage ? "/macro_pins.jsonl" : "/fp_pins.jsonl"));
    ASSERT_TRUE(trace);
    std::string line;
    unsigned    updates = 0;
    while (std::getline(trace, line)) {
      rapidjson::Document sample;
      sample.Parse(line.c_str());
      ASSERT_FALSE(sample.HasParseError());
      if (sample["moved"].GetUint()) {
        ++updates;
      }
    }
    EXPECT_GT(updates, 0);
  }
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, ActualMacroPlacementKeepsPinTrackPhaseAndWarmFrame) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "macro_track_actual";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "ram.lef") << R"(VERSION 5.8 ;
MACRO track_ram
 CLASS BLOCK ; ORIGIN 0 0 ; SIZE 4 BY 5.1 ; SYMMETRY X Y ;
 PIN A
 DIRECTION INPUT ; USE SIGNAL ;
 PORT LAYER met2 ; RECT 0.16 0.1 0.30 0.5 ; END
 END A
 PIN B
 DIRECTION INPUT ; USE SIGNAL ;
 PORT LAYER met2 ; RECT 0.62 0.1 0.76 0.5 ; END
 END B
 PIN Y
 DIRECTION OUTPUT ; USE SIGNAL ;
 PORT LAYER met1 ; RECT 3.0 0.10 3.5 0.24 ; END
 END Y
END track_ram
END LIBRARY
)";
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("macro_track_actual");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto fixed = gu::create_typed_node(*graph, Ntype_op::Sub);
  fixed.set_subnode(inv);
  fixed.set_name("fixed");
  fixed.create_sink_pin("A").connect_driver(graph->get_input_pin("a"));
  auto signal = fixed.create_driver_pin("Y");
  gu::set_bits(signal, 1);
  Ann_place anchor(0, 0, 1380, 2720);
  anchor.status      = Ann_place::Status::FIXED;
  anchor.orientation = Ann_place::Orientation::FN;
  fixed.attr(attrs::place).set(anchor);
  auto ram = source.create_io("track_ram");
  ram->add_input("A", 0);
  ram->add_input("B", 1);
  ram->add_output("Y", 0);
  auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
  node.set_subnode(ram);
  node.set_name("ram");
  node.create_sink_pin("A").connect_driver(signal);
  node.create_sink_pin("B").connect_driver(signal);
  auto output = node.create_driver_pin("Y");
  gu::set_bits(output, 1);
  graph->get_output_pin("y").connect_driver(output);
  physical::Floorplan fp;
  fp.design = "macro_track_actual";
  fp.dbu    = 1000;
  fp.die    = {0, 0, 46000, 81600};
  for (int row = 0; row < 30; ++row) {
    fp.rows.push_back({"r" + std::to_string(row),
                       "unithd",
                       0,
                       row * 2720,
                       100,
                       460,
                       row % 2 ? physical::Orientation::FS : physical::Orientation::N});
  }
  fp.tracks = {
      {"X", "met2",  90, 100, 460},
      {"Y", "met1", 120, 240, 340}
  };
  physical::write_def(fp, (dir / "frame.def").string());
  auto options       = plain_options();
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (dir / "ram.lef").string()};
  options.floorplan  = (dir / "frame.def").string();
  options.output_dir = (dir / "out").string();
  options.cache_dir  = (dir / "cache").string();
  options.iterations = 0;
  options.bins       = 8;
  const auto first   = place(*graph, flat, options);
  EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), first.floorplan));
  const auto macro = std::find_if(first.floorplan.components.begin(), first.floorplan.components.end(), [](const auto& c) {
    return c.name == "ram";
  });
  ASSERT_NE(macro, first.floorplan.components.end());
  EXPECT_EQ((macro->place.x + 230 - 90) % 460, 0);
  EXPECT_EQ((macro->place.y + 170 - 120) % 340, 0);
  EXPECT_EQ(macro->place.x % 5, 0);
  EXPECT_EQ(macro->place.y % 5, 0);
  EXPECT_EQ(first.pin_locations.at("ram:A").first, macro->place.x + 230);
  hhds::GraphLibrary warm_library;
  const auto         warm = place(*graph, warm_library, options);
  EXPECT_EQ(first.floorplan.components, warm.floorplan.components);
}
TEST(Placement, LegalityClipsFixedMacroHalosAtCoreBoundary) {
  auto tech   = physical::read_lef({"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"});
  auto macro  = tech.macros.at("sky130_fd_sc_hd__inv_1");
  macro.klass = "BLOCK";
  tech.macros.emplace("boundary_macro", macro);
  const int64_t       sw = 460, rh = 2720, width = 20 * sw, height = 4 * rh;
  physical::Floorplan fp;
  for (int row = 0; row < 4; ++row) {
    fp.rows.push_back({"r" + std::to_string(row), "unithd", 0, row * rh, 20, sw, Ann_place::Orientation::N});
  }
  Ann_place body(0, 0, 4 * sw, rh);
  body.status = Ann_place::Status::FIXED;
  fp.components.push_back({"macro", "boundary_macro", body});
  for (auto status : {Ann_place::Status::FIXED, Ann_place::Status::LOCKED}) {
    for (auto x : {int64_t{0}, width - body.width}) {
      for (auto y : {int64_t{0}, height - body.height}) {
        fp.components[0].place        = body;
        fp.components[0].place.status = status;
        fp.components[0].place.x      = x;
        fp.components[0].place.y      = y;
        EXPECT_NO_THROW(check_legality(tech, fp, .46, 2.72));
      }
    }
  }
  fp.components[0].place = body;
  Ann_place cell(body.width, 0, 3 * sw, rh);
  cell.status = Ann_place::Status::PLACED;
  fp.components.push_back({"cell", "sky130_fd_sc_hd__inv_1", cell});
  EXPECT_NO_THROW(check_legality(tech, fp));
  EXPECT_THROW(check_legality(tech, fp, .46, 2.72), std::runtime_error);
  fp.components[1].place.x += sw;  // clear the interior halo exactly
  EXPECT_NO_THROW(check_legality(tech, fp, .46, 2.72));
  fp.components[0].place.x = -1;  // clipping must not hide an invalid physical body
  EXPECT_THROW(check_legality(tech, fp, .46, 2.72), std::runtime_error);
  fp.components[0].place        = body;
  fp.components[0].place.status = Ann_place::Status::PLACED;
  EXPECT_THROW(check_legality(tech, fp, .46, 2.72), std::runtime_error);
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, CellHaloSeparatesFinalClearanceFromMacroSpacingAndCache) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "cell_halo";
  std::filesystem::create_directories(dir);
  std::ofstream(dir / "macro.lef") << R"(VERSION 5.8 ;
MACRO halo_macro
 CLASS BLOCK ; ORIGIN 0 0 ; SIZE 1.38 BY 2.72 ; SYMMETRY X Y ;
 PIN A
 DIRECTION INPUT ; PORT LAYER li1 ; RECT 0.1 0.1 1.2 2.6 ; END
 END A
 PIN Y
 DIRECTION OUTPUT ; PORT LAYER li1 ; RECT 0.1 0.1 1.2 2.6 ; END
 END Y
END halo_macro
END LIBRARY
)";
  hhds::GraphLibrary source;
  auto               io = source.create_io("cell_halo");
  io->add_input("a", 0);
  io->add_output("y", 0);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto macro = source.create_io("halo_macro"), inv = source.create_io("sky130_fd_sc_hd__inv_1");
  for (const auto& master : {macro, inv}) {
    master->add_input("A", 0);
    master->add_output("Y", 0);
  }
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  physical::Floorplan frame;
  frame.design = "cell_halo";
  frame.dbu    = 1000;
  frame.die    = {0, 0, 27600, 27200};
  for (int row = 0; row < 10; ++row) {
    frame.rows.push_back({"r" + std::to_string(row),
                          "unithd",
                          0,
                          row * 2720,
                          60,
                          460,
                          row % 2 ? physical::Orientation::FS : physical::Orientation::N});
  }
  for (int i = 0; i < 3; ++i) {
    auto n = gu::create_typed_node(*graph, Ntype_op::Sub);
    n.set_subnode(i == 1 ? inv : macro);
    const auto name = i == 1 ? std::string("dirty") : "macro" + std::to_string(i);
    n.set_name(name);
    n.create_sink_pin("A").connect_driver(signal);
    signal = n.create_driver_pin("Y");
    gu::set_bits(signal, 1);
    Ann_place p(i == 0 ? 4600 : i == 1 ? 6440 : 13800, 5440, 1380, 2720);
    p.status = i == 1 ? Ann_place::Status::PLACED : Ann_place::Status::FIXED;
    n.attr(attrs::place).set(p);
    if (i != 1) {
      frame.components.push_back({name, "halo_macro", p});
    }
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options   = plain_options();
  options.lefs   = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", (dir / "macro.lef").string()};
  options.halo_x = options.halo_y = 2.72;
  options.previous                = std::make_shared<physical::Floorplan>(frame);
  options.changed_cells           = {"dirty"};
  options.legalize_cells          = true;
  options.iterations              = 0;
  options.bins                    = 8;
  options.output_dir              = (dir / "inherited").string();
  hhds::GraphLibrary  inherited_library;
  const auto          inherited = place(*graph, inherited_library, options);
  rapidjson::Document inherited_qor;
  inherited_qor.Parse(inherited.qor.c_str());
  EXPECT_FALSE(inherited_qor.HasMember("cell_halo"));
  EXPECT_FALSE(inherited_qor.HasMember("macro_halo"));
  const auto inherited_context = final_cell_options(options);
  EXPECT_EQ(inherited_context.cell_stage, options.cell_stage);
  EXPECT_EQ(inherited_context.fp_place, options.fp_place);
  EXPECT_EQ(inherited_context.final_cells, options.final_cells);
  EXPECT_EQ(inherited_context.macro_mode, options.macro_mode);
  options.cell_halo  = std::pair{2.72, 2.72};
  options            = final_cell_options(options);
  options.output_dir = (dir / "equal").string();
  hhds::GraphLibrary equal_library;
  const auto         equal = place(*graph, equal_library, options);
  EXPECT_EQ(inherited.floorplan.components, equal.floorplan.components);
  EXPECT_EQ(inherited.floorplan.terminals, equal.floorplan.terminals);
  options.cell_halo  = std::pair{0., 0.};
  options.output_dir = (dir / "zero").string();
  hhds::GraphLibrary zero_library;
  const auto         zero  = place(*graph, zero_library, options);
  const auto         dirty = std::find_if(zero.floorplan.components.begin(), zero.floorplan.components.end(), [](const auto& c) {
    return c.name == "dirty";
  });
  ASSERT_NE(dirty, zero.floorplan.components.end());
  EXPECT_EQ(dirty->place.x, 6440);
  EXPECT_EQ(dirty->place.y, 5440);
  const auto technology = physical::read_lef(options.lefs);
  EXPECT_NO_THROW(check_legality(technology, zero.floorplan, options));
  auto requested_cell_options       = options;
  requested_cell_options.cell_stage = false;
  EXPECT_NO_THROW(check_placed_legality(zero, requested_cell_options));
  EXPECT_THROW(check_legality(technology, zero.floorplan, requested_cell_options), std::runtime_error);
  EXPECT_THROW(check_legality(technology, zero.floorplan, 2.72, 2.72), std::runtime_error);
  for (const auto& c : zero.floorplan.components) {
    if (c.macro == "halo_macro") {
      const auto old
          = std::find_if(frame.components.begin(), frame.components.end(), [&](const auto& x) { return x.name == c.name; });
      ASSERT_NE(old, frame.components.end());
      EXPECT_EQ(c.place, old->place);
    }
  }
  rapidjson::Document qor;
  qor.Parse(zero.qor.c_str());
  EXPECT_TRUE(qor["cell_halo"]["active"].GetBool());
  EXPECT_EQ(qor["halo"]["x_um"].GetDouble(), 0);
  EXPECT_EQ(qor["macro_halo"]["x_dbu"].GetInt64(), 2760);
  // A smaller cell exclusion cannot permit macros to violate their requested spacing.
  auto bad = zero.floorplan;
  bad.components.clear();
  bad.components            = frame.components;
  bad.components[1].place.x = 9200;
  EXPECT_NO_THROW(check_legality(technology, bad));
  EXPECT_THROW(check_legality(technology, bad, options), std::runtime_error);
  auto bad_repair      = zero;
  bad_repair.floorplan = bad;
  EXPECT_THROW(repair_buffers(std::move(bad_repair), options, [](const Result&) { return Timing_feedback{}; }), std::runtime_error);
  options.previous = std::make_shared<physical::Floorplan>(bad);
  hhds::GraphLibrary bad_library;
  EXPECT_THROW(place(*graph, bad_library, options), std::runtime_error);
  options.previous.reset();
  options.floorplan = (dir / "frame.def").string();
  physical::write_def(frame, options.floorplan);
  options.legalize_cells = false;
  options.cache_dir      = (dir / "cache").string();
  options.output_dir     = (dir / "cached").string();
  hhds::GraphLibrary cold_library, warm_library, changed_library;
  const auto         cold = place(*graph, cold_library, options);
  const auto         warm = place(*graph, warm_library, options);
  EXPECT_EQ(cold.floorplan.components, warm.floorplan.components);
  rapidjson::Document warm_qor;
  warm_qor.Parse(warm.qor.c_str());
  EXPECT_TRUE(warm_qor["incremental"]["hit"].GetBool());
  options.cell_halo           = std::pair{.46, 0.};
  const auto          changed = place(*graph, changed_library, options);
  rapidjson::Document changed_qor;
  changed_qor.Parse(changed.qor.c_str());
  EXPECT_FALSE(changed_qor["incremental"]["hit"].GetBool());
  // Explicit context is required for mixed one-pass/previous macro ambiguity.
  options.cell_stage  = false;
  options.final_cells = false;
  hhds::GraphLibrary ambiguous_library;
  EXPECT_THROW(place(*graph, ambiguous_library, options), std::invalid_argument);
  options.final_cells = true;
  options.cache_dir.clear();
  options.output_dir      = (dir / "staged").string();
  options.fp_warp.enabled = options.macro_warp.enabled = true;
  hhds::GraphLibrary  staged_library;
  const auto          staged = place(*graph, staged_library, options);
  rapidjson::Document staged_qor;
  staged_qor.Parse(staged.qor.c_str());
  EXPECT_TRUE(staged_qor["cell_halo"]["active"].GetBool());
  EXPECT_FALSE(staged_qor["fp_warpspace"].GetBool());
  EXPECT_FALSE(staged_qor["macro_warpspace"].GetBool());
  EXPECT_FALSE(std::filesystem::exists(options.output_dir + "/macro_warp_summary.json"));
  EXPECT_FALSE(std::filesystem::exists(options.output_dir + "/fp_place.json"));
  std::ifstream       macro_report(options.output_dir + "/macro/qor.json");
  rapidjson::Document macro_qor;
  const std::string   macro_text{std::istreambuf_iterator<char>(macro_report), {}};
  macro_qor.Parse(macro_text.c_str());
  ASSERT_FALSE(macro_qor.HasParseError());
  EXPECT_FALSE(macro_qor["cell_halo"]["active"].GetBool());
  EXPECT_EQ(macro_qor["halo"]["x_um"].GetDouble(), 2.72);
  options.full = false;
  hhds::GraphLibrary gp_only_library;
  EXPECT_THROW(place(*graph, gp_only_library, options), std::invalid_argument);
  options.full = true;
  for (const auto& pair : {
           std::pair{          3.,                                      0.},
           std::pair{          0.,                                      3.},
           std::pair{         -1.,                                      0.},
           std::pair{          0., std::numeric_limits<double>::infinity()},
           std::pair{std::nan(""),                                      0.}
  }) {
    options.cell_halo = pair;
    hhds::GraphLibrary invalid_library;
    EXPECT_THROW(place(*graph, invalid_library, options), std::invalid_argument);
  }
}
}  // namespace livehd::placement

namespace livehd::placement {
TEST(Placement, UnsetCellHaloPreservesStatefulCallbackAcrossCalls) {
  // A callback can own mutable state without capturing an external reference.
  // Default single-pass placement must invoke that exact stored callable.
  struct Observer {
    unsigned calls = 0;
    bool     operator()(Placement_stage stage, const Result& result) {
      EXPECT_EQ(stage, Placement_stage::final);
      EXPECT_FALSE(result.floorplan.components.empty());
      ++calls;
      return false;
    }
  };
  hhds::GraphLibrary source, first_library, second_library;
  auto               io = source.create_io("callback_identity");
  io->add_input("a", 0);
  io->add_output("y", 0);
  io->set_bits("a", 1);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto master = source.create_io("sky130_fd_sc_hd__inv_1");
  master->add_input("A", 0);
  master->add_output("Y", 0);
  auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
  node.set_name("inv");
  node.set_subnode(master);
  auto input = graph->get_input_pin("a");
  gu::set_bits(input, 1);
  node.create_sink_pin("A").connect_driver(input);
  auto output = node.create_driver_pin("Y");
  gu::set_bits(output, 1);
  graph->get_output_pin("y").connect_driver(output);
  auto options        = plain_options();
  options.lefs        = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir  = std::string(std::getenv("TEST_TMPDIR")) + "/callback_identity";
  options.iterations  = 0;
  options.bins        = 8;
  options.after_stage = Observer{};
  ASSERT_FALSE(options.cell_halo);
  ASSERT_NE(options.after_stage.target<Observer>(), nullptr);
  const auto first = place(*graph, first_library, options);
  EXPECT_EQ(options.after_stage.target<Observer>()->calls, 1);
  options.output_dir += "_second";
  const auto second   = place(*graph, second_library, options);
  EXPECT_EQ(options.after_stage.target<Observer>()->calls, 2);
  options.halo_x = options.halo_y = -1;
  hhds::GraphLibrary automatic_library;
  EXPECT_NO_THROW(place(*graph, automatic_library, options));
  EXPECT_EQ(options.after_stage.target<Observer>()->calls, 3);
  EXPECT_EQ(first.floorplan.components, second.floorplan.components);
}
}  // namespace livehd::placement

TEST(Placement, RoutePoliciesResolveStagesAndRejectInconsistentLevels) {
  livehd::placement::Options options;
  options.route_model                        = "auto";
  options.macro_route.model                  = "rudy";
  options.final_route.policy.switch_overflow = .02;
  options.final_route.policy.target_overflow = .005;
  const auto macro                           = livehd::placement::resolve_route_policy(options, options.macro_route);
  const auto final                           = livehd::placement::resolve_route_policy(options, options.final_route);
  EXPECT_EQ(macro.model, DreamPlace::native::RouteModel::rudy);
  EXPECT_EQ(final.model, DreamPlace::native::RouteModel::automatic);
  EXPECT_DOUBLE_EQ(final.switch_overflow, .02);
  EXPECT_DOUBLE_EQ(final.target_overflow, .005);
  options.final_route.policy.target_overflow = .03;
  EXPECT_THROW(livehd::placement::resolve_route_policy(options, options.final_route), std::invalid_argument);
  options.final_route.policy.target_overflow = std::numeric_limits<double>::infinity();
  EXPECT_THROW(livehd::placement::resolve_route_policy(options, options.final_route), std::invalid_argument);
  options.final_route.policy.target_overflow = 0;
  options.final_route.model                  = "off";
  EXPECT_EQ(livehd::placement::resolve_route_policy(options, options.final_route).model, DreamPlace::native::RouteModel::off);
  options.final_route.model = "unknown";
  EXPECT_THROW(livehd::placement::resolve_route_policy(options, options.final_route), std::invalid_argument);
}

namespace livehd::placement {
TEST(Placement, RouteGridUsesFreshTimingAndSamePhysicalSnapshotWithSatOffOrOn) {
  std::string baseline;
  for (bool sat : {false, true}) {
    hhds::GraphLibrary source, flat;
    auto               graph               = tie_graph(source, false);
    auto               options             = plain_options();
    options.liberty                        = "inou/lefdef/tests/data/sky130hd.lib";
    options.lefs                           = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
    options.output_dir                     = std::string(std::getenv("TEST_TMPDIR")) + (sat ? "/route_grid_on" : "/route_grid_off");
    options.iterations                     = 0;
    options.bins                           = 4;
    options.snapshots                      = true;
    options.routability                    = true;
    options.route_model                    = "rsmt";
    options.macro_timing_interval          = 1;
    options.final_timing_start             = 0;  // Short fixture: exercise in-loop timing.
    options.satlegalize                    = sat;
    options.satlegalize_options.route_grid = true;
    options.satlegalize_options.max_cells  = 65536;
    options.satlegalize_options.max_windows = 1;
    options.satlegalize_options.max_pairs   = 4;
    unsigned calls                          = 0;
    options.fp_evaluate                     = [&](const Result&) {
      ++calls;
      if (calls == 1) {
        EXPECT_FALSE(std::filesystem::exists(options.output_dir + "/sat_route_grid.json"));
      }
      Timing_feedback timing;
      timing.pins["nand:B"].slack = .1;
      timing.pins["inv:A"].slack  = .9;
      timing.wns                  = .1;
      timing.tns                  = 0;
      timing.hold_wns             = .2;
      return timing;
    };
    const auto result = place(*graph, flat, options);
    EXPECT_GE(calls, 3);
    EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), result.floorplan));
    std::ifstream file(options.output_dir + "/sat_route_grid.json");
    ASSERT_TRUE(file);
    const std::string   text{std::istreambuf_iterator<char>(file), {}};
    rapidjson::Document document;
    document.Parse(text.c_str());
    ASSERT_FALSE(document.HasParseError());
    EXPECT_STREQ(document["model"].GetString(), "rsmt");
    EXPECT_EQ(document["bins_x"].GetInt(), 4);
    EXPECT_EQ(document["bins_y"].GetInt(), 4);
    EXPECT_EQ(document["horizontal"].Size(), 16);
    EXPECT_EQ(document["vertical"].Size(), 16);
    EXPECT_EQ(document["pin_utilization"].Size(), 16);
    EXPECT_EQ(document["geometry_topology_fnv1a64"].GetStringLength(), 16);
    EXPECT_EQ(document["net_weights_fnv1a64"].GetStringLength(), 16);
    if (sat) {
      EXPECT_EQ(text, baseline);
    } else {
      baseline = text;
    }
  }
}
// A user DEF may import a FIXED CORE cell already flipped on an FS row. Its
// native pin offsets are built in FS, so installing the unchanged SAT frame
// (which re-applies every cell orientation) must not flip them a second time:
// with nothing accepted, detailed placement must see exactly the SAT-off pins.
TEST(Placement, SatInstallKeepsImportedFlippedCorePins) {
  const auto root = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "fs_import";
  std::filesystem::create_directories(root);
  const std::vector<std::string> lefs{"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  const auto                     tech = physical::read_lef(lefs);
  hhds::GraphLibrary             source;
  auto                           io = source.create_io("fs_import");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph = io->create_graph();
  auto inv   = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (const char* name : {"fixed", "m0", "m1", "m2"}) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(inv);
    node.set_name(name);
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin("Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  const int64_t       sw = 460, rh = 2720;
  physical::Floorplan fp;
  fp.design = "fs_import";
  fp.dbu    = tech.dbu;
  fp.die    = {0, 0, 20 * sw, 4 * rh};
  for (int row = 0; row < 4; ++row) {
    const auto orientation = row % 2 ? Ann_place::Orientation::FS : Ann_place::Orientation::N;
    fp.rows.push_back({"row" + std::to_string(row), "unithd", 0, row * rh, 20, sw, orientation});
  }
  Ann_place fixed(2 * sw, 3 * rh, 3 * sw, rh);
  fixed.status      = Ann_place::Status::FIXED;
  fixed.orientation = Ann_place::Orientation::FS;
  fp.components.push_back({"fixed", "sky130_fd_sc_hd__inv_1", fixed});
  fp.terminals.push_back({"a", "a", "INPUT", 0, 3 * rh + rh / 2, Ann_place::Orientation::N, Ann_place::Status::FIXED, {}});
  fp.terminals.push_back({"y", "y", "OUTPUT", 20 * sw, rh / 2, Ann_place::Orientation::N, Ann_place::Status::FIXED, {}});
  physical::write_def(fp, (root / "input.def").string());
  const auto& master  = tech.macros.at("sky130_fd_sc_hd__inv_1");
  const auto  lef_pin = [&](const std::string& pin) {
    const auto [x, y] = physical::oriented_center(master, master.pins.at(pin).shapes.front().rect, Ann_place::Orientation::FS);
    return std::pair{fixed.x + x * tech.dbu, fixed.y + y * tech.dbu};
  };
  std::vector<double>              kernels[2];
  std::vector<physical::Component> components[2];
  for (bool sat : {false, true}) {
    auto options                  = plain_options();
    options.lefs                  = lefs;
    options.liberty               = "inou/lefdef/tests/data/sky130hd.lib";
    options.floorplan             = (root / "input.def").string();
    options.output_dir            = (root / (sat ? "sat" : "plain")).string();
    options.iterations            = 20;
    options.bins                  = 4;
    options.macro_timing_interval = 1;
    options.final_timing_start    = 0;
    options.satlegalize           = sat;
    options.fp_evaluate           = [&](const Result& r) {
      for (const char* pin : {"A", "Y"}) {
        if (const auto found = r.pin_locations.find(std::string("fixed:") + pin); found != r.pin_locations.end()) {
          EXPECT_NEAR(found->second.first, lef_pin(pin).first, 1e-6) << pin;
          EXPECT_NEAR(found->second.second, lef_pin(pin).second, 1e-6) << pin;
        }
      }
      Timing_feedback timing;
      timing.wns = timing.tns = 0;
      return timing;
    };
    hhds::GraphLibrary placed;
    const auto         result = place(*graph, placed, options);
    EXPECT_NO_THROW(check_legality(tech, result.floorplan));
    components[sat] = result.floorplan.components;
    const auto read = [&](const char* name) {
      std::ifstream       file(options.output_dir + "/" + name);
      const std::string   text{std::istreambuf_iterator<char>(file), {}};
      rapidjson::Document json;
      json.Parse(text.c_str());
      EXPECT_FALSE(json.HasParseError()) << name;
      return json;
    };
    if (sat) {
      EXPECT_EQ(read("satlegalize.json")["accepted"].GetUint(), 0u);
    }
    const auto history = read("gp_history.json");
    ASSERT_FALSE(history["detailed_kernels"].Empty());
    for (const auto& kernel : history["detailed_kernels"].GetArray()) {
      kernels[sat].push_back(kernel["weighted_hpwl_before"].GetDouble());
      kernels[sat].push_back(kernel["weighted_hpwl_after"].GetDouble());
    }
  }
  EXPECT_EQ(kernels[1], kernels[0]);
  EXPECT_EQ(components[1], components[0]);
  const auto imported = std::find_if(components[1].begin(), components[1].end(), [](const auto& c) { return c.name == "fixed"; });
  ASSERT_NE(imported, components[1].end());
  EXPECT_EQ(imported->place.orientation, Ann_place::Orientation::FS);
  EXPECT_EQ(imported->place.status, Ann_place::Status::FIXED);
}
TEST(Placement, RelativeFillerChargeScalesInPreliminaryAndFinalGp) {
  hhds::GraphLibrary source;
  const auto         graph   = tie_graph(source, false);
  auto               options = plain_options();
  options.lefs               = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.fp_place = options.fp_pin_adjust = true;
  options.io_iterations                    = 2;
  options.fp_pin_interval                  = 1;
  options.fp_iterations = options.iterations = 2;
  options.bins                               = 4;
  options.density                            = .5;
  std::array<double, 2> previous_area{};
  for (int scale : {1, 2}) {
    hhds::GraphLibrary placed;
    options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/relative_fp_fillers_" + std::to_string(scale);
    options.core       = {0, 0, scale * 55.2, 54.4};
    options.die        = options.core;
    const auto result  = place(*graph, placed, options);
    EXPECT_NO_THROW(check_legality(physical::read_lef(options.lefs), result.floorplan));
    const auto read = [&](const char* name) {
      std::ifstream       file(options.output_dir + "/" + name);
      const std::string   text((std::istreambuf_iterator<char>(file)), {});
      rapidjson::Document json;
      json.Parse(text.c_str());
      return json;
    };
    auto fp = read("fp_place.json"), gp = read("gp_history.json"), budget = read("virtual_filler_budget.json");
    ASSERT_FALSE(fp.HasParseError());
    ASSERT_FALSE(gp.HasParseError());
    ASSERT_FALSE(budget.HasParseError());
    EXPECT_DOUBLE_EQ(budget["fraction"].GetDouble(), 1);
    EXPECT_DOUBLE_EQ(budget["requested_charge_area_um2"].GetDouble(), budget["available_charge_area_um2"].GetDouble());
    EXPECT_GT(fp["fp_virtual_filler_count"].GetInt(), 0);
    EXPECT_GT(gp["virtual_filler_count"].GetInt(), 0);
    const std::array<double, 2> areas{fp["fp_virtual_filler_area_um2"].GetDouble(), gp["virtual_filler_area_um2"].GetDouble()};
    for (size_t stage = 0; stage < areas.size(); ++stage) {
      EXPECT_GT(areas[stage], previous_area[stage]);
    }
    previous_area = areas;
  }
}

}  // namespace livehd::placement

TEST(Placement, MacroWarpDefaultsUseConstantStrengthAndStabilityStart) {
  const livehd::placement::Options o;
  EXPECT_DOUBLE_EQ(o.macro_warp.weight, .7);
  EXPECT_DOUBLE_EQ(o.macro_warp.start_stability, .2);
  EXPECT_EQ(o.macro_warp.refresh_interval, 25);
  EXPECT_DOUBLE_EQ(o.fp_warp.weight, 1);
  EXPECT_DOUBLE_EQ(o.fp_warp.start_stability, 0);
  EXPECT_EQ(o.fp_warp.refresh_interval, 0);
}

TEST(Placement, AdaptiveSearchRejectsTopologyChangingGpRepairBeforePlacement) {
  hhds::GraphLibrary         source, placed;
  auto                       graph = source.create_io("adaptive_repair")->create_graph();
  livehd::placement::Options options;
  options.gp_search.enabled = true;
  options.gp_resize         = true;
  try {
    livehd::placement::place(*graph, placed, options);
    FAIL() << "adaptive search accepted topology-changing repair";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find("disable gp_resize"), std::string::npos);
  }
}
namespace livehd::placement {
TEST(Placement, CutRowsRequireActualSiteCoverageAndUseSegmentOrientations) {
  physical::Technology tech;
  tech.sites["s"] = {.010, .010, true};
  auto& cell      = tech.macros["cell"];
  cell.width = cell.height = .010;
  cell.site                = "s";
  cell.klass               = "CORE";
  cell.symmetry_y          = true;
  physical::Floorplan fp;
  fp.die  = {0, 0, 100, 20};
  fp.rows = {
      { "r0_left", "s",  0,  0,  3, 10,  physical::Orientation::N},
      {"r0_right", "s", 60,  0,  4, 10,  physical::Orientation::N},
      {      "r1", "s",  0, 10, 10, 10, physical::Orientation::FS}
  };
  Ann_place p(80, 0, 10, 10);
  p.status = physical::Status::PLACED;
  fp.components.push_back({"c", "cell", p});
  EXPECT_NO_THROW(check_legality(tech, fp));
  fp.components[0].place.x = 40;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.components[0].place.x           = 80;
  fp.components[0].place.y           = 10;
  fp.components[0].place.orientation = physical::Orientation::S;
  EXPECT_NO_THROW(check_legality(tech, fp));
  fp.components[0].place.orientation = physical::Orientation::N;
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
  fp.components[0].place.orientation = physical::Orientation::FS;
  std::reverse(fp.rows.begin(), fp.rows.end());
  EXPECT_NO_THROW(check_legality(tech, fp));
  fp.components[0].place.width = fp.components[0].place.height = 0;  // raw DEF, dimensions come from LEF
  EXPECT_NO_THROW(check_legality(tech, fp));
  auto duplicate = fp.components[0];
  duplicate.name = "overlap";
  fp.components.push_back(duplicate);
  EXPECT_THROW(check_legality(tech, fp), std::runtime_error);
}
TEST(Placement, HandoffTapsAreFixedPhysicalOnlyTerminalObstacles) {
  const auto dir = std::filesystem::path(std::getenv("TEST_TMPDIR")) / "handoff_taps";
  std::filesystem::create_directories(dir);
  // Synthetic profile master on the repository fixture's site. Production uses
  // the actual ASAP7 master, verified independently against OpenROAD.
  const auto lef = dir / "tap.lef";
  std::ofstream(lef) << R"(VERSION 5.8 ;
MACRO TAPCELL_ASAP7_75t_R
 CLASS CORE WELLTAP ; ORIGIN 0 0 ; SIZE 0.92 BY 2.72 ;
 SYMMETRY X Y ; SITE unithd ;
END TAPCELL_ASAP7_75t_R
MACRO handoff_ram
 CLASS BLOCK ; ORIGIN 0 0 ; SIZE 9.2 BY 5.44 ; SYMMETRY X Y ;
END handoff_ram
END LIBRARY
)";
  hhds::GraphLibrary source, flat;
  auto               io = source.create_io("handoff_taps");
  io->add_input("a", 0);
  io->set_bits("a", 1);
  io->add_output("y", 0);
  io->set_bits("y", 1);
  auto graph  = io->create_graph();
  auto ram    = source.create_io("handoff_ram");
  auto memory = gu::create_typed_node(*graph, Ntype_op::Sub);
  memory.set_subnode(ram);
  memory.set_name("ram");
  Ann_place macro_place(20240, 21760, 9200, 5440);
  macro_place.status = physical::Status::FIXED;
  memory.attr(attrs::place).set(macro_place);
  auto inv = source.create_io("sky130_fd_sc_hd__inv_1");
  inv->add_input("A", 0);
  inv->add_output("Y", 0);
  auto signal = graph->get_input_pin("a");
  gu::set_bits(signal, 1);
  for (unsigned i = 0; i < 12; ++i) {
    auto node = gu::create_typed_node(*graph, Ntype_op::Sub);
    node.set_subnode(inv);
    node.set_name("inv" + std::to_string(i));
    node.create_sink_pin("A").connect_driver(signal);
    signal = node.create_driver_pin("Y");
    gu::set_bits(signal, 1);
  }
  graph->get_output_pin("y").connect_driver(signal);
  auto options        = plain_options();
  options.lefs        = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef", lef.string()};
  options.core        = {0, 0, 55.2, 54.4};
  options.die         = options.core;
  options.output_dir  = (dir / "out").string();
  options.density     = .5;
  options.final_cells = true;
  options.halo_x = options.halo_y = 4;
  options.iterations              = 0;
  options.bins                    = 4;
  options.optimization_pending    = false;  // handoff taps need fresh terminal legalization
  options.handoff_taps = options.snapshots            = true;
  options.terminal_legalize_options.critical_fraction = 0;
  const auto result                                   = place(*graph, flat, options);
  const auto tech                                     = physical::read_lef(options.lefs);
  EXPECT_NO_THROW(check_legality(tech, result.floorplan, options));
  size_t taps = 0;
  for (const auto& c : result.floorplan.components) {
    if (c.macro == "TAPCELL_ASAP7_75t_R") {
      EXPECT_EQ(c.place.status, physical::Status::LOCKED);
      ++taps;
    }
  }
  EXPECT_GT(taps, 40);
  EXPECT_EQ(result.floorplan.components.size(), 13 + taps);
  EXPECT_GT(result.floorplan.rows.size(), 20);
  auto imported_options         = options;
  imported_options.handoff_taps = false;
  EXPECT_NO_THROW(check_legality(tech, result.floorplan, imported_options));
  auto overlapping = result.floorplan;
  auto tap         = std::find_if(overlapping.components.begin(), overlapping.components.end(), [](const auto& c) {
    return c.macro == "TAPCELL_ASAP7_75t_R";
  });
  ASSERT_NE(tap, overlapping.components.end());
  auto second_tap
      = std::find_if(std::next(tap), overlapping.components.end(), [](const auto& c) { return c.macro == "TAPCELL_ASAP7_75t_R"; });
  ASSERT_NE(second_tap, overlapping.components.end());
  second_tap->place = tap->place;
  EXPECT_THROW(check_legality(tech, overlapping, imported_options), std::runtime_error);
  tap->place.x = macro_place.x;
  tap->place.y = macro_place.y;
  EXPECT_THROW(check_legality(tech, overlapping, imported_options), std::runtime_error);
  const auto snapshot = physical::read_def(options.output_dir + "/after_legalization.def");
  EXPECT_EQ(snapshot.components.size(), result.floorplan.components.size());
  EXPECT_TRUE(std::filesystem::exists(options.output_dir + "/handoff_taps.json"));
}
}  // namespace livehd::placement

TEST(Placement, SteinerRcSharedTopologyPreservesLengthAndCharge) {
  const std::vector<std::pair<std::string, std::pair<double, double>>> pins{
      {    "driver",    {0, 0}},
      {         "a",  {100, 0}},
      {         "b", {50, 100}},
      {"coincident",    {0, 0}}
  };
  const auto rc = livehd::placement::steiner_rc("driver", pins, {2, 3, 4, 5, "test"}, 1000);
  ASSERT_EQ(rc.nodes.size(), 8);
  EXPECT_EQ(rc.edges.size(), 7);
  EXPECT_NEAR(rc.wire_length_um, .2, 1e-12);
  double capacitance = 0, resistance = 0;
  for (const auto& node : rc.nodes) {
    capacitance += node.capacitance_ff;
  }
  for (const auto& edge : rc.edges) {
    resistance += edge.resistance_ohm;
  }
  EXPECT_NEAR(capacitance, .8, 1e-12);
  EXPECT_NEAR(resistance, .6 + 4e-9, 1e-12);
}

TEST(Placement, SteinerRcUsesNamedDriverRegardlessOfPinOrder) {
  std::mt19937                                                   random(817);
  std::vector<std::pair<std::string, std::pair<double, double>>> pins{
      {"driver", {0, 0}}
  };
  for (size_t i = 0; i < 40; ++i) {
    const double x = random() % 1000, y = random() % 1000;
    pins.push_back({
        "sink" + std::to_string(i),
        {x, y}
    });
  }
  const auto before = livehd::placement::steiner_rc("driver", pins, {1, 2, 1, 2, "test"}, 1000);
  std::rotate(pins.begin(), pins.begin() + 1, pins.end());
  const auto after = livehd::placement::steiner_rc("driver", pins, {1, 2, 1, 2, "test"}, 1000);
  EXPECT_EQ(before.wire_length_um, after.wire_length_um);
  EXPECT_EQ(before.nodes.size(), after.nodes.size());
  EXPECT_EQ(before.edges.size(), after.edges.size());
  EXPECT_THROW(livehd::placement::steiner_rc("absent", pins, {1, 2, 1, 2, "test"}, 1000), std::invalid_argument);
}
