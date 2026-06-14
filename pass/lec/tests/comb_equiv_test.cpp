// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Milestone-1 acceptance: the L0 encoder + L1 query prove tiny combinational
// modules equal / different, with verdicts that match the known ground truth
// (and, in the lec.cross path, lgcheck). Graphs are built programmatically so
// the test needs no reader.

#include <memory>
#include <string>

#include "cell.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "query.hpp"

using namespace livehd;
using livehd::lec::Verdict;

namespace {

// Build `out = a <op> b` over two `bits`-bit unsigned inputs. For reduce-style
// ops (And/Or/Xor) and Sum-add, both operands land on sink "a"; `swap` flips
// the edge order (to exercise commutativity in the equal case).
std::shared_ptr<hhds::Graph> build_binop(hhds::GraphLibrary& lib, const std::string& mod, Ntype_op op, int bits,
                                         bool swap = false) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits + 1);  // unsigned: bits attr = magnitude+1
  gio->set_unsign("a", true);
  gio->add_input("b", 1);
  gio->set_bits("b", bits + 1);
  gio->set_unsign("b", true);
  gio->add_output("out", 2);
  gio->set_bits("out", bits + 1);
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto node = graph_util::create_typed_node(*g, op);

  auto sink_a = graph_util::setup_sink_by_name(node, "a");
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
  graph_util::set_bits(dpin, bits + 1);
  graph_util::set_unsign(dpin);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

// Build `out = a + const` (exercises the Nconst / constant path).
std::shared_ptr<hhds::Graph> build_add_const(hhds::GraphLibrary& lib, const std::string& mod, int64_t k, int bits) {
  auto gio = lib.create_io(mod);
  gio->add_input("a", 0);
  gio->set_bits("a", bits + 1);
  gio->set_unsign("a", true);
  gio->add_output("out", 1);
  gio->set_bits("out", bits + 1);
  gio->set_unsign("out", true);

  auto g    = gio->create_graph();
  auto node = graph_util::create_typed_node(*g, Ntype_op::Sum);

  auto sink_a = graph_util::setup_sink_by_name(node, "a");
  g->get_input_pin("a").connect_sink(sink_a);
  auto cval = Dlop::create_integer(k);
  auto cpin = graph_util::create_const(*g, *cval);
  cpin.connect_sink(sink_a);

  auto dpin = node.create_driver_pin(0);
  graph_util::set_bits(dpin, bits + 1);
  graph_util::set_unsign(dpin);
  dpin.connect_sink(g->get_output_pin("out"));
  return g;
}

}  // namespace

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

TEST(CombEquiv, EngineBmcRefutes) {
  hhds::GraphLibrary lib;
  auto               ref  = build_binop(lib, "ref", Ntype_op::And, 4);
  auto               impl = build_binop(lib, "impl", Ntype_op::Or, 4);

  lec::Lec_options o;
  o.engine = "bmc";
  auto r   = lec::prove_equal(ref.get(), impl.get(), o);
  EXPECT_EQ(r.verdict, Verdict::Refuted) << r.detail;
}
