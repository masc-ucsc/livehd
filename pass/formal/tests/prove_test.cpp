// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// pass.formal Prover acceptance: single-design property verdicts on tiny
// hand-built graphs (no reader). Mirrors pass/lec query_test's builders.

#include "prove.hpp"

#include <memory>
#include <string>

#include "cell.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "reduce_lower.hpp"

using namespace livehd;
using livehd::formal::Verdict;

namespace {

// out = a <op> b, both operands on sink "a" (And/EQ/...). `bits` is the
// literal unsigned width.
std::shared_ptr<hhds::Graph> build_binop(hhds::GraphLibrary& lib, const std::string& mod, Ntype_op op, int bits) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits);
  gio->set_unsign("a", true);
  gio->add_input("b", 1);
  gio->set_bits("b", bits);
  gio->set_unsign("b", true);
  gio->add_output("out", 2);
  const int out_bits = op == Ntype_op::EQ ? 1 : bits;
  gio->set_bits("out", out_bits);
  gio->set_unsign("out", true);

  auto g      = gio->create_graph();
  auto node   = graph_util::create_typed_node(*g, op);
  // ONE DRIVER PER SINK PIN (graph/cell.hpp): each operand of a banked op gets
  // its own consecutive pid, so call setup_sink_by_name once PER OPERAND. A
  // single reused sink pin is the old pile-onto-pin-0 shape, which now loses
  // every operand after the first.
  g->get_input_pin("a").connect_sink(graph_util::setup_sink_by_name(node, "as"));
  g->get_input_pin("b").connect_sink(graph_util::setup_sink_by_name(node, "as"));
  auto dpin = node.create_driver_pin(0);
  graph_util::set_ubits(dpin, out_bits);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

// out = (a == a): a structural tautology (always 1) regardless of a's width.
std::shared_ptr<hhds::Graph> build_eq_same(hhds::GraphLibrary& lib, const std::string& mod, int bits) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits);
  gio->set_unsign("a", true);
  gio->add_output("out", 1);
  gio->set_bits("out", 1);
  gio->set_unsign("out", true);

  auto g      = gio->create_graph();
  auto node   = graph_util::create_typed_node(*g, Ntype_op::EQ);
  // a == a: two operands, so two sink pins (see build_binop).
  g->get_input_pin("a").connect_sink(graph_util::setup_sink_by_name(node, "as"));
  g->get_input_pin("a").connect_sink(graph_util::setup_sink_by_name(node, "as"));
  auto dpin = node.create_driver_pin(0);
  graph_util::set_ubits(dpin, 1);
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

TEST(Prove, HotmuxPairsDefaultAndExclusivity) {
  namespace gu = livehd::graph_util;
  hhds::GraphLibrary lib;
  auto               io = lib.create_io("hotmux_pairs");
  io->add_input("x", 0);
  io->set_bits("x", 2);
  io->set_unsign("x", true);
  auto                         g = io->create_graph();
  std::vector<hhds::Pin_class> controls;
  for (int i = 0; i < 2; ++i) {
    auto eq = gu::create_typed_node(*g, Ntype_op::EQ);
    gu::setup_sink_pid(eq, 0).connect_driver(g->get_input_pin("x"));
    gu::setup_sink_pid(eq, 0).connect_driver(gu::create_const(*g, *Dlop::create_integer(i)));
    auto out = eq.create_driver_pin(0);
    gu::set_ubits(out, 1);
    controls.push_back(out);
  }
  auto hot = gu::create_typed_node(*g, Ntype_op::Hotmux);
  for (int i = 0; i < 2; ++i) {
    hot.create_sink_pin(2 * i).connect_driver(controls[i]);
    hot.create_sink_pin(2 * i + 1).connect_driver(gu::create_const(*g, *Dlop::create_integer(9)));
  }
  auto out = hot.create_driver_pin(0);
  gu::set_ubits(out, 4);
  auto           nine = gu::create_const(*g, *Dlop::create_integer(9));
  formal::Prover p(g.get());
  EXPECT_EQ(p.are_exclusive(controls).verdict, Verdict::Proven);
  EXPECT_EQ(p.are_exclusive({controls[0], controls[0]}).verdict, Verdict::Refuted);
  EXPECT_EQ(p.equal(out, nine).verdict, Verdict::Refuted);  // No active control returns zero.
  hot.create_sink_pin(4).connect_driver(nine);
  formal::Prover with_default(g.get());  // A fresh encoder after changing the graph.
  EXPECT_EQ(with_default.equal(out, nine).verdict, Verdict::Proven);
}

TEST(Prove, CountedReductionsMatchBitTreesAndPreserveSourceOnExport) {
  namespace gu = livehd::graph_util;
  for (auto op : {Ntype_op::Rxor, Ntype_op::Popcount}) {
    for (int count : {0, 1, 3, 8, 65}) {
      for (bool signed_input : {false, true}) {
        hhds::GraphLibrary lib;
        auto               io = lib.create_io("counted");
        io->add_input("a", 0);
        io->set_bits("a", 7);
        io->set_unsign("a", !signed_input);
        const int width = op == Ntype_op::Rxor ? 1 : std::max(1, static_cast<int>(std::bit_width(static_cast<unsigned>(count))));
        io->add_output("out", 1);
        io->set_bits("out", width);
        io->set_unsign("out", true);
        auto graph  = io->create_graph();
        auto native = gu::create_typed_node(*graph, op);
        graph->get_input_pin("a").connect_sink(native.create_sink_pin(0));
        gu::create_const(*graph, *Dlop::create_integer(count)).connect_sink(native.create_sink_pin(1));
        auto output = native.create_driver_pin(0);
        gu::set_ubits(output, width);
        output.connect_sink(graph->get_output_pin("out"));
        auto expected = gu::create_const(*graph, *Dlop::create_integer(0));
        for (int bit = 0; bit < count; ++bit) {
          auto slice = gu::create_get_mask(*graph, graph->get_input_pin("a"), bit, bit + 1);
          auto lane  = slice.create_driver_pin(0);
          gu::set_ubits(lane, 1);
          auto combine = gu::create_typed_node(*graph, op == Ntype_op::Rxor ? Ntype_op::Xor : Ntype_op::Sum);
          expected.connect_sink(gu::setup_sink_pid(combine, 0));
          lane.connect_sink(gu::setup_sink_pid(combine, 0));
          expected = combine.create_driver_pin(0);
          gu::set_ubits(expected, width);
        }
        formal::Prover prover(graph.get());
        EXPECT_EQ(prover.equal(output, expected).verdict, Verdict::Proven) << count << ":" << signed_input;
        hhds::GraphLibrary scratch;
        auto               expanded = gu::lower_counted_reductions_copy(graph, scratch);
        EXPECT_NE(expanded.get(), graph.get());
        EXPECT_FALSE(native.is_invalid());
        EXPECT_EQ(gu::type_op_of(native), op);
        for (auto node : expanded->body().nodes()) {
          EXPECT_NE(gu::type_op_of(node), Ntype_op::Rxor);
          EXPECT_NE(gu::type_op_of(node), Ntype_op::Popcount);
        }
      }
    }
  }
}
