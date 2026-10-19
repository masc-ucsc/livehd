// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass/sweep: flatten, unknown refinement and the state sweep (constant and
// unobserved flop bits), each on a graph built by hand.
#include <memory>
#include <vector>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "sweep.hpp"

namespace {
namespace gu = livehd::graph_util;

hhds::Pin_class constant(hhds::Graph& g, int64_t v) { return gu::create_const(g, *Dlop::create_integer(v)); }

// A depth-1 flop of `bits` bits: clock `clk`, async active-low reset `rst` to `reset`, and `din`.
hhds::Node_class make_flop(hhds::Graph& g, int bits, int64_t reset, const hhds::Pin_class& din, const hhds::Pin_class& enable = {}) {
  auto flop = gu::create_typed_node(g, Ntype_op::Flop);
  auto q    = flop.create_driver_pin(0);
  gu::set_ubits(q, bits);
  gu::setup_sink_by_name(flop, "clock_pin").connect_driver(g.get_input_pin("clk"));
  gu::setup_sink_by_name(flop, "reset_pin").connect_driver(g.get_input_pin("rst"));
  gu::setup_sink_by_name(flop, "negreset").connect_driver(constant(g, 1));
  gu::setup_sink_by_name(flop, "async").connect_driver(constant(g, 1));
  gu::setup_sink_by_name(flop, "initial").connect_driver(constant(g, reset));
  gu::setup_sink_by_name(flop, "din").connect_driver(din);
  if (!enable.is_invalid()) {
    gu::setup_sink_by_name(flop, "enable").connect_driver(enable);
  }
  return flop;
}

std::shared_ptr<hhds::Graph> make_graph(const char* lib_name, const char* name, int out_bits) {
  auto& lib = livehd::Hhds_graph_library::instance(lib_name);
  auto  io  = lib.create_io(name);
  io->add_input("clk", 1);
  io->add_input("rst", 2);
  io->add_input("d", 3);
  io->set_bits("d", out_bits);
  io->add_input("en", 4);
  io->add_output("q", 5);
  io->set_bits("q", out_bits);
  io->add_output("r", 6);
  io->set_bits("r", out_bits);
  return io->create_graph();
}

size_t count_op(const hhds::Graph& g, Ntype_op op) {
  size_t n = 0;
  for (auto node : g.body().nodes()) {
    n += gu::type_op_of(node) == op ? 1 : 0;
  }
  return n;
}

TEST(SweepUnknown, ClearsUnknownBitsInLogicButKeepsFlopInitial) {
  auto graph = make_graph("lgdb_sweep_unknown", "unknown", 4);
  auto x     = gu::create_const(*graph, *Dlop::from_binary("1?0?", true));
  auto andn  = gu::create_typed_node(*graph, Ntype_op::And);
  gu::setup_sink_by_name(andn, "as").connect_driver(graph->get_input_pin("d"));
  gu::setup_sink_by_name(andn, "as").connect_driver(x);
  auto out = andn.create_driver_pin(0);
  gu::set_ubits(out, 4);
  out.connect_sink(graph->get_output_pin("q"));
  // a flop whose `initial` is unknown means "no power-on value": it must stay unknown
  auto flop = gu::create_typed_node(*graph, Ntype_op::Flop);
  gu::setup_sink_by_name(flop, "clock_pin").connect_driver(graph->get_input_pin("clk"));
  gu::setup_sink_by_name(flop, "din").connect_driver(graph->get_input_pin("d"));
  auto unknown_init = gu::create_const(*graph, *Dlop::from_binary("??", true));
  gu::setup_sink_by_name(flop, "initial").connect_driver(unknown_init);
  auto q = flop.create_driver_pin(0);
  gu::set_ubits(q, 4);
  q.connect_sink(graph->get_output_pin("r"));

  EXPECT_EQ(livehd::sweep::refine_unknown(*graph), 1u);
  for (auto sink : andn.inp_sorted_pins()) {
    for (auto d : sink.get_driver_pins()) {
      if (d.is_const()) {
        EXPECT_FALSE(gu::const_of(d).has_unknowns());
        EXPECT_EQ(gu::const_of(d).to_just_i64(), 8);  // 1?0? read as 1000
      }
    }
  }
  EXPECT_TRUE(gu::const_of(gu::get_driver_of_sink_name(flop, "initial")).has_unknowns());
}

TEST(SweepFlatten, SplicesTheCalleeBodyAndKeepsBlackBoxes) {
  auto& lib      = livehd::Hhds_graph_library::instance("lgdb_sweep_flatten");
  auto  child_io = lib.create_io("child");
  child_io->add_input("a", 1);
  child_io->set_bits("a", 8);
  child_io->add_output("y", 2);
  child_io->set_bits("y", 8);
  auto child = child_io->create_graph();
  auto mask  = gu::create_typed_node(*child, Ntype_op::And);
  gu::setup_sink_by_name(mask, "as").connect_driver(child->get_input_pin("a"));
  gu::setup_sink_by_name(mask, "as").connect_driver(constant(*child, 0x0f));
  auto masked = mask.create_driver_pin(0);
  gu::set_ubits(masked, 8);
  masked.connect_sink(child->get_output_pin("y"));

  auto top_io = lib.create_io("top");
  top_io->add_input("a", 1);
  top_io->set_bits("a", 8);
  top_io->add_output("y", 2);
  top_io->set_bits("y", 8);
  auto top  = top_io->create_graph();
  auto inst = gu::create_typed_node(*top, Ntype_op::Sub);
  inst.set_subnode(child_io);
  top->get_input_pin("a").connect_sink(inst.create_sink_pin(child_io->get_input_port_id("a")));
  auto out = inst.create_driver_pin(child_io->get_output_port_id("y"));
  gu::set_ubits(out, 8);
  out.connect_sink(top->get_output_pin("y"));

  const auto stats = livehd::sweep::flatten({top});
  EXPECT_EQ(stats.inlined, 1u);
  EXPECT_EQ(stats.kept, 0u);
  EXPECT_EQ(count_op(*top, Ntype_op::Sub), 0u);
  EXPECT_EQ(count_op(*top, Ntype_op::And), 1u);
  // the callee definition is untouched and still reachable only through the library
  EXPECT_EQ(count_op(*child, Ntype_op::And), 1u);
  EXPECT_EQ(livehd::sweep::reachable_closure({top}).size(), 1u);
}

TEST(SweepState, HoldRegisterAtItsResetValueBecomesAConstant) {
  auto graph = make_graph("lgdb_sweep_hold", "hold", 4);
  auto flop  = gu::create_typed_node(*graph, Ntype_op::Flop);  // placeholder to get Q before din exists
  flop.del_node();
  auto probe = make_flop(*graph, 4, 0, constant(*graph, 0));  // din placeholder, rewired below
  auto q     = probe.get_driver_pin(0);
  gu::setup_sink_by_name(probe, "din").del_sink();
  gu::setup_sink_by_name(probe, "din").connect_driver(q);  // Q <= Q: it only ever holds the reset value
  q.connect_sink(graph->get_output_pin("q"));
  constant(*graph, 0).connect_sink(graph->get_output_pin("r"));

  const auto stats = livehd::sweep::sweep_state(*graph);
  EXPECT_EQ(stats.const_bits, 4u);
  EXPECT_EQ(count_op(*graph, Ntype_op::Flop), 0u);
  const auto driver = graph->get_output_pin("q").get_driver_pin();
  ASSERT_TRUE(driver.is_const());
  EXPECT_EQ(gu::const_of(driver).to_just_i64(), 0);
}

TEST(SweepState, PartiallyConstantRegisterKeepsOnlyItsLiveBits) {
  auto graph = make_graph("lgdb_sweep_partial", "partial", 4);
  // din = {d[1:0], 2'b00}: the low two bits only ever load 0, which is their reset value
  auto low  = gu::create_get_mask(*graph, graph->get_input_pin("d"), 0, 2);
  auto lowp = low.create_driver_pin(0);
  gu::set_ubits(lowp, 2);
  auto cat = gu::create_typed_node(*graph, Ntype_op::Concat);
  lowp.connect_sink(cat.create_sink_pin(0));
  constant(*graph, 2).connect_sink(cat.create_sink_pin(1));
  constant(*graph, 0).connect_sink(cat.create_sink_pin(2));
  constant(*graph, 2).connect_sink(cat.create_sink_pin(3));
  auto din = cat.create_driver_pin(0);
  gu::set_ubits(din, 4);
  auto flop = make_flop(*graph, 4, 0, din, graph->get_input_pin("en"));
  flop.get_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  constant(*graph, 0).connect_sink(graph->get_output_pin("r"));

  const auto stats = livehd::sweep::sweep_state(*graph);
  EXPECT_EQ(stats.const_bits, 2u);
  ASSERT_EQ(count_op(*graph, Ntype_op::Flop), 1u);
  for (auto node : graph->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Flop) {
      EXPECT_EQ(gu::bits_of(node.get_driver_pin(0)), 2);
    }
  }
  EXPECT_FALSE(graph->get_output_pin("q").get_driver_pin().is_const());
}

