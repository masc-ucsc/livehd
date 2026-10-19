//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "aegraph.hpp"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "cprop.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "query.hpp"

namespace {
namespace gu = livehd::graph_util;

// Every test runs the rule runtime's strongest check: any rewrite that does not
// preserve values aborts naming the rule.
livehd::aegraph::Options checked() {
  livehd::aegraph::Options o;
  o.check = livehd::aegraph::Options::Check::full;
  return o;
}

// A name-blind-free structural signature: every node's nid, op, name, color and
// each operand's driver (nid.pid, or the constant's spelling), sorted. With no
// rules the engine must leave this untouched except for CSE and constant folds,
// and a run twice must reproduce it exactly.
std::string signature(const std::shared_ptr<hhds::Graph>& g) {
  std::vector<std::string> lines;
  for (auto node : g->body().nodes()) {
    std::string line = std::to_string(node.get_debug_nid()) + ":" + std::to_string(static_cast<int>(gu::type_op_of(node))) + ":"
                       + std::string{gu::node_name_of(node)} + ":" + std::to_string(gu::color_of(node)) + ":"
                       + std::to_string(gu::has_proven(node) ? static_cast<int>(gu::proven_of(node)) : -1) + "<-";
    for (auto sink : node.inp_sorted_pins()) {
      line += std::to_string(sink.get_port_id()) + "=";
      for (const auto& driver : sink.get_driver_pins()) {
        if (driver.is_const()) {
          line += "c" + gu::const_of(driver).to_pyrope() + ",";
        } else {
          line += std::to_string(driver.get_master_node().get_debug_nid()) + "." + std::to_string(driver.get_port_id()) + ",";
        }
      }
      line += ";";
    }
    lines.push_back(std::move(line));
  }
  std::sort(lines.begin(), lines.end());
  std::string out;
  for (const auto& l : lines) {
    out += l + "\n";
  }
  return out;
}

size_t count_op(const std::shared_ptr<hhds::Graph>& g, Ntype_op op) {
  size_t n = 0;
  for (auto node : g->body().nodes()) {
    if (gu::type_op_of(node) == op) {
      ++n;
    }
  }
  return n;
}

hhds::Pin_class out_driver(const std::shared_ptr<hhds::Graph>& g, const char* name) {
  return g->get_output_pin(name).get_driver_pin();
}

std::shared_ptr<hhds::Graph> io_graph(hhds::GraphLibrary& lib, std::string_view name, int inputs, int outputs) {
  auto io = lib.create_io(std::string{name});
  int  pid = 0;
  for (int i = 0; i < inputs; ++i) {
    io->add_input("i" + std::to_string(i), pid++);
    io->set_bits("i" + std::to_string(i), 8);
    io->set_unsign("i" + std::to_string(i), true);
  }
  for (int i = 0; i < outputs; ++i) {
    io->add_output("o" + std::to_string(i), pid++);
    io->set_bits("o" + std::to_string(i), 8);
    io->set_unsign("o" + std::to_string(i), true);
  }
  return io->create_graph();
}

hhds::Pin_class konst(const std::shared_ptr<hhds::Graph>& g, int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); }

hhds::Pin_class binop(const std::shared_ptr<hhds::Graph>& g, Ntype_op op, const hhds::Pin_class& a, const hhds::Pin_class& b) {
  auto n = gu::create_typed_node(*g, op);
  a.connect_sink(gu::setup_sink_by_name(n, "as"));
  b.connect_sink(gu::setup_sink_by_name(n, "as"));
  // An unstamped non-Mux output reads as one bit in the LEC encoder: stamp the
  // interior cells like a lowered design would be after bitwidth.
  gu::set_ubits(n.create_driver_pin(0), 8);
  return n.create_driver_pin(0);
}

// ---- no rules: the round trip is the identity --------------------------------

