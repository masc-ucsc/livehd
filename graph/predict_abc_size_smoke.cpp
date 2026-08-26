//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Pins the ONE per-op constant table in graph/predict_abc_size.hpp -- the
// predicted generic-AIG score that `pass.color synth --set synth_alg=cones`
// budgets its walk in, ranks cone overlaps in and merges under
// (todo/livehd/2c-color-synthcones.html section B). The three uses share the
// unit, so a change to one constant must be made here too or they stop agreeing.
//
// Named *_smoke, not *_test: predict_abc_size is header-only (there is no
// predict_abc_size.cpp), and AGENTS.md reserves the _test suffix for a file that
// pairs with a same-named .cpp.

#include "predict_abc_size.hpp"

#include <cstdint>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"

using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::predict_abc_size;
using livehd::graph_util::set_bits;
using livehd::graph_util::shift_mux_count;
using livehd::graph_util::synthesis_ge_weight;

namespace {

hhds::GraphLibrary& lib_for(const char* dir) { return livehd::Hhds_graph_library::instance(dir); }

// A def with two `bits`-wide inputs feeding `op`'s first two sink pins and a
// `bits`-wide result. `pids` says which sink pins the two operands land on --
// {0,0} is the folded multi-driver `as` shape (And/Or/Xor/EQ/Mult), {0,1} the
// positional one (Sum a/b, LT, SRA).
struct Two_in {
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             n;
};

Two_in two_in(const char* dir, const char* name, Ntype_op op, int32_t bits, hhds::Port_id pa = 0, hhds::Port_id pb = 0,
              int32_t out_bits = 0) {
  auto& lib = lib_for(dir);
  auto  gio = lib.create_io(name);
  gio->add_input("a", bits);
  gio->add_input("b", bits);
  gio->add_output("y", out_bits == 0 ? bits : out_bits);
  auto g = gio->create_graph();
  // Stamp the width on the input PINS too: bits_of() reads the pin attr that
  // pass.bitwidth writes, not the GraphIO declaration, and an operand-keyed op
  // (LT/EQ/Ror/Mult) would otherwise see the pre-bitwidth "unknown" degradation.
  set_bits(g->get_input_pin("a"), bits);
  set_bits(g->get_input_pin("b"), bits);
  auto n = create_typed_node(*g, op);
  g->get_input_pin("a").connect_sink(n.create_sink_pin(pa));
  g->get_input_pin("b").connect_sink(n.create_sink_pin(pb));
  auto d = n.create_driver_pin(0);
  set_bits(d, out_bits == 0 ? bits : out_bits);
  d.connect_sink(g->get_output_pin("y"));
  return {g, n};
}

}  // namespace

// The bitwise band. An AND/OR is one AIG node per bit per extra operand; an XOR
// is three (it is two ANDs and an OR in an AND-inverter graph).
TEST(PredictAbcSize, BitwiseScalesWithWidthAndOperands) {
  for (int32_t bits : {8, 16, 32, 64}) {
    EXPECT_EQ(predict_abc_size(two_in("lgdb_pred_and", ("and" + std::to_string(bits)).c_str(), Ntype_op::And, bits).n),
              static_cast<uint64_t>(bits));
    EXPECT_EQ(predict_abc_size(two_in("lgdb_pred_or", ("or" + std::to_string(bits)).c_str(), Ntype_op::Or, bits).n),
              static_cast<uint64_t>(bits));
    EXPECT_EQ(predict_abc_size(two_in("lgdb_pred_xor", ("xor" + std::to_string(bits)).c_str(), Ntype_op::Xor, bits).n),
              static_cast<uint64_t>(3 * bits));
  }
}

// A ripple-carry full adder is ~8 AIG nodes per bit per extra term (two XORs
// plus the carry majority, sharing the half-XOR). This is the op synthesis GE
// under-counts ~5x, which is why cones has its own table.
TEST(PredictAbcSize, SumIsEightPerBit) {
  for (int32_t bits : {8, 16, 32, 64}) {
    EXPECT_EQ(predict_abc_size(two_in("lgdb_pred_sum", ("sum" + std::to_string(bits)).c_str(), Ntype_op::Sum, bits, 0, 1).n),
              static_cast<uint64_t>(8 * bits));
  }
}

