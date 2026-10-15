#include "place.hpp"

#include <cstdlib>
#include <filesystem>
#include <map>

#include "attrs.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
namespace livehd::placement {
namespace gu = graph_util;
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
}  // namespace livehd::placement
