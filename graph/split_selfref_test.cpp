//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Tests the Sext DESCENT RULE in split_selfref.cpp's slice resolver.
//
// Op_Sext(a, n) has bit i = a[i] for i < n, and a[n-1] (the replicated sign) for
// i >= n -- the bit-level reading of `rsextV` (ResidualSemantics.lean:110), which
// takes a's low n bits and sign-extends them. A slice lying entirely below the
// sign position is therefore EXACTLY the same slice of the operand, so resolving
// it is a pass-through, the same shape as SRA's. Two guards keep that exact:
//
//   hi <= n          at or above the sign position the bits are a[n-1] repeated,
//                    not a[i]; rebuilding that needs a sign broadcast, not a
//                    descent.
//   hi <= bits(a)    past the operand's own width the two sides disagree on the
//                    fill -- Sext zero-fills there (`mk_bv n (bv_uint a)` is
//                    unsigned) while descending into a SIGNED operand pin would
//                    sign-replicate.
//
// Dropping either guard is a SILENT MISCOMPILE: a wrong certificate, not a
// crash. So the refusals are pinned as tightly as the descent, and each is
// pinned by a case that differs from the passing one in ONE guard input.
//
// WHY THIS IS A GRAPH-LEVEL TEST AND NOT AN RTL ONE. Reaching this rule needs an
// Op_Sext sitting on a word-level combinational cycle under a Get_mask reader.
// Op_Sext is only ever created from a signed Pick (inou/yosys/lgyosys_tolg.cpp:368),
// and every RTL spelling tried (signed widening, $signed part-select, dynamic
// >>>) is folded by cprop into SRA/SHL/Get_mask with no Sext cell left to test.
// Building the graph directly is also what lets each case isolate one guard --
// attribution a whole-flow run cannot give.

#include <memory>

#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "split_selfref.hpp"

using livehd::graph_util::create_const;
using livehd::graph_util::create_typed_node;
using livehd::graph_util::set_bits;
using livehd::graph_util::set_unsign;
using livehd::graph_util::split_packed_selfref_wires;

namespace {

struct Fixture {
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             reader;  // the on-cycle Get_mask
  hhds::Node_class             sext;
};

// A word-level combinational cycle whose only bit-field reader resolves THROUGH
// a Sext. Four comb nodes sit on the Kahn cycle (packed / sx / reader /
// feedback); the `field` pair is off it:
//
//     low      = And(x, 0xFF)             off-cycle, proven footprint [0,8)
//     field    = SHL(low, x_shift)        off-cycle, footprint [x_shift,x_shift+8)
//     packed   = Or(field, feedback)      the packed accumulator   <------+
//     sx       = Sext(packed, n)          the node under test             |
//     reader   = Get_mask(sx, [lo,hi))    the on-cycle reader             |
//     feedback = SHL(reader, fb_shift)    the other, disjoint field  -----+
//
// The caller positions `field` to cover the requested slice EXACTLY and parks
// `feedback` somewhere disjoint. That matters more than it looks: resolution
// then has a guaranteed off-cycle terminus, so if a Sext guard is deleted the
// descent SUCCEEDS and produces a (wrong) rewrite. Without that, a refusal test
// would still read zero rewrites for an unrelated downstream reason and would
// pass against the broken rule -- see the mutation results recorded at the
// bottom of this file.
//
// `pack_bits` is the Sext operand's width (the `hi <= bits(a)` guard's input)
// and `sext_amt` its sign position (the `hi <= n` guard's input), so a refusal
// case and its positive control differ in exactly one of them.
Fixture build(const char* dir, const char* name, int sext_amt, int pack_bits, int slice_lo, int slice_hi, int x_shift,
              int fb_shift) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io(name);
  gio->add_input("x", 8);
  gio->add_output("y", 32);
  auto g = gio->create_graph();

  // Created first, connected after: the shape is cyclic, so no creation order
  // makes every driver exist before its sink.
  auto packed   = create_typed_node(*g, Ntype_op::Or);
  auto sx       = create_typed_node(*g, Ntype_op::Sext);
  auto reader   = create_typed_node(*g, Ntype_op::Get_mask);
  auto feedback = create_typed_node(*g, Ntype_op::SHL);

  auto xin = g->get_input_pin("x");
  set_unsign(xin);  // an unsigned pin is what gives footprint() a bound to use

  // And-with-a-nonnegative-const is the footprint rule that yields EXACTLY
  // [0,8); the generic unsigned-pin rule would give [0,7) (it spends a bit on
  // the sign) and the slice would then not be cleanly covered.
  auto low   = create_typed_node(*g, Ntype_op::And);
  auto field = create_typed_node(*g, Ntype_op::SHL);
  xin.connect_sink(low.create_sink_pin(0));
  create_const(*g, *Dlop::get_mask_value(7, 0)).connect_sink(low.create_sink_pin(0));
  auto low_d = low.create_driver_pin(0);
  set_bits(low_d, 9);
  low_d.connect_sink(field.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(x_shift)).connect_sink(field.create_sink_pin(1));
  auto field_d = field.create_driver_pin(0);
  set_bits(field_d, 9 + x_shift);

  auto packed_d = packed.create_driver_pin(0);
  set_bits(packed_d, pack_bits);  // the `hi <= bits(a)` guard reads THIS
  auto sx_d = sx.create_driver_pin(0);
  set_bits(sx_d, 64);
  auto reader_d = reader.create_driver_pin(0);
  set_bits(reader_d, slice_hi - slice_lo + 1);
  auto feedback_d = feedback.create_driver_pin(0);
  set_bits(feedback_d, 33);