// Comparators produce ONE bit, so their cost tracks the OPERAND width, never
// out_width. A subtractor chain is ~5/bit; an equality is an XNOR plus the AND
// tree that reduces it, ~4/bit.
TEST(PredictAbcSize, ComparesScaleWithOperandWidth) {
  for (int32_t bits : {8, 16, 32, 64}) {
    EXPECT_EQ(predict_abc_size(two_in("lgdb_pred_lt", ("lt" + std::to_string(bits)).c_str(), Ntype_op::LT, bits, 0, 1, 1).n),
              static_cast<uint64_t>(5 * bits));
    EXPECT_EQ(predict_abc_size(two_in("lgdb_pred_eq", ("eq" + std::to_string(bits)).c_str(), Ntype_op::EQ, bits, 0, 0, 1).n),
              static_cast<uint64_t>(4 * bits));
  }
}

// A reduce-OR is one OR level per operand bit and, like the comparators, is
// keyed off the operand rather than its single-bit result.
TEST(PredictAbcSize, RorIsOperandWidth) {
  auto& lib = lib_for("lgdb_pred_ror");
  auto  gio = lib.create_io("ror32");
  gio->add_input("a", 32);
  gio->add_output("y", 1);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 32);
  auto n = create_typed_node(*g, Ntype_op::Ror);
  g->get_input_pin("a").connect_sink(n.create_sink_pin(0));
  auto d = n.create_driver_pin(0);
  set_bits(d, 1);
  d.connect_sink(g->get_output_pin("y"));

  EXPECT_EQ(predict_abc_size(n), 32u);
}

// A 2:1 mux is 3 AIG nodes per bit; an N-arm Mux is an (N-1)-deep chain of them.
// A one-hot mux has no chain discount: an AND per arm plus the collapsing OR.
TEST(PredictAbcSize, MuxChainsAndHotmuxDoesNot) {
  auto& lib = lib_for("lgdb_pred_mux");
  auto  gio = lib.create_io("mux4");
  gio->add_input("s", 2);
  gio->add_input("a", 16);
  gio->add_output("y", 16);
  auto g = gio->create_graph();

  auto mux = create_typed_node(*g, Ntype_op::Mux);
  g->get_input_pin("s").connect_sink(mux.create_sink_pin(0));  // sel
  for (hhds::Port_id arm = 1; arm <= 4; ++arm) {
    g->get_input_pin("a").connect_sink(mux.create_sink_pin(arm));
  }
  auto md = mux.create_driver_pin(0);
  set_bits(md, 16);
  md.connect_sink(g->get_output_pin("y"));

  auto hot = create_typed_node(*g, Ntype_op::Hotmux);
  g->get_input_pin("s").connect_sink(hot.create_sink_pin(0));
  for (hhds::Port_id arm = 1; arm <= 4; ++arm) {
    g->get_input_pin("a").connect_sink(hot.create_sink_pin(arm));
  }
  set_bits(hot.create_driver_pin(0), 16);

  EXPECT_EQ(predict_abc_size(mux), static_cast<uint64_t>(3 * 16 * 3));  // 4 arms => 3 muxes
  EXPECT_EQ(predict_abc_size(hot), static_cast<uint64_t>(2 * 16 * 4));  // 4 arms, no chain
}

// A multiplier is keyed by the OPERAND widths (partial-product ANDs plus an
// adder row per operand bit), not by out_width^2 as ge_weight approximates: an
// 8x16 mult is 9*8*16, and squaring the 24-bit result would be 4x that.
TEST(PredictAbcSize, MultUsesOperandWidths) {
  auto& lib = lib_for("lgdb_pred_mult");
  auto  gio = lib.create_io("mult8x16");
  gio->add_input("a", 8);
  gio->add_input("b", 16);
  gio->add_output("y", 24);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 8);
  set_bits(g->get_input_pin("b"), 16);
  auto n = create_typed_node(*g, Ntype_op::Mult);
  g->get_input_pin("a").connect_sink(n.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(n.create_sink_pin(0));  // `as` folds both
  auto d = n.create_driver_pin(0);
  set_bits(d, 24);
  d.connect_sink(g->get_output_pin("y"));

  EXPECT_EQ(predict_abc_size(n), static_cast<uint64_t>(9 * 8 * 16));
}

