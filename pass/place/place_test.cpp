#include "place.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "memory_proxy.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/opentimer/pass_opentimer.hpp"
#include "rapidjson/document.h"
namespace livehd::placement {
namespace gu = graph_util;
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
    Options options;
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
  Options options;
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
  Options options;
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
// a cell input and a top output that keeps its logical constant.
static std::shared_ptr<hhds::Graph> tie_graph(hhds::GraphLibrary& source) {
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
  n.create_sink_pin("B").connect_driver(gu::create_const(*graph, *Dlop::create_integer(1)));
  auto mid = n.create_driver_pin("Y");
  gu::set_bits(mid, 1);
  auto b = gu::create_typed_node(*graph, Ntype_op::Sub);
  b.set_subnode(inv);
  b.set_name("inv");
  b.create_sink_pin("A").connect_driver(mid);
  auto y = b.create_driver_pin("Y");
  gu::set_bits(y, 1);
  graph->get_output_pin("y").connect_driver(y);
  graph->get_output_pin("k").connect_driver(gu::create_const(*graph, *Dlop::create_integer(1)));
  return graph;
}
TEST(Placement, ReplaceReusesTieCellForConstantOutput) {
  hhds::GraphLibrary source;
  auto               graph = tie_graph(source);
  Options            options;
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/tie";
  options.iterations = 20;
  options.bins       = 8;
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
  auto               graph = tie_graph(source);
  Options            options;
  options.lefs       = {"inou/lefdef/tests/data/sky130hd.tlef", "inou/lefdef/tests/data/sky130hd.lef"};
  options.output_dir = std::string(std::getenv("TEST_TMPDIR")) + "/io_frame";
  options.iterations = 20;
  options.bins       = 8;
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
  Options options;
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
  Options options;
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
  Options options;
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
  Options options;
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
  Options options;
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
}  // namespace livehd::placement
