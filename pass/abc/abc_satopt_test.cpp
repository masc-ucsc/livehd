// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_satopt.hpp"
#include "satopt_mux.hpp"

#include <cstdlib>
#include <stdexcept>

#include "blast.hpp"
#include "graph_library_singleton.hpp"
#include "gtest/gtest.h"
#include "node_util.hpp"
#include "prove.hpp"

namespace gu = livehd::graph_util;
namespace {
struct Fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              x, s;
  hhds::Node_class             mux;
  Fixture(std::string_view name) {
    auto io = lib.create_io(name);
    io->add_input("x", 1);
    io->set_bits("x", 8);
    io->add_output("y", 2);
    io->set_bits("y", 8);
    g = io->create_graph();
    x = g->get_input_pin("x");
    gu::set_ubits(x, 8);
    auto red = gu::create_typed_node(*g, Ntype_op::Ror);
    x.connect_sink(red.create_sink_pin(0));
    s = red.create_driver_pin(0);
    gu::set_ubits(s, 1);
    gu::set_color(red, 1);
    mux = gu::create_typed_node(*g, Ntype_op::Mux);
    gu::set_color(mux, 2);
    s.connect_sink(mux.create_sink_pin(0));
    x.connect_sink(mux.create_sink_pin(1));
    gu::create_const(*g, *Dlop::create_integer(255)).connect_sink(mux.create_sink_pin(2));
    auto y = mux.create_driver_pin(0);
    gu::set_ubits(y, 8);
    y.connect_sink(g->get_output_pin("y"));
  }
};
bool has(const livehd::abc::Satopt_result& result, const hhds::Node_class& n, int arm, int bit, livehd::abc::Mux_fact::Kind kind) {
  auto it = result.mux.find(static_cast<uint64_t>(n.get_debug_nid()));
  if (it == result.mux.end()) {
    return false;
  }
  for (const auto& f : it->second) {
    if (f.arm == arm && f.bit == bit && f.kind == kind) {
      return true;
    }
  }
  return false;
}
}  // namespace
TEST(Satopt, CrossRegionConditionalZerosAndExactReuse) {
  Fixture    f("satopt_conditional");
  const auto dir    = std::string(std::getenv("TEST_TMPDIR")) + "/satopt";
  auto       result = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_GT(result->survivors, 0);
  for (int b = 0; b < 8; ++b) {
    EXPECT_TRUE(has(*result, f.mux, 0, b, livehd::abc::Mux_fact::Kind::zero));
  }
  EXPECT_TRUE(livehd::abc::satopt(f.g.get(), dir)->reused);
  // Change a producer across the boundary while leaving the mux untouched.
  auto red = f.s.get_master_node();
  gu::set_type_op(red, Ntype_op::Not);
  auto changed = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_FALSE(changed->reused);
  EXPECT_FALSE(has(*changed, f.mux, 0, 7, livehd::abc::Mux_fact::Kind::zero));
}
// Out of budget the mux-fact search proves nothing, says it was cut short, and
// its row is never reused as a complete search.
TEST(Satopt, StarvedMuxSearchIsPartialAndNotReused) {
  Fixture                f("satopt_starved");
  const auto             dir = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_starved";
  livehd::satopt::Budget none;
  none.queries = 0;
  livehd::satopt::Meter meter(none);
  meter.begin_stage(0);
  const auto* abc = livehd::satopt::registered_mux_prover();
  ASSERT_NE(abc, nullptr);
  const auto starved = livehd::satopt::optimize_muxes(f.g.get(), *abc, dir, false, livehd::satopt::Profile::synthesis, &meter);
  EXPECT_GT(starved.survivors, 0);
  EXPECT_EQ(starved.proven, 0);
  EXPECT_FALSE(starved.complete);
  EXPECT_EQ(starved.budget_skips, starved.survivors);
  EXPECT_FALSE(f.mux.create_sink_pin(1).get_driver_pin().is_const());
  const auto full = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_FALSE(full->reused);
  EXPECT_TRUE(full->complete);
  EXPECT_TRUE(has(*full, f.mux, 0, 7, livehd::abc::Mux_fact::Kind::zero));
  EXPECT_TRUE(livehd::abc::satopt(f.g.get(), dir)->reused);
}
// A search whose very first work charge fails is partial too (its only mux
// was never searched).
TEST(Satopt, WorkStarvedMuxSearchIsPartial) {
  Fixture                f("satopt_work_starved");
  const auto             dir = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_work_starved";
  livehd::satopt::Budget none;
  none.work = 0;
  livehd::satopt::Meter meter(none);
  meter.begin_stage(0);
  const auto starved = livehd::satopt::optimize_muxes(f.g.get(),
                                                      *livehd::satopt::registered_mux_prover(),
                                                      dir,
                                                      false,
                                                      livehd::satopt::Profile::synthesis,
                                                      &meter);
  EXPECT_FALSE(starved.complete);
  EXPECT_EQ(starved.proven, 0);
  const auto full = livehd::abc::satopt(f.g.get(), dir);
  EXPECT_FALSE(full->reused);
  EXPECT_TRUE(has(*full, f.mux, 0, 7, livehd::abc::Mux_fact::Kind::zero));
}
// The facts become an LGraph rewrite of the ARM INPUT: arm 0 (selected when
// |x == 0) is all zero, so the mux reads a constant there. A second pass finds
// nothing left to rewrite (the arm now reads the fact structurally).
TEST(Satopt, MuxFactsRewriteTheArmInput) {
  Fixture    f("satopt_rewrite_whole");
  const auto dir   = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_rewrite";
  const auto stats = livehd::abc::optimize_muxes(f.g.get(), dir);
  EXPECT_EQ(stats.muxes, 1);
  EXPECT_EQ(stats.arms, 1);
  EXPECT_EQ(stats.bits, 8);
  const auto arm0 = f.mux.create_sink_pin(1).get_driver_pin();
  ASSERT_TRUE(arm0.is_const());
  EXPECT_TRUE(gu::const_of(arm0).is_known_zero());
  const auto arm1 = f.mux.create_sink_pin(2).get_driver_pin();
  ASSERT_TRUE(arm1.is_const());
  EXPECT_TRUE(gu::const_of(arm1).is_known_eq(*Dlop::create_integer(255)));
  EXPECT_EQ(f.mux.create_sink_pin(0).get_driver_pin(), f.s);  // the select is untouched
  const auto again = livehd::abc::optimize_muxes(f.g.get(), dir);
  EXPECT_EQ(again.arms, 0);
}