  field_d.connect_sink(packed.create_sink_pin(0));
  feedback_d.connect_sink(packed.create_sink_pin(0));

  packed_d.connect_sink(sx.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(sext_amt)).connect_sink(sx.create_sink_pin(1));

  sx_d.connect_sink(reader.create_sink_pin(0));
  // Same convention as the implementation's own mask_const.
  create_const(*g, *Dlop::get_mask_value(slice_hi - 1, slice_lo)).connect_sink(reader.create_sink_pin(2));

  reader_d.connect_sink(feedback.create_sink_pin(0));
  create_const(*g, *Dlop::create_integer(fb_shift)).connect_sink(feedback.create_sink_pin(1));

  packed_d.connect_sink(g->get_output_pin("y"));
  return {g, reader, sx};
}

// Does the reader still read the Sext? After a successful dissolve its port-0
// driver is the rebuilt off-cycle slice instead -- the topological fact the
// rewrite count stands in for.
bool reader_still_reads_sext(const Fixture& f) {
  for (auto e : f.reader.inp_edges()) {
    if (static_cast<uint32_t>(e.sink.get_port_id()) == 0 && !e.driver.is_invalid()
        && e.driver.get_master_node() == f.sext) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// DESCENT: a slice below both the sign position and the operand width is a
// pass-through, so the resolver recurses past the Sext and the read dissolves.
// ---------------------------------------------------------------------------
TEST(SplitSelfrefSext, SliceBelowSignPositionAndWidthDescends) {
  auto f = build("lgdb_sext_descend", "descend", /*sext_amt=*/16, /*pack_bits=*/16, 0, 8, /*x_shift=*/0,
                 /*fb_shift=*/8);
  ASSERT_TRUE(reader_still_reads_sext(f)) << "fixture precondition: the reader starts on the Sext";
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 1) << "slice [0,8) is below sign pos 16 and width 16: must descend";
  EXPECT_FALSE(reader_still_reads_sext(f)) << "a dissolved read must no longer be driven by the Sext";
}

// Both guards are `<=`, so a slice ending EXACTLY at the sign position and
// exactly at the operand width is still a pass-through. An accidental `<` on
// either one fails here and nowhere else.
TEST(SplitSelfrefSext, SliceEndingExactlyAtBothBoundsDescends) {
  auto f = build("lgdb_sext_exact", "exact", /*sext_amt=*/8, /*pack_bits=*/8, 0, 8, /*x_shift=*/0, /*fb_shift=*/8);
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 1) << "hi == n == bits(a) == 8 is still a pass-through";
  EXPECT_FALSE(reader_still_reads_sext(f));
}

// ---------------------------------------------------------------------------
// The high-slice POSITIVE CONTROL. The two refusal tests below are this exact
// graph with one parameter changed, so each one's zero-rewrite result is
// attributable to the single guard that parameter feeds -- and, because this
// control proves the downstream path resolves, deleting that guard makes the
// refusal test fail rather than pass for an unrelated reason.
// ---------------------------------------------------------------------------
TEST(SplitSelfrefSext, HighSliceBelowBothBoundsDescends) {
  auto f = build("lgdb_sext_high", "high", /*sext_amt=*/32, /*pack_bits=*/32, 12, 20, /*x_shift=*/12, /*fb_shift=*/0);
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 1) << "slice [12,20) is below sign pos 32 and width 32";
  EXPECT_FALSE(reader_still_reads_sext(f));
}

// REFUSAL 1 -- differs from the control ONLY in `sext_amt` (32 -> 16): the
// slice now crosses the sign position, where the bits are a[n-1] replicated.
TEST(SplitSelfrefSext, SliceCrossingSignPositionOnlyRefuses) {
  auto f = build("lgdb_sext_crossonly", "crossonly", /*sext_amt=*/16, /*pack_bits=*/32, 12, 20, /*x_shift=*/12,
                 /*fb_shift=*/0);
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 0) << "hi=20 <= bits(a)=32 but > n=16: sign-position guard alone";
  EXPECT_TRUE(reader_still_reads_sext(f)) << "a refused read must be left exactly as it was";
}

// REFUSAL 2 -- differs from the control ONLY in `pack_bits` (32 -> 16): the
// slice now reaches past the operand's own width, where Sext zero-fills but a
// descent into the operand would sign-replicate.
TEST(SplitSelfrefSext, SlicePastOperandWidthOnlyRefuses) {
  auto f = build("lgdb_sext_wide", "wide", /*sext_amt=*/32, /*pack_bits=*/16, 12, 20, /*x_shift=*/12, /*fb_shift=*/0);
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 0) << "hi=20 <= n=32 but > bits(a)=16: the fills disagree";
  EXPECT_TRUE(reader_still_reads_sext(f));
}

// ---------------------------------------------------------------------------
// MUTATION RESULTS (recorded so a later reader knows these assertions bite).
// Each mutation was applied to the single guard line in split_selfref.cpp and
// the suite re-run:
//
//   if (false)                   rule deleted  -> all three descent tests fail
//   if (hi <= aw)                `hi <= n` gone -> SliceCrossingSignPositionOnly fails
//   if (hi <= n)                 `hi <= aw` gone -> SlicePastOperandWidthOnly fails
//   if (hi < n && hi < aw)       `<=` -> `<`   -> SliceEndingExactlyAtBothBounds fails
//
// Every mutant is killed by the test named for it and by no other, which is the
// property the one-parameter-apart construction above is for.
// ---------------------------------------------------------------------------
