// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// port_reach summaries on hand-built graphs: the per-output input cones, the
// record a definition carries for its callers (attrs::comb_reach, written by
// lnast.tolg), and the callee hook a client uses to splice a stored or a
// conservative summary instead of walking a callee body.

#include "port_reach.hpp"

#include <memory>

#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace gu = livehd::graph_util;
namespace pr = livehd::port_reach;

namespace {

using Pids = absl::flat_hash_set<uint32_t>;

// child(a:0, b:1) -> (x:2 = a & b, y:3 = flop(b)): `x` is combinational in both
// inputs, `y` in none (the flop cuts it).
std::shared_ptr<hhds::GraphIO> build_child(hhds::GraphLibrary& lib) {
  auto io = lib.create_io("child");
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->add_output("x", 2);
  io->add_output("y", 3);
  auto g = io->create_graph();

  auto andn = gu::create_typed_node(*g, Ntype_op::And);
  g->get_input_pin("a").connect_sink(andn.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(andn.create_sink_pin(1));
  andn.create_driver_pin(0).connect_sink(g->get_output_pin("x"));

  auto flop = gu::create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("b").connect_sink(gu::setup_sink_by_name(flop, "din"));
  flop.create_driver_pin(0).connect_sink(g->get_output_pin("y"));
  return io;
}

// parent(p:0, q:1) -> (o:2 = child(a=p, b=q).y)
std::shared_ptr<hhds::Graph> build_parent(hhds::GraphLibrary& lib, const std::shared_ptr<hhds::GraphIO>& child_io) {
  auto io = lib.create_io("parent");
  io->add_input("p", 0);
  io->add_input("q", 1);
  io->add_output("o", 2);
  auto g = io->create_graph();

  auto sub = gu::create_typed_node(*g, Ntype_op::Sub);
  sub.set_subnode(child_io);
  g->get_input_pin("p").connect_sink(sub.create_sink_pin(0));
  g->get_input_pin("q").connect_sink(sub.create_sink_pin(1));
  sub.create_driver_pin(3).connect_sink(g->get_output_pin("o"));
  return g;
}

}  // namespace

TEST(PortReach, FlopCutsTheCone) {
  auto& lib   = livehd::Hhds_graph_library::instance("lgdb_port_reach_cut");
  auto  child = build_child(lib)->get_graph();

  pr::Cache   cache;
  const auto& r = cache.of(child);
  EXPECT_EQ(r.out2ins.at(2), (Pids{0, 1}));
  EXPECT_TRUE(r.input_independent(3)) << "a flop read depends on no input";
}

TEST(PortReach, MixedMemoryCutsOnlyTheRegisteredRead) {
  hhds::GraphLibrary lib;
  auto               io = lib.create_io("mixed_reads");
  io->add_input("a", 1);
  io->add_output("async", 2);
  io->add_output("sync", 3);
  auto       graph = io->create_graph();
  auto       mem   = gu::create_typed_node(*graph, Ntype_op::Memory);
  const auto sink  = [&](const char* name, int port) {
    return mem.create_sink_pin(Ntype::get_sink_pid(Ntype_op::Memory, name) + port * Ntype::Memory_port_stride);
  };
  const auto number = [&](int value) { return gu::create_const(*graph, *Dlop::create_integer(value)); };
  number(Ntype::Memory_type_mixed).connect_sink(sink("type", 0));
  for (int i = 0; i < 2; ++i) {
    graph->get_input_pin("a").connect_sink(sink("addr", i));
    number(i == 0 ? Ntype::Memory_rdport_async : Ntype::Memory_rdport_sync).connect_sink(sink("rdport", i));
    mem.create_driver_pin(i).connect_sink(graph->get_output_pin(i == 0 ? "async" : "sync"));
  }
  for (bool slices : {false, true}) {
    pr::Cache   cache({}, slices);
    const auto& rows = cache.of(graph).out2ins;
    EXPECT_EQ(rows.at(2), (Pids{1}));
    EXPECT_TRUE(rows.at(3).empty());
  }
}

TEST(PortReach, StampRoundTrips) {
  auto& lib   = livehd::Hhds_graph_library::instance("lgdb_port_reach_stamp");
  auto  child = build_child(lib)->get_graph();

  EXPECT_FALSE(pr::stamped(*child).has_value()) << "an unstamped body reads as absent, not as independent";

  pr::Cache cache;
  pr::stamp(*child, cache.of(child));
  const auto back = pr::stamped(*child);
  ASSERT_TRUE(back.has_value());
  EXPECT_EQ(back->out2ins.at(2), (Pids{0, 1}));
  EXPECT_TRUE(back->input_independent(3));

  const auto all = pr::crossbar(*child);
  EXPECT_EQ(all.out2ins.at(2), (Pids{0, 1}));
  EXPECT_EQ(all.out2ins.at(3), (Pids{0, 1}));
}

TEST(PortReach, CalleeHookReplacesTheCalleeWalk) {
  auto& lib      = livehd::Hhds_graph_library::instance("lgdb_port_reach_hook");
  auto  child_io = build_child(lib);
  auto  parent   = build_parent(lib, child_io);

  pr::Cache walked;
  EXPECT_TRUE(walked.of(parent).input_independent(2)) << "the walk splices the child's cut `y`";

  // A client that cannot trust the child body substitutes a crossbar: `y`
  // then depends on both instance inputs, and so does the parent's `o`.
  pr::Cache hooked([](const std::shared_ptr<hhds::Graph>& g) { return std::optional<pr::Def_reach>(pr::crossbar(*g)); });
  EXPECT_EQ(hooked.of(parent).out2ins.at(2), (Pids{0, 1}));
  EXPECT_EQ(hooked.callee_of(child_io->get_graph()).out2ins.at(3), (Pids{0, 1}));
  EXPECT_TRUE(hooked.of(child_io->get_graph()).input_independent(3)) << "of() walks the graph it is handed";
}

TEST(PortReach, WideInstanceKeepsAllDriversAndSkipsUnconnectedPorts) {
  hhds::GraphLibrary lib;
  constexpr unsigned count    = 128;
  auto               child_io = lib.create_io("wide_child");
  for (unsigned i = 0; i < count; ++i) {
    child_io->add_input("in" + std::to_string(i), i);
  }
  constexpr unsigned outputs = 16;
  for (unsigned i = 0; i < outputs; ++i) {
    child_io->add_output("out" + std::to_string(i), count + i);
  }
  auto child = child_io->create_graph();
  auto join  = gu::create_typed_node(*child, Ntype_op::Or);
  for (unsigned i = 0; i < count; ++i) {
    child->get_input_pin("in" + std::to_string(i)).connect_sink(join.create_sink_pin(i));
  }
  for (unsigned i = 0; i < outputs; ++i) {
    join.create_driver_pin(0).connect_sink(child->get_output_pin("out" + std::to_string(i)));
  }

  auto parent_io = lib.create_io("wide_parent");
  parent_io->add_input("a", 0);
  parent_io->add_input("b", 1);
  parent_io->add_input("c", 2);
  parent_io->add_output("out", 3);
  auto parent = parent_io->create_graph();
  auto sub    = gu::create_typed_node(*parent, Ntype_op::Sub);
  sub.set_subnode(child_io);
  // Carry-input shape: both drivers must reach the caller's support.
  parent->get_input_pin("a").connect_sink(sub.create_sink_pin(0));
  parent->get_input_pin("b").connect_sink(sub.get_sink_pin(0));
  for (unsigned i = 1; i + 1 < count; ++i) {
    parent->get_input_pin("c").connect_sink(sub.create_sink_pin(i));
  }
  // The last declared child input is deliberately unconnected.
  auto collect = gu::create_typed_node(*parent, Ntype_op::Or);
  for (unsigned i = 0; i < outputs; ++i) {
    sub.create_driver_pin(count + i).connect_sink(collect.create_sink_pin(i));
  }
  collect.create_driver_pin(0).connect_sink(parent->get_output_pin("out"));

  pr::Cache cache;
  EXPECT_EQ(cache.of(parent).out2ins.at(3), (Pids{0, 1, 2}));
}

TEST(PortReach, PackedCalleeSlicesKeepEveryDriverOfTheSelectedPort) {
  hhds::GraphLibrary lib;
  auto               child = lib.create_io("pack");
  child->add_input("low", 0);
  child->add_input("high", 1);
  child->add_output("word", 2);
  child->set_bits("word", 2);
  auto body = child->create_graph();
  auto pack = gu::create_typed_node(*body, Ntype_op::Concat, 2);
  auto one  = gu::create_const(*body, *Dlop::create_integer(1));
  body->get_input_pin("high").connect_sink(pack.create_sink_pin(0));
  one.connect_sink(pack.create_sink_pin(1));
  body->get_input_pin("low").connect_sink(pack.create_sink_pin(2));
  one.connect_sink(pack.create_sink_pin(3));
  pack.create_driver_pin(0).connect_sink(body->get_output_pin("word"));

  auto io = lib.create_io("wrapper");
  io->add_input("a", 0);
  io->add_input("b", 1);
  io->add_input("c", 2);
  io->add_output("out", 3);
  io->set_bits("out", 2);
  auto graph = io->create_graph();
  auto inst  = gu::create_typed_node(*graph, Ntype_op::Sub);
  inst.set_subnode(child);
  auto low = inst.create_sink_pin(0);
  graph->get_input_pin("a").connect_sink(low);
  graph->get_input_pin("b").connect_sink(low);
  graph->get_input_pin("c").connect_sink(inst.create_sink_pin(1));
  inst.create_driver_pin(2).connect_sink(graph->get_output_pin("out"));

  pr::Cache   ports({}, false);
  pr::Cache   bits;
  const auto& summary = bits.of(graph);
  EXPECT_EQ(summary.out2ins.at(3), (Pids{0, 1, 2}));
  EXPECT_EQ(ports.of(graph).out2ins, summary.out2ins);
  const auto& slices = summary.out_slices.at(3);
  ASSERT_EQ(slices.size(), 2u);
  ASSERT_EQ(slices[0].ins.size(), 2u);
  EXPECT_EQ(slices[0].ins[0].pid, 0u);
  EXPECT_EQ(slices[0].ins[1].pid, 1u);
  EXPECT_EQ(slices[0].ins[0].len, 0u);
  EXPECT_EQ(slices[0].ins[1].len, 0u);
  ASSERT_EQ(slices[1].ins.size(), 1u);
  EXPECT_EQ(slices[1].ins[0].pid, 2u);
}

// A mapped netlist instantiates technology cells that have an interface and no
// body. A flop cell declares its state output `loop_break`: every instance is a
// loop break, so a def holding it must not read as combinational D -> Q (that
// closed false loops through a counter's registered outputs once the hierarchy
// kept the counter as a definition). A body-less cell without the mark stays
// combinational in all of its inputs.
TEST(PortReach, BodylessCellsFollowTheirLoopBreakDeclaration) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_port_reach_cells");