// Only bit 7 of arm 0 is proven (the select IS x[7]): the arm becomes a Concat
// of a constant-zero lane over bit 7 and a slice of the original arm below it.
TEST(Satopt, MixedArmFactsBecomeAConcatOfRuns) {
  Fixture f("satopt_rewrite_runs");
  gu::drop_drivers(f.mux.create_sink_pin(0));
  auto top = gu::create_get_mask(*f.g, f.x, 7, 8);
  gu::set_color(top, 1);
  auto s7 = top.create_driver_pin(0);
  gu::set_ubits(s7, 1);
  s7.connect_sink(f.mux.create_sink_pin(0));
  const auto stats = livehd::abc::optimize_muxes(f.g.get());
  EXPECT_EQ(stats.arms, 1);
  EXPECT_EQ(stats.bits, 1);
  const auto arm0 = f.mux.create_sink_pin(1).get_driver_pin();
  ASSERT_FALSE(arm0.is_const());
  const auto cat = arm0.get_master_node();
  ASSERT_EQ(gu::type_op_of(cat), Ntype_op::Concat);
  EXPECT_EQ(gu::bits_of(arm0), 8);
  EXPECT_EQ(gu::color_of(cat), 2);  // new logic stays in the mux's region
  // Lane 0 (most significant) is the proven-zero bit 7, lane 1 the slice x[6:0].
  const auto msb = cat.create_sink_pin(0).get_driver_pin();
  ASSERT_TRUE(msb.is_const());
  EXPECT_TRUE(gu::const_of(msb).is_known_zero());
  EXPECT_TRUE(gu::const_of(cat.create_sink_pin(1).get_driver_pin()).is_known_eq(*Dlop::create_integer(1)));
  const auto low = cat.create_sink_pin(2).get_driver_pin();
  EXPECT_EQ(gu::type_op_of(low.get_master_node()), Ntype_op::Get_mask);
  EXPECT_EQ(gu::bits_of(low), 7);
  EXPECT_TRUE(gu::const_of(cat.create_sink_pin(3).get_driver_pin()).is_known_eq(*Dlop::create_integer(7)));
}

