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
#include <unistd.h>

#include <cstdint>
#include <map>
#include <set>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

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
//
// A SUCCESSFUL dissolve rewires TWO readers here, not one. The Get_mask reader
// resolves THROUGH the Sext (the descent rule these cases exist for), and the
// Sext is ITSELF a signed field read of the packed Or, which the reader form
// added for intpipe_csr_file now recognises. Both are resolved against the
// unmutated graph, so the descent is still exercised: delete the descent rule
// and the count drops to 1, which these assertions still catch. The refusal
// cases stay at 0 -- neither path may fire when a guard says no.
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
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 2) << "slice [0,8) is below sign pos 16 and width 16: must descend";
  EXPECT_FALSE(reader_still_reads_sext(f)) << "a dissolved read must no longer be driven by the Sext";
}

// Both guards are `<=`, so a slice ending EXACTLY at the sign position and
// exactly at the operand width is still a pass-through. An accidental `<` on
// either one fails here and nowhere else.
TEST(SplitSelfrefSext, SliceEndingExactlyAtBothBoundsDescends) {
  auto f = build("lgdb_sext_exact", "exact", /*sext_amt=*/8, /*pack_bits=*/8, 0, 8, /*x_shift=*/0, /*fb_shift=*/8);
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 2) << "hi == n == bits(a) == 8 is still a pass-through";
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
  EXPECT_EQ(split_packed_selfref_wires(f.g.get()), 2) << "slice [12,20) is below sign pos 32 and width 32";
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

