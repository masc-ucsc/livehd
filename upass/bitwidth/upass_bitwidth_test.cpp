//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_bitwidth.hpp"

#include <memory>
#include <optional>
#include <string>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "lnast_range.hpp"
#include "upass_runner.hpp"

// ── Part 1: Lattice unit tests ────────────────────────────────────────────────

TEST(LnastRangeLattice, Unbounded) {
  auto r = Lnast_range::make_unbounded();
  EXPECT_TRUE(r.is_unbounded());
  EXPECT_FALSE(r.is_constant());
}

TEST(LnastRangeLattice, Constant) {
  auto r = Lnast_range::constant(42);
  EXPECT_FALSE(r.is_unbounded());
  EXPECT_TRUE(r.is_constant());
  EXPECT_EQ(r.min, 42);
  EXPECT_EQ(r.max, 42);
}

TEST(LnastRangeLattice, Boolean) {
  auto r = Lnast_range::boolean();
  EXPECT_FALSE(r.is_unbounded());
  EXPECT_FALSE(r.is_constant());
  EXPECT_EQ(r.min, -1);
  EXPECT_EQ(r.max, 0);
}

// ── sbits ─────────────────────────────────────────────────────────────────────

TEST(LnastRangeLattice, SbitsConstant0) { EXPECT_EQ(Lnast_range::constant(0).get_sbits(), 1); }

TEST(LnastRangeLattice, SbitsConstant7) { EXPECT_EQ(Lnast_range::constant(7).get_sbits(), 4); }

TEST(LnastRangeLattice, SbitsConstantNeg1) { EXPECT_EQ(Lnast_range::constant(-1).get_sbits(), 1); }

TEST(LnastRangeLattice, SbitsRange0To15) {
  Lnast_range r;
  r.min       = 0;
  r.max       = 15;
  r.unbounded = false;
  EXPECT_EQ(r.get_sbits(), 5);
}

TEST(LnastRangeLattice, SbitsBoolean) { EXPECT_EQ(Lnast_range::boolean().get_sbits(), 1); }

TEST(LnastRangeLattice, SbitsUnbounded) { EXPECT_EQ(Lnast_range::make_unbounded().get_sbits(), 64); }

// ── Arithmetic ────────────────────────────────────────────────────────────────

TEST(LnastRangeLattice, AddConstants) {
  auto r = Lnast_range::constant(3).add(Lnast_range::constant(5));
  EXPECT_TRUE(r.is_constant());
  EXPECT_EQ(r.min, 8);
}

TEST(LnastRangeLattice, AddUnbounded) {
  auto r = Lnast_range::make_unbounded().add(Lnast_range::constant(5));
  EXPECT_TRUE(r.is_unbounded());
}

TEST(LnastRangeLattice, SubConstants) {
  auto r = Lnast_range::constant(10).sub(Lnast_range::constant(3));
  EXPECT_TRUE(r.is_constant());
  EXPECT_EQ(r.min, 7);
}

TEST(LnastRangeLattice, MulConstants) {
  auto r = Lnast_range::constant(3).mul(Lnast_range::constant(4));
  EXPECT_TRUE(r.is_constant());
  EXPECT_EQ(r.min, 12);
}

TEST(LnastRangeLattice, NegConstant) {
  auto r = Lnast_range::constant(5).neg();
  EXPECT_TRUE(r.is_constant());
  EXPECT_EQ(r.min, -5);
}

// ── Join / Meet ────────────────────────────────────────────────────────────────

TEST(LnastRangeLattice, Join) {
  auto r = Lnast_range::constant(2).join(Lnast_range::constant(7));
  EXPECT_FALSE(r.is_unbounded());
  EXPECT_EQ(r.min, 2);
  EXPECT_EQ(r.max, 7);
}

TEST(LnastRangeLattice, JoinWithUnbounded) {
  auto r = Lnast_range::constant(2).join(Lnast_range::make_unbounded());
  EXPECT_TRUE(r.is_unbounded());
}