// The select is x[7]: bit 7 of either arm is 0 when selected, and arm 1 is
// ~x. Arm 1 becomes a Concat of that zero bit and Xor(x[6:0], ones); arm 0's
// own complement facts read arm 1's rewritten bits and are dropped. A second
// pass blasts the Xor to the very complement it was proven against, so it
// finds nothing new.
TEST(Satopt, ComplementRewriteIsIdempotent) {
  Fixture f("satopt_rewrite_complement");
  gu::drop_drivers(f.mux.create_sink_pin(0));
  auto top = gu::create_get_mask(*f.g, f.x, 7, 8);
  gu::set_color(top, 1);
  auto s7 = top.create_driver_pin(0);
  gu::set_ubits(s7, 1);
  s7.connect_sink(f.mux.create_sink_pin(0));
  auto inv = gu::create_typed_node(*f.g, Ntype_op::Not);
  f.x.connect_sink(inv.create_sink_pin(0));
  auto value = inv.create_driver_pin(0);
  gu::set_ubits(value, 8);
  gu::set_color(inv, 1);
  gu::drop_drivers(f.mux.create_sink_pin(2));
  value.connect_sink(f.mux.create_sink_pin(2));
  const auto out    = f.mux.create_driver_pin(0);
  const auto before = livehd::satopt::Satopt_seeds{}.sample(out);
  ASSERT_TRUE(before.has_value());
  const auto dir   = std::string(std::getenv("TEST_TMPDIR")) + "/satopt_rewrite_complement";
  const auto first = livehd::abc::optimize_muxes(f.g.get(), dir);
  EXPECT_EQ(first.arms, 2);
  EXPECT_EQ(first.bits, 9);
  const auto arm1 = f.mux.create_sink_pin(2).get_driver_pin();
  ASSERT_EQ(gu::type_op_of(arm1.get_master_node()), Ntype_op::Concat);
  const auto xor_node = arm1.get_master_node().create_sink_pin(2).get_driver_pin().get_master_node();
  ASSERT_EQ(gu::type_op_of(xor_node), Ntype_op::Xor);
  int operands = 0;
  for (const auto& sink : xor_node.inp_sorted_pins()) {
    EXPECT_EQ(sink.get_driver_pins().size(), 1u);  // one driver per sink pin
    ++operands;
  }
  EXPECT_EQ(operands, 2);
  // The same seed vectors give the same mux output after the rewrite.
  const auto after = livehd::satopt::Satopt_seeds{}.sample(out);
  ASSERT_TRUE(after.has_value());
  for (size_t i = 0; i < before->size(); ++i) {
    EXPECT_TRUE((*before)[i].is_known_eq((*after)[i])) << i;
  }
  EXPECT_EQ(livehd::abc::optimize_muxes(f.g.get(), dir).arms, 0);
}

