//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "bitwidth.hpp"

#include <memory>

#include "diag.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
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

TEST(BitwidthRange, MinimalUnsignedWidths) {
  EXPECT_EQ(Bitwidth_range(0, 0).get_ubits(), 1);
  EXPECT_EQ(Bitwidth_range(0, 1).get_ubits(), 1);
  EXPECT_EQ(Bitwidth_range(0, 2).get_ubits(), 2);
  EXPECT_EQ(Bitwidth_range(0, 7).get_ubits(), 3);
  EXPECT_EQ(Bitwidth_range(0, 8).get_ubits(), 4);

  Bitwidth_range wide;
  wide.set_ubits_range(128);
  EXPECT_EQ(wide.get_ubits(), 128);
}

// A Sum fed by two graph inputs that carry NO declared width: bitwidth cannot
// derive a range, so the Sum driver pin stays unbounded (bits == 0) and
// report_unbounded() must surface a `bitwidth-unbounded` warning.
TEST(BitwidthUnbounded, WarnsOnUnboundedDriverPin) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
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

  Bitwidth bw(/*max_iterations=*/3);
  bw.do_trans(g);

  EXPECT_TRUE(has_unbounded_warning(sink)) << "expected a bitwidth-unbounded warning for the unbounded Sum pin";
  sink.clear();
}

// A fully-typed design must NOT produce a bitwidth-unbounded warning.
TEST(BitwidthUnbounded, NoWarnWhenAllBounded) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_bounded");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  gio->set_bits("o", 8);
  auto g = gio->create_graph();

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum, 8);
  livehd::graph_util::set_sbits(sum.create_driver_pin(0), 8);
  g->get_input_pin("a").connect_sink(sum.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(sum.create_sink_pin(1));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  auto& sink = livehd::diag::sink();
  sink.clear();
  sink.set_jsonl_path("off");
  sink.set_human_stderr(false);

  Bitwidth bw(/*max_iterations=*/3);
  bw.do_trans(g);

  EXPECT_FALSE(has_unbounded_warning(sink)) << "a fully-typed design must not warn about unbounded pins";
  sink.clear();
}

// Exercise the per-op value-range processors directly. Frontends may stamp
// conservative widths on these nodes, but those annotations are caches rather
// than finite-width operation contracts and must not suppress inference.

[[nodiscard]] static std::shared_ptr<hhds::Graph> bounded_inputs(const char* name, int abits, int bbits) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
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

  Bitwidth bw(/*max_iterations=*/10);
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

// Subtraction: `as` (pid 0) adds and `bs` (pid 1) subtracts. `bounded_inputs`
// leaves the decls SIGNED, so two 8-bit inputs each span [-128..127] and a-b
// spans [-255..255]: 9 signed bits.
//
// The node used to not survive at all. process_sum accumulated into a DEFAULT-CONSTRUCTED Dlop, which
// is Type::Invalid, and Dlop arithmetic returns nil for a non-numeric operand,
// so both bounds came out nil; Bitwidth_range then read the nil pair back as
// the range [0..0] and adjust_bw const-folded the whole adder to zero. Nothing
// hit it before because every front end stamps a width on its Sum nodes, which
// takes bw_pass's "pin already has bits" early-exit.
TEST(BitwidthInfer, SumSubtractSurvivesAndKeepsWidth) {
  auto g  = bounded_inputs("bw_sum", 8, 8);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);  // no bits
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, op);
  EXPECT_EQ(livehd::graph_util::type_op_of(op), Ntype_op::Sum) << "an unsized Sum must not be const-folded away";
  EXPECT_EQ(bits, 9) << "a-b over [-128..127] spans [-255..255], which is 9 signed bits";
}

// Addition widens too: `as` is a multi-driver sink, so both operands land on
// pid 0 and a+b over [-128..127] spans [-256..254] -- 9 signed bits.
TEST(BitwidthInfer, SumAddWidens) {
  auto g  = bounded_inputs("bw_sum_add", 8, 8);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, op);
  EXPECT_EQ(livehd::graph_util::type_op_of(op), Ntype_op::Sum) << "an unsized Sum must not be const-folded away";
  EXPECT_EQ(bits, 9) << "a+b of two 8-bit signed values needs 9 signed bits";
}