// A graph with nothing to share and nothing to fold: every node survives with
// its nid, name and operands.
TEST(Aegraph, NoRuleRoundTripIsStructurallyIdentical) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "identity", 3, 2);
  auto               s = binop(g, Ntype_op::Sum, g->get_input_pin("i0"), g->get_input_pin("i1"));
  auto               m = binop(g, Ntype_op::Mult, s, g->get_input_pin("i2"));
  auto               x = binop(g, Ntype_op::Xor, m, g->get_input_pin("i0"));
  auto               mux = gu::create_typed_node(*g, Ntype_op::Mux);
  g->get_input_pin("i2").connect_sink(mux.create_sink_pin(0));
  x.connect_sink(mux.create_sink_pin(1));
  s.connect_sink(mux.create_sink_pin(2));
  mux.set_name("named_mux");
  gu::set_color(mux, 3);
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
  auto flop = gu::create_typed_node(*g, Ntype_op::Flop);
  gu::setup_sink_by_name(flop, "din").connect_driver(mux.create_driver_pin(0));
  flop.create_driver_pin(0).connect_sink(g->get_output_pin("o1"));

  const auto before = signature(g);
  const auto stats  = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(signature(g), before);
  EXPECT_EQ(stats.cse_merged, 0u);
  EXPECT_EQ(stats.const_folded, 0u);
  EXPECT_EQ(stats.dead_removed, 0u);
  EXPECT_EQ(stats.nodes_in, stats.nodes_out);
  EXPECT_GE(stats.pure, 4u);
}

// A compact loop (Subnode_loop) and its body: the Sub is a skeleton leaf, the
// carry self-edge survives, and both bodies keep their exact structure.
TEST(Aegraph, SubnodeLoopBodyRoundTrip) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_aegraph_loop");

  auto body_io = lib.create_io("aeg_loop_body");
  body_io->add_input("inv", 0);
  body_io->add_input("i", 1);
  body_io->add_input("acc_in", 2);
  body_io->add_output("acc_out", 3);
  for (const auto* name : {"inv", "acc_in", "acc_out"}) {
    body_io->set_bits(name, 8);
    body_io->set_unsign(name, true);
  }
  body_io->set_bits("i", 4);
  auto body = body_io->create_graph();
  auto t    = gu::create_typed_node(*body, Ntype_op::Not);
  body->get_input_pin("inv").connect_sink(t.create_sink_pin(0));
  gu::set_ubits(t.create_driver_pin(0), 8);
  auto m = binop(body, Ntype_op::And, t.create_driver_pin(0), body->get_input_pin("i"));
  gu::set_ubits(m, 8);
  auto x = binop(body, Ntype_op::Xor, m, body->get_input_pin("acc_in"));
  gu::set_ubits(x, 8);
  x.connect_sink(body->get_output_pin("acc_out"));

  auto parent_io = lib.create_io("aeg_loop_parent");
  parent_io->add_input("seed", 0);
  parent_io->add_input("x", 1);
  parent_io->add_output("result", 2);
  parent_io->set_bits("seed", 8);
  parent_io->set_bits("x", 8);
  parent_io->set_bits("result", 8);
  auto parent = parent_io->create_graph();
  auto loop   = gu::create_typed_node(*parent, Ntype_op::Sub);
  loop.set_name("u_loop");
  hhds::Subnode_loop desc;
  desc.count       = 4;
  desc.index_input = hhds::Port_id{1};
  loop.set_subnode(body_io, desc);
  // A pure producer in front of the loop: it is the operand of a skeleton.
  auto pre = binop(parent, Ntype_op::Sum, parent->get_input_pin("x"), konst(parent, 1));
  pre.connect_sink(loop.create_sink_pin(0));
  parent->get_input_pin("seed").connect_sink(loop.create_sink_pin(2));
  auto out = loop.create_driver_pin(3);
  gu::set_ubits(out, 8);
  out.connect_sink(parent->get_output_pin("result"));
  out.connect_sink(loop.create_sink_pin(2));  // the carry: acc_out -> acc_in
  loop.subnode_group().validate();

  for (const auto& g : {body, parent}) {
    const auto before = signature(g);
    livehd::aegraph::optimize(g, {}, checked());
    EXPECT_EQ(signature(g), before) << g->get_io()->get_name();
  }
  // The carry edge is still there.
  size_t carries = 0;
  for (auto sink : loop.inp_sorted_pins()) {
    if (sink.get_port_id() == 2) {
      carries = sink.get_driver_pins().size();
    }
  }
  EXPECT_EQ(carries, 2u) << "the seed and the carry both drive acc_in";
}

// ---- Hotmux obligations -----------------------------------------------------

hhds::Node_class two_arm_hotmux(const std::shared_ptr<hhds::Graph>& g, const hhds::Pin_class& c0, const hhds::Pin_class& c1,
                                const hhds::Pin_class& v0, const hhds::Pin_class& v1) {
  auto hot = gu::create_typed_node(*g, Ntype_op::Hotmux, 8);
  gu::set_ubits(hot.create_driver_pin(0), 8);
  c0.connect_sink(hot.create_sink_pin(0));
  v0.connect_sink(hot.create_sink_pin(1));
  c1.connect_sink(hot.create_sink_pin(2));
  v1.connect_sink(hot.create_sink_pin(3));
  return hot;
}

