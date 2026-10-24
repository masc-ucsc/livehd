// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Milestone-1 acceptance: the L0 encoder + L1 query prove tiny combinational
// modules equal / different, with verdicts that match the known ground truth
// (and, in the lec.cross path, lgcheck). Graphs are built programmatically so
// the test needs no reader.

#include "acyclic.hpp"
#include "query.hpp"

#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "bitwidth.hpp"
#include "cell.hpp"
#include "cprop.hpp"
#include "enableopt.hpp"
#include "encode.hpp"
#include "flatten.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "inline_sub.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"
#include "proof_prep.hpp"
#include "semdiff.hpp"

using namespace livehd;
using livehd::lec::Verdict;

namespace {

// Build `out = a <op> b` over two `bits`-bit unsigned inputs. For reduce-style
// ops (And/Or/Xor) and Sum-add, both operands land on the multi-driver sink
// "as"; `swap` flips the edge order (to exercise commutativity in the equal case).
std::shared_ptr<hhds::Graph> build_binop(hhds::GraphLibrary& lib, const std::string& mod, Ntype_op op, int bits,
                                         bool swap = false) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits);
  gio->set_unsign("a", true);
  gio->add_input("b", 1);
  gio->set_bits("b", bits);
  gio->set_unsign("b", true);
  gio->add_output("out", 2);
  const int out_bits
      = op == Ntype_op::Sum ? bits + 1 : ((op == Ntype_op::EQ || op == Ntype_op::LT || op == Ntype_op::GT) ? 1 : bits);
  gio->set_bits("out", out_bits);
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto node = graph_util::create_typed_node(*g, op);

  auto sink_a = graph_util::setup_sink_by_name(node, "as");
  auto a      = g->get_input_pin("a");
  auto b      = g->get_input_pin("b");
  if (swap) {
    b.connect_sink(sink_a);
    a.connect_sink(sink_a);
  } else {
    a.connect_sink(sink_a);
    b.connect_sink(sink_a);
  }

  auto dpin = node.create_driver_pin(0);
  graph_util::set_ubits(dpin, out_bits);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

// Build `out = a + const` (exercises the Nconst / constant path).
std::shared_ptr<hhds::Graph> build_add_const(hhds::GraphLibrary& lib, const std::string& mod, int64_t k, int bits) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits);
  gio->set_unsign("a", true);
  gio->add_output("out", 1);
  gio->set_bits("out", bits + 1);
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto node = graph_util::create_typed_node(*g, Ntype_op::Sum);

  auto sink_a = graph_util::setup_sink_by_name(node, "as");
  g->get_input_pin("a").connect_sink(sink_a);
  auto cval = Dlop::create_integer(k);
  auto cpin = graph_util::create_const(*g, *cval);
  cpin.connect_sink(sink_a);

  auto dpin = node.create_driver_pin(0);
  graph_util::set_ubits(dpin, bits + 1);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

// Build a 4-bit Concat lane from an 8-bit input. The Concat cell contract is
// `value mod 2^window`; `pretruncate` spells that mask explicitly on one side.
std::shared_ptr<hhds::Graph> build_overwide_concat(hhds::GraphLibrary& lib, const std::string& mod, bool pretruncate) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", 8);
  gio->set_unsign("a", true);
  gio->add_output("out", 1);
  gio->set_bits("out", 4);  // unsigned u4
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto lane = g->get_input_pin("a");
  if (pretruncate) {
    auto mask = graph_util::create_typed_node(*g, Ntype_op::Get_mask, 4);
    lane.connect_sink(livehd::graph_util::setup_sink_pid(mask, 0));
    graph_util::connect_bit_range(mask, 0, 4);
    lane = mask.create_driver_pin(0);
    graph_util::set_ubits(lane, 4);
  }

  auto concat = graph_util::create_typed_node(*g, Ntype_op::Concat, 4);
  lane.connect_sink(livehd::graph_util::setup_sink_pid(concat, 0));
  graph_util::create_const(*g, *Dlop::create_integer(4)).connect_sink(livehd::graph_util::setup_sink_pid(concat, 1));
  auto out = concat.create_driver_pin(0);
  graph_util::set_ubits(out, 4);
  out.connect_sink(g->get_output_pin("out"));
  return g;
}

std::shared_ptr<hhds::Graph> build_active_loop(hhds::GraphLibrary& lib, uint64_t count = 3) {
  auto body_io = lib.create_io("active_body");
  body_io->add_input("carry", 0);
  body_io->add_input("active", 1);
  body_io->add_output("next_carry", 2);
  body_io->add_output("next_active", 3);
  body_io->set_bits("carry", 9);
  body_io->set_bits("active", 1);
  body_io->set_bits("next_carry", 9);
  body_io->set_bits("next_active", 1);
  body_io->set_unsign("carry", true);
  body_io->set_unsign("active", true);
  body_io->set_unsign("next_carry", true);
  body_io->set_unsign("next_active", true);
  auto body = body_io->create_graph();

  auto sum = graph_util::create_typed_node(*body, Ntype_op::Sum, 9);
  body->get_input_pin("carry").connect_sink(livehd::graph_util::setup_sink_pid(sum, 0));
  graph_util::create_const(*body, *Dlop::create_integer(1)).connect_sink(livehd::graph_util::setup_sink_pid(sum, 0));

  // The lifted body itself preserves the carry while inactive; the occurrence
  // realization additionally inserts the inter-ordinal bypass required by the
  // compact call-binding contract.
  auto mux = graph_util::create_typed_node(*body, Ntype_op::Mux, 9);
  body->get_input_pin("active").connect_sink(livehd::graph_util::setup_sink_pid(mux, 0));
  body->get_input_pin("carry").connect_sink(livehd::graph_util::setup_sink_pid(mux, 1));
  sum.create_driver_pin(0).connect_sink(livehd::graph_util::setup_sink_pid(mux, 2));
  auto next_carry = mux.create_driver_pin(0);
  graph_util::set_bits(next_carry, 9);
  graph_util::set_unsign(next_carry);
  next_carry.connect_sink(body->get_output_pin("next_carry"));
  graph_util::create_const(*body, *Dlop::create_integer(0)).connect_sink(body->get_output_pin("next_active"));

  auto top_io = lib.create_io("active_top");
  top_io->add_input("seed", 0);
  top_io->add_input("enable", 1);
  top_io->add_output("result", 2);
  top_io->set_bits("seed", 9);
  top_io->set_bits("enable", 1);
  top_io->set_bits("result", 9);
  top_io->set_unsign("seed", true);
  top_io->set_unsign("enable", true);
  top_io->set_unsign("result", true);
  auto top  = top_io->create_graph();
  auto call = graph_util::create_typed_node(*top, Ntype_op::Sub);
  call.set_subnode(body_io,
                   hhds::Subnode_loop{
                       .first              = 0,
                       .step               = 1,
                       .count              = count,
                       .index_input        = std::nullopt,
                       .activation_input   = 1,
                       .next_active_output = 3,
                   });
  top->get_input_pin("seed").connect_sink(livehd::graph_util::setup_sink_pid(call, 0));
  top->get_input_pin("enable").connect_sink(livehd::graph_util::setup_sink_pid(call, 1));
  call.create_driver_pin(2).connect_sink(livehd::graph_util::setup_sink_pid(call, 0));
  call.create_driver_pin(2).connect_sink(top->get_output_pin("result"));
  call.subnode_group().validate();
  return top;
}

// `carry_bits` is the DECLARED carry-in port width (0 = width-less). The
// carry-out stays 17 bits, so a narrower carry-in truncates at the port.
std::shared_ptr<hhds::Graph> build_indexed_carry_loop(hhds::GraphLibrary& lib, int64_t first, uint64_t count,
                                                      bool observe_plain_output = false, uint32_t carry_bits = 17) {
  auto body_io = lib.create_io("indexed_body");
  body_io->add_input("index", 0);
  body_io->add_input("x", 1);
  body_io->add_input("carry", 2);
  body_io->add_output("next_carry", 3);
  for (const auto name : {"index", "x", "carry", "next_carry"}) {
    body_io->set_bits(name, 17);
    body_io->set_unsign(name, true);
  }
  body_io->set_bits("carry", carry_bits);
  if (observe_plain_output) {
    body_io->add_output("body_value", 4);
    body_io->set_bits("body_value", 17);
    body_io->set_unsign("body_value", true);
  }
  auto body = body_io->create_graph();
  auto sum  = graph_util::create_typed_node(*body, Ntype_op::Sum, 17);
  body->get_input_pin("index").connect_sink(livehd::graph_util::setup_sink_pid(sum, 0));
  body->get_input_pin("x").connect_sink(livehd::graph_util::setup_sink_pid(sum, 0));
  body->get_input_pin("carry").connect_sink(livehd::graph_util::setup_sink_pid(sum, 0));
  auto sum_out = sum.create_driver_pin(0);
  graph_util::set_bits(sum_out, 17);
  graph_util::set_unsign(sum_out);
  sum_out.connect_sink(body->get_output_pin("next_carry"));
  if (observe_plain_output) {
    body->get_input_pin("x").connect_sink(body->get_output_pin("body_value"));
  }

  auto top_io = lib.create_io("indexed_top");
  top_io->add_input("x", 0);
  top_io->add_output("result", 1);
  top_io->set_bits("x", 17);
  top_io->set_bits("result", 17);
  top_io->set_unsign("x", true);
  top_io->set_unsign("result", true);
  if (observe_plain_output) {
    top_io->add_output("observed", 2);
    top_io->set_bits("observed", 17);
    top_io->set_unsign("observed", true);
  }
  auto top  = top_io->create_graph();
  auto loop = graph_util::create_typed_node(*top, Ntype_op::Sub);
  loop.set_name("loop_site");
  loop.set_subnode(body_io,
                   hhds::Subnode_loop{
                       .first              = first,
                       .step               = 1,
                       .count              = count,
                       .index_input        = 0,
                       .activation_input   = std::nullopt,
                       .next_active_output = std::nullopt,
                   });
  top->get_input_pin("x").connect_sink(livehd::graph_util::setup_sink_pid(loop, 1));
  graph_util::create_const(*top, *Dlop::create_integer(0)).connect_sink(livehd::graph_util::setup_sink_pid(loop, 2));
  auto result = loop.create_driver_pin(3);
  graph_util::set_bits(result, 17);
  graph_util::set_unsign(result);
  result.connect_sink(livehd::graph_util::setup_sink_pid(loop, 2));
  result.connect_sink(top->get_output_pin("result"));
  if (observe_plain_output) {
    auto observed = loop.create_driver_pin(4);
    graph_util::set_bits(observed, 17);
    graph_util::set_unsign(observed);
    observed.connect_sink(top->get_output_pin("observed"));
  }
  loop.subnode_group().validate();
  return top;
}