TEST(LnastRangeLattice, Meet) {
  Lnast_range a;
  a.min       = 1;
  a.max       = 10;
  a.unbounded = false;
  Lnast_range b;
  b.min       = 5;
  b.max       = 20;
  b.unbounded = false;
  auto r      = a.meet(b);
  EXPECT_FALSE(r.is_unbounded());
  EXPECT_EQ(r.min, 5);
  EXPECT_EQ(r.max, 10);
}

// ── is_narrower_than ──────────────────────────────────────────────────────────

TEST(LnastRangeLattice, NarrowerThan) {
  Lnast_range wide;
  wide.min       = 0;
  wide.max       = 100;
  wide.unbounded = false;
  auto narrow    = Lnast_range::constant(42);
  EXPECT_TRUE(narrow.is_narrower_than(wide));
  EXPECT_FALSE(wide.is_narrower_than(narrow));
}

// ── contains (task 1b type-envelope fit predicate) ───────────────────────────

TEST(LnastRangeLattice, ContainsBounded) {
  Lnast_range u8;  // envelope [0, 255]
  u8.min       = 0;
  u8.max       = 255;
  u8.unbounded = false;
  // A non-negative 9-bit-signed value (255) is contained despite its signed
  // width exceeding 8 — the check is sign-agnostic range containment.
  EXPECT_TRUE(u8.contains(Lnast_range::constant(255)));
  EXPECT_TRUE(u8.contains(Lnast_range::constant(0)));
  EXPECT_FALSE(u8.contains(Lnast_range::constant(256)));  // over max
  EXPECT_FALSE(u8.contains(Lnast_range::constant(-1)));   // under min (signed -1)
  EXPECT_FALSE(u8.contains(Lnast_range::boolean()));      // [-1,0] not ⊆ [0,255]
}

TEST(LnastRangeLattice, ContainsUnbounded) {
  EXPECT_TRUE(Lnast_range::make_unbounded().contains(Lnast_range::constant(1000)));
  EXPECT_TRUE(Lnast_range::make_unbounded().contains(Lnast_range::boolean()));
  // A bounded envelope never contains an unbounded value range.
  Lnast_range u8;
  u8.min       = 0;
  u8.max       = 255;
  u8.unbounded = false;
  EXPECT_FALSE(u8.contains(Lnast_range::make_unbounded()));
}

// ── Part 2: Integration tests using the uPass_runner ─────────────────────────

// Build a minimal post-upass LNAST:
//   top -> stmts -> assign(c, a)  where a and b are graph inputs
// Then create an integration test that runs the bitwidth pass.

namespace {

// Build: top → stmts → assign(lhs_ref, rhs_ref_or_const)
static std::shared_ptr<Lnast> make_simple_assign(const std::string& lhs, const std::string& rhs, bool rhs_is_const = false) {
  auto ln    = std::make_shared<Lnast>("bw_test");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  auto asgn  = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(asgn, Lnast_node::create_ref(lhs));
  if (rhs_is_const) {
    ln->add_child(asgn, Lnast_node::create_const(rhs));
  } else {
    ln->add_child(asgn, Lnast_node::create_ref(rhs));
  }
  return ln;
}

// Build: top → stmts → plus(c, a, b)
static std::shared_ptr<Lnast> make_plus(const std::string& lhs, const std::string& rhs1, const std::string& rhs2,
                                        bool r1_const = false, bool r2_const = false) {
  auto ln    = std::make_shared<Lnast>("bw_plus_test");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  auto plus  = ln->add_child(stmts, Lnast_ntype::create_plus());
  ln->add_child(plus, Lnast_node::create_ref(lhs));
  if (r1_const) {
    ln->add_child(plus, Lnast_node::create_const(rhs1));
  } else {
    ln->add_child(plus, Lnast_node::create_ref(rhs1));
  }
  if (r2_const) {
    ln->add_child(plus, Lnast_node::create_const(rhs2));
  } else {
    ln->add_child(plus, Lnast_node::create_ref(rhs2));
  }
  return ln;
}

// Run bitwidth pass only on an LNAST using the runner.
static void run_bw(const std::shared_ptr<Lnast>& ln) {
  auto         lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, {"bitwidth"});
  runner.run();
  // Commit the staging tree back.
  ln->replace_body(runner.take_staging()->tree_ptr());
}

}  // namespace