TEST(Satopt, InRegionFactsAreNotCandidates) {
  Fixture f("satopt_one_region");
  gu::set_color(f.s.get_master_node(), 2);
  auto result = livehd::abc::satopt(f.g.get());
  EXPECT_EQ(result->candidates, 0);
  auto explicit_run = livehd::abc::satopt(f.g.get(), {}, true);
  EXPECT_GT(explicit_run->proven, 0);
}
TEST(Satopt, IndependentFreeInputsDoNotBecomeConstants) {
  Fixture f("satopt_free");
  gu::drop_drivers(f.mux.create_sink_pin(0));
  f.x.connect_sink(f.mux.create_sink_pin(0));
  // A nonzero 8-bit selector must see bit 7, not just bit 0.
  auto result = livehd::abc::satopt(f.g.get(), {}, true);
  for (int b = 0; b < 8; ++b) {
    EXPECT_TRUE(has(*result, f.mux, 0, b, livehd::abc::Mux_fact::Kind::zero));
  }
  EXPECT_FALSE(has(*result, f.mux, 1, 7, livehd::abc::Mux_fact::Kind::zero));
}

TEST(Satopt, ComplementedArmsAreProven) {
  Fixture f("satopt_complement");
  auto    inv = gu::create_typed_node(*f.g, Ntype_op::Not);
  f.x.connect_sink(inv.create_sink_pin(0));
  auto value = inv.create_driver_pin(0);
  gu::set_ubits(value, 8);
  gu::set_color(inv, 1);
  gu::drop_drivers(f.mux.create_sink_pin(2));
  value.connect_sink(f.mux.create_sink_pin(2));
  auto result = livehd::abc::satopt(f.g.get());
  for (int bit = 0; bit < 8; ++bit) {
    EXPECT_TRUE(has(*result, f.mux, 1, bit, livehd::abc::Mux_fact::Kind::complement));
  }
}

namespace {
bool is_const(const hhds::Node_class& n, hhds::Port_id pid, int64_t value) {
  const auto d = n.create_sink_pin(pid).get_driver_pin();
  return d.is_const() && gu::const_of(d).is_known_eq(*Dlop::create_integer(value));
}
}  // namespace

// ---- Shared profile (B) on the mux-arm facts, proven with ABC.
namespace {
// y = sel ? b : a over free 8-bit inputs, with sel = (x == 0). Arm facts are
// planted by the caller.
struct Mux_fixture {
  hhds::GraphLibrary           lib;
  std::shared_ptr<hhds::Graph> g;
  hhds::Pin_class              x, a, b;
  explicit Mux_fixture(std::string_view name, bool signed_out = false) {
    auto io = lib.create_io(name);
    int  pid = 1;
    for (const auto* n : {"x", "a", "b"}) {
      io->add_input(n, pid++);
      io->set_bits(n, 8);
      io->set_unsign(n, true);
    }
    io->add_output("y", pid);
    io->set_bits("y", 8);
    io->set_unsign("y", !signed_out);
    g = io->create_graph();
    x = in("x");
    a = in("a");
    b = in("b");
  }
  hhds::Pin_class in(std::string_view n) {
    auto p = g->get_input_pin(n);
    gu::set_ubits(p, 8);
    return p;
  }
  hhds::Pin_class op(Ntype_op kind, std::initializer_list<hhds::Pin_class> ins, int bits, bool is_signed = false) {
    auto n = gu::create_typed_node(*g, kind);
    for (const auto& p : ins) {
      p.connect_sink(gu::setup_sink_pid(n, 0));
    }
    auto out = n.create_driver_pin(0);
    if (is_signed) {
      gu::set_sbits(out, bits);
    } else {
      gu::set_ubits(out, bits);
    }
    return out;
  }
  hhds::Pin_class konst(int64_t v) { return gu::create_const(*g, *Dlop::create_integer(v)); }
  int             count(Ntype_op kind) const {
    int n = 0;
    for (const auto node : g->body().nodes()) {
      n += gu::type_op_of(node) == kind;
    }
    return n;
  }
};
const livehd::satopt::Mux_prover* prover() { return livehd::satopt::registered_mux_prover(); }
}  // namespace