// A CONSTANT operand must not block inference. Constants created by
// graph_util::create_const live as driver pins on the CONST_NODE singleton,
// which no class traversal ever emits (hhds graph.hpp) -- so process_const
// never runs on them and they never land in bwmap. process_get_mask already
// falls back to hydrate_const for exactly this reason; process_sum and friends
// did not, so `x + 1` was uninferable even with x fully bounded.
TEST(BitwidthInfer, SumWithConstantOperand) {
  auto g  = bounded_inputs("bw_sum_const", 8, 8);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);  // no bits
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(1)).connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, op);
  EXPECT_EQ(livehd::graph_util::type_op_of(op), Ntype_op::Sum) << "an unsized Sum must not be const-folded away";
  EXPECT_EQ(bits, 9) << "a+1 over [-128..127] spans [-127..128]: 9 signed bits";
}

// Same for Mult, which takes the other constant-blind path.
TEST(BitwidthInfer, MultWithConstantOperand) {
  auto g  = bounded_inputs("bw_mult_const", 8, 8);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Mult);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(2)).connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_GT(run_and_read_driver(g, op), 8) << "a*2 over [-128..127] spans [-256..254]: wider than 8 bits";
}

// ...and for Mux, whose data arms take the same lookup.
TEST(BitwidthInfer, MuxWithConstantArm) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_mux_const");
  gio->add_input("sel", 1);
  gio->set_bits("sel", 1);
  gio->add_input("d0", 2);
  gio->set_bits("d0", 8);
  gio->add_output("o", 3);
  auto g = gio->create_graph();

  auto mux = livehd::graph_util::create_typed_node(*g, Ntype_op::Mux);
  g->get_input_pin("sel").connect_sink(mux.create_sink_pin(0));
  g->get_input_pin("d0").connect_sink(mux.create_sink_pin(1));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(0)).connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_GE(run_and_read_driver(g, mux), 8) << "a mux with one constant arm must still infer the data-arm width";
}

// Slang can conservatively stamp a mux with the width of an enclosing
// expression.  Internal mux semantics are unbounded and its selector does not
// contribute to the result width, so two boolean-valued constant arms must
// replace that stale annotation with their exact [0..1] range.
TEST(BitwidthInfer, PrestampedMuxIsReinferredFromDataArms) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_mux_prestamped");
  gio->add_input("sel", 1);
  gio->set_bits("sel", 1);
  gio->add_output("o", 2);
  auto g = gio->create_graph();

  auto mux = livehd::graph_util::create_typed_node(*g, Ntype_op::Mux, 66);
  g->get_input_pin("sel").connect_sink(mux.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(0)).connect_sink(mux.create_sink_pin(1));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(1)).connect_sink(mux.create_sink_pin(2));
  mux.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, mux), 1) << "the mux range [0..1] is an unsigned one-bit value";
}

// Get_mask over a SIGNED input must keep every selected bit. `a` declared 3
// signed bits spans [-4..3]; masking with 0b111 zero-extends, so the result
// spans [0..7] and needs exactly 3 unsigned bits.
TEST(BitwidthInfer, GetMaskKeepsEverySelectedBitOfSignedInput) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_getmask_signed");
  gio->add_input("a", 1);
  gio->set_bits("a", 3);
  gio->set_unsign("a", false);  // signed: [-4..3]
  gio->add_output("o", 2);
  auto g = gio->create_graph();

  auto gm = livehd::graph_util::create_typed_node(*g, Ntype_op::Get_mask);  // no bits
  livehd::graph_util::setup_sink_by_name(gm, "a").connect_driver(g->get_input_pin("a"));
  livehd::graph_util::setup_sink_by_name(gm, "mask").connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(7)));
  gm.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, gm), 3) << "a & 0b111 over [-4..3] spans [0..7]: u3";
}

// Same shape with an UNSIGNED declaration. A PORT's declared `bits` is the
// LITERAL bus width, so `input [2:0]` spans [0..7] -- masking with 0b111 is the
// identity and the result is still u3.
TEST(BitwidthInfer, GetMaskOverUnsignedInputKeepsWidth) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_getmask_unsigned");
  gio->add_input("a", 1);
  gio->set_bits("a", 3);
  gio->set_unsign("a", true);  // unsigned: [0..7]
  gio->add_output("o", 2);
  auto g = gio->create_graph();

  auto gm = livehd::graph_util::create_typed_node(*g, Ntype_op::Get_mask);
  livehd::graph_util::setup_sink_by_name(gm, "a").connect_driver(g->get_input_pin("a"));
  livehd::graph_util::setup_sink_by_name(gm, "mask").connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(7)));
  gm.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, gm), 3) << "a 3-bit unsigned port spans [0..7]: u3";
}

