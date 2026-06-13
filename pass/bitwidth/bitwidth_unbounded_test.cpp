//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>

#include "bitwidth.hpp"
#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"

namespace {

[[nodiscard]] bool has_unbounded_warning(const livehd::diag::Sink& sink) {
  for (const auto& d : sink.records()) {
    if (d.code == "bitwidth-unbounded") {
      return true;
    }
  }
  return false;
}

// A Sum fed by two graph inputs that carry NO declared width: bitwidth cannot
// derive a range, so the Sum driver pin stays unbounded (bits == 0) and
// report_unbounded() must surface a `bitwidth-unbounded` warning.
TEST(BitwidthUnbounded, WarnsOnUnboundedDriverPin) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_unbounded_test");
  auto  gio = lib.create_io("bw_unbounded");
  gio->add_input("a", 1);  // no set_bits -> declared width stays 0
  gio->add_input("b", 2);
  gio->add_output("o", 3);
  auto g = gio->create_graph();

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);  // no bits
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_jsonl_path("off");
  sink.set_human_stderr(false);

  Bitwidth bw(/*hier=*/false, /*max_iterations=*/3);
  bw.do_trans(g);

  EXPECT_TRUE(has_unbounded_warning(sink)) << "expected a bitwidth-unbounded warning for the unbounded Sum pin";
  sink.clear();
}

// A fully-typed design must NOT produce a bitwidth-unbounded warning.
TEST(BitwidthUnbounded, NoWarnWhenAllBounded) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_unbounded_test");
  auto  gio = lib.create_io("bw_bounded");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum, 8);
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_jsonl_path("off");
  sink.set_human_stderr(false);

  Bitwidth bw(/*hier=*/false, /*max_iterations=*/3);
  bw.do_trans(g);

  EXPECT_FALSE(has_unbounded_warning(sink)) << "a fully-typed design must not warn about unbounded pins";
  sink.clear();
}

// The per-op bitwidth processors (process_mult / process_bit_* /
// process_comparator / process_mux) only run on a node whose driver pin has
// NO pre-stamped width — every lhd frontend (yosys-verilog, slang,
// pyrope->tolg) emits fully width-annotated graphs, so bw_pass() takes the
// "pin already has bits" early-continue (bitwidth.cpp:1369) and these
// inference paths stay uncalled from any CLI flow. Construct the width-less
// shape directly (bounded inputs, unbounded op node) and check the width
// bitwidth infers on the op's driver pin.

[[nodiscard]] static std::shared_ptr<hhds::Graph> bounded_inputs(const char* name, int abits, int bbits) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_unbounded_test");
  auto  gio = lib.create_io(name);
  gio->add_input("a", 1);
  gio->set_bits("a", abits);
  gio->add_input("b", 2);
  gio->set_bits("b", bbits);
  gio->add_output("o", 3);
  return gio->create_graph();
}

[[nodiscard]] static int32_t run_and_read_driver(const std::shared_ptr<hhds::Graph>& g, hhds::Node_class& op) {
  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_jsonl_path("off");
  sink.set_human_stderr(false);

  Bitwidth bw(/*hier=*/false, /*max_iterations=*/10);
  bw.do_trans(g);

  int32_t bits = livehd::graph_util::bits_of(op.create_driver_pin(0));
  sink.clear();
  return bits;
}

// Mult of u8 * u4: multiplication widens, so the inferred product width must
// exceed the wider 8-bit operand and stay bounded by the 8+4 magnitude sum.
TEST(BitwidthInfer, MultProductWidth) {
  auto g  = bounded_inputs("bw_mult", 8, 4);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Mult);  // no bits
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, op);
  EXPECT_GT(bits, 8) << "process_mult must widen past the 8-bit operand";
  EXPECT_LE(bits, 13) << "process_mult width must stay within the 8+4 magnitude + sign envelope";
}

// Bitwise XOR of u8 ^ u8: the result spans the wider operand's signed width.
TEST(BitwidthInfer, BitXorWidth) {
  auto g  = bounded_inputs("bw_xor", 8, 8);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Xor);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_GT(run_and_read_driver(g, op), 0) << "process_bit_xor must infer a bounded width";
}

// Bitwise AND of u8 & u4: process_bit_and infers a bounded driver width.
TEST(BitwidthInfer, BitAndWidth) {
  auto g  = bounded_inputs("bw_and", 8, 4);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::And);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_GT(run_and_read_driver(g, op), 0) << "process_bit_and must infer a bounded width";
}

// A comparator (LT) always produces a 1-bit result regardless of operands.
TEST(BitwidthInfer, ComparatorIsOneBit) {
  auto g  = bounded_inputs("bw_cmp", 8, 8);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::LT);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, op), 1) << "process_comparator must set a 1-bit result";
}

// Mux: sink 0 is the selector, sinks 1..N the data arms; the output unions
// the data arms' ranges. Two u8 data arms -> at least 8 bits.
TEST(BitwidthInfer, MuxUnionsDataArms) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_unbounded_test");
  auto  gio = lib.create_io("bw_mux");
  gio->add_input("sel", 1);
  gio->set_bits("sel", 1);
  gio->add_input("d0", 2);
  gio->set_bits("d0", 8);
  gio->add_input("d1", 3);
  gio->set_bits("d1", 8);
  gio->add_output("o", 4);
  auto g = gio->create_graph();

  auto mux = livehd::graph_util::create_typed_node(*g, Ntype_op::Mux);  // no bits
  g->get_input_pin("sel").connect_sink(mux.create_sink_pin(0));
  g->get_input_pin("d0").connect_sink(mux.create_sink_pin(1));
  g->get_input_pin("d1").connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_GE(run_and_read_driver(g, mux), 8) << "process_mux must union the data-arm widths";
}

}  // namespace
