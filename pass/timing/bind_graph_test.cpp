// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bind_graph.hpp"

#include <string>

#include "cell.hpp"
#include "engine.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "test_library.hpp"

namespace fast = livehd::timing::fast;
namespace gu   = livehd::graph_util;
namespace lt   = livehd::liberty::timing;

namespace {
struct Library_fixture {
  lt::Library      library = lt::parse(fast::test::library_text(1.0));
  fast::Cell_types types;
  Library_fixture() {
    const lt::Library* pointers[] = {&library};
    const std::string  names[]    = {"typ"};
    types                         = fast::Cell_types::build(pointers, names);
  }
};
}  // namespace

TEST(BindGraph, WiringIsResolvedToTheDriverPinsAndPortBits) {
  Library_fixture    cells;
  hhds::GraphLibrary library;
  auto               inv = library.create_io("INV");
  inv->add_input("A", 1);
  inv->add_output("Y", 2);
  auto nolib = library.create_io("NOLIB");  // a cell the Liberty model does not know
  nolib->add_input("A", 1);
  nolib->add_output("Y", 2);

  auto top = library.create_io("top");
  top->add_input("d", 1);
  top->set_bits("d", 2);
  top->add_input("clk", 2);
  top->add_output("y0", 1);
  top->add_output("y1", 2);
  top->add_output("y2", 3);
  top->add_output("y3", 4);
  auto g = top->create_graph();

  const auto make = [&](const std::shared_ptr<hhds::GraphIO>& io) {
    auto node = gu::create_typed_node(*g, Ntype_op::Sub);
    node.set_subnode(io);
    return node;
  };
  const auto u0 = make(inv), u1 = make(inv), u2 = make(inv), u3 = make(nolib);
  const auto d = g->get_input_pin("d");
  // u0 reads bit 0 of the bus and u1 bit 1, both through a Get_mask; u2 reads a constant; u3 is not in the library.
  gu::create_get_mask(*g, d, 0).create_driver_pin(0).connect_sink(u0.create_sink_pin("A"));
  gu::create_get_mask(*g, d, 1).create_driver_pin(0).connect_sink(u1.create_sink_pin("A"));
  gu::create_const(*g, *Dlop::create_integer(1)).connect_sink(u2.create_sink_pin("A"));
  u0.create_driver_pin("Y").connect_sink(g->get_output_pin("y0"));
  u1.create_driver_pin("Y").connect_sink(g->get_output_pin("y1"));
  u2.create_driver_pin("Y").connect_sink(g->get_output_pin("y2"));
  u3.create_driver_pin("Y").connect_sink(g->get_output_pin("y3"));

  const auto  bound = fast::bind_graph(*g, cells.types);
  const auto& net   = *bound.netlist;
  EXPECT_EQ(bound.mapped_cells, 3u);  // u3 has no model
  EXPECT_EQ(bound.opaque_cells, 1u);
  EXPECT_EQ(bound.constant_sinks, 1u);
  EXPECT_EQ(bound.unmapped_nodes, 0u);
  // Ports: d[0], d[1], clk, then y0..y3; three cells.
  ASSERT_EQ(net.ports().size(), 7u);
  EXPECT_EQ(net.ports()[0].name, "d[0]");
  EXPECT_EQ(net.ports()[1].name, "d[1]");
  EXPECT_EQ(net.ports()[2].name, "clk");  // a one-bit port keeps its name
  EXPECT_EQ(net.cells(), 7u + 3u);

  const auto cell_of = [&](const hhds::Node_class& node) {
    for (uint32_t c = 0; c < net.cells(); ++c) {
      if (!bound.cell_node[c].is_invalid() && bound.cell_node[c] == node) {
        return c;
      }
    }
    return fast::no_index;
  };
  const auto c0 = cell_of(u0), c1 = cell_of(u1), c2 = cell_of(u2);
  ASSERT_NE(c0, fast::no_index);
  ASSERT_NE(c1, fast::no_index);
  ASSERT_NE(c2, fast::no_index);
  EXPECT_EQ(cell_of(u3), fast::no_index);

  const auto driver_of = [&](uint32_t cell, const char* pin) {
    const auto net_id = net.net_of(net.pin(cell, pin));
    return net_id == fast::no_index ? fast::no_index : net.driver_of(net_id);
  };
  // u0.A is driven by port bit d[0], u1.A by d[1]: the Get_mask was resolved bit by bit.
  EXPECT_EQ(driver_of(c0, "A"), net.pin(net.ports()[0].cell, 0u));
  EXPECT_EQ(driver_of(c1, "A"), net.pin(net.ports()[1].cell, 0u));
  EXPECT_EQ(driver_of(c2, "A"), fast::no_index);  // a constant is no driver
  // The output ports y0 and y1 are driven by the Y pins of u0 and u1; y2 by u2.Y.
  const auto output = [&](const char* name) {
    for (const auto& port : net.ports()) {
      if (!port.input && port.name == name) {
        return net.pin(port.cell, 0u);
      }
    }
    return fast::no_index;
  };
  EXPECT_EQ(net.driver_of(net.net_of(output("y0"))), net.pin(c0, "Y"));
  EXPECT_EQ(net.driver_of(net.net_of(output("y1"))), net.pin(c1, "Y"));
  EXPECT_EQ(net.driver_of(net.net_of(output("y2"))), net.pin(c2, "Y"));
  EXPECT_EQ(net.net_of(output("y3")), fast::no_index);  // behind the uncharacterized cell
  bool said = false;
  for (const auto& line : bound.unsupported) {
    said = said || line.find("uncharacterized cell") != std::string::npos;
  }
  EXPECT_TRUE(said);

  // The bound netlist times: two inverters from the input bits to the outputs.
  fast::Constraints constraints;
  constraints.clocks.push_back({"clk", "clk", 1000, 0, 0});
  constraints.outputs.push_back({"y0", "", 0, 5});
  constraints.outputs.push_back({"y1", "", 0, 5});
  fast::Engine engine(*bound.netlist, constraints);
  engine.evaluate();
  EXPECT_EQ(engine.report().endpoints, 2u);
  EXPECT_NEAR(engine.arrival(output("y0"), 0, 0), 10 + 2 * 5, 1e-3);  // a rising output of an inverter driving 5 fF, input slew 0
  EXPECT_NEAR(engine.arrival(output("y0"), 0, 1), 15 + 2 * 5, 1e-3);
}