// ---------------------------------------------------------------------------
// THE SIGNED FIELD READER: Sext(SRA(word, const k), const n).
//
// Two packed words exchanging signed fields -- the shape seven word-level
// cycles in core-et's intpipe_csr_file have, and which the pass previously
// walked past without attempting (not a refusal: a shape it did not
// recognise). Each word is an Or of off-cycle fields plus the other word's
// extracted field, placed by SHL and zero-extended by Get_mask:
//
//     fromB = SHL(Get_mask(Sext(SRA(wordB, kB), n), -1), pA)
//     wordA = Or(fromB, offA_mid, offA_hi)                    <--------+
//     fromA = SHL(Get_mask(Sext(SRA(wordA, kA), n), -1), pB)           |
//     wordB = Or(offB_lo, fromA)                              ---------+
//
// Whether this is a REAL loop is decided bit by bit: A takes B[kB, kB+n) and
// puts it at [pA, pA+n); B takes A[kA, kA+n) and puts it at [pB, pB+n). Make
// the ranges miss each other and no bit depends on itself; make them meet and
// one does. Nothing in the pass is told which case it is -- `resolve`'s
// on-stack set finds out, and that is the point of having both here.
namespace {

struct PairOpts {
  bool nonconst_sra = false;  // dynamic field position
  bool nonconst_shl = false;  // dynamic placement
  bool bad_mask     = false;  // the zext is a partial mask, not all-ones
  bool nonconst_n   = false;  // dynamic sign position on the Sext itself
  bool huge_n       = false;  // a sign position far above any int
  bool huge_k       = false;  // a field position far above any int
};

struct PairFixture {
  std::shared_ptr<hhds::Graph> g;
  hhds::Node_class             rdB;  // the Sext reading wordB -- the node under test
  // The two packed words, by id. Recovering them by "sort every Or's debug id"
  // would silently pick a different node the moment the fixture gains one.
  uint64_t                     id_a = 0;
  uint64_t                     id_b = 0;
};

// `wbits` is each packed word's width; kB+n past it is the out-of-range case.
PairFixture build_pair(const char* dir, const char* name, int n, int kA, int pA, int kB, int pB, int wbits,
                       PairOpts o = {}) {
  auto& lib = livehd::Hhds_graph_library::instance(dir);
  auto  gio = lib.create_io(name);
  gio->add_input("x", 8);
  gio->add_output("y", 64);
  auto g = gio->create_graph();

  auto xin = g->get_input_pin("x");
  set_unsign(xin);

  // An off-cycle field with the EXACT footprint [at, at+8): And-with-a-
  // nonnegative-const then SHL, the same idiom the Sext fixture above uses.
  auto off_field = [&](int at) {
    auto a = create_typed_node(*g, Ntype_op::And);
    xin.connect_sink(a.create_sink_pin(0));
    create_const(*g, *Dlop::get_mask_value(7, 0)).connect_sink(a.create_sink_pin(0));
    auto ad = a.create_driver_pin(0);
    set_bits(ad, 9);
    auto sh = create_typed_node(*g, Ntype_op::SHL);
    ad.connect_sink(sh.create_sink_pin(0));
    create_const(*g, *Dlop::create_integer(at)).connect_sink(sh.create_sink_pin(1));
    auto sd = sh.create_driver_pin(0);
    set_bits(sd, 9 + at);
    return sd;
  };

  auto wordA = create_typed_node(*g, Ntype_op::Or);
  auto wordB = create_typed_node(*g, Ntype_op::Or);
  auto wA    = wordA.create_driver_pin(0);
  auto wB    = wordB.create_driver_pin(0);
  set_bits(wA, wbits);
  set_bits(wB, wbits);

  // src -> Sext(SRA(src, k), n) -> Get_mask(.., -1) -> SHL(.., place)
  auto extract = [&](const hhds::Pin_class& src, int k, int place, hhds::Node_class* out_sext) {
    auto sra = create_typed_node(*g, Ntype_op::SRA);
    src.connect_sink(sra.create_sink_pin(0));
    if (o.nonconst_sra) {
      xin.connect_sink(sra.create_sink_pin(1));  // dynamic: no constant field position
    } else if (o.huge_k) {
      // 2^40: positive, far above the word, and -- the point -- it WRAPS to 0
      // on a narrowing cast, so a range test applied after the cast sees an
      // in-range field that is not the one the RTL names.
      create_const(*g, *Dlop::create_integer(int64_t{1} << 40)).connect_sink(sra.create_sink_pin(1));
    } else {
      create_const(*g, *Dlop::create_integer(k)).connect_sink(sra.create_sink_pin(1));
    }
    auto sd = sra.create_driver_pin(0);
    set_bits(sd, n);

    auto sx = create_typed_node(*g, Ntype_op::Sext);
    sd.connect_sink(sx.create_sink_pin(0));
    if (o.nonconst_n) {
      xin.connect_sink(sx.create_sink_pin(1));  // dynamic: the sign position is a runtime value
    } else if (o.huge_n) {
      create_const(*g, *Dlop::create_integer(int64_t{1} << 40)).connect_sink(sx.create_sink_pin(1));
    } else {
      create_const(*g, *Dlop::create_integer(n)).connect_sink(sx.create_sink_pin(1));
    }
    auto xd = sx.create_driver_pin(0);
    set_bits(xd, n);
    if (out_sext != nullptr) {
      *out_sext = sx;
    }

    auto gm = create_typed_node(*g, Ntype_op::Get_mask);
    xd.connect_sink(gm.create_sink_pin(0));
    if (o.bad_mask) {
      create_const(*g, *Dlop::get_mask_value(n - 2, 0)).connect_sink(gm.create_sink_pin(2));
    } else {
      create_const(*g, *Dlop::create_integer(-1)).connect_sink(gm.create_sink_pin(2));
    }
    auto gd = gm.create_driver_pin(0);
    set_bits(gd, n + 1);

    auto sh = create_typed_node(*g, Ntype_op::SHL);
    gd.connect_sink(sh.create_sink_pin(0));
    if (o.nonconst_shl) {
      xin.connect_sink(sh.create_sink_pin(1));  // dynamic placement
    } else {
      create_const(*g, *Dlop::create_integer(place)).connect_sink(sh.create_sink_pin(1));
    }
    auto hd = sh.create_driver_pin(0);
    set_bits(hd, n + 1 + place);
    return hd;
  };

  hhds::Node_class rdB;
  auto             fromB = extract(wB, kB, pA, &rdB);   // A takes B's field
  auto             fromA = extract(wA, kA, pB, nullptr);  // B takes A's field

  // The off-cycle fields must not COLLIDE with the placements. Overlapping Or
  // footprints make the descent refuse on disjointness grounds, which would
  // mask whatever the case is actually about -- the first version parked them
  // at fixed 16/24 and 0, so every fixture with pA=16 refused for the wrong
  // reason and the self-dependency guard was never exercised.
  auto park = [&](int placed_lo, int placed_hi, int nth) {
    for (int at = 0; at + 8 <= 64; at += 8) {
      if (at + 8 <= placed_lo || at >= placed_hi) {
        if (nth-- == 0) {
          return at;
        }
      }
    }
    return 56;
  };
  const int a_lo = pA, a_hi = pA + n + 1;
  const int b_lo = pB, b_hi = pB + n + 1;

  fromB.connect_sink(wordA.create_sink_pin(0));
  off_field(park(a_lo, a_hi, 0)).connect_sink(wordA.create_sink_pin(0));
  off_field(park(a_lo, a_hi, 1)).connect_sink(wordA.create_sink_pin(0));

  off_field(park(b_lo, b_hi, 0)).connect_sink(wordB.create_sink_pin(0));
  fromA.connect_sink(wordB.create_sink_pin(0));

  wA.connect_sink(g->get_output_pin("y"));
  return {g, rdB, static_cast<uint64_t>(wordA.get_debug_nid()), static_cast<uint64_t>(wordB.get_debug_nid())};
}

// Does the reader still read through the SRA? After a dissolve its port-0
// driver is the rebuilt off-cycle slice instead.
bool sext_still_reads_sra(const PairFixture& f) {
  for (auto e : f.rdB.inp_edges()) {
    if (static_cast<uint32_t>(e.sink.get_port_id()) == 0 && !e.driver.is_invalid()
        && livehd::graph_util::type_op_of(e.driver.get_master_node()) == Ntype_op::SRA) {
      return true;
    }
  }
  return false;
}

// Assert a refusal properly: ARMED before (the reader really has the shape),
// the pass actually RUN, and the reader UNCHANGED after. The first version of
// these cases asserted only the last part -- and did not run the pass -- so they
// passed against any fixture at all.
// `want_diag`, when given, is a substring the pass's own trace must contain.
//
// WHY THE DIAGNOSTIC MATTERS HERE. A self-dependent field is caught by THREE
// layered nets -- the on-stack set at depth 10, the depth cap at 65, and the
// node-creation budget -- so disabling any one of them still refuses, and a
// mutant that removes the on-stack check cannot be killed by "it refused".
// Measured: with the on-stack set disabled the trace reads
// `refuse depth=65` instead of `on-stack self-dependency [16,24)`. Asserting
// WHICH net fired is what distinguishes the guard from its backstop.
void expect_refused(PairFixture& f, const char* why, const char* want_diag = nullptr) {
  ASSERT_TRUE(sext_still_reads_sra(f)) << "fixture is not armed: the reader does not read through an SRA";
  std::string trace;
  if (want_diag != nullptr) {
    // Capture the pass's stderr trace for this one call.
    //
    // NOT /tmp. This project keeps run artifacts under a writable project-local
    // path; `std::filesystem::temp_directory_path()` is root /tmp, which is
    // forbidden here. Under bazel TEST_TMPDIR is the sandbox's own directory,
    // and outside it the test's working directory is the right place.
    const char* td   = std::getenv("TEST_TMPDIR");
    std::string base = (td != nullptr && *td != '\0') ? std::string{td} : std::string{"."};
    auto        path = base + "/split_selfref_trace_" + std::to_string(::getpid()) + ".txt";

    // Restore whatever the variable was, rather than unsetting it: a developer
    // running the suite with the trace on should get it back afterwards.
    const char* prev     = std::getenv("LIVEHD_SIM_SPLIT_DEBUG");
    std::string prev_val = prev != nullptr ? std::string{prev} : std::string{};
    const bool  had_prev = prev != nullptr;
    setenv("LIVEHD_SIM_SPLIT_DEBUG", "1", 1);

    fflush(stderr);
    int   saved = dup(fileno(stderr));
    FILE* tf    = freopen(path.c_str(), "w", stderr);
    const int n = split_packed_selfref_wires(f.g.get());
    fflush(stderr);
    if (tf != nullptr) {
      dup2(saved, fileno(stderr));
    }
    close(saved);
    if (had_prev) {
      setenv("LIVEHD_SIM_SPLIT_DEBUG", prev_val.c_str(), 1);
    } else {
      unsetenv("LIVEHD_SIM_SPLIT_DEBUG");
    }
    {
      std::ifstream in(path);
      trace.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);  // leave nothing behind
    EXPECT_TRUE(sext_still_reads_sra(f)) << why << " (pass rewired " << n << " read(s))";
    EXPECT_NE(trace.find(want_diag), std::string::npos)
        << why << ": expected the trace to say `" << want_diag << "`, so that a refusal by a "
        << "DIFFERENT guard cannot pass this case. Trace:\n"
        << trace.substr(0, 1200);
    return;
  }
  const int n = split_packed_selfref_wires(f.g.get());
  EXPECT_TRUE(sext_still_reads_sra(f)) << why << " (pass rewired " << n << " read(s))";
}

// ...and a positive the same way round.
void expect_dissolved(PairFixture& f, const char* why) {
  ASSERT_TRUE(sext_still_reads_sra(f)) << "fixture is not armed: the reader does not read through an SRA";
  EXPECT_GT(split_packed_selfref_wires(f.g.get()), 0) << why;
  EXPECT_FALSE(sext_still_reads_sra(f)) << why << ": the reader must now read the rebuilt slice";
}

}  // namespace