// A non-exclusive Hotmux is the UnusedOverlapSurvivesForFormal obligation: it
// is never merged with an identical twin and never deleted, even unread.
TEST(Aegraph, NonExclusiveHotmuxIsASkeletonLeaf) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "hot_overlap", 2, 2);
  // Both controls are the constant 1: overlapping, never established exclusive.
  auto a = two_arm_hotmux(g, konst(g, 1), konst(g, 1), g->get_input_pin("i0"), g->get_input_pin("i1"));
  auto b = two_arm_hotmux(g, konst(g, 1), konst(g, 1), g->get_input_pin("i0"), g->get_input_pin("i1"));
  a.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
  b.create_driver_pin(0).connect_sink(g->get_output_pin("o1"));
  // An unread overlapping one: still an obligation.
  auto unread = two_arm_hotmux(g, konst(g, 1), konst(g, 1), g->get_input_pin("i1"), g->get_input_pin("i0"));
  (void)unread;

  const auto before = signature(g);
  const auto stats  = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(signature(g), before);
  EXPECT_EQ(count_op(g, Ntype_op::Hotmux), 3u);
  EXPECT_EQ(stats.cse_merged, 0u);
  EXPECT_EQ(stats.dead_removed, 0u);
}

// Exclusive twins merge; the proven code is part of the key, so a proven
// Hotmux never merges with an unproven twin (that would lose or forge the
// obligation).
TEST(Aegraph, HotmuxProvenCodeIsPartOfTheKey) {
  for (const bool both_proven : {false, true}) {
    hhds::GraphLibrary lib;
    auto               g = io_graph(lib, "hot_proven", 4, 2);
    const auto         pin   = [&](const char* name) { return g->get_input_pin(name); };
    auto               hot_a = two_arm_hotmux(g, pin("i0"), pin("i1"), pin("i2"), pin("i3"));
    auto               hot_b = two_arm_hotmux(g, pin("i0"), pin("i1"), pin("i2"), pin("i3"));
    gu::set_proven(hot_a, gu::kFormalOnehot);
    if (both_proven) {
      gu::set_proven(hot_b, gu::kFormalOnehot);
    }
    hot_a.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
    hot_b.create_driver_pin(0).connect_sink(g->get_output_pin("o1"));
    livehd::aegraph::optimize(g, {}, checked());
    if (both_proven) {
      EXPECT_EQ(count_op(g, Ntype_op::Hotmux), 1u) << "exclusive proven twins merge";
      EXPECT_TRUE(out_driver(g, "o0") == out_driver(g, "o1"));
    } else {
      // hot_b is not provably exclusive: a skeleton, never merged.
      EXPECT_EQ(count_op(g, Ntype_op::Hotmux), 2u);
    }
  }
}

// ---- CSE, constants, P7 ----------------------------------------------------

TEST(Aegraph, CommutativeBanksAreSortedAndMultiplicityIsKept) {
  hhds::GraphLibrary lib;
  auto               g  = io_graph(lib, "cse_banks", 2, 3);
  auto               a  = g->get_input_pin("i0");
  auto               b  = g->get_input_pin("i1");
  auto               s1 = binop(g, Ntype_op::Sum, a, b);
  auto               s2 = binop(g, Ntype_op::Sum, b, a);  // same multiset, other order
  auto               d  = binop(g, Ntype_op::Sum, a, a);  // 2a: multiplicity matters
  s1.connect_sink(g->get_output_pin("o0"));
  s2.connect_sink(g->get_output_pin("o1"));
  d.connect_sink(g->get_output_pin("o2"));
  const auto stats = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(stats.cse_merged, 1u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 2u);
  EXPECT_TRUE(out_driver(g, "o0") == out_driver(g, "o1"));
  EXPECT_FALSE(out_driver(g, "o0") == out_driver(g, "o2"));
}