// An arm whose driver is a latch Q is never rebuilt: the latch contract keeps
// the Q a direct data arm of its hold mux.
TEST(SatoptShared, LatchArmIsNotRebuilt) {
  if (prover() == nullptr) {
    GTEST_SKIP() << "no bit-level mux prover linked";
  }
  Mux_fixture f("shared_latch_arm");
  auto        latch = gu::create_typed_node(*f.g, Ntype_op::Latch);
  f.a.connect_sink(latch.create_sink_pin(3));  // din
  f.op(Ntype_op::Ror, {f.b}, 1).connect_sink(latch.create_sink_pin(4));  // enable
  auto q = latch.create_driver_pin(0);
  gu::set_ubits(q, 8);
  // Arm 1 (x != 0) is x AND 0: proven 0 when selected, and equal to arm 0's
  // bits nowhere -- the only candidate that touches the latch arm reads it.
  auto sel = f.op(Ntype_op::Ror, {f.x}, 1);
  auto m   = gu::create_typed_node(*f.g, Ntype_op::Mux);
  sel.connect_sink(m.create_sink_pin(0));
  q.connect_sink(m.create_sink_pin(1));         // selected when x == 0
  f.op(Ntype_op::And, {f.x, f.konst(0)}, 8).connect_sink(m.create_sink_pin(2));
  auto y = m.create_driver_pin(0);
  gu::set_ubits(y, 8);
  y.connect_sink(f.g->get_output_pin("y"));
  livehd::satopt::optimize_muxes(f.g.get(), *prover(), {}, true, livehd::satopt::Profile::shared);
  EXPECT_TRUE(m.create_sink_pin(1).get_driver_pin() == q);
}

// A signed mux keeps each rebuilt arm's integer value: the pattern is read
// back through a sign extension, never as an unsigned (positive) number.
TEST(SatoptShared, SignedMuxArmKeepsItsValue) {
  if (prover() == nullptr) {
    GTEST_SKIP() << "no bit-level mux prover linked";
  }
  Mux_fixture f("shared_signed_arm", true);
  auto        sel = f.op(Ntype_op::Ror, {f.x}, 1);
  // arm1 (x != 0): a mix of a proven-zero low bit and free high bits.
  auto low_zero = f.op(Ntype_op::And, {f.a, f.konst(0xfe)}, 8, true);
  auto m        = gu::create_typed_node(*f.g, Ntype_op::Mux);
  sel.connect_sink(m.create_sink_pin(0));
  f.b.connect_sink(m.create_sink_pin(1));
  low_zero.connect_sink(m.create_sink_pin(2));
  auto y = m.create_driver_pin(0);
  gu::set_sbits(y, 8);
  y.connect_sink(f.g->get_output_pin("y"));
  const auto before = livehd::satopt::Satopt_seeds{}.sample(y);
  livehd::satopt::optimize_muxes(f.g.get(), *prover(), {}, true, livehd::satopt::Profile::shared);
  const auto arm = m.create_sink_pin(2).get_driver_pin();
  ASSERT_TRUE(arm != low_zero);  // bit 0 is proven 0 when the arm is selected
  EXPECT_EQ(gu::type_op_of(arm.get_master_node()), Ntype_op::Sext);  // read back as a signed value
  EXPECT_FALSE(gu::is_unsign(arm));
  const auto after = livehd::satopt::Satopt_seeds{}.sample(y);
  ASSERT_TRUE(before && after);
  for (size_t i = 0; i < before->size(); ++i) {
    EXPECT_TRUE((*before)[i].is_known_eq((*after)[i])) << i;
  }
}

