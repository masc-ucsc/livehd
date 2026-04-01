//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <memory>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "upass_constprop.hpp"

namespace {

// Exposes the protected symbol table and cursor so tests can inspect results
// without going through the runner.
class TestableConstprop : public uPass_constprop {
public:
  using uPass_constprop::uPass_constprop;

  void position(const Lnast_nid& nid) { move_to_nid(nid); }

  Lconst get_result(std::string_view name) const {
    if (!st.has_trivial(name)) {
      return Lconst::invalid();
    }
    return st.get_trivial(name);
  }

  // Pre-populate the symbol table so process_if() can look up conditions.
  void seed(std::string_view name, const Lconst& val) { st.set(name, val); }
};

// Build a fresh Lnast and Lnast_manager for each test.
struct ConstpropFixture {
  std::shared_ptr<Lnast>               ln;
  std::shared_ptr<upass::Lnast_manager> lm;
  Lnast_nid                            stmts_nid;

  ConstpropFixture() {
    ln    = std::make_shared<Lnast>("test");
    ln->set_root(Lnast_node::create_top("top"));
    stmts_nid = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts("stmts"));
    lm    = std::make_shared<upass::Lnast_manager>(ln);
  }

  // Build: <op_type> ref("dst") const(lhs) const(rhs)
  // Returns the nid of the operator node.
  Lnast_nid add_binary_node(Lnast_node op_node,
                             std::string_view dst,
                             int64_t lhs,
                             int64_t rhs) {
    auto op = ln->add_child(stmts_nid, op_node);
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_const(lhs));
    ln->add_child(op, Lnast_node::create_const(rhs));
    return op;
  }

  // Build: <op_type> ref("dst") const(operand)
  Lnast_nid add_unary_node(Lnast_node op_node,
                            std::string_view dst,
                            int64_t operand) {
    auto op = ln->add_child(stmts_nid, op_node);
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_const(operand));
    return op;
  }

  // Build:
  //   [if: [ref:cond_var], [stmts:then_body], [stmts:else_body]?]
  //
  // The then/else builders receive the stmts nid they should populate.
  // Returns the nid of the if-node itself.
  Lnast_nid add_if_node(std::string_view cond_var,
                         std::function<void(Lnast_nid)> build_then,
                         std::function<void(Lnast_nid)> build_else = nullptr) {
    auto if_nid     = ln->add_child(stmts_nid, Lnast_node::create_if());
    ln->add_child(if_nid, Lnast_node::create_ref(cond_var));
    auto then_stmts = ln->add_child(if_nid, Lnast_node::create_stmts());
    if (build_then) build_then(then_stmts);
    if (build_else) {
      auto else_stmts = ln->add_child(if_nid, Lnast_node::create_stmts());
      build_else(else_stmts);
    }
    return if_nid;
  }

  // Convenience: add a binary op node under an arbitrary parent stmts nid.
  Lnast_nid add_binary_under(Lnast_nid parent_stmts, Lnast_node op_node,
                               std::string_view dst,
                               int64_t lhs, int64_t rhs) {
    auto op = ln->add_child(parent_stmts, op_node);
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_const(lhs));
    ln->add_child(op, Lnast_node::create_const(rhs));
    return op;
  }
};

// ── Arithmetic ──────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsPlus) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_plus(), "a", 2, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_plus();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 5);
}

TEST(UpassConstprop, FoldsMinus) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_minus(), "a", 10, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_minus();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 7);
}

TEST(UpassConstprop, FoldsMult) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_mult(), "a", 3, 4);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_mult();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 12);
}

TEST(UpassConstprop, FoldsDiv) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_div(), "a", 8, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_div();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 4);
}

TEST(UpassConstprop, FoldsMod) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_mod(), "a", 7, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_mod();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

// ── Shift ────────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsShl) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_shl(), "a", 1, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_shl();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 8);  // 1 << 3 = 8
}

TEST(UpassConstprop, FoldsSra) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_sra(), "a", 16, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_sra();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 4);  // 16 >> 2 = 4
}

// ── Bitwise ──────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsBitAnd) {
  ConstpropFixture f;
  // 0b1010 & 0b1100 = 0b1000 = 8
  auto op = f.add_binary_node(Lnast_node::create_bit_and(), "a", 0b1010, 0b1100);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0b1000);
}

TEST(UpassConstprop, FoldsBitOr) {
  ConstpropFixture f;
  // 0b1010 | 0b0101 = 0b1111 = 15
  auto op = f.add_binary_node(Lnast_node::create_bit_or(), "a", 0b1010, 0b0101);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0b1111);
}