// Test: assign c = 3 (const) → bw_meta["c"] should be constant(3)
TEST(BitwidthIntegration, ConstAssign) {
  auto ln = make_simple_assign("c", "3", /*rhs_is_const=*/true);
  run_bw(ln);
  const auto& meta = ln->bw_meta().ranges;
  auto        it   = meta.find("c");
  ASSERT_NE(it, meta.end()) << "Expected 'c' in bw_meta";
  EXPECT_FALSE(it->second.unbounded);
  EXPECT_EQ(it->second.min, 3);
  EXPECT_EQ(it->second.max, 3);
}

// Test: plus c = 3 + 5 → bw_meta["c"] should be constant(8)
TEST(BitwidthIntegration, PlusTwoConsts) {
  auto ln = make_plus("c", "3", "5", true, true);
  run_bw(ln);
  const auto& meta = ln->bw_meta().ranges;
  auto        it   = meta.find("c");
  ASSERT_NE(it, meta.end()) << "Expected 'c' in bw_meta after plus(3,5)";
  EXPECT_FALSE(it->second.unbounded);
  EXPECT_EQ(it->second.min, 8);
  EXPECT_EQ(it->second.max, 8);
}

// Test: plus c = a + 5 where a is unknown → c should be unbounded
TEST(BitwidthIntegration, PlusUnknownRef) {
  auto ln = make_plus("c", "a", "5", false, true);
  run_bw(ln);
  const auto& meta = ln->bw_meta().ranges;
  auto        it   = meta.find("c");
  // c may be absent (unbounded = never stored) or explicitly unbounded.
  if (it != meta.end()) {
    EXPECT_TRUE(it->second.unbounded) << "c should be unbounded";
  }
  // Either way: not a concrete finite range.
}

// Test: Boolean result from comparison (eq) → bw_meta should be [-1,0]
TEST(BitwidthIntegration, EqResultBoolean) {
  auto ln    = std::make_shared<Lnast>("bw_eq_test");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  auto eq    = ln->add_child(stmts, Lnast_ntype::create_eq());
  ln->add_child(eq, Lnast_node::create_ref("x"));
  ln->add_child(eq, Lnast_node::create_ref("a"));
  ln->add_child(eq, Lnast_node::create_ref("b"));

  run_bw(ln);
  const auto& meta = ln->bw_meta().ranges;
  auto        it   = meta.find("x");
  ASSERT_NE(it, meta.end()) << "Expected 'x' in bw_meta after eq";
  EXPECT_EQ(it->second.min, -1);
  EXPECT_EQ(it->second.max, 0);
  EXPECT_FALSE(it->second.unbounded);
}

// Test: assign propagates range: assign c = 3 (const) → fold_ref("c") = 3
// We verify through bw_meta only (fold_ref is internal to the pass).
TEST(BitwidthIntegration, PropagateConst) {
  // Chain: assign tmp = 3 (const), assign c = tmp (ref propagation)
  auto ln    = std::make_shared<Lnast>("bw_chain");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());

  auto asgn1 = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(asgn1, Lnast_node::create_ref("tmp"));
  ln->add_child(asgn1, Lnast_node::create_const("3"));

  auto asgn2 = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(asgn2, Lnast_node::create_ref("c"));
  ln->add_child(asgn2, Lnast_node::create_ref("tmp"));

  run_bw(ln);
  const auto& meta = ln->bw_meta().ranges;

  auto it_tmp = meta.find("tmp");
  ASSERT_NE(it_tmp, meta.end());
  EXPECT_EQ(it_tmp->second.min, 3);
  EXPECT_EQ(it_tmp->second.max, 3);

  auto it_c = meta.find("c");
  ASSERT_NE(it_c, meta.end());
  EXPECT_EQ(it_c->second.min, 3);
  EXPECT_EQ(it_c->second.max, 3);
}