// Wiring mints no gate: a Concat renames bit positions, a constant-mask
// Get_mask is a slice, a Sext replicates a bit, and a Not is a complement EDGE
// in an AIG. Zero here is deliberate -- a zero-score node consumes none of the
// cone walk's max_gate budget.
TEST(PredictAbcSize, WiringIsFree) {
  auto& lib = lib_for("lgdb_pred_wire");
  auto  gio = lib.create_io("wiring");
  gio->add_input("a", 32);
  gio->add_output("y", 32);
  auto g = gio->create_graph();

  auto nt = create_typed_node(*g, Ntype_op::Not);
  g->get_input_pin("a").connect_sink(nt.create_sink_pin(0));
  set_bits(nt.create_driver_pin(0), 32);

  auto sx = create_typed_node(*g, Ntype_op::Sext);
  g->get_input_pin("a").connect_sink(sx.create_sink_pin(0));
  set_bits(sx.create_driver_pin(0), 64);

  auto gm = create_typed_node(*g, Ntype_op::Get_mask);
  g->get_input_pin("a").connect_sink(gm.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(0xff)).connect_sink(gm.create_sink_pin(2));  // const mask
  set_bits(gm.create_driver_pin(0), 8);

  // A Concat is lane renaming: its sinks are INTERLEAVED (value, declared-width)
  // pairs, so a 2-lane 32+32 concat is four sink pins and still scores 0.
  auto cc = create_typed_node(*g, Ntype_op::Concat);
  g->get_input_pin("a").connect_sink(cc.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(32)).connect_sink(cc.create_sink_pin(1));
  g->get_input_pin("a").connect_sink(cc.create_sink_pin(2));
  create_const(*g, *Dlop::create_integer(32)).connect_sink(cc.create_sink_pin(3));
  set_bits(cc.create_driver_pin(0), 64);

  EXPECT_EQ(predict_abc_size(nt), 0u);
  EXPECT_EQ(predict_abc_size(sx), 0u);
  EXPECT_EQ(predict_abc_size(gm), 0u);
  EXPECT_EQ(predict_abc_size(cc), 0u);
}

// A constant shift amount is wired straight through by the mapper -- no barrel
// network, so no score. A RUNTIME amount is the barrel, charged at 3 AIG nodes
// per structural mux: exactly HALF of what synthesis_ge_weight charges, whose
// x6 carries an extra ABC-TIME safety factor that a SIZE prediction must not.
TEST(PredictAbcSize, ShiftsFollowTheStructuralMuxCount) {
  auto& lib = lib_for("lgdb_pred_shift");
  auto  gio = lib.create_io("shifts");
  gio->add_input("a", 32);
  gio->add_input("amt", 5);
  gio->add_output("y", 32);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("a"), 32);
  set_bits(g->get_input_pin("amt"), 5);

  auto k = create_typed_node(*g, Ntype_op::SRA);
  g->get_input_pin("a").connect_sink(k.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(3)).connect_sink(k.create_sink_pin(1));
  set_bits(k.create_driver_pin(0), 32);

  auto r = create_typed_node(*g, Ntype_op::SRA);
  g->get_input_pin("a").connect_sink(r.create_sink_pin(0));
  g->get_input_pin("amt").connect_sink(r.create_sink_pin(1));
  auto rd = r.create_driver_pin(0);
  set_bits(rd, 32);
  rd.connect_sink(g->get_output_pin("y"));

  EXPECT_EQ(shift_mux_count(k), 0u);
  EXPECT_EQ(predict_abc_size(k), 0u);

  const uint64_t muxes = shift_mux_count(r);
  EXPECT_GT(muxes, 0u);
  EXPECT_EQ(predict_abc_size(r), 3 * muxes);
  EXPECT_EQ(synthesis_ge_weight(r), 2 * predict_abc_size(r));
}