// A takes B[0,8) to [0,8); B takes A[16,24) to [16,24). The two never meet, so
// no bit depends on itself and both reads dissolve. This is the real shape.
TEST(SplitSelfrefSignedReader, DisjointFieldsDissolve) {
  auto f = build_pair("lgdb_sr_disjoint", "disjoint", /*n=*/8, /*kA=*/16, /*pA=*/0, /*kB=*/0, /*pB=*/16, /*wbits=*/48);
  expect_dissolved(f, "disjoint signed fields must dissolve");
}

// EXACT BOUNDARY: the field ends precisely at the word's width. One bit more is
// the out-of-range case below, so the two differ in exactly that.
TEST(SplitSelfrefSignedReader, FieldEndingExactlyAtTheWordWidthDissolves) {
  auto f = build_pair("lgdb_sr_bound", "bound", /*n=*/8, /*kA=*/16, /*pA=*/0, /*kB=*/40, /*pB=*/16, /*wbits=*/48);
  expect_dissolved(f, "k+n == word width is in range");
}

// ...and one bit past it must REFUSE: above the word an arithmetic shift yields
// the replicated sign, which is not the zero the Or/footprint descent would
// otherwise answer.
TEST(SplitSelfrefSignedReader, FieldPastTheWordWidthRefuses) {
  auto f = build_pair("lgdb_sr_oor", "oor", /*n=*/8, /*kA=*/16, /*pA=*/0, /*kB=*/41, /*pB=*/16, /*wbits=*/48);
  expect_refused(f, "k+n past the word width must not be rewritten");
}