// The tup_in_port shape: `get_mask(a, 0b111) + 1` where `a` is 3 signed bits.
// The mask yields [0..7], so the sum spans [1..8] and needs u4.
TEST(BitwidthInfer, MaskedInputPlusOneKeepsCarry) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_mask_plus1");
  gio->add_input("a", 1);
  gio->set_bits("a", 3);
  gio->set_unsign("a", false);  // signed: [-4..3]
  gio->add_output("o", 2);
  auto g = gio->create_graph();

  auto gm = livehd::graph_util::create_typed_node(*g, Ntype_op::Get_mask);
  livehd::graph_util::setup_sink_by_name(gm, "a").connect_driver(g->get_input_pin("a"));
  livehd::graph_util::setup_sink_by_name(gm, "mask").connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(7)));

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  gm.create_driver_pin(0).connect_sink(sum.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(1)).connect_sink(sum.create_sink_pin(0));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, sum);
  EXPECT_EQ(livehd::graph_util::bits_of(gm.create_driver_pin(0)), 3) << "a & 0b111 over [-4..3] spans [0..7]";
  EXPECT_EQ(bits, 4) << "[0..7] + 1 spans [1..8], which needs u4";
}

// The full tup_in_port chain: Get_mask(a,0b111) -> Sext(.,5) -> +1. The mask
// yields [0..7], the Sext is a no-op on a non-negative source, so the sum still
// spans [1..8] and needs u4. process_sext BYPASSES and deletes a
// Sext whose source already fits `sign_max`; the sum must be sized from the
// surviving source either way.
TEST(BitwidthInfer, MaskThenSextThenPlusOneKeepsCarry) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_mask_sext_plus1");
  gio->add_input("a", 1);
  gio->set_bits("a", 3);
  gio->set_unsign("a", false);  // signed: [-4..3]
  gio->add_output("o", 2);
  auto g = gio->create_graph();

  auto gm = livehd::graph_util::create_typed_node(*g, Ntype_op::Get_mask);
  livehd::graph_util::setup_sink_by_name(gm, "a").connect_driver(g->get_input_pin("a"));
  livehd::graph_util::setup_sink_by_name(gm, "mask").connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(7)));

  auto sx = livehd::graph_util::create_typed_node(*g, Ntype_op::Sext);
  livehd::graph_util::setup_sink_by_name(sx, "a").connect_driver(gm.create_driver_pin(0));
  livehd::graph_util::setup_sink_by_name(sx, "b").connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(5)));

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  sx.create_driver_pin(0).connect_sink(sum.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(1)).connect_sink(sum.create_sink_pin(0));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, sum);
  EXPECT_EQ(bits, 4) << "[0..7] sign-extended then +1 spans [1..8]: u4";
}

// The zero-extend every slang front end emits: a signed N-bit port masked with
// an ALL-ONES (negative, -1) mask. `x` spans [-128..127] and `x & -1` maps
// x=-1 to 255, so the image is [0..255] and `+1` needs u9.
//
// The worst-case probe used the literal -1, but a negative mask makes
// get_mask_op copy bits [0, src_bits) of the SOURCE and -1 is one bit wide, so
// the single-bit rule returned -1 and the bound fell back to 2^(N-1) = 128.
// get_bits(128) == get_bits(255) == 9, so the mask pin itself looked identical
// and only the consumer exposed it: the sum came out one bit narrow and cgen
// truncated (`r(ref=256 impl=0) @ x=255`).
TEST(BitwidthInfer, AllOnesMaskOverSignedPortKeepsFullRange) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_allones_mask");
  gio->add_input("x", 1);
  gio->set_bits("x", 8);
  gio->set_unsign("x", false);  // signed: [-128..127]
  gio->add_output("o", 2);
  auto g = gio->create_graph();

  auto gm = livehd::graph_util::create_typed_node(*g, Ntype_op::Get_mask);
  livehd::graph_util::setup_sink_by_name(gm, "a").connect_driver(g->get_input_pin("x"));
  livehd::graph_util::setup_sink_by_name(gm, "mask")
      .connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(-1)));

  auto sum = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  gm.create_driver_pin(0).connect_sink(sum.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(1)).connect_sink(sum.create_sink_pin(0));
  sum.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, sum);
  EXPECT_EQ(bits, 9) << "[0..255] + 1 spans [1..256], which needs u9";
}