// The subtract bank is a different bank: a-b is not b-a, and neither is a+b.
TEST(Aegraph, SumBanksStayDistinct) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "cse_sub", 2, 3);
  auto               a = g->get_input_pin("i0");
  auto               b = g->get_input_pin("i1");
  const auto         diff = [&](const hhds::Pin_class& x, const hhds::Pin_class& y) {
    auto n = gu::create_typed_node(*g, Ntype_op::Sum);
    x.connect_sink(gu::setup_sink_by_name(n, "as"));
    y.connect_sink(gu::setup_sink_by_name(n, "bs"));
    return n.create_driver_pin(0);
  };
  diff(a, b).connect_sink(g->get_output_pin("o0"));
  diff(b, a).connect_sink(g->get_output_pin("o1"));
  diff(a, b).connect_sink(g->get_output_pin("o2"));
  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 2u);
  EXPECT_TRUE(out_driver(g, "o0") == out_driver(g, "o2"));
  EXPECT_FALSE(out_driver(g, "o0") == out_driver(g, "o1"));
}

TEST(Aegraph, ColorAndSynthPolicyAreBarriers) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "cse_color", 2, 4);
  auto               a = g->get_input_pin("i0");
  auto               b = g->get_input_pin("i1");
  auto               x = binop(g, Ntype_op::Xor, a, b);
  auto               y = binop(g, Ntype_op::Xor, a, b);
  auto               z = binop(g, Ntype_op::Xor, a, b);
  auto               w = binop(g, Ntype_op::Xor, a, b);
  gu::set_color(y.get_master_node(), 7);
  z.get_master_node().attr(livehd::attrs::synth_policy).set(std::string{"keep"});
  x.connect_sink(g->get_output_pin("o0"));
  y.connect_sink(g->get_output_pin("o1"));
  z.connect_sink(g->get_output_pin("o2"));
  w.connect_sink(g->get_output_pin("o3"));
  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(count_op(g, Ntype_op::Xor), 3u) << "plain, colored and policy twins stay apart; the plain twins merge";
  EXPECT_TRUE(out_driver(g, "o0") == out_driver(g, "o3"));
}

// Two named twins are both kept; a named twin absorbs an unnamed one.
TEST(Aegraph, NamedTwinPolicy) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "cse_names", 2, 3);
  auto               a = g->get_input_pin("i0");
  auto               b = g->get_input_pin("i1");
  auto               first  = binop(g, Ntype_op::And, a, b);
  auto               second = binop(g, Ntype_op::And, a, b);
  auto               third  = binop(g, Ntype_op::And, a, b);
  first.get_master_node().set_name("keep_first");
  second.get_master_node().set_name("keep_second");
  first.connect_sink(g->get_output_pin("o0"));
  second.connect_sink(g->get_output_pin("o1"));
  third.connect_sink(g->get_output_pin("o2"));
  const auto stats = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(count_op(g, Ntype_op::And), 2u);
  EXPECT_EQ(stats.kept_twin, 1u);
  EXPECT_FALSE(out_driver(g, "o0") == out_driver(g, "o1")) << "two named twins both stay";
  EXPECT_TRUE(out_driver(g, "o2") == out_driver(g, "o0") || out_driver(g, "o2") == out_driver(g, "o1"))
      << "the unnamed twin is absorbed by a named one";
}

TEST(Aegraph, FullyKnownConesFoldToConstants) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "fold", 1, 4);
  // (3 + 4) * 2 -> 14, through two nodes.
  auto sum  = binop(g, Ntype_op::Sum, konst(g, 3), konst(g, 4));
  auto mult = binop(g, Ntype_op::Mult, sum, konst(g, 2));
  mult.connect_sink(g->get_output_pin("o0"));
  // An op with a runtime operand and a 0 mask: the constant analysis alone does
  // not fold it (the operand is not constant); the `annihilate` rule does.
  auto masked = binop(g, Ntype_op::And, g->get_input_pin("i0"), konst(g, 0));
  masked.connect_sink(g->get_output_pin("o1"));
  // A constant select forwards the chosen arm.
  auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
  konst(g, 1).connect_sink(mux.create_sink_pin(0));
  g->get_input_pin("i0").connect_sink(mux.create_sink_pin(1));
  konst(g, 9).connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("o2"));
  // EQ of distinct constants: a known 0.
  auto eq = binop(g, Ntype_op::EQ, konst(g, 5), konst(g, 6));
  eq.connect_sink(g->get_output_pin("o3"));

  const auto stats = livehd::aegraph::optimize(g, {}, checked());
  ASSERT_TRUE(out_driver(g, "o0").is_const());
  EXPECT_EQ(gu::const_of(out_driver(g, "o0")).to_just_i64(), 14);
  ASSERT_TRUE(out_driver(g, "o1").is_const());
  EXPECT_EQ(gu::const_of(out_driver(g, "o1")).to_just_i64(), 0);
  ASSERT_TRUE(out_driver(g, "o2").is_const());
  EXPECT_EQ(gu::const_of(out_driver(g, "o2")).to_just_i64(), 9);
  ASSERT_TRUE(out_driver(g, "o3").is_const());
  EXPECT_EQ(gu::const_of(out_driver(g, "o3")).to_just_i64(), 0);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::Mult), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::Mux), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::EQ), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::And), 0u);
  EXPECT_GE(stats.const_folded, 3u);
}

