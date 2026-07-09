// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass.formal Prover acceptance: single-design property verdicts on tiny
// hand-built graphs (no reader). Mirrors pass/lec query_test's builders.

#include <memory>
#include <string>

#include "cell.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "prove.hpp"

using namespace livehd;
using livehd::formal::Verdict;

namespace {

// out = a <op> b, both operands on sink "a" (And/EQ/...). `bits` is the magnitude
// width; the bits attr is magnitude+1 (unsigned drops the spare sign bit).
std::shared_ptr<hhds::Graph> build_binop(hhds::GraphLibrary& lib, const std::string& mod, Ntype_op op, int bits) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits + 1);
  gio->set_unsign("a", true);
  gio->add_input("b", 1);
  gio->set_bits("b", bits + 1);
  gio->set_unsign("b", true);
  gio->add_output("out", 2);
  gio->set_bits("out", bits + 1);
  gio->set_unsign("out", true);

  auto g      = gio->create_graph();
  auto node   = graph_util::create_typed_node(*g, op);
  auto sink_a = graph_util::setup_sink_by_name(node, "as");
  g->get_input_pin("a").connect_sink(sink_a);
  g->get_input_pin("b").connect_sink(sink_a);
  auto dpin = node.create_driver_pin(0);
  graph_util::set_bits(dpin, bits + 1);
  graph_util::set_unsign(dpin);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

// out = (a == a): a structural tautology (always 1) regardless of a's width.
std::shared_ptr<hhds::Graph> build_eq_same(hhds::GraphLibrary& lib, const std::string& mod, int bits) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits + 1);
  gio->set_unsign("a", true);
  gio->add_output("out", 1);
  gio->set_bits("out", 2);
  gio->set_unsign("out", true);

  auto g      = gio->create_graph();
  auto node   = graph_util::create_typed_node(*g, Ntype_op::EQ);
  auto sink_a = graph_util::setup_sink_by_name(node, "as");
  g->get_input_pin("a").connect_sink(sink_a);
  g->get_input_pin("a").connect_sink(sink_a);  // a == a
  auto dpin = node.create_driver_pin(0);
  graph_util::set_bits(dpin, 2);  // 1-bit result (magnitude+1)
  graph_util::set_unsign(dpin);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

hhds::Pin_class out_drv(const std::shared_ptr<hhds::Graph>& g) { return g->get_output_pin("out").get_driver_pins()[0]; }

}  // namespace

TEST(Prove, TautologyEqSame) {
  hhds::GraphLibrary lib;
  auto               g = build_eq_same(lib, "m", 4);
  formal::Prover     p(g.get());
  auto               d = out_drv(g);
  EXPECT_EQ(p.is_true(d).verdict, Verdict::Proven);    // a == a is always true
  EXPECT_EQ(p.is_false(d).verdict, Verdict::Refuted);  // ...so not always false
}

TEST(Prove, EqAbNotAlwaysTrue) {
  hhds::GraphLibrary lib;
  auto               g = build_binop(lib, "m", Ntype_op::EQ, 4);
  formal::Prover     p(g.get());
  auto               d = out_drv(g);
  EXPECT_EQ(p.is_true(d).verdict, Verdict::Refuted);  // a == b has a falsifying assignment
}

TEST(Prove, Onehot0Const) {
  hhds::GraphLibrary lib;
  auto               g = build_binop(lib, "m", Ntype_op::And, 4);  // just need a graph to host consts
  formal::Prover     p(g.get());
  auto               c4v = Dlop::create_integer(4);  // 0b100 — exactly one bit
  auto               c3v = Dlop::create_integer(3);  // 0b011 — two bits
  auto               c0v = Dlop::create_integer(0);  // zero — allowed (default arm)
  auto               c4  = graph_util::create_const(*g, *c4v);
  auto               c3  = graph_util::create_const(*g, *c3v);
  auto               c0  = graph_util::create_const(*g, *c0v);
  EXPECT_EQ(p.is_onehot0(c4).verdict, Verdict::Proven);
  EXPECT_EQ(p.is_onehot0(c0).verdict, Verdict::Proven);
  EXPECT_EQ(p.is_onehot0(c3).verdict, Verdict::Refuted);
}

TEST(Prove, ConeMaxGateDefers) {
  hhds::GraphLibrary    lib;
  auto                  g = build_binop(lib, "m", Ntype_op::And, 4);
  formal::Prove_options o;
  o.cone_max = 1;  // cone is {And, a, b} > 1 -> skip the solver
  formal::Prover p(g.get(), o);
  auto           d = out_drv(g);
  EXPECT_EQ(p.is_true(d).verdict, Verdict::Unknown);
}