// GENUINE OVERLAP: both words read and write the SAME bits, so bit 16 depends
// on bit 16. The pass must refuse, and this is what makes the positives mean
// something.
TEST(SplitSelfrefSignedReader, SameFieldBothWaysRefuses) {
  auto f = build_pair("lgdb_sr_same", "same", /*n=*/8, /*kA=*/16, /*pA=*/16, /*kB=*/16, /*pB=*/16, /*wbits=*/48);
  expect_refused(f, "a field that depends on itself must not be rewritten", "on-stack self-dependency");
}

// PARTIAL overlap: A takes B[12,20) and puts it at [12,20); B takes A[16,24)
// and puts it at [16,24). Bits 16..19 close on themselves (A16 <- B16 <- A16)
// while 12..15 terminate in B's gap, so SOME of the field is self-dependent and
// the read must still be refused.
//
// The first version of this case placed A's copy at [16,24) instead, and the
// pass dissolved it -- correctly: A bit 16 <- B bit 12, and bit 12 is in
// neither of B's operands, so every chain terminated at zero. It was a shifted
// read, not an overlap. The assertion was wrong, not the rule.
TEST(SplitSelfrefSignedReader, PartiallyOverlappingFieldsRefuse) {
  auto f = build_pair("lgdb_sr_part", "part", /*n=*/8, /*kA=*/16, /*pA=*/12, /*kB=*/12, /*pB=*/16, /*wbits=*/48);
  expect_refused(f, "a partially self-dependent field must not be rewritten", "on-stack self-dependency");
}

