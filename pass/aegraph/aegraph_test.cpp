//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "aegraph.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "cprop.hpp"
#include "cprop_summary.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "pass.hpp"
#include "pass_aegraph.hpp"
#include "query.hpp"
#include "runtime/match.hpp"

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

TEST(Aegraph, ForwardingPreservesRoutingExclusionAndSignalRoles) {
  for (bool cell_marker : {false, true}) {
    hhds::GraphLibrary library;
    auto g = io_graph(library, "protected_forward", 2, 1);
    const auto input = g->get_input_pin("i0");
    const auto value = binop(g, Ntype_op::Or, input, konst(g, 0));
    if (cell_marker) {
      value.get_master_node().attr(livehd::attrs::preroute_exclude).set();
    } else {
      value.attr(livehd::attrs::preroute_net_exclude).set();
    }
    gu::add_signal_roles(value, livehd::attrs::signal_reset);
    value.connect_sink(g->get_output_pin("o0"));
    livehd::aegraph::optimize(g, {}, checked());
    EXPECT_EQ(out_driver(g, "o0"), input);
    EXPECT_TRUE(gu::preroute_net_excluded(input));
    EXPECT_EQ(gu::signal_roles_of(input), livehd::attrs::signal_reset);
    EXPECT_FALSE(gu::preroute_net_excluded(g->get_input_pin("i1")));
    EXPECT_FALSE(g->get_input_node().attr(livehd::attrs::preroute_exclude).has());
  }
}

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
  auto               d  = binop(g, Ntype_op::Sum, a, a);  // 2a: multiplicity matters (and sum_double writes it a << 1)
  s1.connect_sink(g->get_output_pin("o0"));
  s2.connect_sink(g->get_output_pin("o1"));
  d.connect_sink(g->get_output_pin("o2"));
  const auto stats = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(stats.cse_merged, 1u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 1u);  // a + b once; a + a became a << 1
  EXPECT_TRUE(out_driver(g, "o0") == out_driver(g, "o1"));
  EXPECT_FALSE(out_driver(g, "o0") == out_driver(g, "o2"));
}

// The br_credit_counter shape: t = (v + inc) & 31 is read by a compare and by two subtractions. Merging the windowed
// sum into each subtraction (getmask_sum_chain) would build two more adders over the same v and inc, so a window other
// cells read keeps its sum: one adder for v + inc, one per subtraction.
TEST(Aegraph, ASharedWindowedSumIsNotAddedAgainInEachConsumer) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "shared_window_sum", 4, 3);
  const auto         window = [&](const hhds::Pin_class& x) {
    auto m = gu::create_typed_node(*g, Ntype_op::Get_mask);
    gu::connect_mask_operands(m, x, 0, 5);
    gu::set_ubits(m.create_driver_pin(0), 5);
    return m.create_driver_pin(0);
  };
  const auto diff = [&](const hhds::Pin_class& x, const hhds::Pin_class& y) {
    auto n = gu::create_typed_node(*g, Ntype_op::Sum);
    x.connect_sink(gu::setup_sink_by_name(n, "as"));
    y.connect_sink(gu::setup_sink_by_name(n, "bs"));
    gu::set_ubits(n.create_driver_pin(0), 8);
    return n.create_driver_pin(0);
  };
  // 5-bit operands, so the merged sums would be no wider than the window (the condition of getmask_sum_chain)
  const auto v = window(g->get_input_pin("i0")), inc = window(g->get_input_pin("i1")), w = window(g->get_input_pin("i2"));
  const auto x = window(g->get_input_pin("i3"));
  auto       t = window(binop(g, Ntype_op::Sum, v, inc));
  window(diff(t, w)).connect_sink(g->get_output_pin("o0"));
  window(diff(t, x)).connect_sink(g->get_output_pin("o1"));
  binop(g, Ntype_op::GT, t, w).connect_sink(g->get_output_pin("o2"));
  livehd::aegraph::optimize(g, {}, checked());
  size_t sums = 0, operands = 0;
  for (auto node : g->body().nodes()) {
    if (gu::type_op_of(node) != Ntype_op::Sum) {
      continue;
    }
    ++sums;
    for (auto sink : node.inp_sorted_pins()) {
      operands += sink.get_driver_pins().size();
    }
  }
  EXPECT_EQ(sums, 3u);
  EXPECT_EQ(operands, 6u) << "each subtraction adds one operand to the shared sum, not v and inc again";
}