TEST(SweepState, TwoRegistersThatOnlyKeepEachOtherAtZeroAreProvenByInduction) {
  auto graph = make_graph("lgdb_sweep_ring", "ring", 1);
  // a <= b & x, b <= a & y: each next state is 0 whenever the other is 0
  auto a = gu::create_typed_node(*graph, Ntype_op::Flop);
  auto b = gu::create_typed_node(*graph, Ntype_op::Flop);
  gu::set_ubits(a.create_driver_pin(0), 1);
  gu::set_ubits(b.create_driver_pin(0), 1);
  const auto wire = [&](hhds::Node_class& flop, const hhds::Pin_class& other_q, const char* input) {
    auto g = gu::create_typed_node(*graph, Ntype_op::And);
    gu::setup_sink_by_name(g, "as").connect_driver(other_q);
    gu::setup_sink_by_name(g, "as").connect_driver(graph->get_input_pin(input));
    auto out = g.create_driver_pin(0);
    gu::set_ubits(out, 1);
    gu::setup_sink_by_name(flop, "clock_pin").connect_driver(graph->get_input_pin("clk"));
    gu::setup_sink_by_name(flop, "reset_pin").connect_driver(graph->get_input_pin("rst"));
    gu::setup_sink_by_name(flop, "negreset").connect_driver(constant(*graph, 1));
    gu::setup_sink_by_name(flop, "async").connect_driver(constant(*graph, 1));
    gu::setup_sink_by_name(flop, "initial").connect_driver(constant(*graph, 0));
    gu::setup_sink_by_name(flop, "din").connect_driver(out);
  };
  wire(a, b.get_driver_pin(0), "d");
  wire(b, a.get_driver_pin(0), "en");
  a.get_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  b.get_driver_pin(0).connect_sink(graph->get_output_pin("r"));

  const auto stats = livehd::sweep::sweep_state(*graph);
  EXPECT_EQ(stats.const_bits, 2u);
  EXPECT_EQ(count_op(*graph, Ntype_op::Flop), 0u);
}