// A DYNAMIC field position is not a field at all.
TEST(SplitSelfrefSignedReader, NonConstantSraAmountRefuses) {
  PairOpts o;
  o.nonconst_sra = true;
  auto f = build_pair("lgdb_sr_dynsra", "dynsra", 8, 16, 0, 0, 16, 48, o);
  expect_refused(f, "a dynamic shift gives no constant field position");
}

// ...and a dynamic PLACEMENT leaves the writer's footprint unbounded, so the
// disjointness the Or descent relies on cannot be established.
TEST(SplitSelfrefSignedReader, NonConstantShlAmountRefuses) {
  PairOpts o;
  o.nonconst_shl = true;
  auto f = build_pair("lgdb_sr_dynshl", "dynshl", 8, 16, 0, 0, 16, 48, o);
  expect_refused(f, "a dynamic placement leaves the pack unbounded");
}

// A DYNAMIC SIGN POSITION names no fixed field: which bits are value and which
// are replicated sign is a runtime answer, so there is nothing to slice.
TEST(SplitSelfrefSignedReader, NonConstantSignPositionRefuses) {
  PairOpts o;
  o.nonconst_n = true;
  auto f = build_pair("lgdb_sr_dynn", "dynn", 8, 16, 0, 0, 16, 48, o);
  expect_refused(f, "a dynamic sign position names no fixed field");
}

// A SIGN POSITION ABOVE THE WORD, spelled large enough to wrap a narrowing
// cast. 2^40 is positive and far out of range; truncated to `int` it becomes 0,
// which a range test applied after the cast would accept.
TEST(SplitSelfrefSignedReader, HugeSignPositionRefuses) {
  PairOpts o;
  o.huge_n = true;
  auto f = build_pair("lgdb_sr_hugen", "hugen", 8, 16, 0, 0, 16, 48, o);
  expect_refused(f, "a sign position above the word must be refused, not truncated");
}

// ...and the same for the field position, where `k + n` could also overflow
// before any comparison with the word width.
TEST(SplitSelfrefSignedReader, HugeFieldPositionRefuses) {
  PairOpts o;
  o.huge_k = true;
  auto f = build_pair("lgdb_sr_hugek", "hugek", 8, 16, 0, 0, 16, 48, o);
  expect_refused(f, "a field position above the word must be refused, not truncated");
}

// A Get_mask that is not all-ones drops bits -- but it sits on the CONSUMER of
// the read, downstream of the Sext, so it cannot make the read itself unsound:
// replacing `Sext(SRA(w,k),n)` with `Sext(slice,n)` is value-preserving
// whatever is done with the result afterwards. So this dissolves, and saying so
// is more useful than inventing a refusal the rule does not owe.
//
// The mask guard that DOES matter is on the descent -- reading THROUGH a
// non-all-ones Get_mask to reach the packed word -- and that is `resolve`'s
// own re-basing rule, pinned by the Get_mask reader form and by
// scripts/packed_field_intervals.py's F3 mutant.
TEST(SplitSelfrefSignedReader, PartialMaskOnTheConsumerStillDissolves) {
  PairOpts o;
  o.bad_mask = true;
  auto f = build_pair("lgdb_sr_mask", "mask", 8, 16, 0, 0, 16, 48, o);
  expect_dissolved(f, "the consumer's mask does not affect the read");
}