// A VARIABLE arithmetic right shift. `a` is a fixed constant-like range and the
// shift amount spans [1..4], so `a >> n` spans [a>>4 .. a>>1]. Shifting BOTH
// bounds by the smallest amount collapses the range to one point, and adjust_bw
// then const-folds the SRA away entirely -- which is what turned
// `packed_assign`'s array select into a constant.
TEST(BitwidthInfer, VariableSraKeepsRangeAndSurvives) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_sra_var");
  gio->add_input("n", 1);
  gio->set_bits("n", 3);
  gio->set_unsign("n", true);  // [0..7]
  gio->add_output("o", 2);
  auto g = gio->create_graph();

  // shift amount = (n & 3) + 1  -> [1..4], strictly positive
  auto nm = livehd::graph_util::create_typed_node(*g, Ntype_op::Get_mask);
  livehd::graph_util::setup_sink_by_name(nm, "a").connect_driver(g->get_input_pin("n"));
  livehd::graph_util::setup_sink_by_name(nm, "mask").connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(3)));
  auto amt = livehd::graph_util::create_typed_node(*g, Ntype_op::Sum);
  nm.create_driver_pin(0).connect_sink(amt.create_sink_pin(0));
  livehd::graph_util::create_const(*g, *Dlop::create_integer(1)).connect_sink(amt.create_sink_pin(0));

  auto sra = livehd::graph_util::create_typed_node(*g, Ntype_op::SRA);
  livehd::graph_util::setup_sink_by_name(sra, "a").connect_driver(
      livehd::graph_util::create_const(*g, *Dlop::create_integer(9345)));
  livehd::graph_util::setup_sink_by_name(sra, "b").connect_driver(amt.create_driver_pin(0));
  sra.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  int32_t bits = run_and_read_driver(g, sra);
  EXPECT_EQ(livehd::graph_util::type_op_of(sra), Ntype_op::SRA)
      << "a variable-amount SRA spans a real range and must not be const-folded";
  EXPECT_GT(bits, 0);
}

// Not's value contract is ~x == -x-1 (see process_not), so Not over a {0,1}
// boolean spans {-1,-2}: 2 SIGNED bits, and NEVER zero. This lock exists
// because tolg's not1() used to spell LOGICAL negation as a bitwise Not with a
// hand-stamped 1-bit unsigned attr -- annotation truncation that only cgen's
// declared-width clip turned back into a boolean. When bitfuzz stripped the
// stamp, the honest [-2,-1] came back, `if (not_56)` went always-true, and a
// memory wrote on every cycle regardless of its enable
// (tests/equiv/comb_array_const_index_read). The fix is in tolg (EQ-against-0,
// the landed rule); do NOT "fix" it here by making Not of a boolean stay
// 1-bit -- that would re-introduce attrs-carry-semantics and break the
// documented ~x == -x-1 contract abc and the LEC encoder both rely on.
TEST(BitwidthInfer, NotOfBooleanIsTwoBitSignedNeverZero) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_not_bool");
  gio->add_input("a", 1);
  gio->set_bits("a", 8);
  gio->add_input("b", 2);
  gio->set_bits("b", 8);
  gio->add_output("o", 3);
  auto g = gio->create_graph();

  auto cmp = livehd::graph_util::create_typed_node(*g, Ntype_op::LT);
  g->get_input_pin("a").connect_sink(cmp.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(cmp.create_sink_pin(1));

  auto nt = livehd::graph_util::create_typed_node(*g, Ntype_op::Not);
  cmp.create_driver_pin(0).connect_sink(nt.create_sink_pin(0));
  nt.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, nt), 2) << "~{0,1} spans {-2,-1}: 2 signed bits";
  EXPECT_FALSE(livehd::graph_util::is_unsign(nt.create_driver_pin(0)))
      << "Not of a boolean is always negative -- it is NOT a logical negation";
}

// Or of two 1-bit SIGNED values. Each spans [-1..0] (get_sbits 1), so the
// old process_bit_or built its lower bound from
// Dlop::get_neg_mask_value(max_bits - 1) == get_neg_mask_value(0), which
// returns +1 rather than a negative value -- producing the inverted range
// [1..0] and aborting inside Bitwidth_range::set_range. It must infer a
// bounded width instead.
TEST(BitwidthInfer, BitOrOfOneBitSignedValues) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
  auto  gio = lib.create_io("bw_or_1bit_signed");
  gio->add_input("a", 1);
  gio->set_bits("a", 1);
  gio->set_unsign("a", false);  // signed 1 bit: [-1..0]
  gio->add_input("b", 2);
  gio->set_bits("b", 1);
  gio->set_unsign("b", false);
  gio->add_output("o", 3);
  auto g = gio->create_graph();

  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Or);  // no bits
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(0));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, op), 1) << "a|b over [-1..0] stays 1 signed bit";
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

