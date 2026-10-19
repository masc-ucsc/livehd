#include "place.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "memory_proxy.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "rapidjson/document.h"
#include "repair.hpp"
namespace livehd::placement {
namespace gu = graph_util;
namespace {
// Fixtures for the independent plain/H2D/ECO paths pin their original controls.
Options plain_options() {
  Options o;
  o.fp_place           = false;
  o.final_cells        = false;
  o.macro_snap         = false;
  o.macro_align_weight = 0;
  o.macro_align_global = false;
  o.utilization        = .5;
  o.density            = .8;
  o.halo_x = o.halo_y   = 0;
  o.iterations          = 500;
  o.bins                = 32;
  o.scale_macro_density = o.exact_macro_density = o.adaptive_density = o.routability = false;
  o.macro_overlap_weight = o.macro_jitter = 0;
  o.schedule_iterations = o.io_iterations = 0;
  return o;
}
}  // namespace

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
    options.iterations = 0;
    options.bins       = 8;
    if (mode >= 3) {
      EXPECT_THROW((void)place(*graph, flat, options), std::runtime_error) << mode;
    } else {
      const auto result = place(*graph, flat, options);
      ASSERT_EQ(result.floorplan.components.size(), 1);
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
      for (auto node : result.graph->body().nodes()) {
        EXPECT_NE(gu::type_op_of(node), Ntype_op::Latch);
      }
    }
    EXPECT_EQ(gu::type_op_of(latch), Ntype_op::Latch);
    EXPECT_EQ(gu::get_driver_of_sink_name(latch, "din"), a);
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
      EXPECT_EQ(terminal.status, physical::Status::FIXED);
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
    // This whitespace would exceed the native one-million-filler budget if
    // LiveHD enabled virtual fillers. Keep the IOs fixed to isolate the GP.
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
    soft.output_dir = (root / "no_virtual_fillers").string();
    soft.density    = .8;
    hhds::GraphLibrary sparse_library;
    const auto         sparse = place(*graph, sparse_library, soft);
    EXPECT_EQ(sparse.floorplan.components.size(), 2);
    EXPECT_EQ(sparse.floorplan.die.xh, frame.die.xh);
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
    if (c.name == "__place_tiehi" || c.name == "__place_tielo") {
      const bool high = c.name == "__place_tiehi";
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
                              [&](const auto& c) { return c.name == instance; }),
                1);
      bool connected = false;
      for (const auto& net : result.floorplan.nets) {
        for (const auto& pin : net.connections) {
          if (pin.instance == instance) {
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
  std::erase_if(missing->components, [](const auto& cell) { return cell.name == "__place_tiehi"; });
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
                          [](const auto& c) { return c.name == "__place_tielo"; }),
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
    return c.name == "__place_tielo";
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
        if (pin.instance == instance) {
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
      return c.name == "__place_tiehi";
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
      tie |= c.name == "__place_tiehi";
    }
    EXPECT_TRUE(tie);
  }
}
TEST(Placement, ReplaceReusesTieCellForConstantOutput) {
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
  // Timing/repair/resynth rounds re-place the placed graph, which already
  // holds __place_tiehi while k still has its logical constant driver.
  for (const bool eco : {false, true}) {
    auto again = options;
    if (eco) {
      again.previous = std::make_shared<physical::Floorplan>(placed.floorplan);
    }
    hhds::GraphLibrary second;
    const auto         replaced = place(*placed.graph, second, again);
    unsigned           ties     = 0;
    for (const auto& c : replaced.floorplan.components) {
      ties += c.name == "__place_tiehi";
    }
    EXPECT_EQ(ties, 1) << "eco=" << eco;
    std::string k_net;
    for (const auto& t : replaced.floorplan.terminals) {
      if (t.name == "k") {
        k_net = t.net;
      }
    }
    // The constant output and the nand's B input share the one tie net.
    bool shared = false;
    for (const auto& net : replaced.floorplan.nets) {
      if (net.name != k_net) {
        continue;
      }
      for (const auto& c : net.connections) {
        shared |= c.instance == "nand" && c.pin == "B";
      }
    }
    EXPECT_TRUE(shared) << "eco=" << eco;
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
TEST(Placement, HighFanoutRcFallbackRemainsATreeAndIsMarkedApproximate) {
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
  EXPECT_TRUE(result.parasitics->approximate);
  bool high_fanout = false;
  for (const auto& net : result.parasitics->nets) {
    EXPECT_EQ(net.edges.size() + 1, net.nodes.size());
    high_fanout |= net.nodes.size() > 2000;
  }
  EXPECT_TRUE(high_fanout);
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
  for (int b = 4; b < 8; ++b) {
    EXPECT_TRUE(drives(std::format("y[{}]", b), "__place_tielo:LO")) << b;
  }
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
  const auto clock = inverter("clock_logic", input), enable = inverter("enable_logic", input);
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
  for (const auto* color_source : {"placement", "memory_first"}) {
    o.fp_color_source  = color_source;
    o.fp_color_minimum = 0;
    o.fp_iterations    = 40;
    o.iterations       = 20;
    o.io_iterations    = 10;
    o.fp_pin_interval  = 5;
    o.bins             = 8;
    o.utilization      = .05;
    const auto result  = place(*graph, flat, o);
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
    EXPECT_EQ(report["attraction_outside"].GetDouble(), 4);
    EXPECT_GT(report["fp_attraction_coefficient"].GetDouble(), 0);
    EXPECT_GT(report["macro_attraction_coefficient"].GetDouble(), 0);
    EXPECT_EQ(report["macro_attraction_calibration_iteration"].GetInt(), 0);
    EXPECT_GE(report["pin_updates"].GetUint64(), 2);
    EXPECT_GT(report["fp_iterations"].GetInt(), 0);
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
  o.analyze_colors_only = true;
  o.final_cells         = false;
  o.fp_evaluate         = [](const Result&) -> Timing_feedback { throw std::runtime_error("analysis invoked STA"); };
  o.output_dir          = (dir / "analysis").string();
  const auto analysis   = place(*graph, flat, o);
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
  EXPECT_EQ(placed.floorplan.components.size(), 5);  // Two macros, two controls, and the enable tie.
  EXPECT_EQ(std::count_if(placed.floorplan.components.begin(),
                          placed.floorplan.components.end(),
                          [](const auto& c) { return c.macro == "sky130_fd_sc_hd__inv_1"; }),
            2);
}
}  // namespace livehd::placement