// ---------------------------------------------------------------------------
// AN INDEPENDENT SEMANTIC GATE FOR THE REWRITE.
//
// Everything above checks STRUCTURE: which reads were rewired, which refused.
// Structure is not meaning. This evaluates the graph BEFORE and AFTER
// `split_packed_selfref_wires` over every input and requires the same answer,
// and checks both against a closed-form golden derived from the fixture's
// PARAMETERS rather than from the graph.
//
// WHAT MAKES IT INDEPENDENT. `eval_node` walks the graph op by op from the
// semantics in graph/cell.hpp. It never calls `resolve`, `footprint` or
// anything else the rewrite uses, and the golden never looks at a graph at all.
// The before-graph is CYCLIC, so it is evaluated the way hardware settles it --
// iterate to a fixed point and fail if it does not converge. That is also what
// makes the overlapping case meaningful: a genuine bit loop does NOT converge,
// and the test says so.
//
// WHAT IT DOES NOT PROVE. This is one small instance of the shape, not
// intpipe_csr_file. It says the rewrite preserves meaning on a graph with these
// fields and these widths; it says nothing about the 35k-node module, whose own
// RTL evidence is still open (pass/lean/CYCLE_PROVENANCE.txt part 5).
namespace {

// Fixed-width BITVECTOR evaluation. LGraph operators are fixed-width; host
// `int64_t` is not. Signed shifts of negative values are undefined, a host `>>`
// uses bit 63 rather than the operand's declared sign bit, and nothing is
// truncated to the pin that carries it. So every value here is a `uint64_t`
// masked to its node's declared width, and sign extension is explicit.
constexpr uint64_t kBad = ~uint64_t{0};  // "not evaluable" sentinel

uint64_t wmask(int bits) {
  return (bits <= 0 || bits >= 64) ? ~uint64_t{0} : ((uint64_t{1} << bits) - 1);
}

uint64_t trunc_to(uint64_t v, int bits) { return v & wmask(bits); }

// Interpret `v`'s low `bits` as two's complement and sign-extend to 64.
uint64_t sext_from(uint64_t v, int bits) {
  if (bits <= 0 || bits >= 64) {
    return v;
  }
  const uint64_t m = uint64_t{1} << (bits - 1);
  return ((v & wmask(bits)) ^ m) - m;
}

// A node's declared output width, from the pin that carries it.
int node_bits(const hhds::Node_class& n) {
  for (auto e : n.out_edges()) {
    const int b = livehd::graph_util::bits_of(e.driver);
    if (b > 0) {
      return b;
    }
  }
  return 64;
}

uint64_t op_value(const hhds::Node_class& n, uint64_t x, const std::map<uint64_t, uint64_t>& prev) {
  namespace gu  = livehd::graph_util;
  const int  nw = node_bits(n);
  auto       rd = [&](const hhds::Pin_class& p, int* out_bits) -> uint64_t {
    if (out_bits != nullptr) {
      *out_bits = p.is_invalid() ? 0 : gu::bits_of(p);
    }
    if (p.is_invalid()) {
      return kBad;
    }
    if (gu::is_const_pin(p)) {
      auto c = gu::hydrate_const(p);
      return c.is_just_i64() ? static_cast<uint64_t>(c.to_just_i64()) : kBad;
    }
    if (gu::is_graph_input_pin(p)) {
      return x;
    }
    auto it = prev.find(static_cast<uint64_t>(p.get_master_node().get_debug_nid()));
    return it == prev.end() ? uint64_t{0} : it->second;
  };
  auto in = [&](uint32_t pid) {
    for (auto e : n.inp_edges()) {
      if (static_cast<uint32_t>(e.sink.get_port_id()) == pid) {
        return e.driver;
      }
    }
    return hhds::Pin_class{};
  };
  auto shift_amount = [&](const hhds::Pin_class& p, int64_t* out) {
    if (p.is_invalid() || !gu::is_const_pin(p)) {
      return false;
    }
    auto c = gu::hydrate_const(p);
    if (!c.is_just_i64()) {
      return false;
    }
    *out = c.to_just_i64();
    return *out >= 0 && *out < 64;
  };

  switch (gu::type_op_of(n)) {
    case Ntype_op::Or:
    case Ntype_op::And: {
      const bool is_or = gu::type_op_of(n) == Ntype_op::Or;
      bool       first = true;
      uint64_t   acc   = 0;
      for (auto e : n.inp_edges()) {
        if (static_cast<uint32_t>(e.sink.get_port_id()) != 0) {
          continue;
        }
        int            ob = 0;
        const uint64_t v  = rd(e.driver, &ob);
        if (v == kBad) {
          return kBad;
        }
        const uint64_t vt = trunc_to(v, ob > 0 ? ob : 64);
        acc               = first ? vt : (is_or ? (acc | vt) : (acc & vt));
        first             = false;
      }
      return first ? uint64_t{0} : trunc_to(acc, nw);
    }
    case Ntype_op::SHL: {
      int            ob = 0;
      const uint64_t a0 = rd(in(0), &ob);
      int64_t        sh = 0;
      if (a0 == kBad || !shift_amount(in(1), &sh)) {
        return kBad;
      }
      return trunc_to(trunc_to(a0, ob > 0 ? ob : 64) << sh, nw);
    }
    case Ntype_op::SRA: {
      int            ob = 0;
      const uint64_t a0 = rd(in(0), &ob);
      int64_t        sh = 0;
      if (a0 == kBad || !shift_amount(in(1), &sh)) {
        return kBad;
      }
      // ARITHMETIC, from the OPERAND's declared sign bit -- not bit 63.
      const int64_t sv = static_cast<int64_t>(sext_from(a0, ob > 0 ? ob : 64));
      return trunc_to(static_cast<uint64_t>(sv >> sh), nw);
    }
    case Ntype_op::Sext: {
      int            ob = 0;
      const uint64_t a0 = rd(in(0), &ob);
      int64_t        nb = 0;
      if (a0 == kBad || !shift_amount(in(1), &nb) || nb <= 0) {
        return kBad;
      }
      return trunc_to(sext_from(a0, static_cast<int>(nb)), nw);
    }
    case Ntype_op::Get_mask: {
      int            ob = 0;
      const uint64_t a0 = rd(in(0), &ob);
      auto           md = in(2);
      if (a0 == kBad || md.is_invalid() || !gu::is_const_pin(md)) {
        return kBad;
      }
      auto mc = gu::hydrate_const(md);
      if (mc.is_just_i64() && mc.to_just_i64() == -1) {
        return trunc_to(trunc_to(a0, ob > 0 ? ob : 64), nw);  // to-unsigned
      }
      auto [lo, hi] = mc.get_mask_range();
      if (lo < 0 || hi <= lo || hi > 63) {
        return kBad;
      }
      return trunc_to((a0 >> lo) & wmask(hi - lo), nw);
    }
    default: return kBad;
  }
}

// Solve the fixture's equations by iteration.
//
// JUSTIFIED ONLY FOR THIS FIXTURE, and only for the DISJOINT one: its bit-level
// dependency graph is acyclic (that is the measured property these cases are
// about), so the equations have exactly one solution and the iteration computes
// it. That is a statement about this graph, not a definition of what a cyclic
// RTL equation means -- settling or not settling proves nothing in general, and
// the refusal cases are decided structurally, not here.
bool settle(hhds::Graph* g, uint64_t x, uint64_t init, std::map<uint64_t, uint64_t>* out) {
  std::vector<hhds::Node_class> nodes;
  for (auto nd : g->fast_class()) {
    nodes.push_back(nd);
  }
  std::map<uint64_t, uint64_t> prev;
  for (auto& nd : nodes) {
    prev[static_cast<uint64_t>(nd.get_debug_nid())] = trunc_to(init, node_bits(nd));
  }
  for (int round = 0; round < 64; ++round) {
    std::map<uint64_t, uint64_t> cur;
    for (auto& nd : nodes) {
      const uint64_t v                               = op_value(nd, x, prev);
      cur[static_cast<uint64_t>(nd.get_debug_nid())] = (v == kBad) ? uint64_t{0} : v;
    }
    if (cur == prev) {
      *out = cur;
      return true;
    }
    prev = std::move(cur);
  }
  return false;
}

}  // namespace