// P7: a '?' literal is one const node; a fold that keeps '?' bits keeps its op
// and never mints a second '?' constant. A fold that is FULLY KNOWN folds, even
// through a '?' operand (And(?, 0) is 0 under every resolution).
TEST(Aegraph, UnknownLiteralsAreNeverSplit) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "p7", 1, 3);
  auto               q = gu::create_const(*g, *Dlop::from_pyrope("0ub????"));
  ASSERT_TRUE(gu::const_of(q).has_unknowns());

  // Get_mask(q, [0,2)) keeps 2 unknown bits: it must stay a Get_mask over q.
  auto mask = gu::create_typed_node(*g, Ntype_op::Get_mask);
  gu::connect_mask_operands(mask, q, 0, 2);
  mask.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
  // And(q, 0) is fully known.
  auto known = binop(g, Ntype_op::And, q, konst(g, 0));
  known.connect_sink(g->get_output_pin("o1"));
  // A window covering the whole literal is the literal itself: its own pin.
  auto whole = gu::create_typed_node(*g, Ntype_op::Get_mask);
  gu::connect_mask_operands(whole, q, 0, 4);
  whole.create_driver_pin(0).connect_sink(g->get_output_pin("o2"));

  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(gu::type_op_of(out_driver(g, "o0").get_master_node()), Ntype_op::Get_mask);
  ASSERT_TRUE(out_driver(g, "o1").is_const());
  EXPECT_TRUE(gu::const_of(out_driver(g, "o1")).is_known_zero());
  ASSERT_TRUE(out_driver(g, "o2").is_const());
  EXPECT_TRUE(out_driver(g, "o2") == q) << "the whole-literal window forwards the literal's own pin";
  // No new '?' constant was minted by any fold.
  size_t unknown_consts = 0;
  for (auto node : g->body().nodes()) {
    for (auto sink : node.inp_sorted_pins()) {
      for (const auto& driver : sink.get_driver_pins()) {
        if (driver.is_const() && gu::const_of(driver).has_unknowns() && !(driver == q)) {
          ++unknown_consts;
        }
      }
    }
  }
  EXPECT_EQ(unknown_consts, 0u);
}

// ---- cycles ----------------------------------------------------------------

// A word-level combinational cycle (possibly false at bit level) is opaque: no
// member is hash-consed or folded.
TEST(Aegraph, CombinationalCycleMembersAreOpaque) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "cycle", 1, 1);
  auto               p = gu::create_typed_node(*g, Ntype_op::Or);
  auto               q = gu::create_typed_node(*g, Ntype_op::Or);
  g->get_input_pin("i0").connect_sink(gu::setup_sink_by_name(p, "as"));
  q.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(p, "as"));
  g->get_input_pin("i0").connect_sink(gu::setup_sink_by_name(q, "as"));
  p.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(q, "as"));
  q.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
  // A constant-foldable node that is NOT on the cycle still folds.
  auto sum = binop(g, Ntype_op::Sum, konst(g, 1), konst(g, 2));
  sum.connect_sink(gu::setup_sink_by_name(q, "as"));

  const auto stats = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(stats.opaque, 2u);
  EXPECT_EQ(count_op(g, Ntype_op::Or), 2u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 0u);
}