// Signed truncation between additions must not serialize a modular reduction.
TEST(Aegraph, SignedModularSumChainFlattensOnlyAcrossWideEnoughBoundaries) {
  for (const bool subtract : {false, true}) {
    for (const int keep : {7, 8}) {
      hhds::GraphLibrary lib;
      auto               g    = io_graph(lib, "signed_sum_chain", 8, 1);
      const auto         sext = [&](const hhds::Pin_class& value, int bits) {
        auto n = gu::create_typed_node(*g, Ntype_op::Sext);
        value.connect_sink(gu::setup_sink_by_name(n, "a"));
        konst(g, bits).connect_sink(gu::setup_sink_by_name(n, "b"));
        gu::set_sbits(n.create_driver_pin(0), bits);
        return n.create_driver_pin(0);
      };
      const auto term = [&](int i) {
        return binop(g,
                     Ntype_op::And,
                     sext(g->get_input_pin("i" + std::to_string(i)), 8),
                     sext(g->get_input_pin("i" + std::to_string(i + 4)), 8));
      };
      auto value = term(0);
      for (int i = 1; i < 4; ++i) {
        auto sum = gu::create_typed_node(*g, Ntype_op::Sum);
        term(i).connect_sink(gu::setup_sink_by_name(sum, "as"));
        value.connect_sink(gu::setup_sink_by_name(sum, subtract ? "bs" : "as"));
        gu::set_ubits(sum.create_driver_pin(0), 8);
        value = sum.create_driver_pin(0);
        if (i != 3) {
          value = sext(value, keep);
        }
      }
      auto window = gu::create_get_mask(*g, value, 0, 8).create_driver_pin(0);
      gu::set_ubits(window, 8);
      window.connect_sink(g->get_output_pin("o0"));
      livehd::aegraph::optimize(g, {}, checked());
      EXPECT_EQ(count_op(g, Ntype_op::Sum), keep == 8 ? 1u : 3u);
    }
  }
}