// The storage element is 0 AIG -- ABC maps it to a DFF cell. What costs is the
// next-state logic it folds into the latch D: an enable mux and a reset mux,
// ~3 AIG nodes per bit each. This is why a cone's register root scores nothing
// on its own and a register file must lower to Memory to be seen at all.
TEST(PredictAbcSize, FlopIsFreeUntilItHasControl) {
  auto& lib = lib_for("lgdb_pred_flop");
  auto  gio = lib.create_io("flops");
  gio->add_input("d", 16);
  gio->add_input("e", 1);
  gio->add_input("r", 1);
  gio->add_output("q", 16);
  auto g = gio->create_graph();
  set_bits(g->get_input_pin("d"), 16);

  auto plain = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("d").connect_sink(plain.create_sink_pin(3));  // din
  set_bits(plain.create_driver_pin(0), 16);

  auto with_en = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("d").connect_sink(with_en.create_sink_pin(3));
  g->get_input_pin("e").connect_sink(with_en.create_sink_pin(4));  // enable
  auto qd = with_en.create_driver_pin(0);
  set_bits(qd, 16);
  qd.connect_sink(g->get_output_pin("q"));

  auto with_both = create_typed_node(*g, Ntype_op::Flop);
  g->get_input_pin("d").connect_sink(with_both.create_sink_pin(3));
  g->get_input_pin("e").connect_sink(with_both.create_sink_pin(4));
  g->get_input_pin("r").connect_sink(with_both.create_sink_pin(7));  // reset_pin
  set_bits(with_both.create_driver_pin(0), 16);

  // A LATCH with the very same control pins scores 0: pass.abc keeps it a native
  // boundary and folds nothing into it, so charging it a flop's enable/reset
  // muxes would double-count logic already charged to the drivers.
  auto latch = create_typed_node(*g, Ntype_op::Latch);
  g->get_input_pin("d").connect_sink(latch.create_sink_pin(3));  // din
  g->get_input_pin("e").connect_sink(latch.create_sink_pin(4));  // enable
  g->get_input_pin("r").connect_sink(latch.create_sink_pin(7));  // reset_pin
  set_bits(latch.create_driver_pin(0), 16);

  // An Fflop has NO enable pin (cell.cpp), so `ctrl_pids` must not read pid 4 as
  // one -- `Ntype::get_sink_pid(Fflop, "enable")` trips a debug assert, which is
  // why those pids are spelled out rather than looked up.
  auto fflop = create_typed_node(*g, Ntype_op::Fflop);
  g->get_input_pin("d").connect_sink(fflop.create_sink_pin(3));  // din
  g->get_input_pin("r").connect_sink(fflop.create_sink_pin(7));  // reset_pin
  set_bits(fflop.create_driver_pin(0), 16);

  EXPECT_EQ(predict_abc_size(plain), 0u);
  EXPECT_EQ(predict_abc_size(with_en), static_cast<uint64_t>(3 * 16));
  EXPECT_EQ(predict_abc_size(with_both), static_cast<uint64_t>(6 * 16));
  EXPECT_EQ(predict_abc_size(latch), 0u);
  EXPECT_EQ(predict_abc_size(fflop), static_cast<uint64_t>(3 * 16));
}

// Blackboxes weigh nothing HERE: a Sub's logic is scored inside its own def's
// cones, and a Memory/Div is a macro pass.abc does not blast by default.
TEST(PredictAbcSize, BlackboxesAreZero) {
  auto& lib = lib_for("lgdb_pred_black");
  auto  gio = lib.create_io("black");
  gio->add_input("a", 32);
  gio->add_input("b", 32);
  gio->add_output("y", 32);
  auto g = gio->create_graph();

  auto dv = create_typed_node(*g, Ntype_op::Div);
  g->get_input_pin("a").connect_sink(dv.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(dv.create_sink_pin(1));
  auto dd = dv.create_driver_pin(0);
  set_bits(dd, 32);
  dd.connect_sink(g->get_output_pin("y"));

  // A Memory is a hard macro under the default memory=false, and a Sub is a
  // blackbox whose logic is weighed inside its OWN def's cones -- both must score
  // 0 here or every register-file and every instance would be double-counted.
  auto mem = create_typed_node(*g, Ntype_op::Memory);
  g->get_input_pin("a").connect_sink(mem.create_sink_pin(0));  // port 0 addr
  g->get_input_pin("b").connect_sink(mem.create_sink_pin(3));  // port 0 din
  set_bits(mem.create_driver_pin(0), 32);

  auto sub = create_typed_node(*g, Ntype_op::Sub);
  g->get_input_pin("a").connect_sink(sub.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sub.create_sink_pin(1));
  set_bits(sub.create_driver_pin(0), 32);

  EXPECT_EQ(predict_abc_size(dv), 0u);
  EXPECT_EQ(predict_abc_size(mem), 0u);
  EXPECT_EQ(predict_abc_size(sub), 0u);
}

// `a % 2^k` never reaches ABC as a remainder -- it is rewritten to an AND, so it
// is charged as one rather than as the 0 a true divider gets.
TEST(PredictAbcSize, PowerOfTwoRemIsAnAnd) {
  auto& lib = lib_for("lgdb_pred_rem");
  auto  gio = lib.create_io("rems");
  gio->add_input("a", 32);
  gio->add_input("b", 32);
  gio->add_output("y", 32);
  auto g = gio->create_graph();

  auto masked = create_typed_node(*g, Ntype_op::Rem);
  g->get_input_pin("a").connect_sink(masked.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(16)).connect_sink(masked.create_sink_pin(1));
  auto md = masked.create_driver_pin(0);
  set_bits(md, 32);
  md.connect_sink(g->get_output_pin("y"));

  auto runtime = create_typed_node(*g, Ntype_op::Rem);
  g->get_input_pin("a").connect_sink(runtime.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(runtime.create_sink_pin(1));
  set_bits(runtime.create_driver_pin(0), 32);

  EXPECT_EQ(predict_abc_size(masked), 32u);
  EXPECT_EQ(predict_abc_size(runtime), 0u);
}