TEST(SweepState, RegisterThatLoadsAnInputIsLeftAlone) {
  auto graph = make_graph("lgdb_sweep_live", "live", 4);
  auto flop  = make_flop(*graph, 4, 0, graph->get_input_pin("d"));
  flop.get_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  constant(*graph, 0).connect_sink(graph->get_output_pin("r"));
  const auto stats = livehd::sweep::sweep_state(*graph);
  EXPECT_EQ(stats.const_bits, 0u);
  EXPECT_EQ(stats.dead_bits, 0u);
  EXPECT_FALSE(stats.changed());
  EXPECT_EQ(count_op(*graph, Ntype_op::Flop), 1u);
}

TEST(SweepState, StateNothingObservesIsRemovedAndObservedStateKept) {
  auto graph = make_graph("lgdb_sweep_dead", "dead", 4);
  // dead: Q feeds only its own hold mux; live: Q reaches an output
  auto dead = gu::create_typed_node(*graph, Ntype_op::Flop);
  auto dq   = dead.create_driver_pin(0);
  gu::set_ubits(dq, 4);
  auto hold = gu::create_typed_node(*graph, Ntype_op::Mux);
  graph->get_input_pin("en").connect_sink(hold.create_sink_pin(0));
  dq.connect_sink(hold.create_sink_pin(1));
  graph->get_input_pin("d").connect_sink(hold.create_sink_pin(2));
  auto hq = hold.create_driver_pin(0);
  gu::set_ubits(hq, 4);
  gu::setup_sink_by_name(dead, "clock_pin").connect_driver(graph->get_input_pin("clk"));
  gu::setup_sink_by_name(dead, "din").connect_driver(hq);

  auto live = make_flop(*graph, 4, 0, graph->get_input_pin("d"));
  live.get_driver_pin(0).connect_sink(graph->get_output_pin("q"));
  constant(*graph, 0).connect_sink(graph->get_output_pin("r"));

  const auto stats = livehd::sweep::sweep_state(*graph);
  EXPECT_EQ(stats.dead_bits, 4u);
  EXPECT_EQ(count_op(*graph, Ntype_op::Flop), 1u);
}

}  // namespace
