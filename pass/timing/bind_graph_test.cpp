// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bind_graph.hpp"

#include <format>
#include <string>

#include "cell.hpp"
#include "engine.hpp"
#include "gtest/gtest.h"
#include "hhds/attrs/name.hpp"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "pass/partition/flatten.hpp"
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

// The hierarchy is bound in place: the same cells, names and drivers as binding a flattened copy, with the connections
// resolved across the instance boundaries, and a declared port acting as a cast.
TEST(BindGraph, HierarchyIsBoundInPlaceLikeAFlattenedCopy) {
  Library_fixture    cells;
  hhds::GraphLibrary library;
  auto               inv = library.create_io("INV");
  inv->add_input("A", 1);
  inv->add_output("Y", 2);
  const auto cell = [&](hhds::Graph& g, const char* name) {
    auto node = gu::create_typed_node(g, Ntype_op::Sub);
    node.set_subnode(inv);
    node.attr(hhds::attrs::name).set(name);
    return node;
  };
  const auto instance = [&](hhds::Graph& g, const std::shared_ptr<hhds::GraphIO>& io, const char* name) {
    auto node = gu::create_typed_node(g, Ntype_op::Sub);
    node.set_subnode(io);
    node.attr(hhds::attrs::name).set(name);
    return node;
  };

  // stage: y = !a[1]
  auto stage = library.create_io("stage");
  stage->add_input("a", 1);
  stage->set_bits("a", 2);
  stage->add_output("y", 2);
  auto sg = stage->create_graph();
  {
    const auto u = cell(*sg, "u");
    gu::create_get_mask(*sg, sg->get_input_pin("a"), 1).create_driver_pin(0).connect_sink(u.create_sink_pin("A"));
    u.create_driver_pin("Y").connect_sink(sg->get_output_pin("y"));
  }
  // narrow: its port n is one unsigned bit, so bit 1 of n reads 0 whatever drives it; signed (snarrow), bit 1 repeats
  // bit 0.
  const auto narrow_module = [&](const char* module, bool unsign) {
    auto io = library.create_io(module);
    io->add_input("n", 1);
    io->set_bits("n", 1);
    io->set_unsign("n", unsign);
    io->add_output("z", 2);
    auto g = io->create_graph();
    const auto w = cell(*g, "w");
    gu::create_get_mask(*g, g->get_input_pin("n"), 1).create_driver_pin(0).connect_sink(w.create_sink_pin("A"));
    w.create_driver_pin("Y").connect_sink(g->get_output_pin("z"));
    return io;
  };
  auto narrow = narrow_module("narrow", true), snarrow = narrow_module("snarrow", false);
  // top: two stages fed by d, an inverter after the first, the narrow module fed by d too.
  auto top = library.create_io("top");
  top->add_input("d", 1);
  top->set_bits("d", 2);
  top->add_output("o0", 2);
  top->add_output("o1", 3);
  top->add_output("o2", 4);
  top->add_output("o3", 5);
  auto tg = top->create_graph();
  const auto s0 = instance(*tg, stage, "s0"), s1 = instance(*tg, stage, "s1"), nw = instance(*tg, narrow, "nw"),
             sn = instance(*tg, snarrow, "sn");
  for (const auto& s : {s0, s1}) {
    tg->get_input_pin("d").connect_sink(s.create_sink_pin(1));
  }
  tg->get_input_pin("d").connect_sink(nw.create_sink_pin(1));
  tg->get_input_pin("d").connect_sink(sn.create_sink_pin(1));
  const auto t = cell(*tg, "t");
  s0.create_driver_pin(2).connect_sink(t.create_sink_pin("A"));
  t.create_driver_pin("Y").connect_sink(tg->get_output_pin("o0"));
  s1.create_driver_pin(2).connect_sink(tg->get_output_pin("o1"));
  nw.create_driver_pin(2).connect_sink(tg->get_output_pin("o2"));
  sn.create_driver_pin(2).connect_sink(tg->get_output_pin("o3"));

  const auto  hier = fast::bind_hier(*tg, cells.types);
  const auto& net  = *hier.netlist;
  EXPECT_TRUE(hier.hierarchical());
  EXPECT_EQ(hier.mapped_cells, 5u);
  EXPECT_EQ(hier.constant_sinks, 1u);  // w.A: bit 1 of a one-bit port
  const auto cell_named = [](const fast::Bound_graph& b, std::string_view name) {
    for (uint32_t c = 0; c < b.netlist->cells(); ++c) {
      if (!b.cell_node[c].is_invalid() && b.name(c) == name) {
        return c;
      }
    }
    return fast::no_index;
  };
  const auto driver_name = [](const fast::Bound_graph& b, uint32_t at, const char* pin) -> std::string {
    const auto& n      = *b.netlist;
    const auto  net_id = n.net_of(n.pin(at, pin));
    if (net_id == fast::no_index) {
      return "-";
    }
    const auto driver = n.driver_of(net_id);
    const auto owner  = n.cell_of(driver);
    if (b.cell_node[owner].is_invalid()) {
      for (const auto& port : n.ports()) {
        if (port.cell == owner) {
          return port.name;
        }
      }
    }
    return std::format("{}:{}", b.name(owner), n.pin_type(driver).name);
  };
  for (const char* name : {"s0.u", "s1.u", "t", "nw.w", "sn.w"}) {
    EXPECT_NE(cell_named(hier, name), fast::no_index) << name;
  }
  EXPECT_EQ(driver_name(hier, cell_named(hier, "s0.u"), "A"), "d[1]");
  EXPECT_EQ(driver_name(hier, cell_named(hier, "s1.u"), "A"), "d[1]");
  EXPECT_EQ(driver_name(hier, cell_named(hier, "t"), "A"), "s0.u:Y");  // down into s0
  EXPECT_EQ(driver_name(hier, cell_named(hier, "nw.w"), "A"), "-");     // unsigned: a zero
  EXPECT_EQ(driver_name(hier, cell_named(hier, "sn.w"), "A"), "d[0]");  // signed: the sign bit
  for (const auto& port : net.ports()) {
    if (port.name == "o1") {
      const auto driver = net.driver_of(net.net_of(net.pin(port.cell, 0u)));
      EXPECT_EQ(hier.name(net.cell_of(driver)), "s1.u");
    }
  }

  // A flattened copy binds to the same design.
  auto flat = livehd::partition::flatten_hierarchy(tg.get(), &library, "top__flat");
  ASSERT_NE(flat, nullptr);
  const auto flat_bound = fast::bind_graph(*flat, cells.types);
  EXPECT_EQ(flat_bound.mapped_cells, hier.mapped_cells);
  EXPECT_EQ(flat_bound.constant_sinks, hier.constant_sinks);
  EXPECT_EQ(flat_bound.netlist->ports().size(), net.ports().size());
  for (const char* name : {"s0.u", "s1.u", "t", "nw.w", "sn.w"}) {
    const auto c = cell_named(flat_bound, name);
    ASSERT_NE(c, fast::no_index) << name;
    EXPECT_EQ(driver_name(flat_bound, c, "A"), driver_name(hier, cell_named(hier, name), "A")) << name;
  }
}