// The golden, in closed form, from the fixture's PARAMETERS -- not from any
// graph. The fixture is:
//     wordA = fromB | off(a0) | off(a1)      fromB = (B[kB +: n] signed) << pA
//     wordB = off(b0) | fromA                fromA = (A[kA +: n] signed) << pB
// With disjoint fields the system settles in one pass: whatever A takes from B
// comes from B's OFF-cycle fields, and vice versa. `off(at)` is (x & 0xFF) << at.
namespace {

uint64_t golden_disjoint(uint64_t x, int n, int kA, int pA, int kB, int pB, int a0, int a1, int b0,
                         uint64_t* out_b) {
  const uint64_t ox    = x & 0xFF;
  auto           field = [&](uint64_t w, int k) {  // signed n-bit field at offset k
    return sext_from(w >> k, n);
  };
  // to-unsigned of a signed n-bit field is exactly its low n bits: the result
  // is non-negative and bit n is 0. Masking to n+1 instead KEEPS the
  // sign-extended bit n, which is where this golden first disagreed with the
  // graph -- at x=128, the first input whose field is negative.
  auto zext = [&](uint64_t v) { return v & wmask(n); };

  // round 0: both words from their off-cycle fields only
  uint64_t wa = (ox << a0) | (ox << a1);
  uint64_t wb = (ox << b0);
  // settle
  for (int i = 0; i < 8; ++i) {
    const uint64_t na = (zext(field(wb, kB)) << pA) | (ox << a0) | (ox << a1);
    const uint64_t nb = (ox << b0) | (zext(field(wa, kA)) << pB);
    if (na == wa && nb == wb) {
      break;
    }
    wa = na;
    wb = nb;
  }
  *out_b = wb;
  return wa;
}

}  // namespace

