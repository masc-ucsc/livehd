// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Milestone-1 acceptance: the L0 encoder + L1 query prove tiny combinational
// modules equal / different, with verdicts that match the known ground truth
// (and, in the lec.cross path, lgcheck). Graphs are built programmatically so
// the test needs no reader.

#include "query.hpp"

#include <memory>
#include <string>

#include "cell.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "occurrence_materialize.hpp"

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

  auto sink_a = graph_util::setup_sink_by_name(node, "as");
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

std::shared_ptr<hhds::Graph> build_active_loop(hhds::GraphLibrary& lib) {
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
  body->get_input_pin("carry").connect_sink(sum.create_sink_pin(0));
  graph_util::create_const(*body, *Dlop::create_integer(1)).connect_sink(sum.create_sink_pin(0));

  // The lifted body itself preserves the carry while inactive; the occurrence
  // realization additionally inserts the inter-ordinal bypass required by the
  // compact call-binding contract.
  auto mux = graph_util::create_typed_node(*body, Ntype_op::Mux, 9);
  body->get_input_pin("active").connect_sink(mux.create_sink_pin(0));
  body->get_input_pin("carry").connect_sink(mux.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(mux.create_sink_pin(2));
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
                       .count              = 3,
                       .activation_input   = 1,
                       .next_active_output = 3,
                   });
  top->get_input_pin("seed").connect_sink(call.create_sink_pin(0));
  top->get_input_pin("enable").connect_sink(call.create_sink_pin(1));
  call.create_driver_pin(2).connect_sink(call.create_sink_pin(0));
  call.create_driver_pin(2).connect_sink(top->get_output_pin("result"));
  call.subnode_group().validate();
  return top;
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