TEST(UpassConstprop, FoldsBitXor) {
  ConstpropFixture f;
  // 0b1010 ^ 0b1100 = 0b0110 = 6
  auto op = f.add_binary_node(Lnast_node::create_bit_xor(), "a", 0b1010, 0b1100);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_xor();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0b0110);
}

TEST(UpassConstprop, FoldsBitNot) {
  ConstpropFixture f;
  auto op = f.add_unary_node(Lnast_node::create_bit_not(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_not();
  EXPECT_TRUE(cp.has_changed());
  // ~0 = -1 in two's complement
  EXPECT_EQ(cp.get_result("a").to_i(), -1);
}

// ── Logical ──────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsLogAndBothTrue) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_log_and(), "a", 1, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

TEST(UpassConstprop, FoldsLogAndOneFalse) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_log_and(), "a", 0, 1);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);
}

TEST(UpassConstprop, FoldsLogOrOneFalse) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_log_or(), "a", 0, 1);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

TEST(UpassConstprop, FoldsLogOrBothFalse) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_log_or(), "a", 0, 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);
}

TEST(UpassConstprop, FoldsLogNotFalse) {
  ConstpropFixture f;
  auto op = f.add_unary_node(Lnast_node::create_log_not(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_not();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);  // !0 = 1
}

TEST(UpassConstprop, FoldsLogNotTrue) {
  ConstpropFixture f;
  auto op = f.add_unary_node(Lnast_node::create_log_not(), "a", 5);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_not();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);  // !5 = 0
}

// ── Convergence ──────────────────────────────────────────────────────────────