// A plain (non-loop) instance `u` of `narrow_callee`, whose input `a` is
// declared `in_bits` wide (0 = width-less, a scalar in cgen and cgen_sim) and
// `in_unsigned`; the callee passes `a` straight to its 17-bit output `y`. The
// top feeds `a` from an 8-bit input `x` (`x_unsigned`) and drives `result` from
// `y`. `in_bits < 0` builds the flat reference instead, with no instance:
// `result = x#[0..<-in_bits]`.
std::shared_ptr<hhds::Graph> build_port_fit_top(hhds::GraphLibrary& lib, int in_bits, bool in_unsigned = true,
                                                bool x_unsigned = true) {
  auto top_io = lib.create_io("port_fit_top");
  top_io->add_input("x", 0);
  top_io->set_bits("x", 8);
  top_io->set_unsign("x", x_unsigned);
  top_io->add_output("result", 1);
  top_io->set_bits("result", 17);
  top_io->set_unsign("result", true);
  if (in_bits < 0) {
    auto top  = top_io->create_graph();
    auto mask = graph_util::create_get_mask(*top, top->get_input_pin("x"), 0, -in_bits);
    graph_util::set_ubits(mask.create_driver_pin(0), -in_bits);
    mask.create_driver_pin(0).connect_sink(top->get_output_pin("result"));
    return top;
  }

  auto callee_io = lib.create_io("narrow_callee");
  callee_io->add_input("a", 0);
  callee_io->set_bits("a", static_cast<uint32_t>(in_bits));
  callee_io->set_unsign("a", in_unsigned);
  callee_io->add_output("y", 1);
  callee_io->set_bits("y", 17);
  callee_io->set_unsign("y", in_unsigned);
  auto callee = callee_io->create_graph();
  callee->get_input_pin("a").connect_sink(callee->get_output_pin("y"));

  auto top = top_io->create_graph();
  auto sub = graph_util::create_typed_node(*top, Ntype_op::Sub);
  sub.set_name("u");
  sub.set_subnode(callee_io);
  top->get_input_pin("x").connect_sink(livehd::graph_util::setup_sink_pid(sub, 0));
  auto y = sub.create_driver_pin(1);
  graph_util::set_bits(y, 17);
  if (in_unsigned) {
    graph_util::set_unsign(y);
  } else {
    graph_util::set_sign(y);
  }
  y.connect_sink(top->get_output_pin("result"));
  return top;
}

// `result = u.y` where the callee `net_callee` forwards its unsigned 8-bit `a`
// to its unsigned 8-bit `y`, but the instance pin `u.y` -- the parent net cgen
// declares for it -- is stamped 8 bits `net_signed`. The 17-bit `result` then
// reads that net: sign-extended when it is signed, as the netlist's Verilog
// does (`wire signed [7:0] u_y; assign result = u_y;`).
std::shared_ptr<hhds::Graph> build_net_fit_top(hhds::GraphLibrary& lib, bool net_signed) {
  auto callee_io = lib.create_io("net_callee");
  callee_io->add_input("a", 0);
  callee_io->add_output("y", 1);
  for (const auto* p : {"a", "y"}) {
    callee_io->set_bits(p, 8);
    callee_io->set_unsign(p, true);
  }
  auto callee = callee_io->create_graph();
  callee->get_input_pin("a").connect_sink(callee->get_output_pin("y"));

  auto top_io = lib.create_io("port_fit_top");
  top_io->add_input("x", 0);
  top_io->set_bits("x", 8);
  top_io->set_unsign("x", true);
  top_io->add_output("result", 1);
  top_io->set_bits("result", 17);
  top_io->set_unsign("result", true);
  auto top = top_io->create_graph();
  auto sub = graph_util::create_typed_node(*top, Ntype_op::Sub);
  sub.set_name("u");
  sub.set_subnode(callee_io);
  top->get_input_pin("x").connect_sink(graph_util::setup_sink_pid(sub, 0));
  auto y = sub.create_driver_pin(1);
  if (net_signed) {
    graph_util::set_sbits(y, 8);
  } else {
    graph_util::set_ubits(y, 8);
  }
  y.connect_sink(top->get_output_pin("result"));
  return top;
}

// The flat `result = x` sign-extended from bit 7 (`x_signed`) or zero-extended.
std::shared_ptr<hhds::Graph> build_net_fit_ref(hhds::GraphLibrary& lib, bool x_signed) {
  auto io = lib.create_io("port_fit_top");
  io->add_input("x", 0);
  io->set_bits("x", 8);
  io->set_unsign("x", true);
  io->add_output("result", 1);
  io->set_bits("result", 17);
  io->set_unsign("result", true);
  auto ref = io->create_graph();
  if (!x_signed) {
    ref->get_input_pin("x").connect_sink(ref->get_output_pin("result"));
    return ref;
  }
  auto sext = graph_util::create_typed_node(*ref, Ntype_op::Sext);
  ref->get_input_pin("x").connect_sink(graph_util::setup_sink_pid(sext, 0));
  graph_util::create_const(*ref, *Dlop::create_integer(8)).connect_sink(graph_util::setup_sink_pid(sext, 1));
  graph_util::set_sbits(sext.create_driver_pin(0), 17);
  sext.create_driver_pin(0).connect_sink(ref->get_output_pin("result"));
  return ref;
}

}  // namespace

TEST(LecNames, PyropeQuotedStateMatchesDirectRtlName) {
  EXPECT_EQ(lec::canon_flop_name("`msg_port_enabled.e0`"), lec::canon_flop_name("msg_port_enabled.e0"));
  EXPECT_EQ(lec::canon_flop_name("top.`state.part`"), lec::canon_flop_name("top.state.part"));
  EXPECT_EQ(lec::canon_flop_name("csrMod\\_Mhpmevent10_0"), lec::canon_flop_name("csrMod_Mhpmevent10_0"));
}

// Loop-iteration tags are outermost first, whether a level is rolled (a
// `u_loop_<n>__li<k>` wrapper) or unrolled (a `__li<k>` instance suffix).
TEST(LecNames, RolledLoopFoldKeepsOuterTagsFirst) {
  EXPECT_EQ(lec::canon_flop_name("u_loop_0__li0.u_loop_0__li1.t.acc"), "t__li0__li1_acc");
  EXPECT_EQ(lec::canon_flop_name("u_loop_0__li0.t__li1.acc"), lec::canon_flop_name("t__li0__li1.acc"));
  EXPECT_EQ(lec::canon_flop_name("u_loop_2__li3.t.acc"), "t__li3_acc");
}

// The reference memory knowledge state (BMC, gold_x=ignore). The old encoding
// put an all-unknown constant array next to the all-known one and cvc5 threw
// "write-chains connecting two different constant arrays" as soon as a solve
// could merge them (a write through an unknown address, then a reset).
TEST(LecState, MemoryKnowledgeRestartsWithoutConstantArrayChains) {
  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  solver.setLogic("QF_ABV");
  solver.setOption("arrays-exp", "true");  // as configure_solver sets for a query with memories
  solver.setOption("arrays-prop", "1");
  solver.setOption("incremental", "true");
  const lec::Mem_sig sig{.size = 4, .bits = 8, .addr_w = 2, .n_rd = 1, .n_wr = 1};
  const auto         bv2   = tm.mkBitVectorSort(2);
  const auto         bv8   = tm.mkBitVectorSort(8);
  const auto         a     = tm.mkConst(bv2, "a");
  const auto         b     = tm.mkConst(bv2, "b");
  const auto         x     = tm.mkConst(bv8, "x");
  const auto         cloud = tm.mkConst(tm.getBooleanSort(), "cloud");
  const auto         rst   = tm.mkConst(tm.getBooleanSort(), "rst");
  const auto         ones  = tm.mkBitVector(8, 0xff);
  const auto         zero  = tm.mkBitVector(8, 0);
  const auto         init  = tm.mkConst(tm.mkBitVectorSort(32), "init");  // per-entry reset contents

  // Two writes, a conditional cloud, a conditional reset to per-entry contents.
  auto s0 = lec::mem_x_fresh(tm, sig, /*unknown=*/false, "e0");
  auto s1 = lec::mem_x_write(tm, s0, a, x);
  auto s2 = lec::mem_x_restart(tm, s1, sig, cloud, ones);
  auto s3 = lec::mem_x_write(tm, s2, b, zero);
  auto s4 = lec::mem_x_restart(tm, s3, sig, rst, zero, init);
  auto u0 = lec::mem_x_fresh(tm, sig, /*unknown=*/true, "u0");
  auto u1 = lec::mem_x_write(tm, u0, a, x);

  auto entry = [&](const cvc5::Term& bus, const cvc5::Term& at) {
    auto e = tm.mkTerm(tm.mkOp(cvc5::Kind::BITVECTOR_EXTRACT, {7, 0}), {bus});
    for (uint32_t i = 1; i < 4; ++i) {
      e = tm.mkTerm(cvc5::Kind::ITE,
                    {tm.mkTerm(cvc5::Kind::EQUAL, {at, tm.mkBitVector(2, i)}),
                     tm.mkTerm(tm.mkOp(cvc5::Kind::BITVECTOR_EXTRACT, {8 * i + 7, 8 * i}), {bus}),
                     e});
    }
    return e;
  };
  auto ite = [&](const cvc5::Term& c, const cvc5::Term& t, const cvc5::Term& e) { return tm.mkTerm(cvc5::Kind::ITE, {c, t, e}); };
  auto eq  = [&](const cvc5::Term& l, const cvc5::Term& r) { return tm.mkTerm(cvc5::Kind::EQUAL, {l, r}); };
  // Reference model of reading entry `b` (then `a`) after each step.
  const auto              after_s1 = [&](const cvc5::Term& at) { return ite(eq(at, a), x, zero); };
  const auto              after_s3 = [&](const cvc5::Term& at) { return ite(eq(at, b), zero, ite(cloud, ones, after_s1(at))); };
  const auto              after_s4 = [&](const cvc5::Term& at) { return ite(rst, entry(init, at), after_s3(at)); };
  std::vector<cvc5::Term> wrong{
      tm.mkTerm(cvc5::Kind::DISTINCT, {lec::mem_x_read(tm, s1, b), after_s1(b)}),
      tm.mkTerm(cvc5::Kind::DISTINCT, {lec::mem_x_read(tm, s3, a), after_s3(a)}),
      tm.mkTerm(cvc5::Kind::DISTINCT, {lec::mem_x_read(tm, s4, a), after_s4(a)}),
      tm.mkTerm(cvc5::Kind::DISTINCT, {lec::mem_x_read(tm, s4, b), after_s4(b)}),
      tm.mkTerm(cvc5::Kind::DISTINCT, {lec::mem_x_read(tm, u1, b), ite(eq(a, b), x, ones)}),
  };
  for (const auto& w : wrong) {
    solver.push();
    solver.assertFormula(w);
    cvc5::Result r;
    ASSERT_NO_THROW(r = solver.checkSat());
    EXPECT_TRUE(r.isUnsat()) << w;
    solver.pop();
  }
  // Both outcomes of a cloud/reset stay reachable (nothing is vacuous).
  solver.assertFormula(tm.mkTerm(cvc5::Kind::DISTINCT, {lec::mem_x_read(tm, s4, a), lec::mem_x_read(tm, u1, a)}));
  cvc5::Result r;
  ASSERT_NO_THROW(r = solver.checkSat());
  EXPECT_TRUE(r.isSat());
}