TEST(BitwidthIntegration, RefCallArgClearsRange) {
  auto ln    = std::make_shared<Lnast>("bw_ref_call");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());

  auto asgn = ln->add_child(stmts, Lnast_ntype::create_store());
  ln->add_child(asgn, Lnast_node::create_ref("c"));
  ln->add_child(asgn, Lnast_node::create_const("1"));

  auto fcall = ln->add_child(stmts, Lnast_ntype::create_func_call());
  ln->add_child(fcall, Lnast_node::create_ref("out"));
  ln->add_child(fcall, Lnast_node::create_ref("opaque"));
  auto ref_arg = ln->add_child(fcall, Lnast_ntype::create_store());
  ln->add_child(ref_arg, Lnast_node::create_ref("__ref_arg"));
  ln->add_child(ref_arg, Lnast_node::create_ref("c"));

  run_bw(ln);
  const auto& meta = ln->bw_meta().ranges;
  EXPECT_EQ(meta.find("c"), meta.end()) << "explicit ref call arg must invalidate c's range";
}

// ── attr_set narrowing tests ─────────────────────────────────────────────────

namespace {
// Append an attr_set node under `stmts`: attr_set ref(target) const(attr)
// const(value).
static void add_attr_set(const std::shared_ptr<Lnast>& ln, Lnast_nid stmts, std::string_view target, std::string_view attr,
                         std::string_view value) {
  auto node = ln->add_child(stmts, Lnast_ntype::create_attr_set());
  ln->add_child(node, Lnast_node::create_ref(std::string{target}));
  ln->add_child(node, Lnast_node::create_const(std::string{attr}));
  ln->add_child(node, Lnast_node::create_const(std::string{value}));
}
}  // namespace

// Non-bitwidth attributes (e.g. `comptime`) are ignored — no range stored.
TEST(BitwidthAttrSet, NonBitwidthAttrIgnored) {
  auto ln    = std::make_shared<Lnast>("bw_attr_other");
  auto root  = ln->set_root(Lnast_ntype::create_top());
  auto stmts = ln->add_child(root, Lnast_ntype::create_stmts());
  add_attr_set(ln, stmts, "x", "comptime", "true");

  run_bw(ln);
  const auto& meta = ln->bw_meta().ranges;
  EXPECT_EQ(meta.find("x"), meta.end()) << "comptime attr should not record a range";
}

// ── Cross-invocation persistence (T3 #8) ─────────────────────────────────────
//
// First sweep proves nothing about `c = a + b` because a and b are unknown
// refs (no prior writes). After the first run, the caller injects ranges for
// a and b into bw_meta — simulating a prior pass.upass call that constrained
// them — and re-runs bitwidth. The second sweep's begin_iteration seeds
// range_map_ from bw_meta, so the plus(a, b) operands now resolve to finite
// ranges and `c` is proven [a.min + b.min, a.max + b.max].
TEST(BitwidthCrossInvocation, TightenAfterReseed) {
  auto ln = make_plus("c", "a", "b");

  // First pass: a and b are unknown, c remains unbounded (or absent).
  run_bw(ln);
  {
    const auto& meta = ln->bw_meta().ranges;
    auto        it   = meta.find("c");
    if (it != meta.end()) {
      EXPECT_TRUE(it->second.unbounded) << "c should still be unbounded after first sweep";
    }
  }

  // Inject ranges for a and b into bw_meta — what a prior pass.upass call
  // would have left behind for an earlier-bounded register.
  auto&         meta_w = ln->bw_meta();
  BitwidthEntry ea{};
  ea.min             = 0;
  ea.max             = 7;
  ea.unbounded       = false;
  meta_w.ranges["a"] = ea;
  BitwidthEntry eb{};
  eb.min             = 1;
  eb.max             = 2;
  eb.unbounded       = false;
  meta_w.ranges["b"] = eb;

  // Second pass: begin_iteration reloads a,b into range_map_; plus computes
  // c = [0+1, 7+2] = [1, 9].
  run_bw(ln);

  const auto& meta = ln->bw_meta().ranges;
  auto        it_c = meta.find("c");
  ASSERT_NE(it_c, meta.end()) << "c should be proven finite after re-seed";
  EXPECT_FALSE(it_c->second.unbounded);
  EXPECT_EQ(it_c->second.min, 1);
  EXPECT_EQ(it_c->second.max, 9);
  // Re-seeded inputs are preserved across end_run (write-clear-rewrite).
  EXPECT_NE(meta.find("a"), meta.end());
  EXPECT_NE(meta.find("b"), meta.end());
}