// Running the same fold a second time should not mark changed (value already in st).
TEST(UpassConstprop, SecondRunConverges) {
  ConstpropFixture f;
  auto op = f.add_binary_node(Lnast_node::create_plus(), "a", 2, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_plus();
  ASSERT_TRUE(cp.has_changed());  // first run

  cp.begin_iteration();  // reset changed flag
  cp.position(op);
  cp.process_plus();
  EXPECT_FALSE(cp.has_changed());  // second run: value unchanged
}

// ── process_if: conservative condition inspection ─────────────────────────────
// These tests verify that process_if() handles each structural variant without
// throwing, and that it correctly returns the cursor to the if-node so the
// runner can traverse children afterwards.

// 1. if-only (no else): cond is known-true.
//    Layout: [if: [ref:cond], [stmts:then]]
TEST(UpassConstpropIf, KnownTrueConditionNoElse) {
  ConstpropFixture f;
  auto if_nid = f.add_if_node("cond", [&](Lnast_nid then_stmts) {
    f.add_binary_under(then_stmts, Lnast_node::create_plus(), "x", 1, 2);
  });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Lconst(1));  // condition is known-true
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 2. if-only: cond is known-false.
TEST(UpassConstpropIf, KnownFalseConditionNoElse) {
  ConstpropFixture f;
  auto if_nid = f.add_if_node("cond", [&](Lnast_nid then_stmts) {
    f.add_binary_under(then_stmts, Lnast_node::create_plus(), "x", 10, 20);
  });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Lconst(0));  // condition is known-false
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 3. if-only: condition variable not in symbol table (unknown).
TEST(UpassConstpropIf, UnknownConditionNoElse) {
  ConstpropFixture f;
  auto if_nid = f.add_if_node("unknown_cond", [&](Lnast_nid then_stmts) {
    f.add_binary_under(then_stmts, Lnast_node::create_plus(), "y", 3, 4);
  });
  TestableConstprop cp(f.lm);
  // Do NOT seed "unknown_cond" — leave it absent from the symbol table.
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 4. if-else: two branches, known-true condition.
//    Layout: [if: [ref:cond], [stmts:then], [stmts:else]]
TEST(UpassConstpropIf, KnownTrueConditionWithElse) {
  ConstpropFixture f;
  auto if_nid = f.add_if_node(
      "cond",
      [&](Lnast_nid then_stmts) {
        f.add_binary_under(then_stmts, Lnast_node::create_plus(), "r", 1, 0);
      },
      [&](Lnast_nid else_stmts) {
        f.add_binary_under(else_stmts, Lnast_node::create_plus(), "r", 0, 0);
      });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Lconst(1));
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 5. if-else: known-false condition.
TEST(UpassConstpropIf, KnownFalseConditionWithElse) {
  ConstpropFixture f;
  auto if_nid = f.add_if_node(
      "cond",
      [&](Lnast_nid then_stmts) {
        f.add_binary_under(then_stmts, Lnast_node::create_plus(), "r", 5, 5);
      },
      [&](Lnast_nid else_stmts) {
        f.add_binary_under(else_stmts, Lnast_node::create_plus(), "r", 9, 9);
      });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Lconst(0));
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 6. Literal const as condition (if (1) { ... }) — should complete cleanly.
TEST(UpassConstpropIf, ConstConditionNoElse) {
  ConstpropFixture f;
  // Build [if: [const:1], [stmts:...]] — constant-true condition literal.
  auto if_nid     = f.ln->add_child(f.stmts_nid, Lnast_node::create_if());
  f.ln->add_child(if_nid, Lnast_node::create_const(1));
  auto then_stmts = f.ln->add_child(if_nid, Lnast_node::create_stmts());
  f.add_binary_under(then_stmts, Lnast_node::create_plus(), "z", 7, 8);
  TestableConstprop cp(f.lm);
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// ── Reduce ops ───────────────────────────────────────────────────────────────

// red_or: 1 if any bit set, 0 if zero.
TEST(UpassConstprop, RedOrNonZero) {
  ConstpropFixture f;
  auto op = f.add_unary_node(Lnast_node::create_red_or(), "a", 5);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);  // 5 != 0 → 1
}

TEST(UpassConstprop, RedOrZero) {
  ConstpropFixture f;
  auto op = f.add_unary_node(Lnast_node::create_red_or(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);  // 0 == 0 → 0
}

TEST(UpassConstprop, RedOrUnknownInputNoStore) {
  // Input is an unknown ref — result should NOT be stored.
  // Build: [red_or: ref("b"), ref("unknown_x")]
  ConstpropFixture f;
  auto red_nid = f.ln->add_child(f.stmts_nid, Lnast_node::create_red_or());
  f.ln->add_child(red_nid, Lnast_node::create_ref("b"));
  f.ln->add_child(red_nid, Lnast_node::create_ref("unknown_x"));
  TestableConstprop cp(f.lm);
  cp.position(red_nid);
  cp.process_red_or();
  // "b" must not appear in the symbol table.
  EXPECT_TRUE(cp.get_result("b").is_invalid());
  EXPECT_FALSE(cp.has_changed());
}

// red_and: 1 only when all bits are 1 (value is all-ones mask).
TEST(UpassConstprop, RedAndAllOnes) {
  ConstpropFixture f;
  // 0b111 = 7: all bits set → red_and = 1.
  auto op = f.add_unary_node(Lnast_node::create_red_and(), "a", 0b111);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

TEST(UpassConstprop, RedAndNotAllOnes) {
  ConstpropFixture f;
  // 0b101 = 5: bit 1 is clear → red_and = 0.
  auto op = f.add_unary_node(Lnast_node::create_red_and(), "a", 0b101);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);
}

// red_xor: parity of set bits (1 if popcount is odd).
TEST(UpassConstprop, RedXorOddParity) {
  ConstpropFixture f;
  // 0b101 = 5: popcount = 2 (even) → red_xor = 0
  // Let's use 0b111 = 7: popcount = 3 (odd) → red_xor = 1.
  auto op = f.add_unary_node(Lnast_node::create_red_xor(), "a", 0b111);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_xor();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);  // popcount(7) = 3, odd → 1
}

TEST(UpassConstprop, RedXorEvenParity) {
  ConstpropFixture f;
  // 0b1010 = 10: popcount = 2 (even) → red_xor = 0.
  auto op = f.add_unary_node(Lnast_node::create_red_xor(), "a", 0b1010);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_xor();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);  // popcount(10) = 2, even → 0
}

// ── sext ─────────────────────────────────────────────────────────────────────

// Fixture helper: [sext: ref(dst), const_or_ref(src), const(nbits)]
// Since add_binary_node uses two int64_t literals, we build this manually.
static Lnast_nid add_sext_node(ConstpropFixture &f,
                                 std::string_view dst,
                                 int64_t src_val,
                                 int64_t nbits) {
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_sext());
  f.ln->add_child(op, Lnast_node::create_ref(dst));
  f.ln->add_child(op, Lnast_node::create_const(src_val));
  f.ln->add_child(op, Lnast_node::create_const(nbits));
  return op;
}

TEST(UpassConstprop, SextNoTruncation) {
  ConstpropFixture f;
  // src = 3 (0b011), ebits = 10 → ebits >= bits, so no change: result = 3.
  auto op = add_sext_node(f, "a", 3, 10);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_sext();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 3);
}

TEST(UpassConstprop, SextSignExtendNarrows) {
  ConstpropFixture f;
  // src = 4 (0b100, 4 bits), ebits = 2.
  // sext_op(2): tests bit 2 of 4 (0b100) = 1 → sign bit is set → negative.
  // Lower bits (0,1) of 4 are 00 → signed 3-bit 0b100 = -4.
  auto op = add_sext_node(f, "a", 4, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_sext();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), -4);  // 3-bit 0b100 sign-extended = -4
}

}  // namespace