// A comparator (LT) produces the unsigned boolean {0, 1} regardless of its
// operands. It must NOT come back as the 1-bit signed {-1, 0}:
// booleans get shifted into position when a `match` is lowered to a one-hot
// selector, and a negative boolean sign-extends over the bits above it.
TEST(BitwidthInfer, ComparatorIsUnsignedBoolean) {
  auto g  = bounded_inputs("bw_cmp", 8, 8);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::LT);
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(1));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, op), 1) << "a boolean is [0..1]: u1";
  EXPECT_TRUE(livehd::graph_util::is_unsign(op.create_driver_pin(0)))
      << "a comparator result is non-negative; a signed {-1,0} boolean breaks `cmp << k`";
}

// The failure shape that made this matter: a boolean shifted into a one-hot
// position. Under the {0,1} convention `(a<b) << 2` spans [0..4]; under the
// signed {-1,0} one it spans [-4..0], whose sign extension sets every bit above
// bit 2 and destroys the one-hotness of the selector it is OR-ed into.
TEST(BitwidthInfer, ShiftedComparatorStaysNonNegative) {
  auto g   = bounded_inputs("bw_cmp_shl", 8, 8);
  auto cmp = livehd::graph_util::create_typed_node(*g, Ntype_op::LT);
  g->get_input_pin("a").connect_sink(cmp.create_sink_pin(0));
  g->get_input_pin("b").connect_sink(cmp.create_sink_pin(1));

  auto shl = livehd::graph_util::create_typed_node(*g, Ntype_op::SHL);
  livehd::graph_util::setup_sink_by_name(shl, "a").connect_driver(cmp.create_driver_pin(0));
  livehd::graph_util::setup_sink_by_name(shl, "b").connect_driver(livehd::graph_util::create_const(*g, *Dlop::create_integer(2)));
  shl.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  (void)run_and_read_driver(g, shl);
  EXPECT_TRUE(livehd::graph_util::is_unsign(shl.create_driver_pin(0))) << "a shifted boolean must stay non-negative";
}

// Mux: sink 0 is the selector, sinks 1..N the data arms; the output unions
// the data arms' ranges. Two u8 data arms -> at least 8 bits.
TEST(BitwidthInfer, MuxUnionsDataArms) {
  auto& lib = livehd::Hhds_graph_library::instance("lgdb_bitwidth_test");
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

// Concat: sinks are interleaved (value, declared-width) pairs, MSB-first. The
// output range is a function of the DECLARED widths alone -- here 3+5 -- and
// not of the lane values, which are two SIGNED ports spanning [-4..3] and
// [-16..15]. Each lane occupies its own window, so the result is u8.
//
// The lane drivers are sized to FIT their windows (3 and 5) because a driver
// wider than its window is now an internal compile error, not a truncation --
// see graph_util::concat_lane_violation and process_concat. A driver may still
// be NARROWER than its window; it sign-extends into it.
//
// The two ways to get the width wrong both look plausible and both miscompile:
// folding the width CONSTANTS in as data operands (they are 3 and 5, so the
// result would look 4 bits wide), or re-deriving a lane's width from its
// driver's bits_of -- which is why the drivers here are deliberately NOT the
// same width as each other.
TEST(BitwidthInfer, ConcatWidthIsSumOfDeclaredLanes) {
  auto g  = bounded_inputs("bw_concat", 3, 5);
  auto op = livehd::graph_util::create_typed_node(*g, Ntype_op::Concat);  // no bits
  g->get_input_pin("a").connect_sink(op.create_sink_pin(0));              // lane 0 value (MSB lane)
  livehd::graph_util::create_const(*g, *Dlop::create_integer(3)).connect_sink(op.create_sink_pin(1));
  g->get_input_pin("b").connect_sink(op.create_sink_pin(2));  // lane 1 value (LSB lane)
  livehd::graph_util::create_const(*g, *Dlop::create_integer(5)).connect_sink(op.create_sink_pin(3));
  op.create_driver_pin(0).connect_sink(g->get_output_pin("o"));

  EXPECT_EQ(run_and_read_driver(g, op), 8) << "a concat is exactly the sum of its declared lane widths";
  EXPECT_TRUE(livehd::graph_util::is_unsign(op.create_driver_pin(0)))
      << "every lane is masked into its own window, so a concat result is never negative";
}

}  // namespace