// A feedback ring closed through Muxes with constant selectors is acyclic: each
// Mux reads only its chosen arm. The pre-pass must not mark them opaque, and the
// constant-selector fold must resolve the ring to the input (the shape pass.legalize
// leaves after inlining a constant-configured router).
TEST(Aegraph, ConstantSelectorMuxRingIsNotACycle) {
  hhds::GraphLibrary lib;
  auto               g  = io_graph(lib, "mux_ring", 2, 1);
  auto               m1 = gu::create_typed_node(*g, Ntype_op::Mux);
  auto               m2 = gu::create_typed_node(*g, Ntype_op::Mux);
  konst(g, 0).connect_sink(m1.create_sink_pin(0));
  g->get_input_pin("i0").connect_sink(m1.create_sink_pin(1));
  m2.create_driver_pin(0).connect_sink(m1.create_sink_pin(2));  // feedback: the dead arm
  konst(g, 1).connect_sink(m2.create_sink_pin(0));
  g->get_input_pin("i1").connect_sink(m2.create_sink_pin(1));
  m1.create_driver_pin(0).connect_sink(m2.create_sink_pin(2));
  m2.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));

  const auto stats = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(stats.opaque, 0u);
  EXPECT_EQ(count_op(g, Ntype_op::Mux), 0u);
  EXPECT_TRUE(out_driver(g, "o0") == g->get_input_pin("i0"));
}

TEST(Aegraph, MuxWithEqualConnectedArmsCollapses) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "mux_same", 2, 3);
  const auto         mux = [&](const std::vector<std::pair<int, hhds::Pin_class>>& arms) {
    auto m = gu::create_typed_node(*g, Ntype_op::Mux);
    g->get_input_pin("i0").connect_sink(m.create_sink_pin(0));
    for (const auto& [port, driver] : arms) {
      driver.connect_sink(m.create_sink_pin(static_cast<hhds::Port_id>(port)));
    }
    return m.create_driver_pin(0);
  };
  auto x = g->get_input_pin("i1");
  mux({{1, x}, {2, x}}).connect_sink(g->get_output_pin("o0"));
  mux({{1, x}, {2, x}, {3, x}}).connect_sink(g->get_output_pin("o1"));
  // A disconnected arm 2: the connected arms agree.
  mux({{1, x}, {3, x}}).connect_sink(g->get_output_pin("o2"));
  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(count_op(g, Ntype_op::Mux), 0u);
  for (const char* name : {"o0", "o1", "o2"}) {
    EXPECT_TRUE(out_driver(g, name) == x) << name;
  }
}

TEST(Aegraph, HotmuxCollapsesOnlyWhenSoundAndExclusive) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "hot_fold", 3, 4);
  auto               c0 = g->get_input_pin("i0");
  auto               c1 = g->get_input_pin("i1");
  auto               v  = g->get_input_pin("i2");
  using Pin = hhds::Pin_class;
  const auto         hot = [&](const Pin& a, const Pin& b, const Pin& va, const Pin& vb, const Pin& fallback) {
    auto h = two_arm_hotmux(g, a, b, va, vb);
    if (!fallback.is_invalid()) {
      fallback.connect_sink(h.create_sink_pin(4));
    }
    gu::set_proven(h, gu::kFormalOnehot);
    return h.create_driver_pin(0);
  };
  // Equal values and an equal default: the result is that value.
  hot(c0, c1, v, v, v).connect_sink(g->get_output_pin("o0"));
  // Equal values but no default: all-controls-zero reads 0, which v is not.
  hot(c0, c1, v, v, hhds::Pin_class{}).connect_sink(g->get_output_pin("o1"));
  // Equal ZERO values and no default agree with the implicit 0.
  hot(c0, c1, konst(g, 0), konst(g, 0), hhds::Pin_class{}).connect_sink(g->get_output_pin("o2"));
  // Constant controls select a value.
  hot(konst(g, 0), konst(g, 1), konst(g, 7), konst(g, 9), hhds::Pin_class{}).connect_sink(g->get_output_pin("o3"));
  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_TRUE(out_driver(g, "o0") == v);
  EXPECT_EQ(gu::type_op_of(out_driver(g, "o1").get_master_node()), Ntype_op::Hotmux);
  ASSERT_TRUE(out_driver(g, "o2").is_const());
  EXPECT_TRUE(gu::const_of(out_driver(g, "o2")).is_known_zero());
  ASSERT_TRUE(out_driver(g, "o3").is_const());
  EXPECT_EQ(gu::const_of(out_driver(g, "o3")).to_just_i64(), 9);
}

// ---- determinism and idempotence ---------------------------------------------