TEST(CombEquiv, LegalizedPackedFeedbackProves) {
  hhds::GraphLibrary lib;
  auto               build = [&](const std::string& name, bool packed, int invert, bool real_cycle = false) {
    namespace gu = graph_util;
    auto io      = lib.create_io(name);
    io->add_input("a", 0);
    io->set_bits("a", 2);
    io->set_unsign("a", true);
    io->add_output("out", 1);
    io->set_bits("out", 4);
    io->set_unsign("out", true);
    auto g      = io->create_graph();
    auto concat = gu::create_typed_node(*g, Ntype_op::Concat, 4);
    auto word   = concat.create_driver_pin(0);
    gu::set_ubits(word, 4);
    auto lane = g->get_input_pin("a");
    if (packed) {
      auto slice = gu::create_typed_node(*g, Ntype_op::Get_mask, 2);
      word.connect_sink(livehd::graph_util::setup_sink_pid(slice, 0));
      gu::connect_bit_range(slice, real_cycle ? 0 : 2, real_cycle ? 2 : 4);
      lane = slice.create_driver_pin(0);
      gu::set_ubits(lane, 2);
    }
    auto flip = gu::create_typed_node(*g, Ntype_op::Xor, 2);
    lane.connect_sink(livehd::graph_util::setup_sink_pid(flip, 0));
    gu::create_const(*g, *Dlop::create_integer(invert)).connect_sink(livehd::graph_util::setup_sink_pid(flip, 0));
    auto high = flip.create_driver_pin(0);
    gu::set_ubits(high, 2);
    g->get_input_pin("a").connect_sink(livehd::graph_util::setup_sink_pid(concat, 0));
    gu::create_const(*g, *Dlop::create_integer(2)).connect_sink(livehd::graph_util::setup_sink_pid(concat, 1));
    high.connect_sink(livehd::graph_util::setup_sink_pid(concat, 2));
    gu::create_const(*g, *Dlop::create_integer(2)).connect_sink(livehd::graph_util::setup_sink_pid(concat, 3));
    word.connect_sink(g->get_output_pin("out"));
    return g;
  };
  auto ref    = build("ref", false, 3);
  auto packed = build("packed", true, 3);
  auto wrong  = build("wrong", true, 1);
  auto cycle  = build("cycle", true, 3, true);
  // Before legalize the packed word reads its own upper lane: a word-level
  // cycle the encoder refuses. LEC never repairs loops itself.
  {
    cvc5::TermManager tm;
    lec::Encoder      encoder(tm);
    auto              original = encoder.encode(packed.get());
    EXPECT_FALSE(original.ok);
    EXPECT_NE(original.error.find("WORD-LEVEL CYCLE"), std::string::npos) << original.error;
  }
  // pass.legalize, the one owner of combinational loops, removes the false
  // (bit-disjoint) packed cycles and reports the genuine one.
  livehd::legalize::Split_state state;
  const auto                    legal = livehd::legalize::make_acyclic({packed, wrong}, state);
  EXPECT_EQ(legal.loops, 0);
  EXPECT_GT(legal.slices_rewired, 0);
  livehd::legalize::Split_state cycle_state;
  EXPECT_EQ(livehd::legalize::make_acyclic({cycle}, cycle_state).loops, 1);

  lec::Lec_options opts;
  opts.engine = "ind";
  auto good   = lec::prove_equal(ref.get(), packed.get(), opts);
  EXPECT_EQ(good.verdict, Verdict::Proven) << good.detail;
  auto bad = lec::prove_equal(ref.get(), wrong.get(), opts);
  EXPECT_EQ(bad.verdict, Verdict::Refuted) << bad.detail;
  // A genuine loop that reaches prove_equal (legalize reported it; a direct
  // caller ignored that) must fail CLOSED: never a verdict, only unsupported.
  auto unresolved = lec::prove_equal(ref.get(), cycle.get(), opts);
  EXPECT_EQ(unresolved.verdict, Verdict::Undecided) << unresolved.detail;
  EXPECT_TRUE(unresolved.unsupported);
}

TEST(LecState, PartialUnknownInitialPreservesKnownBits) {
  hhds::GraphLibrary lib;
  auto               gio = lib.create_io("top");
  gio->add_output("q", 0);
  gio->set_bits("q", 4);
  gio->set_unsign("q", true);
  auto g    = gio->create_graph();
  auto flop = graph_util::create_typed_node(*g, Ntype_op::Flop);
  graph_util::create_const(*g, *Dlop::from_binary("10?1", /*unsigned_result=*/true))
      .connect_sink(graph_util::setup_sink_by_name(flop, "initial"));

  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  auto              init = lec::flop_initial(tm, flop, 4, /*x_as_undefined=*/true);
  ASSERT_TRUE(init.has_value());
  EXPECT_EQ(init->width, 4);
  EXPECT_EQ(solver.simplify(init->term), tm.mkBitVector(4, "1001", 2));
  EXPECT_EQ(solver.simplify(init->x_mask), tm.mkBitVector(4, "0010", 2));

  auto concrete = lec::flop_initial(tm, flop, 4, /*x_as_undefined=*/false);
  ASSERT_TRUE(concrete.has_value());
  EXPECT_EQ(solver.simplify(concrete->term), tm.mkBitVector(4, "1001", 2));
  EXPECT_TRUE(concrete->x_mask.isNull());
}

TEST(LecState, UnknownWriteEnableMergesWriteAndHoldKnowledge) {
  hhds::GraphLibrary lib;
  auto               io = lib.create_io("enabled");
  for (const auto name : {"clock", "enable"}) {
    io->add_input(name, name == std::string("clock") ? 0 : 1);
    io->set_bits(name, 1);
    io->set_unsign(name, true);
  }
  io->add_output("out", 2);
  io->set_bits("out", 4);
  io->set_unsign("out", true);
  auto g    = io->create_graph();
  auto flop = graph_util::create_typed_node(*g, Ntype_op::Flop);
  flop.set_name("state");
  auto q = flop.create_driver_pin(0);
  graph_util::set_ubits(q, 4);
  q.connect_sink(g->get_output_pin("out"));
  g->get_input_pin("clock").connect_sink(graph_util::setup_sink_by_name(flop, "clock_pin"));
  g->get_input_pin("enable").connect_sink(graph_util::setup_sink_by_name(flop, "enable"));
  graph_util::create_const(*g, *Dlop::from_binary("1010", true)).connect_sink(graph_util::setup_sink_by_name(flop, "din"));
  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  for (int enable : {0, 1}) {
    lec::Io_name_map<lec::Val> inputs;
    inputs["state"]  = {tm.mkBitVector(4, 8), 4, false, tm.mkBitVector(4, 1)};
    inputs["enable"] = {tm.mkBitVector(1, enable), 1, false, tm.mkBitVector(1, 1)};
    lec::Encoder encoder(tm);
    encoder.set_x_dontcare(true);
    auto encoded = encoder.encode(g.get(), &inputs);
    ASSERT_TRUE(encoded.ok) << encoded.error;
    const auto& next = encoded.outputs.at("\x01nxt:state");
    // 10?0 vs 100?: bit 1 differs and bit 0 is unknown; high bits agree.
    EXPECT_EQ(solver.simplify(next.x_mask), tm.mkBitVector(4, 3));
  }
}

TEST(LecState, PowerOnInitialIsPerDesignWithOrWithoutOtherReset) {
  for (bool other_reset : {false, true}) {
    hhds::GraphLibrary lib;
    auto               build = [&](const std::string& name, const char* initial) {
      auto io = lib.create_io(name);
      io->add_input("clock", 0);
      io->set_bits("clock", 1);
      io->set_unsign("clock", true);
      if (other_reset) {
        io->add_input("reset", 1);
        io->set_bits("reset", 1);
        io->set_unsign("reset", true);
      }
      io->add_output("out", 2);
      io->set_bits("out", 4);
      io->set_unsign("out", true);
      auto g = io->create_graph();
      auto f = graph_util::create_typed_node(*g, Ntype_op::Flop);
      f.set_name("state");  // Same name must not override different contents.
      auto q = f.create_driver_pin(0);
      graph_util::set_ubits(q, 4);
      q.connect_sink(graph_util::setup_sink_by_name(f, "din"));
      q.connect_sink(g->get_output_pin("out"));
      g->get_input_pin("clock").connect_sink(graph_util::setup_sink_by_name(f, "clock_pin"));
      graph_util::create_const(*g, *Dlop::from_binary(initial, /*unsigned_result=*/true))
          .connect_sink(graph_util::setup_sink_by_name(f, "initial"));
      return g;
    };
    auto             ref  = build("ref", "10?1");
    auto             same = build("same", "1011");
    auto             bad  = build("bad", "0011");
    lec::Lec_options opts;
    opts.engine      = "bmc";
    opts.phase       = "after_reset";
    opts.gold_x      = "ignore";
    auto good_result = lec::prove_equal(ref.get(), same.get(), opts);
    EXPECT_EQ(good_result.verdict, Verdict::Proven) << good_result.detail;
    auto bad_result = lec::prove_equal(ref.get(), bad.get(), opts);
    EXPECT_EQ(bad_result.verdict, Verdict::Refuted) << bad_result.detail;
  }
}

TEST(CombEquiv, AndCommutativeProven) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::And, 4, false);
  auto               impl = build_binop(lib, "impl", Ntype_op::And, 4, true);  // b & a

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, AndVsOrRefuted) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::And, 4);
  auto               impl = build_binop(lib, "impl", Ntype_op::Or, 4);

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Refuted) << r.detail;
}

TEST(CombEquiv, SumCommutativeProven) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::Sum, 4, false);
  auto               impl = build_binop(lib, "impl", Ntype_op::Sum, 4, true);  // b + a

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, AddConstEqualProven) {
  hhds::GraphLibrary lib;
  auto               ref  = build_add_const(lib, "ref", 1, 4);
  auto               impl = build_add_const(lib, "impl", 1, 4);

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, AddConstOffByOneRefuted) {
  hhds::GraphLibrary lib;
  auto               ref  = build_add_const(lib, "ref", 1, 4);
  auto               impl = build_add_const(lib, "impl", 2, 4);  // off by one

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Refuted) << r.detail;
}

TEST(CombEquiv, ConcatTruncatesOverwideLaneToDeclaredWindow) {
  hhds::GraphLibrary lib;
  auto               ref  = build_overwide_concat(lib, "ref", false);
  auto               impl = build_overwide_concat(lib, "impl", true);

  auto r = lec::prove_equal(ref.get(), impl.get());
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
}

TEST(CombEquiv, EngineBmcRefutes) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::And, 4);
  auto               impl = build_binop(lib, "impl", Ntype_op::Or, 4);

  lec::Lec_options o;
  o.engine = "bmc";
  auto r   = lec::prove_equal(ref.get(), impl.get(), o);
  EXPECT_EQ(r.verdict, Verdict::Refuted) << r.detail;
}