// THE GATE. Same graph, evaluated before and after the rewrite, over every
// input, and both compared against the closed-form golden.
TEST(SplitSelfrefSemantics, DisjointRewritePreservesEveryValue) {
  auto f = build_pair("lgdb_sem_ok", "semok", /*n=*/8, /*kA=*/16, /*pA=*/0, /*kB=*/0, /*pB=*/16, /*wbits=*/48);
  // the two packed words, found by op rather than by remembering node ids
  const uint64_t ida = f.id_a;
  const uint64_t idb = f.id_b;

  std::vector<std::pair<uint64_t, uint64_t>> before;
  for (uint64_t x = 0; x < 256; ++x) {
    std::map<uint64_t, uint64_t> z, o;
    ASSERT_TRUE(settle(f.g.get(), x, 0, &z)) << "the DISJOINT fixture must settle: x=" << x;
    ASSERT_TRUE(settle(f.g.get(), x, ~uint64_t{0}, &o)) << "the DISJOINT fixture must settle: x=" << x;
    // Unique fixed point: a disjoint pack is a FUNCTION of its inputs, so where
    // it starts cannot matter. This is the property the rewrite relies on.
    ASSERT_EQ(z[ida], o[ida]) << "disjoint word A is not input-determined at x=" << x;
    ASSERT_EQ(z[idb], o[idb]) << "disjoint word B is not input-determined at x=" << x;
    before.emplace_back(z[ida], z[idb]);
  }

  ASSERT_GT(split_packed_selfref_wires(f.g.get()), 0);

  for (uint64_t x = 0; x < 256; ++x) {
    std::map<uint64_t, uint64_t> st;
    ASSERT_TRUE(settle(f.g.get(), x, 0, &st)) << "after the rewrite: x=" << x;
    EXPECT_EQ(st[ida], before[x].first) << "word A changed value at x=" << x;
    EXPECT_EQ(st[idb], before[x].second) << "word B changed value at x=" << x;
  }

  // ...and both agree with a model that never saw the graph. The off-cycle
  // placements come from build_pair's `park` helper: for pA=0 (field [0,9))
  // they are 16 and 24, and for pB=16 (field [16,25)) the first gap is 0.
  for (uint64_t x = 0; x < 256; ++x) {
    uint64_t gb = 0;
    const uint64_t ga = golden_disjoint(x, 8, 16, 0, 0, 16, 16, 24, 0, &gb);
    EXPECT_EQ(before[x].first, ga) << "word A disagrees with the closed-form golden at x=" << x;
    EXPECT_EQ(before[x].second, gb) << "word B disagrees with the closed-form golden at x=" << x;
  }
}

// NO SEMANTIC CONTROL FOR THE OVERLAPPING CASE, deliberately.
//
// An earlier version asserted that the overlapping fixture "does not settle",
// then that its fixed point was not unique. Neither is what a cyclic RTL
// equation means, and the first was simply false -- it settles, because
// A[16,24) = B[16,24) = A[16,24) has a consistent solution and the iteration
// finds it. Overlap is decided STRUCTURALLY by the already-armed refusal cases
// above, which assert the pass's own on-stack diagnostic. This oracle covers
// the DISJOINT, bit-acyclic case, where iteration is justified.