// Layers of Sum(Xor(a,b), Xor(b,a)): every layer's commuted twin is a CSE hit.
std::shared_ptr<hhds::Graph> busy_graph(hhds::GraphLibrary& lib) {
  auto                         g = io_graph(lib, "busy", 4, 3);
  std::vector<hhds::Pin_class> level;
  for (const char* name : {"i0", "i1", "i2", "i3"}) {
    level.push_back(g->get_input_pin(name));
  }
  for (int round = 0; round < 4; ++round) {
    std::vector<hhds::Pin_class> next;
    for (size_t i = 0; i < level.size(); ++i) {
      const auto& l = level[i];
      const auto& r = level[(i + 1) % level.size()];
      next.push_back(binop(g, Ntype_op::Sum, binop(g, Ntype_op::Xor, l, r), binop(g, Ntype_op::Xor, r, l)));
    }
    level = next;
  }
  level[0].connect_sink(g->get_output_pin("o0"));
  level[1].connect_sink(g->get_output_pin("o1"));
  binop(g, Ntype_op::Sum, konst(g, 20), konst(g, 22)).connect_sink(g->get_output_pin("o2"));
  return g;
}

TEST(Aegraph, DeterministicAndIdempotent) {
  hhds::GraphLibrary lib_a, lib_b;
  auto               a = busy_graph(lib_a);
  auto               b = busy_graph(lib_b);
  ASSERT_EQ(signature(a), signature(b));
  const auto stats_a = livehd::aegraph::optimize(a, {}, checked());
  const auto stats_b = livehd::aegraph::optimize(b, {}, checked());
  EXPECT_EQ(signature(a), signature(b)) << "two runs on equal input must elaborate the same graph";
  EXPECT_EQ(stats_a.cse_merged, stats_b.cse_merged);
  EXPECT_GT(stats_a.cse_merged, 0u);

  const auto settled = signature(a);
  const auto again   = livehd::aegraph::optimize(a, {}, checked());
  EXPECT_EQ(signature(a), settled) << "a second run finds nothing more";
  EXPECT_EQ(again.cse_merged + again.const_folded + again.forwarded + again.dead_removed, 0u);
}

// ---- soundness -------------------------------------------------------------

// Whatever the engine did, the result is LEC-equal to the input.
TEST(Aegraph, ElaboratedGraphIsEquivalentToTheInput) {
  hhds::GraphLibrary lib_ref, lib_impl;
  auto               ref  = busy_graph(lib_ref);
  auto               impl = busy_graph(lib_impl);
  livehd::aegraph::optimize(impl, {}, checked());
  const auto arith = [](const std::shared_ptr<hhds::Graph>& g) { return count_op(g, Ntype_op::Xor) + count_op(g, Ntype_op::Sum); };
  ASSERT_LT(arith(impl), arith(ref));
  livehd::lec::Lec_options options;
  options.cones   = "false";
  options.timeout = 10;
  const auto result = livehd::lec::prove_equal(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail;
}

// The engine followed by the classic post steps (Cprop::do_post) is still sound
// on the shapes that reach muxctx and flop-enable folding.
TEST(Aegraph, PostStepsKeepTheGraphEquivalent) {
  const auto build = [](hhds::GraphLibrary& lib) {
    auto g = io_graph(lib, "post", 3, 1);
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
    g->get_input_pin("i0").connect_sink(mux.create_sink_pin(0));
    g->get_input_pin("i1").connect_sink(mux.create_sink_pin(1));
    g->get_input_pin("i2").connect_sink(mux.create_sink_pin(2));
    auto flop = gu::create_typed_node(*g, Ntype_op::Flop);
    gu::setup_sink_by_name(flop, "din").connect_driver(mux.create_driver_pin(0));
    gu::setup_sink_by_name(flop, "enable").connect_driver(konst(g, 1));
    gu::set_ubits(flop.create_driver_pin(0), 8);
    flop.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
    return g;
  };
  hhds::GraphLibrary lib_ref, lib_impl;
  auto               ref  = build(lib_ref);
  auto               impl = build(lib_impl);
  livehd::aegraph::optimize(impl, {}, checked());
  Cprop{true}.do_post(impl);
  // The constant-true enable is folded away by the post step.
  for (auto node : impl->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Flop) {
      EXPECT_TRUE(gu::get_driver_of_sink_name(node, "enable").is_invalid());
    }
  }
  livehd::lec::Lec_options options;
  options.cones   = "false";
  options.timeout = 10;
  const auto result = livehd::lec::prove_equal(ref.get(), impl.get(), options);
  EXPECT_NE(result.verdict, livehd::lec::Verdict::Refuted) << result.detail;
}

}  // namespace