// `match op { == 0 {A} == 1 {B} == 2 {A} == 3 {B} else {C} }`: the post steps flatten the Mux chain into one Hotmux whose
// controls read `op == k` directly (an equality chain needs no priority decode). The hardware profile keeps that Hotmux; the
// simulation profile keeps the Mux chain, because the flattening costs predicate gates the simulator pays for.
TEST(Aegraph, TheSimProfileKeepsTheMuxChainTheSynthProfileFlattens) {
  for (const bool sim : {false, true}) {
    hhds::GraphLibrary lib;
    auto               g    = io_graph(lib, sim ? "eq_chain_sim" : "eq_chain_synth", 3, 1);
    auto               root = konst(g, 77);
    for (int k = 3; k >= 0; --k) {
      auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
      binop(g, Ntype_op::EQ, g->get_input_pin("i0"), konst(g, k)).connect_sink(mux.create_sink_pin(0));
      root.connect_sink(mux.create_sink_pin(1));
      g->get_input_pin(k % 2 ? "i1" : "i2").connect_sink(mux.create_sink_pin(2));
      gu::set_ubits(mux.create_driver_pin(0), 8);
      root = mux.create_driver_pin(0);
    }
    root.connect_sink(g->get_output_pin("o0"));
    Cprop                    cp{true};
    livehd::aegraph::Options options;
    options.cost = sim ? livehd::aegraph::Options::Cost::sim : livehd::aegraph::Options::Cost::synth;
    livehd::aegraph::optimize_body(cp, g, options);
    EXPECT_EQ(count_op(g, Ntype_op::Hotmux), sim ? 0u : 1u);
    EXPECT_EQ(count_op(g, Ntype_op::Mux), sim ? 4u : 0u);
  }
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

// A Boolean and an Integer of the same value are one constant class, so their
// users are CSE twins; a String of the same spelling stays apart.
TEST(Aegraph, ConstantsOfEqualValueAndDifferentTagShareAClass) {
  hhds::GraphLibrary lib;
  auto               g  = io_graph(lib, "cse_const_tag", 1, 2);
  auto               a  = g->get_input_pin("i0");
  auto               s1 = binop(g, Ntype_op::Sum, a, gu::create_const(*g, *Dlop::create_bool(true)));
  auto               s2 = binop(g, Ntype_op::Sum, a, gu::create_const(*g, *Dlop::create_integer(1)));
  s1.connect_sink(g->get_output_pin("o0"));
  s2.connect_sink(g->get_output_pin("o1"));
  const auto stats = livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(stats.cse_merged, 1u);
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 1u);
  EXPECT_TRUE(out_driver(g, "o0") == out_driver(g, "o1"));
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

// A chain of single-reader Ors ends as ONE banked cell with every input wired
// once, exactly as classic cprop leaves it (no cap on the width): the top of the
// chain absorbs every link below it in one firing and the links leave the work to
// it, so no quadratic pile of dead intermediate banks is built.
std::shared_ptr<hhds::Graph> wide_chain(hhds::GraphLibrary& lib, int inputs, Ntype_op op) {
  auto g   = io_graph(lib, "wide_chain", inputs, 1);
  auto acc = g->get_input_pin("i0");
  for (int i = 1; i < inputs; ++i) {
    acc = binop(g, op, acc, g->get_input_pin("i" + std::to_string(i)));
  }
  acc.connect_sink(g->get_output_pin("o0"));
  return g;
}

size_t sink_count(const hhds::Node_class& node) {
  size_t width = 0;
  for ([[maybe_unused]] auto sink : node.inp_sorted_pins()) {
    ++width;
  }
  return width;
}

TEST(Aegraph, WideChainFlattensIntoOneBank) {
  constexpr int kInputs = 150;
  for (const auto op : {Ntype_op::Or, Ntype_op::And, Ntype_op::Xor, Ntype_op::Sum, Ntype_op::Mult}) {
    hhds::GraphLibrary lib_ref, lib_impl;
    auto               ref  = wide_chain(lib_ref, kInputs, op);
    auto               impl = wide_chain(lib_impl, kInputs, op);
    livehd::aegraph::optimize(impl, {}, checked());
    ASSERT_EQ(count_op(impl, op), 1u) << Ntype::get_name(op) << ": classic leaves one banked cell, so must the e-graph";
    for (auto node : impl->body().nodes()) {
      if (gu::type_op_of(node) == op) {
        EXPECT_EQ(sink_count(node), static_cast<size_t>(kInputs));
      }
    }
    if (op == Ntype_op::Mult) {
      continue;  // the 8-bit product of 150 inputs is not worth a proof
    }
    livehd::lec::Lec_options options;
    options.cones   = "false";
    options.timeout = 10;
    const auto result = livehd::lec::prove_equal(ref.get(), impl.get(), options);
    EXPECT_NE(result.verdict, livehd::lec::Verdict::Refuted) << result.detail;
  }
}

// The work bound of the same chain at 5000 operands: a link-by-link flatten (or a
// per-operand rescan in rewire) leaves O(k^2) = 12M pool entries (~100 MB) behind;
// the linear flatten stays within a few hundred KB of e-graph tables.
TEST(Aegraph, VeryWideChainDoesWorkLinearInTheWidth) {
  constexpr int      kInputs = 5000;
  hhds::GraphLibrary lib;
  auto               g     = wide_chain(lib, kInputs, Ntype_op::Or);
  const auto         start = std::chrono::steady_clock::now();
  const auto         stats = livehd::aegraph::optimize(g, {});
  const auto         secs  = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  EXPECT_EQ(count_op(g, Ntype_op::Or), 1u);
  EXPECT_LT(stats.aeg_bytes, size_t{2} << 20) << "pool entries must stay linear in the chain width";
  EXPECT_LT(secs, 20.0);
}

// A link shared with another reader is not absorbed (it would be copied), and the
// links above it still merge into one cell.
TEST(Aegraph, SharedChainLinkStopsTheFlatten) {
  hhds::GraphLibrary lib_ref, lib_impl;
  const auto         build = [](hhds::GraphLibrary& lib) {
    auto g   = io_graph(lib, "shared_link", 6, 2);
    auto acc = binop(g, Ntype_op::Or, g->get_input_pin("i0"), g->get_input_pin("i1"));
    acc.connect_sink(g->get_output_pin("o1"));  // the shared link has a second reader
    for (int i = 2; i < 6; ++i) {
      acc = binop(g, Ntype_op::Or, acc, g->get_input_pin("i" + std::to_string(i)));
    }
    acc.connect_sink(g->get_output_pin("o0"));
    return g;
  };
  auto ref  = build(lib_ref);
  auto impl = build(lib_impl);
  livehd::aegraph::optimize(impl, {}, checked());
  EXPECT_EQ(count_op(impl, Ntype_op::Or), 2u) << "the shared Or(i0,i1) and one bank over it plus i2..i5";
  livehd::lec::Lec_options options;
  options.cones   = "false";
  options.timeout = 10;
  const auto result = livehd::lec::prove_equal(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail;
}

// ---- the cprop-summary probe: what it reports, and in which units -----------------

size_t driven_edges(const std::shared_ptr<hhds::Graph>& g) {
  size_t edges = 0;
  for (auto node : g->body().nodes()) {
    for (auto sink : node.inp_sorted_pins()) {
      edges += sink.get_driver_pins().size();
    }
  }
  return edges;
}

// lg_bytes is the footprint at the body's LARGEST (input or output), so an engine
// that shrinks the body still reports what it was handed; aeg_bytes is bytes in
// use and never exceeds the capacity-based number next to it.
TEST(Aegraph, ProbeReportsTheLargestFootprintAndInUseBytes) {
  using livehd::cprop_summary::Probe;
  hhds::GraphLibrary lib;
  auto               g        = busy_graph(lib);
  const auto         nodes_in = [&] {
    size_t n = 0;
    for ([[maybe_unused]] auto node : g->body().nodes()) {
      ++n;
    }
    return n;
  }();
  const auto edges_in = driven_edges(g);
  Probe      probe(*g, true);
  const auto stats  = livehd::aegraph::optimize(g, {});
  const auto record = probe.finish(*g, "busy", stats.rules_fired, stats.aeg_bytes, stats.aeg_cap_bytes);
  ASSERT_LT(record.nodes_out, nodes_in) << "the fixture must shrink";
  EXPECT_EQ(record.nodes_in, nodes_in);
  EXPECT_EQ(record.lg_bytes, Probe::footprint(nodes_in, edges_in)) << "max(input, output), not the output";
  EXPECT_EQ(record.lg_bytes_v1, record.nodes_out * 32 + driven_edges(g) * 16);
  EXPECT_GT(record.aeg_bytes, 0u);
  EXPECT_LE(record.aeg_bytes, record.aeg_cap_bytes);
}

// ---- pass.aegraph's after_bitwidth label ------------------------------------------

// Sum(a << 4, b): low-lane narrowing (Cprop{true}) rewrites it into
// Or(Shl(Sum(a, b >> 4), 4), Get_mask(b)), which the post-bitwidth slot must not do.
std::string run_pass_aegraph(const Eprp_var::Eprp_dict& labels) {
  static std::once_flag once;
  std::call_once(once, [] { Pass_aegraph::setup(); });
  static int         serial = 0;
  hhds::GraphLibrary lib;
  auto               g   = io_graph(lib, "lanes" + std::to_string(serial++), 2, 1);
  auto               shl = gu::create_typed_node(*g, Ntype_op::SHL);
  gu::setup_sink_by_name(shl, "a").connect_driver(g->get_input_pin("i0"));
  gu::setup_sink_by_name(shl, "b").connect_driver(konst(g, 4));
  gu::set_ubits(shl.create_driver_pin(0), 8);
  binop(g, Ntype_op::Sum, shl.create_driver_pin(0), g->get_input_pin("i1")).connect_sink(g->get_output_pin("o0"));
  Eprp_var var;
  var.graphs.push_back(g);
  Pass::eprp.run_method_now("pass.aegraph", var, labels);
  return std::to_string(count_op(g, Ntype_op::Get_mask)) + "/" + std::to_string(count_op(g, Ntype_op::Or)) + "/"
         + std::to_string(count_op(g, Ntype_op::Sum));
}

TEST(PassAegraph, AfterBitwidthKeepsLowLaneNarrowingOff) {
  const auto narrowed = run_pass_aegraph({{"low_lanes", "true"}, {"after_bitwidth", "false"}});
  const auto plain    = run_pass_aegraph({{"low_lanes", "false"}});
  const auto after    = run_pass_aegraph({{"low_lanes", "true"}, {"after_bitwidth", "true"}});
  EXPECT_NE(narrowed, plain) << "low_lanes=true before bitwidth must narrow the fixture";
  EXPECT_EQ(after, plain) << "after_bitwidth=true must behave as low_lanes=false";
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


// ---- milestone G: the packed ready/valid shape of br_amba_axi_demux ---------------

// Test-only rule for the preflight tests (rules_only selects it): the low bit of x as the second bit of
// 2x, Rxor(x, 1) = Get_mask(Mult(x, 2), 1, 2). Value-preserving; the interior Mult is unbounded over a
// leaf, so it cannot be stamped.
AEG_RULE_GROUP(aegraph_test);
AEG_NATIVE_RULE(test_rxor_via_mult, simplify, 50, Ntype_op::Rxor) {
  if (root.ops.size() != 2 || root.ops[0].bank != 0 || root.ops[1].bank != 1 || !ctx.is_int(root.ops[1].cls, 1)) {
    return;
  }
  using livehd::aegraph::Operand;
  const auto twice = ctx.make(Ntype_op::Mult, {Operand{0, root.ops[0].cls}, Operand{0, ctx.konst(2)}});
  out.subsume(ctx.make(Ntype_op::Get_mask, {Operand{0, twice}, Operand{2, ctx.konst(1)}, Operand{3, ctx.konst(2)}}));
}

hhds::Pin_class bit_of(const std::shared_ptr<hhds::Graph>& g, const hhds::Pin_class& word, int bit) {
  auto n = gu::create_get_mask(*g, word, bit, bit + 1);
  gu::set_ubits(n.create_driver_pin(0), 1);
  return n.create_driver_pin(0);
}

// The word-level cycle of a ready/valid network built from packed vectors: valid[i] reads ready[i], ready[i]
// reads valid[i-1], so every bit is a DAG (v0 -> r1 -> v1 -> r2 -> v2) but the packed ready word feeds the packed
// valid word that feeds the packed ready word.
std::shared_ptr<hhds::Graph> packed_ring(hhds::GraphLibrary& lib, const char* name) {
  auto g = io_graph(lib, name, 3, 2);
  // Forward references: the Concat nodes are created first and wired last.
  auto vpack = gu::create_typed_node(*g, Ntype_op::Concat);
  auto rpack = gu::create_typed_node(*g, Ntype_op::Concat);
  gu::set_ubits(vpack.create_driver_pin(0), 3);
  gu::set_ubits(rpack.create_driver_pin(0), 3);
  std::vector<hhds::Pin_class> v(3), r(3);
  for (int i = 0; i < 3; ++i) {
    const auto req = bit_of(g, g->get_input_pin("i" + std::to_string(i)), 0);
    auto       a   = gu::create_typed_node(*g, Ntype_op::And);
    req.connect_sink(gu::setup_sink_by_name(a, "as"));
    bit_of(g, rpack.create_driver_pin(0), i).connect_sink(gu::setup_sink_by_name(a, "as"));
    gu::set_ubits(a.create_driver_pin(0), 1);
    v[i] = a.create_driver_pin(0);
  }
  for (int i = 0; i < 3; ++i) {
    const auto env = bit_of(g, g->get_input_pin("i" + std::to_string((i + 1) % 3)), 1);
    if (i == 0) {
      r[i] = env;
      continue;
    }
    auto x = gu::create_typed_node(*g, Ntype_op::Xor);
    env.connect_sink(gu::setup_sink_by_name(x, "as"));
    bit_of(g, vpack.create_driver_pin(0), i - 1).connect_sink(gu::setup_sink_by_name(x, "as"));
    gu::set_ubits(x.create_driver_pin(0), 1);
    r[i] = x.create_driver_pin(0);
  }
  for (int i = 0; i < 3; ++i) {
    v[2 - i].connect_sink(vpack.create_sink_pin(static_cast<hhds::Port_id>(2 * i)));
    konst(g, 1).connect_sink(vpack.create_sink_pin(static_cast<hhds::Port_id>(2 * i + 1)));
    r[2 - i].connect_sink(rpack.create_sink_pin(static_cast<hhds::Port_id>(2 * i)));
    konst(g, 1).connect_sink(rpack.create_sink_pin(static_cast<hhds::Port_id>(2 * i + 1)));
  }
  vpack.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
  rpack.create_driver_pin(0).connect_sink(g->get_output_pin("o1"));
  return g;
}

// The pre-pass resolves the packed slices that make the ring a FALSE cycle: no member is left opaque, both
// engines' results prove equal to each other, and the engine's own packed-slice rules finish the job.
TEST(Aegraph, PackedFalseCycleIsResolvedInThePrePass) {
  hhds::GraphLibrary lib_classic, lib_aeg;
  auto               classic = packed_ring(lib_classic, "ring_classic");
  auto               aeg     = packed_ring(lib_aeg, "ring_aeg");
  Cprop{}.do_trans(classic);
  Cprop cp;
  const auto stats = livehd::aegraph::optimize_body(cp, aeg, checked());
  EXPECT_EQ(stats.opaque, 0u) << "the false cycle is cut before the build";
  for (const auto& g : {classic, aeg}) {
    // No Get_mask reads a Concat any more: every slice of a pack was resolved to its lane.
    for (auto node : g->body().nodes()) {
      if (gu::type_op_of(node) != Ntype_op::Get_mask) {
        continue;
      }
      const auto source = node.try_get_sink_pin(0).get_driver_pin();
      EXPECT_TRUE(source.is_const() || gu::type_op_of(source.get_master_node()) != Ntype_op::Concat)
          << "a slice of a Concat survived: " << gu::node_name_of(node);
    }
  }
  livehd::lec::Lec_options options;
  options.cones   = "false";
  options.timeout = 20;
  const auto result = livehd::lec::prove_equal(classic.get(), aeg.get(), options);
  EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail;
}

// formal.lec.preflight_engine=aegraph runs the engine the way the LEC preflight does (no bitwidth follows):
// the same ring, optimized with Options::preflight, stays equal to the classic result and leaves no unstamped
// cell behind.
TEST(Aegraph, PreflightEngineOnTheDemuxShapeIsStampedAndEquivalent) {
  hhds::GraphLibrary lib_classic, lib_aeg;
  auto               classic = packed_ring(lib_classic, "pf_classic");
  auto               aeg     = packed_ring(lib_aeg, "pf_aeg");
  Cprop{}.do_trans(classic);
  Cprop                   cp;
  livehd::aegraph::Options options = checked();
  options.preflight                 = true;
  const auto stats                  = livehd::aegraph::optimize_body(cp, aeg, options);
  EXPECT_EQ(stats.refused, 0u);
  for (auto node : aeg->body().nodes()) {
    const auto op = gu::type_op_of(node);
    if (gu::is_builtin_node(node) || !Ntype::is_comb(op) || op == Ntype_op::Mux || !node.has_out_edges()) {
      continue;
    }
    EXPECT_NE(gu::bits_of(node.create_driver_pin(0)), 0) << "an unstamped " << Ntype::get_name(op) << " cell reaches the LEC encoder";
  }
  livehd::lec::Lec_options lec;
  lec.cones   = "false";
  lec.timeout = 20;
  const auto result = livehd::lec::prove_equal(classic.get(), aeg.get(), lec);
  EXPECT_EQ(result.verdict, livehd::lec::Verdict::Proven) << result.detail;
}

// A new interior cell is stamped from the range analysis when one bounds it, and the engine REFUSES the graph
// (leaves it, reports refused) when a new cell has no bound.
TEST(Aegraph, PreflightStampsBoundedNewCellsAndRefusesUnboundedOnes) {
  const auto build = [](hhds::GraphLibrary& lib, const char* name, bool declared) {
    auto io = lib.create_io(name);
    io->add_input("i0", 0);
    if (declared) {
      io->set_bits("i0", 8);
      io->set_unsign("i0", true);
    }
    io->add_output("o0", 1);
    io->set_bits("o0", 8);
    io->set_unsign("o0", true);
    auto g = io->create_graph();
    auto r = gu::create_typed_node(*g, Ntype_op::Rxor);
    g->get_input_pin("i0").connect_sink(gu::setup_sink_pid(r, 0));
    konst(g, 1).connect_sink(gu::setup_sink_pid(r, 1));
    gu::set_ubits(r.create_driver_pin(0), 1);
    r.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
    return g;
  };
  livehd::aegraph::Options options = checked();
  options.rules_only               = "test_rxor_via_mult";

  // Without preflight a new cell is left to bitwidth.
  hhds::GraphLibrary lib_off;
  auto               plain = build(lib_off, "pf_plain", false);
  const auto         off   = livehd::aegraph::optimize(plain, {}, options);
  EXPECT_EQ(off.refused, 0u);
  EXPECT_EQ(count_op(plain, Ntype_op::Rxor), 0u);
  ASSERT_EQ(count_op(plain, Ntype_op::Mult), 1u);
  for (auto node : plain->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Mult) {
      EXPECT_EQ(gu::bits_of(node.create_driver_pin(0)), 0) << "outside preflight a new cell is left to bitwidth";
    }
  }

  options.preflight = true;
  // A declared 8-bit input bounds Mult(i0, 2) to [0, 510]: stamped unsigned 9 bits.
  hhds::GraphLibrary lib_declared;
  auto               declared = build(lib_declared, "pf_declared", true);
  Cprop              cp;
  const auto         ok = livehd::aegraph::optimize_body(cp, declared, options);
  EXPECT_EQ(ok.refused, 0u);
  EXPECT_GE(ok.stamped, 1u);
  for (auto node : declared->body().nodes()) {
    if (gu::type_op_of(node) == Ntype_op::Mult) {
      EXPECT_EQ(gu::bits_of(node.create_driver_pin(0)), 9);
      EXPECT_TRUE(gu::is_unsign(node.create_driver_pin(0)));
    }
  }

  // An undeclared input has no bound: the engine REFUSES the graph and classic keeps it.
  hhds::GraphLibrary lib_refuse;
  auto               stamped = build(lib_refuse, "pf_refuse", false);
  const auto         on      = livehd::aegraph::optimize_body(cp, stamped, options);
  EXPECT_EQ(on.refused, 1u) << "Mult(i0, 2) over an undeclared input is unbounded: nothing can stamp it";
  EXPECT_EQ(count_op(stamped, Ntype_op::Mult), 0u) << "the refused graph is not elaborated";
}

// ---- milestone H -------------------------------------------------------------

// The readers of a node are counted over its whole structural twin group before the walk: A = i0 + i1 feeds
// C = A + i2 while a twin A' = i0 + i1 (created later, read by an output) arrives AFTER C was inserted. Absorbing A
// into C (flatten) would leave A' as a second i0 + i1; the twin group has two readers, so the sum stays shared.
TEST(Aegraph, TwinReadersBlockFlattenOfASharedSum) {
  hhds::GraphLibrary lib;
  auto               g  = io_graph(lib, "twin_readers", 3, 2);
  auto               a  = binop(g, Ntype_op::Sum, g->get_input_pin("i0"), g->get_input_pin("i1"));
  auto               c  = binop(g, Ntype_op::Sum, a, g->get_input_pin("i2"));
  auto               a2 = binop(g, Ntype_op::Sum, g->get_input_pin("i0"), g->get_input_pin("i1"));
  c.connect_sink(g->get_output_pin("o0"));
  a2.connect_sink(g->get_output_pin("o1"));

  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(count_op(g, Ntype_op::Sum), 2u) << "i0 + i1 is built once and read by the output and by the outer sum";
  const auto outer = out_driver(g, "o0").get_master_node();
  size_t     n     = 0;
  for ([[maybe_unused]] auto sink : outer.inp_sorted_pins()) {
    ++n;
  }
  EXPECT_EQ(n, 2u) << "the outer sum reads the shared sum and i2, not three flat operands";
}

// Mux(s, base, Set_mask(base, w, v)) keeps the Mux on the lane: Set_mask(base, w, Mux(s, Get_mask(base, w), v)). A
// latch's own hold mux is a root of the extraction (latch contract rule B: Q reaches D only as a direct Mux arm),
// so the same shape over a latch's Q is left alone, while a flop's din is rewritten.
TEST(Aegraph, LatchHoldMuxIsARootAndAFlopHoldIsLaneFactored) {
  const auto build = [](hhds::GraphLibrary& lib, std::string_view name, Ntype_op state) {
    auto g    = io_graph(lib, name, 3, 1);
    auto reg  = gu::create_typed_node(*g, state);
    auto q    = reg.create_driver_pin(0);
    auto sm   = gu::create_set_mask(*g, q, g->get_input_pin("i1"), 2, 3);
    auto mux  = gu::create_typed_node(*g, Ntype_op::Mux);
    g->get_input_pin("i0").connect_sink(mux.create_sink_pin(0));
    q.connect_sink(mux.create_sink_pin(1));
    sm.create_driver_pin(0).connect_sink(mux.create_sink_pin(2));
    gu::setup_sink_by_name(reg, "din").connect_driver(mux.create_driver_pin(0));
    q.connect_sink(g->get_output_pin("o0"));
    return std::pair{g, reg};
  };
  hhds::GraphLibrary lib_latch, lib_flop;
  auto [latch_g, latch] = build(lib_latch, "latch_hold", Ntype_op::Latch);
  auto [flop_g, flop]   = build(lib_flop, "flop_hold", Ntype_op::Flop);
  livehd::aegraph::optimize(latch_g, {}, checked());
  livehd::aegraph::optimize(flop_g, {}, checked());
  const auto latch_din = gu::get_driver_of_sink_name(latch, "din").get_master_node();
  const auto flop_din  = gu::get_driver_of_sink_name(flop, "din").get_master_node();
  EXPECT_EQ(gu::type_op_of(latch_din), Ntype_op::Mux) << "the latch hold mux is a root: no rule rewrites it";
  EXPECT_EQ(gu::type_op_of(flop_din), Ntype_op::Set_mask) << "a flop's din is lane-factored";
}

// A packed array read is a shift by the lane offset; the lane layout of a Set_mask chain (and of a Concat window
// that spans lanes) resolves it without the word.
TEST(Aegraph, PackedLaneReadsResolveThroughTheLayout) {
  hhds::GraphLibrary lib;
  auto               g   = io_graph(lib, "lane_reads", 2, 3);
  auto               low = gu::create_set_mask(*g, konst(g, 0), g->get_input_pin("i0"), 0, 8);
  auto               top = gu::create_set_mask(*g, low.create_driver_pin(0), g->get_input_pin("i1"), 8, 16);
  auto               sra = gu::create_typed_node(*g, Ntype_op::SRA);
  gu::setup_sink_by_name(sra, "a").connect_driver(top.create_driver_pin(0));
  gu::setup_sink_by_name(sra, "b").connect_driver(konst(g, 8));
  sra.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
  // A window across both lanes of a pack nobody else reads.
  auto low2     = gu::create_set_mask(*g, konst(g, 0), g->get_input_pin("i1"), 0, 8);
  auto top2     = gu::create_set_mask(*g, low2.create_driver_pin(0), g->get_input_pin("i0"), 8, 16);
  auto straddle = gu::create_get_mask(*g, top2.create_driver_pin(0), 4, 12);
  straddle.create_driver_pin(0).connect_sink(g->get_output_pin("o1"));
  // A write that covers the top lane of a pack and extends past it: [4, 12) over a pack of two 4-bit lanes.
  auto w0 = gu::create_set_mask(*g, konst(g, 0), g->get_input_pin("i0"), 0, 4);
  auto w1 = gu::create_set_mask(*g, w0.create_driver_pin(0), g->get_input_pin("i1"), 4, 8);
  auto w2 = gu::create_set_mask(*g, w1.create_driver_pin(0), g->get_input_pin("i0"), 4, 12);
  w2.create_driver_pin(0).connect_sink(g->get_output_pin("o2"));

  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(count_op(g, Ntype_op::SRA), 0u) << "sra_lane + getmask_lane read the lane directly";
  const auto o0 = out_driver(g, "o0");
  EXPECT_NE(gu::type_op_of(o0.get_master_node()), Ntype_op::Set_mask);
  EXPECT_EQ(count_op(g, Ntype_op::Set_mask), 0u) << "the chain is a pack (setmask_to_concat) and then dead";
  EXPECT_EQ(gu::type_op_of(out_driver(g, "o1").get_master_node()), Ntype_op::Concat)
      << "a window across two lanes of a private pack is the narrower pack";
}

// Q1: a constant selector that names a disconnected arm (or lies outside the arms) of an n-ary Mux is a '?' the
// Mux owns; 0 is one legal resolution and the extraction takes it (a constant is cheaper than the Mux). A selector
// that names a connected arm is that arm.
TEST(Aegraph, NaryMuxOutOfRangeSelectionIsAnAltOfZero) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "mux_oob", 3, 3);
  const auto         nary = [&](int64_t sel, std::initializer_list<hhds::Port_id> ports) {
    auto mux = gu::create_typed_node(*g, Ntype_op::Mux);
    konst(g, sel).connect_sink(mux.create_sink_pin(0));
    int n = 0;
    for (auto pid : ports) {
      g->get_input_pin("i" + std::to_string(n++)).connect_sink(mux.create_sink_pin(pid));
    }
    return mux.create_driver_pin(0);
  };
  nary(1, {1, 3}).connect_sink(g->get_output_pin("o0"));  // arm 2 is disconnected: '?'
  nary(7, {1, 2, 3}).connect_sink(g->get_output_pin("o1"));  // out of range: '?'
  nary(2, {1, 2, 3}).connect_sink(g->get_output_pin("o2"));  // arm 3 = i2: connected
  livehd::aegraph::optimize(g, {}, checked());
  ASSERT_TRUE(out_driver(g, "o0").is_const());
  EXPECT_TRUE(gu::const_of(out_driver(g, "o0")).is_known_zero());
  ASSERT_TRUE(out_driver(g, "o1").is_const());
  EXPECT_TRUE(gu::const_of(out_driver(g, "o1")).is_known_zero());
  EXPECT_TRUE(out_driver(g, "o2") == g->get_input_pin("i2"));
}