TEST(CombEquiv, NativeActivationLoopMatchesPrivatePhysicalRealization) {
  hhds::GraphLibrary compact_lib;
  auto               compact = build_active_loop(compact_lib);

  hhds::GraphLibrary physical_lib;
  ASSERT_TRUE(physical_lib.copy_from(compact_lib, "active_body"));
  ASSERT_TRUE(physical_lib.copy_from(compact_lib, "active_top"));
  auto physical = physical_lib.find_io("active_top")->get_graph();
  ASSERT_EQ(graph_util::materialize_occurrences(physical.get(), "test"), 1);

  lec::Lec_options options;
  options.engine = "ind";
  auto result    = lec::prove_equal(compact.get(), physical.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
}

TEST(CombEquiv, ProvenBodyAndMatchedDescriptorUseCompactLoopCertificate) {
  constexpr uint64_t kPastMaterializationCap = (1u << 20) + 1;
  hhds::GraphLibrary ref_lib;
  hhds::GraphLibrary impl_lib;
  auto               ref  = build_active_loop(ref_lib, kPastMaterializationCap);
  auto               impl = build_active_loop(impl_lib, kPastMaterializationCap);

  lec::Lec_options body_options;
  body_options.engine = "ind";
  auto body_result    = lec::prove_equal(ref_lib.find_io("active_body")->get_graph().get(),
                                      impl_lib.find_io("active_body")->get_graph().get(),
                                      body_options);
  ASSERT_EQ(body_result.verdict, Verdict::Proven) << body_result.detail;

  lec::Lec_options top_options;
  top_options.engine   = "ind";
  top_options.collapse = {"active_body"};  // the discharged body theorem
  auto result          = lec::prove_equal(ref.get(), impl.get(), top_options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
  EXPECT_NE(result.detail.find("loop certificate: 1 matched compact recurrence"), std::string::npos) << result.detail;
  ASSERT_EQ(result.loop_certificates.size(), 1);
  EXPECT_NE(result.loop_certificates.front().find("P0=descriptor-exact"), std::string::npos);
  EXPECT_NE(result.loop_certificates.front().find("P4/P5=ordinal-recurrence"), std::string::npos);
}

TEST(CombEquiv, IndexedCarryLoopCertificateOmitsNoDescriptorObligation) {
  hhds::GraphLibrary ref_lib;
  hhds::GraphLibrary impl_lib;
  auto               ref  = build_indexed_carry_loop(ref_lib, 0, 4);
  auto               impl = build_indexed_carry_loop(impl_lib, 0, 4);

  lec::Lec_options options;
  options.engine   = "ind";
  options.collapse = {"indexed_body"};
  auto result      = lec::prove_equal(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
  EXPECT_NE(result.detail.find("loop certificate: 1 matched compact recurrence"), std::string::npos) << result.detail;
  ASSERT_EQ(result.loop_certificates.size(), 1);
  EXPECT_NE(result.loop_certificates.front().find("body=indexed_body"), std::string::npos);
}

TEST(CombEquiv, NestedLoopCertificatesSurviveUnresolvedParents) {
  constexpr uint64_t count = (1u << 20) + 1;
  for (bool outer_loop : {false, true}) {
    SCOPED_TRACE(outer_loop);
    hhds::GraphLibrary ref_lib, impl_lib;
    auto               ref_child  = build_active_loop(ref_lib, count);
    auto               impl_child = build_active_loop(impl_lib, count);
    lec::Lec_options   options;
    options.engine   = "ind";
    options.collapse = {"active_body"};
    auto child_proof = lec::prove_equal(ref_child.get(), impl_child.get(), options);
    ASSERT_EQ(child_proof.verdict, Verdict::Proven) << child_proof.detail;
    const auto wrap = [&](hhds::GraphLibrary& lib, const std::shared_ptr<hhds::Graph>& child) {
      auto io = lib.create_io("wrapper");
      io->add_input("seed", 0);
      io->add_input("enable", 1);
      io->add_output("result", 2);
      for (auto name : {"seed", "enable", "result"}) {
        io->set_bits(name, std::string_view(name) == "enable" ? 1 : 9);
        io->set_unsign(name, true);
      }
      auto graph = io->create_graph();
      auto node  = graph_util::create_typed_node(*graph, Ntype_op::Sub);
      if (outer_loop) {
        node.set_subnode(child->get_io(),
                         hhds::Subnode_loop{
                             .first              = 0,
                             .step               = 1,
                             .count              = count,
                             .index_input        = std::nullopt,
                             .activation_input   = std::nullopt,
                             .next_active_output = std::nullopt,
                         });
        node.create_driver_pin(2).connect_sink(livehd::graph_util::setup_sink_pid(node, 0));
      } else {
        node.set_subnode(child->get_io());
      }
      graph->get_input_pin("seed").connect_sink(livehd::graph_util::setup_sink_pid(node, 0));
      graph->get_input_pin("enable").connect_sink(livehd::graph_util::setup_sink_pid(node, 1));
      auto result = node.create_driver_pin(2);
      graph_util::set_ubits(result, 9);
      result.connect_sink(graph->get_output_pin("result"));
      return graph;
    };
    auto ref  = wrap(ref_lib, ref_child);
    auto impl = wrap(impl_lib, impl_child);
    if (outer_loop) {
      // The outer body theorem was just discharged; even the huge inner
      // loop implementation must now be excluded from this obligation.
      options.collapse = {"active_top"};
    }
    auto result = lec::prove_equal(ref.get(), impl.get(), options);
    EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
    EXPECT_EQ(result.loop_certificates.size(), 1);
  }
}

TEST(CombEquiv, IndexedCarryDescriptorMismatchFallsBackAndRefutes) {
  hhds::GraphLibrary ref_lib;
  hhds::GraphLibrary impl_lib;
  auto               ref  = build_indexed_carry_loop(ref_lib, 0, 4);
  auto               impl = build_indexed_carry_loop(impl_lib, 1, 4);

  lec::Lec_options options;
  options.engine   = "ind";
  options.collapse = {"indexed_body"};
  auto result      = lec::prove_equal(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Refuted) << result.detail;
  EXPECT_EQ(result.detail.find("loop certificate:"), std::string::npos) << result.detail;
  EXPECT_TRUE(result.loop_certificates.empty());
}

// A carry-in port narrower than the carry-out that feeds it truncates the
// carry at every ordinal in cgen and cgen_sim; a width-less port is a scalar.
// The occurrence view threads the carry straight to the previous ordinal's
// 17-bit output, so LEC used to prove the width-less loop equal to the full
// 17-bit one: a false PROVEN against the netlist's own emitted Verilog.
TEST(CombEquiv, LoopCarryInIsTruncatedToItsDeclaredPortWidth) {
  hhds::GraphLibrary wide_lib;
  hhds::GraphLibrary widthless_lib;
  hhds::GraphLibrary scalar_lib;
  auto               wide      = build_indexed_carry_loop(wide_lib, 0, 4);
  auto               widthless = build_indexed_carry_loop(widthless_lib, 0, 4, false, 0);
  auto               scalar    = build_indexed_carry_loop(scalar_lib, 0, 4, false, 1);

  lec::Lec_options options;
  options.engine = "ind";
  auto refuted   = lec::prove_equal(wide.get(), widthless.get(), options);
  EXPECT_EQ(refuted.verdict, Verdict::Refuted) << refuted.detail;

  auto proven = lec::prove_equal(scalar.get(), widthless.get(), options);
  EXPECT_EQ(proven.verdict, Verdict::Proven) << proven.detail;
}

// The same boundary through a PLAIN instance -- the rolled loop above once
// materialized into ordinary Subs. The occurrence view threads `a` straight to
// the 8-bit `x`, so without the fit a width-less `a` was proved equal to an
// 8-bit one while the netlist's own Verilog keeps one bit.
TEST(CombEquiv, PlainSubInputIsTruncatedToItsDeclaredPortWidth) {
  hhds::GraphLibrary wide_lib;
  hhds::GraphLibrary widthless_lib;
  hhds::GraphLibrary scalar_lib;
  auto               wide      = build_port_fit_top(wide_lib, 8);
  auto               widthless = build_port_fit_top(widthless_lib, 0);
  auto               scalar    = build_port_fit_top(scalar_lib, 1);

  lec::Lec_options options;
  options.engine = "ind";
  auto refuted   = lec::prove_equal(wide.get(), widthless.get(), options);
  EXPECT_EQ(refuted.verdict, Verdict::Refuted) << refuted.detail;

  auto proven = lec::prove_equal(scalar.get(), widthless.get(), options);
  EXPECT_EQ(proven.verdict, Verdict::Proven) << proven.detail;
}

// A same-width connection that changes the sign is a reinterpretation too: a
// signed 8-bit `x` into an unsigned 8-bit port reads 0..255 (cgen declares the
// port `input [7:0] a`), and an unsigned one into a signed port reads -128..127.
TEST(CombEquiv, SubPortSignIsReinterpretedAtTheBoundary) {
  hhds::GraphLibrary masked_lib;
  hhds::GraphLibrary to_unsigned_lib;
  hhds::GraphLibrary to_signed_lib;
  auto               masked      = build_port_fit_top(masked_lib, -8, true, false);
  auto               to_unsigned = build_port_fit_top(to_unsigned_lib, 8, true, false);
  auto               to_signed   = build_port_fit_top(to_signed_lib, 8, false, true);

  lec::Lec_options options;
  options.engine = "ind";
  auto proven    = lec::prove_equal(masked.get(), to_unsigned.get(), options);
  EXPECT_EQ(proven.verdict, Verdict::Proven) << proven.detail;

  auto refuted = lec::prove_equal(masked.get(), to_signed.get(), options);
  EXPECT_EQ(refuted.verdict, Verdict::Refuted) << refuted.detail;
}

// The Sext a signed port performs keeps the port's whole width: `x` read
// through a signed 8-bit port is exactly `x` sign-extended from bit 7.
TEST(CombEquiv, SignedPortFitKeepsItsWholeWidth) {
  hhds::GraphLibrary ref_lib;
  hhds::GraphLibrary to_signed_lib;
  auto               ref_io = ref_lib.create_io("port_fit_top");
  ref_io->add_input("x", 0);
  ref_io->set_bits("x", 8);
  ref_io->set_unsign("x", true);
  ref_io->add_output("result", 1);
  ref_io->set_bits("result", 17);
  ref_io->set_unsign("result", true);
  auto ref  = ref_io->create_graph();
  auto sext = graph_util::create_typed_node(*ref, Ntype_op::Sext);
  ref->get_input_pin("x").connect_sink(graph_util::setup_sink_pid(sext, 0));
  graph_util::create_const(*ref, *Dlop::create_integer(8)).connect_sink(graph_util::setup_sink_pid(sext, 1));
  graph_util::set_sbits(sext.create_driver_pin(0), 17);
  sext.create_driver_pin(0).connect_sink(ref->get_output_pin("result"));
  auto to_signed = build_port_fit_top(to_signed_lib, 8, false, true);

  lec::Lec_options options;
  options.engine = "ind";
  auto proven    = lec::prove_equal(ref.get(), to_signed.get(), options);
  EXPECT_EQ(proven.verdict, Verdict::Proven) << proven.detail;
}

// `lhd lec` first dissolves every instance whose definition the other side
// lacks (single_edge::inline_instances_missing_from_other_side, the call
// lhd_kernel_formal makes), so prove_equal never sees that boundary: the
// splice itself must keep the fit. A width-less `a` keeps one bit of the 8-bit
// `x` -- the netlist's own Verilog does -- so the flat 8-bit reference refutes
// and the flat 1-bit one proves. The splice used to thread `x` straight
// through and PROVE the 8-bit reference.
TEST(CombEquiv, AbsentHierarchyInlineKeepsTheSubPortFit) {
  for (const auto& [keep, expect] : {
           std::pair{8, Verdict::Refuted},
           std::pair{1,  Verdict::Proven}
  }) {
    SCOPED_TRACE(keep);
    hhds::GraphLibrary                              ref_lib;
    hhds::GraphLibrary                              impl_lib;
    auto                                            ref  = build_port_fit_top(ref_lib, -keep);
    auto                                            impl = build_port_fit_top(impl_lib, 0);
    const std::vector<std::shared_ptr<hhds::Graph>> ref_graphs{ref};
    std::vector<std::shared_ptr<hhds::Graph>>       impl_graphs;
    for (const auto& g : impl->definitions().graphs()) {
      impl_graphs.push_back(g);
    }
    ASSERT_EQ(single_edge::inline_instances_missing_from_other_side({}, impl_graphs, ref_graphs, impl.get()), 1U);

    lec::Lec_options options;
    options.engine = "ind";
    auto result    = lec::prove_equal(ref.get(), impl.get(), options);
    EXPECT_EQ(result.verdict, expect) << result.detail;
  }
}

namespace {
// impl: word = {b, high} (Concat is MSB first: b in bits [2,4), high in
// [0,2)); u = mixer(word) with mixer.y = a ^ 4'b0101; high = u.y[lo, lo+2);
// o = word. ref: the same function spelled flat, o = {b, b ^ 1}.
// With lo == 2 the loop through `u` exists per WORD only -- pass.legalize keeps
// such an instance, bit-level arcs being acyclic -- and the absent-def splice
// turns it into a packed-slice loop INSIDE the host. With lo == 0 it is a real
// loop (high = high ^ 1).
std::pair<std::shared_ptr<hhds::Graph>, std::shared_ptr<hhds::Graph>> build_spliced_packed_loop(hhds::GraphLibrary& ref_lib,
                                                                                                hhds::GraphLibrary& impl_lib,
                                                                                                int                 lo) {
  namespace gu  = graph_util;
  auto make_top = [](hhds::GraphLibrary& lib) {
    auto io = lib.create_io("spliced_top");
    io->add_input("b", 0);
    io->set_bits("b", 2);
    io->set_unsign("b", true);
    io->add_output("o", 1);
    io->set_bits("o", 4);
    io->set_unsign("o", true);
    auto g = io->create_graph();
    gu::set_ubits(g->get_input_pin("b"), 2);
    return g;
  };
  auto concat_into = [](hhds::Graph& g, const hhds::Pin_class& msb, const hhds::Pin_class& lsb) {
    auto cat = gu::create_typed_node(g, Ntype_op::Concat, 4);
    msb.connect_sink(gu::setup_sink_pid(cat, 0));
    gu::create_const(g, *Dlop::create_integer(2)).connect_sink(gu::setup_sink_pid(cat, 1));
    lsb.connect_sink(gu::setup_sink_pid(cat, 2));
    gu::create_const(g, *Dlop::create_integer(2)).connect_sink(gu::setup_sink_pid(cat, 3));
    auto word = cat.create_driver_pin(0);
    gu::set_ubits(word, 4);
    word.connect_sink(g.get_output_pin("o"));
    return cat;
  };

  auto ref  = make_top(ref_lib);
  auto flip = gu::create_typed_node(*ref, Ntype_op::Xor, 2);
  ref->get_input_pin("b").connect_sink(gu::setup_sink_pid(flip, 0));
  gu::create_const(*ref, *Dlop::create_integer(1)).connect_sink(gu::setup_sink_pid(flip, 0));
  gu::set_ubits(flip.create_driver_pin(0), 2);
  (void)concat_into(*ref, ref->get_input_pin("b"), flip.create_driver_pin(0));

  auto mio = impl_lib.create_io("mixer");
  mio->add_input("a", 0);
  mio->set_bits("a", 4);
  mio->set_unsign("a", true);
  mio->add_output("y", 1);
  mio->set_bits("y", 4);
  mio->set_unsign("y", true);
  auto mixer = mio->create_graph();
  gu::set_ubits(mixer->get_input_pin("a"), 4);
  auto mix = gu::create_typed_node(*mixer, Ntype_op::Xor, 4);
  mixer->get_input_pin("a").connect_sink(gu::setup_sink_pid(mix, 0));
  gu::create_const(*mixer, *Dlop::create_integer(5)).connect_sink(gu::setup_sink_pid(mix, 0));
  gu::set_ubits(mix.create_driver_pin(0), 4);
  mix.create_driver_pin(0).connect_sink(mixer->get_output_pin("y"));

  auto impl = make_top(impl_lib);
  auto sub  = gu::create_typed_node(*impl, Ntype_op::Sub);
  sub.set_name("u");
  sub.set_subnode(mio);
  auto y = sub.create_driver_pin(1);
  gu::set_ubits(y, 4);
  auto slice = gu::create_get_mask(*impl, y, lo, lo + 2);
  auto high  = slice.create_driver_pin(0);
  gu::set_ubits(high, 2);
  auto cat = concat_into(*impl, impl->get_input_pin("b"), high);
  cat.create_driver_pin(0).connect_sink(gu::setup_sink_pid(sub, 0));
  return {ref, impl};
}
}  // namespace

// lhd lec dissolves an instance whose def the other side lacks; that splice
// can expose a packed-slice loop the encoder cannot order (the instance kept
// it at the boundary). The splice re-applies pass.legalize's packed-slice
// repair to the host, so the false loop proves -- and a real loop still
// fails CLOSED (undecided + unsupported), never a verdict.
TEST(CombEquiv, AbsentHierarchyInlineRepairsAPackedSliceLoop) {
  for (const int lo : {2, 0}) {
    SCOPED_TRACE(lo);
    hhds::GraphLibrary ref_lib;
    hhds::GraphLibrary impl_lib;
    auto [ref, impl] = build_spliced_packed_loop(ref_lib, impl_lib, lo);
    std::vector<std::shared_ptr<hhds::Graph>> impl_graphs;
    for (const auto& g : impl->definitions().graphs()) {
      impl_graphs.push_back(g);
    }
    ASSERT_EQ(single_edge::inline_instances_missing_from_other_side({}, impl_graphs, {ref}, impl.get()), 1U);

    lec::Lec_options options;
    options.engine = "ind";
    auto result    = lec::prove_equal(ref.get(), impl.get(), options);
    if (lo == 2) {
      EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
    } else {
      EXPECT_EQ(result.verdict, Verdict::Undecided) << result.detail;
      EXPECT_TRUE(result.unsupported) << result.detail;
      EXPECT_NE(result.detail.find("WORD-LEVEL CYCLE"), std::string::npos) << result.detail;
    }
  }
}

// An instance output crosses TWO declarations: the callee's port, then the
// parent net the instance pin is stamped with. A signed net reinterprets the
// unsigned port's 0..255 as -128..127, so the 17-bit reader sign-extends it.
// The hierarchical proof (fit_sub_port_widths) and the kernel's absent-def
// splice (inline_sub) must both apply that net step, and agree.
TEST(CombEquiv, InstanceOutputNetStepIsKeptByBothBoundaryViews) {
  for (const bool net_signed : {true, false}) {
    for (const bool inline_first : {false, true}) {
      SCOPED_TRACE(std::format("net_signed={} inline_first={}", net_signed, inline_first));
      hhds::GraphLibrary ref_lib;
      hhds::GraphLibrary wrong_lib;
      hhds::GraphLibrary impl_lib;
      auto               ref   = build_net_fit_ref(ref_lib, net_signed);
      auto               wrong = build_net_fit_ref(wrong_lib, !net_signed);
      auto               impl  = build_net_fit_top(impl_lib, net_signed);
      if (inline_first) {
        std::vector<std::shared_ptr<hhds::Graph>> impl_graphs;
        for (const auto& g : impl->definitions().graphs()) {
          impl_graphs.push_back(g);
        }
        ASSERT_EQ(single_edge::inline_instances_missing_from_other_side({}, impl_graphs, {ref}, impl.get()), 1U);
      }

      lec::Lec_options options;
      options.engine = "ind";
      auto proven    = lec::prove_equal(ref.get(), impl.get(), options);
      EXPECT_EQ(proven.verdict, Verdict::Proven) << proven.detail;
      auto refuted = lec::prove_equal(wrong.get(), impl.get(), options);
      EXPECT_EQ(refuted.verdict, Verdict::Refuted) << refuted.detail;
    }
  }
}

TEST(CombEquiv, ZeroCountObservedPlainOutputIsRejectedBeforeCertification) {
  hhds::GraphLibrary lib;
  EXPECT_THROW((void)build_indexed_carry_loop(lib, 0, 0, true), std::logic_error);
}

// --- worker-pipe framing -----------------------------------------------------
//
// The soundness contract of frame_blob/unframe_blob: a blob the pipe truncated
// must FAIL to unframe, so the parent tags the worker "no result" (Unknown)
// instead of reading a half-parsed Query_result whose trailing soundness
// qualifiers (bounded / unsupported / nothing_compared) silently default to
// "nothing to worry about". Before framing, a child SIGKILLed mid-write handed
// the parent an UNBOUNDED Proven for a bounded claim.
TEST(WorkerFrame, RoundTripsAnyPayload) {
  for (const auto& payload : std::vector<std::string>{{}, {"x"}, std::string(9000, '\x01')}) {
    const std::string framed = livehd::lec::frame_blob(payload);
    EXPECT_EQ(framed.size(), payload.size() + sizeof(uint64_t));
    std::string_view got;
    ASSERT_TRUE(livehd::lec::unframe_blob(framed, got));
    EXPECT_EQ(std::string(got), payload);
  }
}

TEST(WorkerFrame, TruncationIsNotAResult) {
  const std::string payload(4096, '\x7f');  // > PIPE_BUF: a real short-write shape
  const std::string framed = livehd::lec::frame_blob(payload);
  std::string_view  got;
  // Every proper prefix must be rejected: a header cut mid-length, a header with
  // no payload, and a payload cut at any point (including one byte short).
  for (size_t n = 0; n < framed.size(); ++n) {
    EXPECT_FALSE(livehd::lec::unframe_blob(std::string_view{framed}.substr(0, n), got))
        << "a " << n << "-byte prefix of a " << framed.size() << "-byte frame was accepted";
  }
  // Trailing garbage (two children's writes interleaved) is not a result either.
  EXPECT_FALSE(livehd::lec::unframe_blob(framed + "junk", got));
  EXPECT_TRUE(livehd::lec::unframe_blob(framed, got));
}

// Prove the cprop rewrite against an untouched graph, with every data bit and
// control symbolic. Induction covers both arbitrary old state and initialization;
// reset is free to toggle, including while the original enable is false.
TEST(LecState, CpropMuxSharingPreservesTransition) {
  namespace gu = graph_util;
  for (bool decoded : {false, true}) {
    for (bool signed_data : {false, true}) {
      hhds::GraphLibrary ref_lib;
      auto               io = ref_lib.create_io("mux_sharing");
      for (int i = 0; i < 6; ++i) {
        auto name = "c" + std::to_string(i);
        io->add_input(name, i);
        io->set_bits(name, 8);  // binary Mux tests nonzero, not only bit zero
        io->set_unsign(name, false);
      }
      for (const auto& [name, pid, bits] : std::vector<std::tuple<std::string, int, int>>{
               {       "a",  6, 16},
               {       "b",  7, 16},
               {"selector",  8,  3},
               {   "clock",  9,  1},
               {   "reset", 10,  1},
               {      "en", 11,  1}
      }) {
        io->add_input(name, pid);
        io->set_bits(name, bits);
        io->set_unsign(name, name == "a" || name == "b" ? !signed_data : true);
      }
      io->add_output("out", 12);
      io->set_bits("out", 16);
      io->set_unsign("out", !signed_data);
      auto ref      = io->create_graph();
      auto constant = [&](int64_t k) { return gu::create_const(*ref, *Dlop::create_integer(k)); };
      auto make     = [&](Ntype_op op) {
        auto node = gu::create_typed_node(*ref, op, 16);
        if (signed_data) {
          gu::set_sbits(node.create_driver_pin(0), 16);
        } else {
          gu::set_ubits(node.create_driver_pin(0), 16);
        }
        return node;
      };
      auto flop = make(Ntype_op::Flop);
      flop.set_name("state");
      auto q = flop.create_driver_pin(0);
      q.connect_sink(ref->get_output_pin("out"));
      gu::setup_sink_by_name(flop, "enable").connect_driver(ref->get_input_pin("en"));
      gu::setup_sink_by_name(flop, "clock_pin").connect_driver(ref->get_input_pin("clock"));
      gu::setup_sink_by_name(flop, "reset_pin").connect_driver(ref->get_input_pin("reset"));
      gu::setup_sink_by_name(flop, "posclk").connect_driver(constant(1));
      gu::setup_sink_by_name(flop, "initial").connect_driver(constant(signed_data ? -3 : 37));
      auto data = q;
      if (decoded) {
        auto hot = make(Ntype_op::Hotmux);
        for (int i = 0; i < 6; ++i) {
          auto eq = gu::create_typed_node(*ref, Ntype_op::EQ, 1);
          gu::set_ubits(eq.create_driver_pin(0), 1);
          livehd::graph_util::setup_sink_pid(eq, 0).connect_driver(ref->get_input_pin("selector"));
          livehd::graph_util::setup_sink_pid(eq, 0).connect_driver(constant(i));
          hot.create_sink_pin(2 * i).connect_driver(eq.create_driver_pin(0));
          hot.create_sink_pin(2 * i + 1).connect_driver(i % 3 == 2 ? q : ref->get_input_pin(i % 3 ? "b" : "a"));
        }
        livehd::graph_util::setup_sink_pid(hot, 12).connect_driver(q);
        data = hot.create_driver_pin(0);
      } else {
        for (int i = 5; i >= 0; --i) {
          auto mux = make(Ntype_op::Mux);
          livehd::graph_util::setup_sink_pid(mux, 0).connect_driver(ref->get_input_pin("c" + std::to_string(i)));
          livehd::graph_util::setup_sink_pid(mux, 1).connect_driver(data);
          livehd::graph_util::setup_sink_pid(mux, 2).connect_driver(i % 3 == 2 ? q : ref->get_input_pin(i % 3 ? "b" : "a"));
          data = mux.create_driver_pin(0);
        }
      }
      data.connect_sink(gu::setup_sink_by_name(flop, "din"));
      hhds::GraphLibrary impl_lib;
      ASSERT_TRUE(impl_lib.copy_from(ref_lib, "mux_sharing"));
      auto impl = impl_lib.find_io("mux_sharing")->get_graph();
      Cprop{}.do_trans(impl);
      Bitwidth{10}.do_trans(impl);
      Enableopt{}.do_trans(impl);
      Cprop{}.do_trans(impl);
      Bitwidth{10}.do_trans(impl);
      size_t muxes = 0;
      for (auto n : impl->body().nodes()) {
        muxes += gu::type_op_of(n) == Ntype_op::Mux;
        if (gu::type_op_of(n) == Ntype_op::Flop) {
          EXPECT_NE(gu::get_driver_of_sink_name(n, "enable"), impl->get_input_pin("en"));
        }
      }
      EXPECT_EQ(muxes, 0);
      lec::Lec_options options;
      options.engine  = "ind";
      options.cones   = "false";
      options.phase   = "free_toreset";
      options.timeout = 20;
      auto result     = lec::prove_equal(ref.get(), impl.get(), options);
      EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail << " decoded=" << decoded << " signed=" << signed_data;
    }
  }
}

// Positional X-plane insertion: each Set_mask writes ONE window (graph/cell.hpp),
// so the unknown bits it covers become known and the ones outside it stay
// unknown -- and a window that is not [0, W) is what makes that positional.
TEST(CombEquiv, WindowInsertionPreservesAndClearsPositionalUnknowns) {
  hhds::GraphLibrary lib;
  auto               io = lib.create_io("window_mask");
  io->add_output("partial", 0);
  io->add_output("complete", 1);
  io->add_output("negative", 2);
  for (const auto name : {"partial", "complete", "negative"}) {
    io->set_bits(name, 8);
    io->set_unsign(name, true);
  }
  auto g      = io->create_graph();
  auto insert = [&](hhds::Pin_class base, int lo, int hi, const char* value) {
    auto node = graph_util::create_typed_node(*g, Ntype_op::Set_mask);
    base.connect_sink(graph_util::setup_sink_by_name(node, "a"));
    graph_util::connect_bit_range(node, lo, hi);
    graph_util::create_const(*g, *Dlop::from_binary(value, true)).connect_sink(graph_util::setup_sink_by_name(node, "value"));
    auto out = node.create_driver_pin(0);
    graph_util::set_ubits(out, 8);
    return out;
  };
  const auto all_unknown = [&] { return graph_util::create_const(*g, *Dlop::from_binary("????????", true)); };
  // bits [2,6) <- 1,?,0,1 (LSB-first), so bits 0,1,6,7 and bit 3 stay unknown.
  auto       partial     = insert(all_unknown(), 2, 6, "10?1");
  // Three more windows cover every remaining unknown bit, bit 3 included.
  auto       complete    = insert(insert(insert(partial, 0, 2, "01"), 6, 8, "10"), 3, 4, "1");
  // The -1 spelling is "replace the whole value": nothing of the base survives.
  auto       negative    = insert(all_unknown(), 0, 8, "1011011");
  partial.connect_sink(g->get_output_pin("partial"));
  complete.connect_sink(g->get_output_pin("complete"));
  negative.connect_sink(g->get_output_pin("negative"));
  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  lec::Encoder      encoder(tm);
  encoder.set_x_dontcare(true);
  auto encoded = encoder.encode(g.get());
  ASSERT_TRUE(encoded.ok) << encoded.error;
  const auto& p = encoded.outputs.at("partial");
  const auto& c = encoded.outputs.at("complete");
  EXPECT_EQ(solver.simplify(p.x_mask), tm.mkBitVector(8, 0xcb));  // bits 0,1,3,6,7
  EXPECT_EQ(solver.simplify(c.x_mask), tm.mkBitVector(8, 0));
  EXPECT_EQ(solver.simplify(c.term), tm.mkBitVector(8, 0xad));
  const auto& n = encoded.outputs.at("negative");
  EXPECT_EQ(solver.simplify(n.x_mask), tm.mkBitVector(8, 0));
  EXPECT_EQ(solver.simplify(n.term), tm.mkBitVector(8, 0x5b));
}

// ---- transparent `__flat___` hierarchy wrappers (graph/README.md) -----------
// The virtual (HHDS occurrence), physically flattened and inlined forms of a
// hierarchy with transparent wrappers must all name the state `foo.bar.q`.

namespace {

struct Transparent_design {
  hhds::GraphLibrary                        lib;
  std::vector<std::shared_ptr<hhds::Graph>> graphs;

  std::shared_ptr<hhds::Graph> empty() {
    auto io = lib.create_io("m" + std::to_string(graphs.size()));
    io->add_input("clk", 0);
    io->add_input("d", 1);
    io->add_output("q", 2);
    for (auto name : {"clk", "d", "q"}) {
      io->set_bits(name, 1);
      io->set_unsign(name, true);
    }
    auto g = io->create_graph();
    graphs.push_back(g);
    return g;
  }

  std::shared_ptr<hhds::Graph> state(const std::string& name, bool wrong = false) {
    auto g    = empty();
    auto flop = graph_util::create_typed_node(*g, Ntype_op::Flop);
    flop.set_name(name);
    auto d = g->get_input_pin("d");
    if (wrong) {
      auto inv = graph_util::create_typed_node(*g, Ntype_op::Not);
      d.connect_sink(graph_util::setup_sink_by_name(inv, "a"));
      d = inv.create_driver_pin(0);
      graph_util::set_ubits(d, 1);
    }
    d.connect_sink(graph_util::setup_sink_by_name(flop, "din"));
    g->get_input_pin("clk").connect_sink(graph_util::setup_sink_by_name(flop, "clock_pin"));
    graph_util::create_const(*g, *Dlop::create_integer(0)).connect_sink(graph_util::setup_sink_by_name(flop, "initial"));
    auto q = flop.create_driver_pin(0);
    graph_util::set_ubits(q, 1);
    graph_util::set_pin_name(q, name);
    q.connect_sink(g->get_output_pin("q"));
    return g;
  }

  hhds::Node_class instance(hhds::Graph* g, const std::shared_ptr<hhds::Graph>& child, const std::string& name) {
    auto n = graph_util::create_typed_node(*g, Ntype_op::Sub);
    n.set_subnode(child->get_io());
    if (!name.empty()) {
      n.set_name(name);
    }
    g->get_input_pin("clk").connect_sink(n.create_sink_pin(0));
    g->get_input_pin("d").connect_sink(n.create_sink_pin(1));
    graph_util::set_ubits(n.create_driver_pin(2), 1);
    return n;
  }

  std::shared_ptr<hhds::Graph> wrap(const std::shared_ptr<hhds::Graph>& child, const std::string& name) {
    auto g = empty();
    auto n = instance(g.get(), child, name);
    n.create_driver_pin(2).connect_sink(g->get_output_pin("q"));
    return g;
  }

  std::shared_ptr<hhds::Graph> hierarchy(bool wrong = false) {
    return wrap(wrap(wrap(wrap(state("q", wrong), "__flat___inner"), "bar"), "__flat___outer"), "foo");
  }
};

// Logical state names over the virtual (occurrence) hierarchy.
std::set<std::string> logical_state_names(hhds::Graph* g) {
  std::set<std::string> names;
  for (const auto& n : g->occurrences().nodes()) {
    if (graph_util::type_op_of(n) == Ntype_op::Flop) {
      names.insert(graph_util::logical_hier_name(n.get_hier_name()));
    }
  }
  return names;
}

// RAW (un-normalized) flop names of a physical body: flatten/inline must not
// spell a transparent wrapper into the names they build.
std::set<std::string> raw_flop_names(hhds::Graph* g) {
  std::set<std::string> names;
  for (auto n : g->body().nodes()) {
    if (graph_util::type_op_of(n) == Ntype_op::Flop) {
      names.insert(std::string{graph_util::node_name_of(n)});
    }
  }
  return names;
}

// Inline every Sub of `g`'s body (outermost first), one splice per collection.
void inline_all(hhds::Graph* g) {
  for (;;) {
    hhds::Node_class sub;
    for (auto n : g->body().nodes()) {
      if (graph_util::type_op_of(n) == Ntype_op::Sub) {
        sub = n;
        break;
      }
    }
    if (sub.is_invalid()) {
      return;
    }
    ASSERT_TRUE(graph_util::inline_sub_instance(g, sub, "test"));
  }
}

}  // namespace

TEST(LecNames, TransparentVirtualPhysicalAndInlineNamesAgree) {
  Transparent_design d;
  auto               flat = d.state("foo.bar.q");
  auto               hier = d.hierarchy();
  EXPECT_EQ(logical_state_names(hier.get()), logical_state_names(flat.get()));
  auto materialized = livehd::partition::flatten_hierarchy(hier.get(), &d.lib, "materialized");
  ASSERT_NE(materialized, nullptr);
  EXPECT_EQ(raw_flop_names(materialized.get()), (std::set<std::string>{"foo.bar.q"}));
  EXPECT_EQ(logical_state_names(materialized.get()), logical_state_names(flat.get()));
  semdiff::Semdiff_options so;
  so.matching_names = true;
  EXPECT_TRUE(semdiff::structural_identical(flat.get(), materialized.get(), so));
  lec::Lec_options lo;
  lo.engine          = "ind";
  lo.timeout         = 2;
  auto virtual_proof = lec::prove_equal(flat.get(), hier.get(), lo);
  EXPECT_EQ(virtual_proof.verdict, Verdict::Proven) << virtual_proof.detail;
  // Inline the outer call first: a subsequently inlined wrapper carries the
  // combined name foo.__flat___outer. Both forms must preserve foo.bar.q.
  inline_all(hier.get());
  EXPECT_EQ(raw_flop_names(hier.get()), (std::set<std::string>{"foo.bar.q"}));
  EXPECT_EQ(logical_state_names(hier.get()), logical_state_names(flat.get()));
  EXPECT_TRUE(semdiff::structural_identical(flat.get(), hier.get(), so));
  auto proof = lec::prove_equal(flat.get(), hier.get(), lo);
  EXPECT_EQ(proof.verdict, Verdict::Proven) << proof.detail;
}

// An ANONYMOUS instance is transparent too (HHDS get_hier_name drops it), and
// flatten/inline must agree rather than spell a `sub_<nid>` level.
TEST(LecNames, AnonymousInstanceIsTransparentWhenMaterialized) {
  Transparent_design d;
  auto               mid = d.wrap(d.state("q"), "");
  auto               top = d.wrap(mid, "foo");
  auto               m   = livehd::partition::flatten_hierarchy(top.get(), &d.lib, "materialized");
  ASSERT_NE(m, nullptr);
  EXPECT_EQ(raw_flop_names(m.get()), (std::set<std::string>{"foo.q"}));
  // Deep inline walks children first (graph/inline_sub.hpp): dissolving the
  // anonymous level inside `mid` adds no component, then `foo.` is prefixed.
  inline_all(mid.get());
  inline_all(top.get());
  EXPECT_EQ(raw_flop_names(top.get()), (std::set<std::string>{"foo.q"}));
}

TEST(LecNames, TransparentChangedTransitionDoesNotMatch) {
  Transparent_design d;
  auto               ref = d.state("foo.bar.q");
  auto               bad = d.hierarchy(true);
  lec::Lec_options   lo;
  lo.engine  = "bmc";
  lo.bound   = 2;
  lo.timeout = 2;
  auto proof = lec::prove_equal(ref.get(), bad.get(), lo);
  EXPECT_EQ(proof.verdict, Verdict::Refuted) << proof.detail;
  auto flat_bad = livehd::partition::flatten_hierarchy(bad.get(), &d.lib, "bad_flat");
  ASSERT_NE(flat_bad, nullptr);
  semdiff::Semdiff_options so;
  so.matching_names = true;
  EXPECT_FALSE(semdiff::structural_identical(ref.get(), flat_bad.get(), so));
}

// Two transparent instances of one stateful def share the logical state name
// `q`: the encoder must refuse the ambiguity, never merge the two registers.
TEST(LecNames, TransparentRepeatedInstancesDoNotMergeState) {
  Transparent_design d;
  auto               child = d.state("q");
  auto               top   = d.wrap(child, "left");
  auto               right = d.instance(top.get(), child, "right");
  EXPECT_EQ(logical_state_names(top.get()), (std::set<std::string>{"left.q", "right.q"}));
  for (auto n : top->body().nodes()) {
    if (graph_util::type_op_of(n) == Ntype_op::Sub) {
      n.set_name(n == right ? "__flat___right" : "__flat___left");
    }
  }
  lec::Lec_options lo;
  lo.engine  = "ind";
  auto proof = lec::prove_equal(top.get(), top.get(), lo);
  EXPECT_EQ(proof.verdict, Verdict::Undecided) << proof.detail;
  EXPECT_NE(proof.detail.find("ambiguous logical state name"), std::string::npos);
}

namespace {
struct Scoped_racer_stall {
  std::optional<std::string> seconds, only;
  Scoped_racer_stall() {
    if (const char* value = std::getenv("LHD_LEC_RACER_STALL_S")) {
      seconds = value;
    }
    if (const char* value = std::getenv("LHD_LEC_RACER_STALL_ONLY")) {
      only = value;
    }
    ::setenv("LHD_LEC_RACER_STALL_S", "60", 1);
    ::setenv("LHD_LEC_RACER_STALL_ONLY", "0", 1);
  }
  ~Scoped_racer_stall() {
    if (seconds) {
      ::setenv("LHD_LEC_RACER_STALL_S", seconds->c_str(), 1);
    } else {
      ::unsetenv("LHD_LEC_RACER_STALL_S");
    }
    if (only) {
      ::setenv("LHD_LEC_RACER_STALL_ONLY", only->c_str(), 1);
    } else {
      ::unsetenv("LHD_LEC_RACER_STALL_ONLY");
    }
  }
};
}  // namespace

// A preprocessing/CaDiCaL stall ignores cvc5's tlimit. Killing that induction
// attempt must leave the dispatcher alive to check reachable behaviour by BMC.
TEST(LecBudget, StalledInductionLeavesBmcForBoundedProofAndReachableRefute) {
  Scoped_racer_stall stall;
  for (const bool wrong : {false, true}) {
    Transparent_design d;
    auto               ref  = d.state("q");
    auto               impl = d.state("q", wrong);
    lec::Lec_options   options;
    options.engine            = "auto";
    options.timeout           = 2;
    options.min_timeout       = 1;
    options.bound             = 2;
    // An entity-name hint must not take the entire grant before the ladder.
    options._preferred_engine = "ind";
    const auto start          = std::chrono::steady_clock::now();
    const auto result         = lec::prove_equal_isolated(ref.get(), impl.get(), options);
    const auto elapsed        = std::chrono::steady_clock::now() - start;
    EXPECT_LT(elapsed, std::chrono::seconds(6)) << result.detail;
    EXPECT_EQ(result.engine, "bmc") << result.detail;
    EXPECT_FALSE(result.arms.empty()) << result.detail;
    if (!wrong) { EXPECT_TRUE(std::any_of(result.arms.begin(),result.arms.end(),[](const auto& arm) {return arm.engine == "ind" && (arm.killed || arm.queued);})) << result.detail; }
    if (wrong) {
      EXPECT_EQ(result.verdict, Verdict::Refuted) << result.detail;
      EXPECT_FALSE(result.trace.empty()) << result.detail;
    } else {
      EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
      EXPECT_TRUE(result.bounded) << result.detail;
      EXPECT_GT(result.output_checks, 0);
      EXPECT_TRUE(result.trace.empty());
    }
  }
}

TEST(LecBudget, FastInductionStillProvesUnboundedWithReservedBudget) {
  Transparent_design d;
  auto               ref  = d.state("q");
  auto               impl = d.state("q");
  lec::Lec_options   options;
  options.engine      = "auto";
  options.timeout     = 2;
  options.min_timeout = 1;
  const auto result   = lec::prove_equal_isolated(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
  EXPECT_FALSE(result.bounded) << result.detail;
  EXPECT_EQ(result.engine, "ind") << result.detail;
}

TEST(LecBudget, UnreachableInductionCounterexampleStillRunsBmc) {
  Transparent_design d;
  auto               ref  = d.state("q");
  auto               impl = d.state("q");
  for (const auto& graph : {ref, impl}) {
    for (auto node : graph->body().nodes()) {
      if (graph_util::type_op_of(node) == Ntype_op::Flop) {
        auto din = graph_util::setup_sink_by_name(node, "din");
        din.del_sink(din.get_driver_pin());
        graph_util::create_const(*graph, *Dlop::create_integer(0)).connect_sink(din);
      }
    }
  }
  // q is always zero from initialization. The arbitrary-state induction step
  // can see q=1, where the output expressions differ; that is unreachable.
  auto output = impl->get_output_pin("q");
  output.del_sink(output.get_driver_pin());
  graph_util::create_const(*impl, *Dlop::create_integer(0)).connect_sink(output);
  lec::Lec_options options;
  options.engine      = "ind";
  options.timeout     = 2;
  options.min_timeout = 1;
  options.bound       = 2;
  const auto step     = lec::prove_equal_isolated(ref.get(), impl.get(), options);
  EXPECT_NE(step.verdict, Verdict::Proven) << step.detail;
  // A FORCED ind that cannot clear an (unreachable) step counterexample is the
  // options' doing: REFUTED-unchecked (exit 7), never REFUTED-cex.
  EXPECT_EQ(lec::query_outcome(step, /*engine_forced=*/true), lec::Outcome::Refuted_unchecked) << step.detail;
  options.engine    = "auto";
  const auto result = lec::prove_equal_isolated(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
  EXPECT_TRUE(result.bounded) << result.detail;
  EXPECT_EQ(result.engine, "bmc") << result.detail;
}

TEST(LecBudget, ExhaustedCollapsedInductionShareGoesDirectlyToBmc) {
  Scoped_racer_stall stall;
  Transparent_design d;
  auto               ref  = d.state("q");
  auto               impl = d.state("q");
  lec::Lec_options   options;
  options.engine                = "auto";
  options.timeout               = 2;
  options.min_timeout           = 1;
  options._bmc_reserved_timeout = 2;
  options._wall_timeout_ms      = 1000;
  const auto result             = lec::prove_equal_isolated(ref.get(), impl.get(), options);
  EXPECT_EQ(result.verdict, Verdict::Proven) << result.detail;
  EXPECT_TRUE(result.bounded) << result.detail;
  EXPECT_EQ(result.engine, "bmc") << result.detail;
  EXPECT_NE(result.detail.find("goes directly to BMC"), std::string::npos) << result.detail;
}

// ── Top-level outcome mapping (owner rulings 2026-10-14) ────────────────────
// Every undecided result is named by its cause; "unknown" names nothing.
TEST(LecOutcome, ClassifierNamesMemoryBoundAndNoReason) {
  using lec::Outcome;
  EXPECT_EQ(lec::undecided_outcome(" (out of memory: cvc5 reported MEMOUT, a resource limit; the run is TIMEOUT-memory)", false, false),
            Outcome::Timeout_memory);
  EXPECT_EQ(lec::undecided_outcome("out of memory in the bmc engine (std::bad_alloc): the memory backstop (RLIMIT_AS) denied", false,
                                   false),
            Outcome::Timeout_memory);
  EXPECT_EQ(lec::undecided_outcome("isolated proof worker died: killed by signal 9 (Killed); out of memory: a SIGKILL", false, false),
            Outcome::Timeout_memory);
  EXPECT_EQ(lec::undecided_outcome(" (hit formal.timeout=5s: cvc5 reported TIMEOUT, a resource limit)", false, false),
            Outcome::Timeout_time);
  EXPECT_EQ(lec::undecided_outcome("never checked: its window/past() needs 29 cycle(s) of history, deeper than formal.bound=4", false,
                                   false),
            Outcome::Refuted_unchecked);
  EXPECT_EQ(lec::undecided_outcome(" (cvc5 gave NO reason (UNKNOWN_REASON): this is a bug; please report it)", false, false),
            Outcome::Refuted_unexplained);
  EXPECT_EQ(lec::undecided_outcome(" (cvc5 cannot check this functionality: it reported INCOMPLETE)", false, false),
            Outcome::Refuted_unsupported);
  EXPECT_EQ(lec::undecided_outcome(" (cvc5 cannot check this functionality: it reported INCOMPLETE)", false, true),
            Outcome::Refuted_unchecked);
}

// The auto portfolio folds its arms by the DECIDING arm's own cause: a wall
// kill in one arm must not hide another arm's contradictory assumptions, while
// an engine that merely cannot check the logic yields to another's timeout.
TEST(LecOutcome, PortfolioFoldClassifiesTheDecidingArm) {
  using lec::Outcome;
  lec::Query_result r;
  r.verdict      = Verdict::Undecided;
  r.detail       = "auto: every engine/profile arm was undecided; bmc:internal=TIMEOUT: hit formal.timeout (shared portfolio "
                   "wall deadline); ind:internal=VACUOUS: ; active assumption set is CONTRADICTORY (vacuous proof rejected)";
  r.cause_detail = "auto: ind:internal: ; active assumption set is CONTRADICTORY (vacuous proof rejected)";
  EXPECT_EQ(lec::query_outcome(r, false), Outcome::Refuted_vacuous);  // contradictory assumes: a vacuous check is a problem
  r.cause_detail.clear();
  EXPECT_EQ(lec::query_outcome(r, false), Outcome::Timeout_time) << "the concatenation alone lets the timeout win";
}

// An arm that ran out of time decides over an arm that crashed or gave up
// without a reason (owner ruling 2026-10-15: undecided-by-time is
// TIMEOUT-time, never REFUTED-unexplained; br_ram_flops: two bmc profiles hit
// a cvc5 array-theory exception while bmc:intblast reached the wall deadline).
// A design-level refusal and contradictory assumptions still win.
TEST(LecOutcome, TimeLimitedArmDecidesOverCrashedArm) {
  using lec::Outcome;
  lec::Query_result crash, timeout, refusal, vacuous;
  for (auto* r : {&crash, &timeout, &refusal, &vacuous}) {
    r->verdict = Verdict::Undecided;
  }
  crash.detail
      = "ENGINE CRASH (bmc): Array theory solver does not yet support write-chains connecting two different "
        "constant arrays";
  timeout.detail = "hit formal.timeout (shared portfolio wall deadline)";
  refusal.detail = "; ref encode failed: the encoder does not model cell X";
  vacuous.detail = "; active assumption set is CONTRADICTORY (vacuous proof rejected)";
  EXPECT_EQ(lec::deciding_attempt({&crash, &timeout, &crash}), 1);
  EXPECT_EQ(lec::deciding_attempt({&timeout, &crash}), 0);
  EXPECT_EQ(lec::deciding_attempt({&crash}), 0) << "alone, the crash still names a bug";
  EXPECT_EQ(lec::deciding_attempt({&crash, &timeout, &refusal}), 2) << "no amount of time models a refused design";
  EXPECT_EQ(lec::deciding_attempt({&timeout, &vacuous}), 1);
  lec::Query_result folded;
  folded.verdict = Verdict::Undecided;
  folded.detail  = "auto: every engine/profile arm was undecided; bmc:internal=UNEXPLAINED: " + crash.detail
                  + "; bmc:intblast=TIMEOUT: " + timeout.detail;
  folded.cause_detail = "auto: bmc:intblast: " + timeout.detail;
  EXPECT_EQ(lec::query_outcome(folded, false), Outcome::Timeout_time);
}

TEST(LecOutcome, InterfaceVacuousAndBoundedDepth) {
  using lec::Outcome;
  lec::Query_result r;
  r.verdict        = Verdict::Undecided;
  r.detail         = "; matched portion EQUIVALENT (only the unmatched cut points above remain)";
  r.unmatched_impl = {"extra"};
  EXPECT_EQ(lec::query_outcome(r, false), Outcome::Refuted_interface) << "a one-sided top output";
  r.unmatched_impl = {"nxt:reg"};
  EXPECT_EQ(lec::query_outcome(r, false), Outcome::Refuted_unexplained) << "internal state alone is a trigger, not a verdict";
  r.nothing_compared = true;
  r.unmatched_impl.clear();
  r.detail = "; nothing to compare: neither side exposes an output or state cell";
  EXPECT_EQ(lec::query_outcome(r, false), Outcome::Refuted_vacuous);

  lec::Query_result p;
  p.verdict       = Verdict::Proven;
  p.bounded       = true;
  p.checked_steps = 6;
  const auto o    = lec::query_outcome(p, false);
  EXPECT_EQ(lec::outcome_word(o, lec::query_outcome_depth(p, o)), "PROVEN-bounded(6)");
  p.bounded = false;
  EXPECT_EQ(lec::query_outcome(p, false), Outcome::Proven_full);
  lec::Query_result c;
  c.verdict = Verdict::Refuted;
  EXPECT_EQ(lec::outcome_info(lec::query_outcome(c, false)).exit_code, lec::kExitCex);
}

// A bmc divergence that needs only the free outputs of boxes on ONE side (a
// netlist checked without its cell models) is a real counterexample (owner
// ruling 2026-10-14); anything that could be a pairing failure is not.
TEST(LecOutcome, OneSidedBoxDivergenceIsACounterexample) {
  using lec::Outcome;
  lec::Query_result r;
  r.verdict        = Verdict::Undecided;
  r.engine         = "auto";
  r.detail         = "auto: every engine/profile arm was undecided; bmc:lazy=UNPAIRED: ...; matched portion DIFFERS (witness "
                     "below; may be an artifact of the unmatched cut points)";
  r.witness        = "first divergence at checked step 6/6: y(ref=1 impl=0)";
  r.unmatched_impl = {"bbin:INVx1#o0:A", "bbin:INVx1#o1:A"};
  EXPECT_EQ(lec::query_outcome(r, false), Outcome::Refuted_cex);
  EXPECT_EQ(lec::outcome_info(lec::query_outcome(r, false)).exit_code, lec::kExitCex);

  r.unmatched_ref = {"bbin:INVx1#u0:A"};
  EXPECT_NE(lec::query_outcome(r, false), Outcome::Refuted_cex) << "boxes on both sides may be one unpaired cell";
  r.unmatched_ref.clear();
  r.unmatched_impl.push_back("nxt:q");
  EXPECT_NE(lec::query_outcome(r, false), Outcome::Refuted_cex) << "one-sided state can make the divergence an artifact";
  r.unmatched_impl.pop_back();
  r.engine = "ind";
  EXPECT_NE(lec::query_outcome(r, false), Outcome::Refuted_cex) << "an induction step starts from an arbitrary state";
  r.engine = "auto";
  r.witness.clear();
  EXPECT_NE(lec::query_outcome(r, false), Outcome::Refuted_cex) << "no trace, no counterexample";
  r.witness = "first divergence at checked step 6/6: y(ref=1 impl=0)";
  r.detail  = "; matched portion EQUIVALENT (only the unmatched cut points above remain)";
  EXPECT_NE(lec::query_outcome(r, false), Outcome::Refuted_cex) << "the common outputs agree";
}

TEST(VerifyOutcome, RowsFoldBySeverity) {
  using lec::Outcome;
  lec::Verify_result v;
  v.verdict          = Verdict::Proven;
  v.nothing_to_check = true;
  EXPECT_EQ(lec::verify_outcome(v, false), Outcome::Refuted_vacuous);

  v.nothing_to_check = false;
  v.verdict          = Verdict::Undecided;
  v.reset_hold       = 2;
  lec::Prop_result proven;
  proven.kind      = "assert";
  proven.verdict   = Verdict::Proven;
  proven.proven_to = 7;
  lec::Prop_result never;
  never.kind          = "assert";
  never.undecided_why = "never checked: its window/past() needs 29 cycle(s) of history, deeper than formal.bound=4";
  lec::Prop_result timed;
  timed.kind          = "assert";
  timed.undecided_at  = 4;
  timed.undecided_why = "solver gave up at cycle 4 (hit formal.timeout=1s: cvc5 reported TIMEOUT, a resource limit)";
  v.props             = {proven, timed};
  EXPECT_EQ(lec::verify_outcome(v, false), Outcome::Timeout_time);
  v.props = {proven, timed, never};
  EXPECT_EQ(lec::verify_outcome(v, false), Outcome::Refuted_unchecked) << "rows fold by severity: unchecked > timeout";
  lec::Prop_result silent;
  silent.kind = "assert";
  v.props     = {proven, silent};
  EXPECT_EQ(lec::verify_outcome(v, false), Outcome::Refuted_unexplained) << "an undecided row with no reason is a bug";

  v.verdict = Verdict::Proven;
  v.props   = {proven};
  const auto o = lec::verify_outcome(v, false);
  EXPECT_EQ(lec::outcome_word(o, lec::verify_outcome_depth(v, o)), "PROVEN-bounded(6)");
}

// A verify run with NO assert obligation is REFUTED-vacuous at the top level
// (nothing to check: a sign of a problem), never undecided in the engine and
// never a silent full proof.
TEST(VerifyEngine, NoObligationRunIsProvenNothingToCheck) {
  hhds::GraphLibrary lib;
  auto               g = build_add_const(lib, "noprop", 1, 4);
  lec::Lec_options   o;
  o.engine = "bmc";
  o.bound  = 2;
  auto r   = lec::prove_properties(g.get(), o);
  EXPECT_EQ(r.verdict, Verdict::Proven) << r.detail;
  EXPECT_TRUE(r.nothing_to_check) << r.detail;
  EXPECT_FALSE(r.cex_free_box);
  EXPECT_EQ(lec::verify_outcome(r, true), lec::Outcome::Refuted_vacuous) << r.detail;
}
