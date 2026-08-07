//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Unit tests for pass.analyze's three checks. Each builds the SMALLEST graph
// that exhibits the shape, because the value of this pass is that its verdict is
// specific: "there is a loop" was never the hard part — "which of the four
// repairs does this loop need" is.

#include "analyze.hpp"
#include "cell.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

using livehd::analyze::analyze_def;
using livehd::analyze::Clock_kind;
using livehd::analyze::Loop_kind;
using livehd::analyze::Opts;
using livehd::analyze::Report;
using livehd::graph_util::create_typed_node;

namespace {

Opts only(bool loops, bool clocks, bool colors) {
  Opts o;
  o.loops  = loops;
  o.clocks = clocks;
  o.colors = colors;
  return o;
}

}  // namespace

// An ordinary comb cone has no findings at all — the pass must be silent on a
// healthy design or its output is noise.
TEST(Analyze, CleanCombCone) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_analyze_test");
  auto  gio = lib.create_io("an_clean");
  gio->add_input("a", 1);
  gio->add_input("b", 2);
  gio->add_output("y", 3);
  auto g = gio->create_graph();

  auto sum = create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("y"));

  Report rep;
  analyze_def(g.get(), only(true, true, true), rep);
  EXPECT_TRUE(rep.loops.empty());
  EXPECT_TRUE(rep.clocks.empty());
  EXPECT_TRUE(rep.colors.empty());
  EXPECT_EQ(1, rep.n_defs);
}

// A genuine comb ring (`x = x + a`) must be found AND classified `other` — there
// is no memory, no instance and no packed op on it, so none of the structural
// repairs apply and the only honest answer is "this is a real loop".
TEST(Analyze, GenuineCombLoopIsOther) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_analyze_test");
  auto  gio = lib.create_io("an_loop");
  gio->add_input("a", 1);
  gio->add_output("y", 2);
  auto g = gio->create_graph();

  auto sum = create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  auto sum_o = sum.create_driver_pin(0);
  sum_o.connect_sink(sum.create_sink_pin(1));  // the ring
  sum_o.connect_sink(g->get_output_pin("y"));

  Report rep;
  analyze_def(g.get(), only(true, false, false), rep);
  ASSERT_EQ(1u, rep.loops.size());
  EXPECT_EQ(Loop_kind::other, rep.loops[0].kind);
  EXPECT_GE(rep.loops[0].n_on_cycle, 1);
  EXPECT_EQ("an_loop", rep.loops[0].def);
}

// The packed-slice shape: a Get_mask read of a Set_mask chain that feeds itself.
// This is the class `graph/split_selfref.cpp` dissolves, and telling it apart
// from a memory or an instance loop is what decides whether the splitter is even
// the right tool.
TEST(Analyze, PackedSliceLoopIsClassified) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_analyze_test");
  auto  gio = lib.create_io("an_packed");
  gio->add_input("a", 1);
  gio->add_output("y", 2);
  auto g = gio->create_graph();

  auto sm = create_typed_node(*g, Ntype_op::Set_mask);
  auto gm = create_typed_node(*g, Ntype_op::Get_mask);

  auto sm_o = sm.create_driver_pin(0);
  sm_o.connect_sink(gm.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Get_mask, "a"))));
  auto gm_o = gm.create_driver_pin(0);
  // the read feeds the chain's own value input -> a word-level ring
  gm_o.connect_sink(sm.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Set_mask, "value"))));
  g->get_input_pin("a").connect_sink(
      sm.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Set_mask, "a"))));
  sm_o.connect_sink(g->get_output_pin("y"));

  Report rep;
  analyze_def(g.get(), only(true, false, false), rep);
  ASSERT_EQ(1u, rep.loops.size());
  const auto& k = rep.loops[0].kinds;
  EXPECT_NE(k.end(), std::find(k.begin(), k.end(), Loop_kind::set_mask));
  EXPECT_NE(k.end(), std::find(k.begin(), k.end(), Loop_kind::get_mask));
}