// Guarded accumulation: `if s { acc |= x }` is Mux(s, acc, acc | x); inside a lifted loop body it becomes
// acc | Mux(s, 0, x). Outside a loop body (or with a held STATE output) it stays a Mux.
TEST(Aegraph, GuardedAccumulationMovesTheMuxOffTheAccumulatorInALoopBody) {
  const auto build = [](hhds::GraphLibrary& lib, std::string_view name, bool state_hold) {
    auto g    = io_graph(lib, name, 3, 1);
    auto hold = g->get_input_pin("i1");
    if (state_hold) {
      auto flop = gu::create_typed_node(*g, Ntype_op::Flop);
      hold      = flop.create_driver_pin(0);
    }
    auto update = binop(g, Ntype_op::Or, hold, g->get_input_pin("i2"));
    auto mux    = gu::create_typed_node(*g, Ntype_op::Mux);
    g->get_input_pin("i0").connect_sink(mux.create_sink_pin(0));
    hold.connect_sink(mux.create_sink_pin(1));
    update.connect_sink(mux.create_sink_pin(2));
    mux.create_driver_pin(0).connect_sink(g->get_output_pin("o0"));
    return g;
  };
  auto options         = checked();
  options.guard_scope  = true;
  options.unstamped_ok = true;
  hhds::GraphLibrary lib_on, lib_off, lib_state;
  auto               on = build(lib_on, "guard_on", false);
  livehd::aegraph::optimize(on, {}, options);
  EXPECT_EQ(gu::type_op_of(out_driver(on, "o0").get_master_node()), Ntype_op::Or) << "the accumulator is the Or's operand";
  EXPECT_EQ(count_op(on, Ntype_op::Mux), 1u);

  auto off = build(lib_off, "guard_off", false);
  livehd::aegraph::optimize(off, {}, checked());
  EXPECT_EQ(gu::type_op_of(out_driver(off, "o0").get_master_node()), Ntype_op::Mux) << "not a loop body: the Mux stays";

  auto state = build(lib_state, "guard_state", true);
  livehd::aegraph::optimize(state, {}, options);
  EXPECT_EQ(gu::type_op_of(out_driver(state, "o0").get_master_node()), Ntype_op::Mux) << "a held state output keeps its Mux";
}