// A macro's bus pins (pass/liberty expands a Liberty bus into one pin per bit, `D[0]`, `D[1]`) bind bit by bit: an
// input bus member is driven by that bit of the instance pin's driver and checked against its setup arc, and an output
// bus member drives that bit of the instance output.
TEST(BindGraph, MacroBusPinsBindPerBit) {
  const std::string text = R"lib(
library(bus) {
  time_unit : "1ps";
  capacitive_load_unit(1,ff);
  type(bus2) { base_type : array; data_type : bit; bit_width : 2; bit_from : 1; bit_to : 0; downto : true; }
  cell(RAM) {
    area : 100;
    memory() { type : ram; address_width : 1; word_width : 2; }
    pin(CLK) { direction : input; clock : true; capacitance : 1; }
    bus(D) { bus_type : bus2; direction : input; capacitance : 1;
      timing() { related_pin : CLK; timing_type : setup_rising;
        rise_constraint(scalar) { values("50"); } fall_constraint(scalar) { values("50"); } } }
    bus(Q) { bus_type : bus2; direction : output;
      timing() { related_pin : CLK; timing_type : rising_edge; timing_sense : non_unate;
        cell_rise(scalar) { values("100"); } cell_fall(scalar) { values("100"); }
        rise_transition(scalar) { values("10"); } fall_transition(scalar) { values("10"); } } }
  }
}
)lib";
  lt::Library        lib  = lt::parse(text);
  const lt::Library* ps[] = {&lib};
  const std::string  ns[] = {"typ"};
  const auto         types = fast::Cell_types::build(ps, ns);

  hhds::GraphLibrary library;
  auto               ram = library.create_io("RAM");
  ram->add_input("CLK", 1);
  ram->add_input("D", 2);
  ram->set_bits("D", 2);
  ram->add_output("Q", 3);
  ram->set_bits("Q", 2);
  auto top = library.create_io("top");
  top->add_input("clk", 1);
  top->add_input("d", 2);
  top->set_bits("d", 2);
  top->add_output("y", 3);
  top->set_bits("y", 2);
  auto g = top->create_graph();
  auto u = gu::create_typed_node(*g, Ntype_op::Sub);
  u.set_subnode(ram);
  g->get_input_pin("clk").connect_sink(u.create_sink_pin("CLK"));
  g->get_input_pin("d").connect_sink(u.create_sink_pin("D"));
  u.create_driver_pin("Q").connect_sink(g->get_output_pin("y"));

  const auto  bound = fast::bind_graph(*g, types);
  const auto& net   = *bound.netlist;
  for (const auto& line : bound.unsupported) {
    EXPECT_EQ(line.find("missing Liberty pin"), std::string::npos) << line;
  }
  uint32_t cell = fast::no_index;
  for (uint32_t c = 0; c < net.cells(); ++c) {
    if (!bound.cell_node[c].is_invalid() && bound.cell_node[c] == u) {
      cell = c;
    }
  }
  ASSERT_NE(cell, fast::no_index);
  const auto port = [&](const char* name) {
    for (const auto& p : net.ports()) {
      if (p.name == name) {
        return net.pin(p.cell, 0u);
      }
    }
    return fast::no_index;
  };
  EXPECT_EQ(net.driver_of(net.net_of(net.pin(cell, "D[0]"))), port("d[0]"));
  EXPECT_EQ(net.driver_of(net.net_of(net.pin(cell, "D[1]"))), port("d[1]"));
  EXPECT_EQ(net.driver_of(net.net_of(port("y[0]"))), net.pin(cell, "Q[0]"));
  EXPECT_EQ(net.driver_of(net.net_of(port("y[1]"))), net.pin(cell, "Q[1]"));

  // The bus members are endpoints: a d input delay of 900 ps against a 1000 ps clock and a 50 ps setup leaves 50 ps.
  fast::Constraints constraints;
  constraints.clocks.push_back({"clk", "clk", 1000, 0, 0});
  constraints.inputs.push_back({"d[0]", "clk", 900, 0});
  constraints.inputs.push_back({"d[1]", "clk", 900, 0});
  fast::Engine engine(*bound.netlist, constraints);
  engine.evaluate();
  EXPECT_NEAR(engine.slack(net.pin(cell, "D[1]")), 50, 1e-3);
  // A synchronous memory launches like a flip-flop: the read bus is 100 ps after the clock.
  EXPECT_NEAR(engine.arrival(port("y[1]"), 0, 0), 100, 1e-3);
}