// Every arm of a Hotmux proven 0 when selected would leave it foldable, and
// cprop would drop its exclusivity obligation: without an exclusivity proof
// the rewrite is declined; with exclusive controls it goes through.
TEST(SatoptShared, FoldableHotmuxNeedsExclusiveControls) {
  if (prover() == nullptr) {
    GTEST_SKIP() << "no bit-level mux prover linked";
  }
  for (const bool exclusive : {false, true}) {
    Mux_fixture f(exclusive ? "shared_fold_exclusive" : "shared_fold_overlap");
    // c0 = x[0], c1 = exclusive ? !x[0] : x[1]; arm_i = a AND (c_i ? 0 : -1).
    auto x0  = gu::create_get_mask(*f.g, f.x, 0, 1);
    auto p0  = x0.create_driver_pin(0);
    gu::set_ubits(p0, 1);
    hhds::Pin_class p1;
    if (exclusive) {
      p1 = f.op(Ntype_op::EQ, {p0, f.konst(0)}, 1);  // !x[0] (Not is a bitwise complement)
    } else {
      auto x1 = gu::create_get_mask(*f.g, f.x, 1, 2);
      p1      = x1.create_driver_pin(0);
      gu::set_ubits(p1, 1);
    }
    // Arm values that are 0 whenever their control is 1: a AND (0 - !c).
    const auto zero_when = [&](const hhds::Pin_class& c) {
      auto nc   = f.op(Ntype_op::Not, {c}, 1);
      auto wide = gu::create_typed_node(*f.g, Ntype_op::Sext);
      nc.connect_sink(wide.create_sink_pin(0));
      f.konst(1).connect_sink(wide.create_sink_pin(1));
      auto w = wide.create_driver_pin(0);
      gu::set_sbits(w, 8);
      return f.op(Ntype_op::And, {f.a, w}, 8);
    };
    auto hot = gu::create_typed_node(*f.g, Ntype_op::Hotmux);
    p0.connect_sink(hot.create_sink_pin(0));
    zero_when(p0).connect_sink(hot.create_sink_pin(1));
    p1.connect_sink(hot.create_sink_pin(2));
    zero_when(p1).connect_sink(hot.create_sink_pin(3));
    auto y = hot.create_driver_pin(0);
    gu::set_ubits(y, 8);
    y.connect_sink(f.g->get_output_pin("y"));
    auto s = livehd::satopt::optimize_muxes(f.g.get(), *prover(), {}, true, livehd::satopt::Profile::shared);
    const bool zeroed = is_const(hot, 1, 0) && is_const(hot, 3, 0);
    EXPECT_EQ(zeroed, exclusive) << s.arms;
  }
}

// The shared profile on an uncolored graph is idempotent: the second run sees
// its own rewrite through the wiring it built.
TEST(SatoptShared, SecondRunIsANoOp) {
  if (prover() == nullptr) {
    GTEST_SKIP() << "no bit-level mux prover linked";
  }
  Mux_fixture f("shared_idempotent");
  auto        sel = f.op(Ntype_op::Ror, {f.x}, 1);
  auto        m   = gu::create_typed_node(*f.g, Ntype_op::Mux);
  sel.connect_sink(m.create_sink_pin(0));
  f.x.connect_sink(m.create_sink_pin(1));  // selected only when x == 0: always 0 then
  f.b.connect_sink(m.create_sink_pin(2));
  auto y = m.create_driver_pin(0);
  gu::set_ubits(y, 8);
  y.connect_sink(f.g->get_output_pin("y"));
  auto first = livehd::satopt::optimize_muxes(f.g.get(), *prover(), {}, true, livehd::satopt::Profile::shared);
  EXPECT_GT(first.arms, 0u);
  EXPECT_TRUE(is_const(m, 1, 0));
  auto second = livehd::satopt::optimize_muxes(f.g.get(), *prover(), {}, true, livehd::satopt::Profile::shared);
  EXPECT_EQ(second.arms, 0u);
}