// The per-lane form of the bit-reduction vectorizer: four adjacent single bits of one word under an And (Or) are one
// window test. Under the `sim` profile (a bit select is a shift and a mask, so four of them cost far more than one
// window and a compare) the extraction takes it; under `synth` bit selects are wires and an and-tree of four bits costs
// the same as the window compare, a tie, so the original stays.
namespace {
std::shared_ptr<hhds::Graph> bit_run_graph(hhds::GraphLibrary& lib, const char* name) {
  auto       g    = io_graph(lib, name, 2, 2);
  const auto bits = [&](Ntype_op op) {
    auto n = gu::create_typed_node(*g, op);
    for (int k : {2, 5, 3, 4}) {  // any order
      auto bit = gu::create_get_mask(*g, g->get_input_pin("i0"), k);
      bit.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(n, "as"));
    }
    auto rest = gu::create_get_mask(*g, g->get_input_pin("i1"), 0);
    rest.create_driver_pin(0).connect_sink(gu::setup_sink_by_name(n, "as"));
    return n.create_driver_pin(0);
  };
  bits(Ntype_op::And).connect_sink(g->get_output_pin("o0"));
  bits(Ntype_op::Or).connect_sink(g->get_output_pin("o1"));
  return g;
}
}  // namespace

TEST(Aegraph, AdjacentBitRunsOfAReductionBecomeAWindowTestUnderTheSimProfile) {
  hhds::GraphLibrary lib;
  auto               g = bit_run_graph(lib, "bit_runs_sim");
  auto               options = checked();
  options.cost               = livehd::aegraph::Options::Cost::sim;
  livehd::aegraph::optimize(g, {}, options);
  EXPECT_EQ(count_op(g, Ntype_op::EQ), 1u) << "And of bits 2..5 = (window == 15)";
  EXPECT_EQ(count_op(g, Ntype_op::Ror), 1u) << "Or of bits 2..5 = Ror(window)";
  EXPECT_EQ(count_op(g, Ntype_op::Get_mask), 2u) << "the window is built once and shared by both reductions, plus the one rest bit";
}