// A flop wired straight to a clock INPUT is the ordinary case: `sim_can_lower`
// must say yes, and with verbose off the pass must not even mention it.
TEST(Analyze, PlainClockIsNotAFinding) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_analyze_test");
  auto  gio = lib.create_io("an_clk");
  gio->add_input("clk", 1);
  gio->add_input("d", 2);
  gio->add_output("q", 3);
  auto g = gio->create_graph();

  auto ff = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("clk").connect_sink(
      ff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin"))));
  g->get_input_pin("d").connect_sink(
      ff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"))));
  ff.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

  Report rep;
  analyze_def(g.get(), only(false, true, false), rep);
  EXPECT_TRUE(rep.clocks.empty()) << "a plain clock port must not be reported";
  EXPECT_EQ(1, rep.n_state_elements);

  Opts v = only(false, true, false);
  v.verbose = true;
  Report vrep;
  analyze_def(g.get(), v, vrep);
  ASSERT_EQ(1u, vrep.clocks.size());
  EXPECT_EQ(Clock_kind::plain_input, vrep.clocks[0].kind);
  EXPECT_TRUE(livehd::analyze::sim_can_lower(vrep.clocks[0].kind));
}

// A flop clocked by a Sub OUTPUT is the hierarchy-crossing case: the gate lives
// in a callee, so no per-definition analysis can fold it. This is the shape that
// accounts for every one of minion's `gated-clock-unsupported` sites, and the
// pass must name it as such rather than as a generic "derived clock".
TEST(Analyze, ClockFromSubIsReported) {
  auto& lib  = livehd::Hhds_graph_library::instance("lgdb_analyze_test");
  auto  cgio = lib.create_io("an_gate_cell");
  cgio->add_input("clk_i", 1);
  cgio->add_output("clk_o", 2);
  auto cg = cgio->create_graph();
  cg->get_input_pin("clk_i").connect_sink(cg->get_output_pin("clk_o"));
  (void)cg;

  auto gio = lib.create_io("an_clk_sub");
  gio->add_input("clk", 1);
  gio->add_input("d", 2);
  gio->add_output("q", 3);
  auto g = gio->create_graph();

  auto inst = create_typed_node(*g, Ntype_op::Sub);
  inst.set_subnode(cgio);
  g->get_input_pin("clk").connect_sink(inst.create_sink_pin(1));

  auto ff = create_typed_node(*g, Ntype_op::Flop);
  inst.create_driver_pin(2).connect_sink(
      ff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin"))));
  g->get_input_pin("d").connect_sink(
      ff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"))));
  ff.create_driver_pin(0).connect_sink(g->get_output_pin("q"));

  Report rep;
  analyze_def(g.get(), only(false, true, false), rep);
  ASSERT_EQ(1u, rep.clocks.size());
  // The callee is a bare passthrough, NOT a recognized ICG cell, so this is the
  // opaque case. A callee that DID match `match_icg_def` classifies as
  // `gate_cell` and is lowerable — cgen_sim inlines it.
  EXPECT_EQ(Clock_kind::from_sub, rep.clocks[0].kind);
  EXPECT_FALSE(livehd::analyze::sim_can_lower(rep.clocks[0].kind));
}

// THE SILENT CLASS: a gate in THIS definition whose output crosses into a child
// as that child's clock port. Neither definition can see it alone — the parent
// sees an ordinary Sub input, the child sees an ordinary graph input — so the
// child commits every tick and the gate is dead code. This is the finding that
// justifies surveying a whole library.
TEST(Analyze, GateIntoChildClockPortIsReported) {
  auto& lib  = livehd::Hhds_graph_library::instance("lgdb_analyze_test");
  auto  cgio = lib.create_io("an_child_clocked");
  cgio->add_input("gclk", 1);
  cgio->add_input("d", 2);
  cgio->add_output("q", 3);
  auto cg = cgio->create_graph();
  {
    auto cff = create_typed_node(*cg, Ntype_op::Flop);
    cg->get_input_pin("gclk").connect_sink(
        cff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin"))));
    cg->get_input_pin("d").connect_sink(
        cff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"))));
    cff.create_driver_pin(0).connect_sink(cg->get_output_pin("q"));
  }

  auto gio = lib.create_io("an_gate_down");
  gio->add_input("clk", 1);
  gio->add_input("en", 2);
  gio->add_input("d", 3);
  gio->add_output("q", 4);
  auto g = gio->create_graph();

  // A flop on the plain clock, so `clk` is a recognized clock ROOT here.
  auto ff = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("clk").connect_sink(
      ff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "clock_pin"))));
  g->get_input_pin("d").connect_sink(
      ff.create_sink_pin(static_cast<hhds::Port_id>(Ntype::get_sink_pid(Ntype_op::Flop, "din"))));

  // ...and the GATE, feeding the child's clock port.
  auto gate = create_typed_node(*g, Ntype_op::And);
  g->get_input_pin("clk").connect_sink(gate.create_sink_pin(0));
  g->get_input_pin("en").connect_sink(gate.create_sink_pin(0));

  auto inst = create_typed_node(*g, Ntype_op::Sub);
  inst.set_subnode(cgio);
  gate.create_driver_pin(0).connect_sink(inst.create_sink_pin(1));
  g->get_input_pin("d").connect_sink(inst.create_sink_pin(2));
  inst.create_driver_pin(3).connect_sink(g->get_output_pin("q"));

  Report rep;
  analyze_def(g.get(), only(false, true, false), rep);
  bool found = false;
  for (const auto& f : rep.clocks) {
    if (f.kind == Clock_kind::gates_child_port) {
      found = true;
      EXPECT_EQ("Sub", f.op);
      EXPECT_GE(f.n_guards, 1);
      EXPECT_FALSE(livehd::analyze::sim_can_lower(f.kind));
    }
  }
  EXPECT_TRUE(found) << "a gate feeding a child's clock port must be reported";
}