  auto dff = lib.create_io("DFF_CELL");
  dff->add_input("D", 1);
  dff->add_input("CLK", 2);
  dff->add_output("Q", 3, /*loop_break=*/true);
  auto inv = lib.create_io("INV_CELL");
  inv->add_input("A", 1);
  inv->add_output("Y", 2);

  auto io = lib.create_io("holder");
  io->add_input("d", 0);
  io->add_input("clk", 1);
  io->add_output("q", 2);
  io->add_output("n", 3);
  auto g = io->create_graph();
  auto f = gu::create_typed_node(*g, Ntype_op::Sub);
  f.set_subnode(dff);
  g->get_input_pin("d").connect_sink(f.create_sink_pin(1));
  g->get_input_pin("clk").connect_sink(f.create_sink_pin(2));
  f.create_driver_pin(3).connect_sink(g->get_output_pin("q"));
  auto i = gu::create_typed_node(*g, Ntype_op::Sub);
  i.set_subnode(inv);
  g->get_input_pin("d").connect_sink(i.create_sink_pin(1));
  i.create_driver_pin(2).connect_sink(g->get_output_pin("n"));

  pr::Cache cache;
  const auto& r = cache.of(g);
  EXPECT_TRUE(r.input_independent(2)) << "a registered output depends on no input";
  EXPECT_EQ(r.out2ins.at(3), (Pids{0})) << "an inverter cell stays combinational";
}