TEST(Aegraph, AdjacentBitRunsOfAReductionTieUnderTheSynthProfileAndStayAsTheyAre) {
  hhds::GraphLibrary lib;
  auto               g = bit_run_graph(lib, "bit_runs_synth");
  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(count_op(g, Ntype_op::EQ), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::Ror), 0u);
  EXPECT_EQ(count_op(g, Ntype_op::And), 1u);
  EXPECT_EQ(count_op(g, Ntype_op::Or), 1u);
}

// Q3 / P10: LGraph has no types, a string is a Dlop constant like any other. Mux(s, "str", s) is `s ? s : "str"`;
// it is NOT `s` (classic once read a string arm as zero). The same Mux with a literal 0 arm is `s`.
TEST(Aegraph, MuxWithAStringArmIsNotCollapsedToTheSelector) {
  hhds::GraphLibrary lib;
  auto               g = io_graph(lib, "string_arm", 1, 2);
  const auto         mux = [&](const hhds::Pin_class& false_arm) {
    auto m = gu::create_typed_node(*g, Ntype_op::Mux);
    g->get_input_pin("i0").connect_sink(m.create_sink_pin(0));
    false_arm.connect_sink(m.create_sink_pin(1));
    g->get_input_pin("i0").connect_sink(m.create_sink_pin(2));
    return m.create_driver_pin(0);
  };
  mux(gu::create_const(*g, *Dlop::create_string("str"))).connect_sink(g->get_output_pin("o0"));
  mux(konst(g, 0)).connect_sink(g->get_output_pin("o1"));
  livehd::aegraph::optimize(g, {}, checked());
  EXPECT_EQ(gu::type_op_of(out_driver(g, "o0").get_master_node()), Ntype_op::Mux) << "a string arm is not zero";
  EXPECT_TRUE(out_driver(g, "o1") == g->get_input_pin("i0")) << "Mux(s, 0, s) = s";
}

}  // namespace
