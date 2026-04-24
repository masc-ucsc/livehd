//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "upass_constprop.hpp"

#include <memory>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"

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

  // ── §5.a ST query helpers exposed for testing ──────────────────────────────
  bool st_is_known_const(std::string_view name) const { return st.is_known_const(name); }
  bool st_is_reg(std::string_view name)         const { return st.is_reg(name); }
  bool st_is_input(std::string_view name)       const { return st.is_input(name); }
  bool st_is_output(std::string_view name)      const { return st.is_output(name); }
};

// Build a fresh Lnast and Lnast_manager for each test.
struct ConstpropFixture {
  std::shared_ptr<Lnast>                ln;
  std::shared_ptr<upass::Lnast_manager> lm;
  Lnast_nid                             stmts_nid;

  ConstpropFixture() {
    ln = std::make_shared<Lnast>("test");
    ln->set_root(Lnast_node::create_top("top"));
    stmts_nid = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts("stmts"));
    lm        = std::make_shared<upass::Lnast_manager>(ln);
  }

  // Build: <op_type> ref("dst") const(lhs) const(rhs)
  // Returns the nid of the operator node.
  Lnast_nid add_binary_node(Lnast_node op_node, std::string_view dst, int64_t lhs, int64_t rhs) {
    auto op = ln->add_child(stmts_nid, op_node);
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_const(lhs));
    ln->add_child(op, Lnast_node::create_const(rhs));
    return op;
  }

  // Build: <op_type> ref("dst") const(operand)
  Lnast_nid add_unary_node(Lnast_node op_node, std::string_view dst, int64_t operand) {
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
  Lnast_nid add_if_node(std::string_view cond_var, std::function<void(Lnast_nid)> build_then,
                        std::function<void(Lnast_nid)> build_else = nullptr) {
    auto if_nid = ln->add_child(stmts_nid, Lnast_node::create_if());
    ln->add_child(if_nid, Lnast_node::create_ref(cond_var));
    auto then_stmts = ln->add_child(if_nid, Lnast_node::create_stmts());
    if (build_then) {
      build_then(then_stmts);
    }
    if (build_else) {
      auto else_stmts = ln->add_child(if_nid, Lnast_node::create_stmts());
      build_else(else_stmts);
    }
    return if_nid;
  }

  // Convenience: add a binary op node under an arbitrary parent stmts nid.
  Lnast_nid add_binary_under(Lnast_nid parent_stmts, Lnast_node op_node, std::string_view dst, int64_t lhs, int64_t rhs) {
    auto op = ln->add_child(parent_stmts, op_node);
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_const(lhs));
    ln->add_child(op, Lnast_node::create_const(rhs));
    return op;
  }

  // Build: [tuple_add: ref(dst), const(vals[0]), const(vals[1]), ...]
  // Each entry is a positional constant field.
  Lnast_nid add_tuple_add_node(std::string_view dst, std::initializer_list<int64_t> vals) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_tuple_add());
    ln->add_child(op, Lnast_node::create_ref(dst));
    for (auto v : vals) {
      ln->add_child(op, Lnast_node::create_const(v));
    }
    return op;
  }

  // Build: [tuple_get: ref(dst), ref(src), const(field)]
  // field is a string like "0", "1", "foo".
  Lnast_nid add_tuple_get_node(std::string_view dst, std::string_view src, std::string_view field) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_tuple_get());
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_ref(src));
    ln->add_child(op, Lnast_node::create_const(field));
    return op;
  }

  // Build: [tuple_get: ref(dst), ref(src), ref(idx_var)]
  // idx_var is a runtime variable (not a compile-time const).
  Lnast_nid add_tuple_get_ref_idx_node(std::string_view dst, std::string_view src, std::string_view idx_var) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_tuple_get());
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_ref(src));
    ln->add_child(op, Lnast_node::create_ref(idx_var));
    return op;
  }

  // Build: [tuple_set: ref(tuple_var), const(field), const(value)]
  // field is a string key; value is a compile-time integer.
  Lnast_nid add_tuple_set_node(std::string_view tuple_var, std::string_view field, int64_t value) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_tuple_set());
    ln->add_child(op, Lnast_node::create_ref(tuple_var));
    ln->add_child(op, Lnast_node::create_const(field));
    ln->add_child(op, Lnast_node::create_const(value));
    return op;
  }

  // Build: [attr_set: ref(tuple_var), const(attr_name), const(value)]
  Lnast_nid add_attr_set_node(std::string_view tuple_var,
                               std::string_view attr_name,
                               int64_t          value) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_attr_set());
    ln->add_child(op, Lnast_node::create_ref(tuple_var));
    ln->add_child(op, Lnast_node::create_const(attr_name));
    ln->add_child(op, Lnast_node::create_const(value));
    return op;
  }

  // Build: [attr_get: ref(dst), ref(tuple_var), const(attr_name)]
  Lnast_nid add_attr_get_node(std::string_view dst,
                               std::string_view tuple_var,
                               std::string_view attr_name) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_attr_get());
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_ref(tuple_var));
    ln->add_child(op, Lnast_node::create_const(attr_name));
    return op;
  }

  // Build: [tuple_concat: ref(dst), ref(lhs), ref(rhs)]
  Lnast_nid add_tuple_concat_node(std::string_view dst,
                                   std::string_view lhs,
                                   std::string_view rhs) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_tuple_concat());
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_ref(lhs));
    ln->add_child(op, Lnast_node::create_ref(rhs));
    return op;
  }

  // Build: [delay_assign: ref(dst), ref(src), const(offset)]
  Lnast_nid add_delay_assign_node(std::string_view dst,
                                   std::string_view src,
                                   int64_t          offset) {
    auto op = ln->add_child(stmts_nid, Lnast_node::create_delay_assign());
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_ref(src));
    ln->add_child(op, Lnast_node::create_const(offset));
    return op;
  }
};

// ── Arithmetic ──────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsPlus) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_plus(), "a", 2, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_plus();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 5);
}

TEST(UpassConstprop, FoldsMinus) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_minus(), "a", 10, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_minus();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 7);
}

TEST(UpassConstprop, FoldsMult) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_mult(), "a", 3, 4);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_mult();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 12);
}

TEST(UpassConstprop, FoldsDiv) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_div(), "a", 8, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_div();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 4);
}

TEST(UpassConstprop, FoldsMod) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_mod(), "a", 7, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_mod();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

// ── Shift ────────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsShl) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_shl(), "a", 1, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_shl();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 8);  // 1 << 3 = 8
}

TEST(UpassConstprop, FoldsSra) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_sra(), "a", 16, 2);
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
  auto              op = f.add_binary_node(Lnast_node::create_bit_and(), "a", 0b1010, 0b1100);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0b1000);
}

TEST(UpassConstprop, FoldsBitOr) {
  ConstpropFixture f;
  // 0b1010 | 0b0101 = 0b1111 = 15
  auto              op = f.add_binary_node(Lnast_node::create_bit_or(), "a", 0b1010, 0b0101);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0b1111);
}

TEST(UpassConstprop, FoldsBitXor) {
  ConstpropFixture f;
  // 0b1010 ^ 0b1100 = 0b0110 = 6
  auto              op = f.add_binary_node(Lnast_node::create_bit_xor(), "a", 0b1010, 0b1100);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_xor();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0b0110);
}

TEST(UpassConstprop, FoldsBitNot) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_node::create_bit_not(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_bit_not();
  EXPECT_TRUE(cp.has_changed());
  // ~0 = -1 in two's complement
  EXPECT_EQ(cp.get_result("a").to_i(), -1);
}

TEST(UpassConstprop, FoldsGetMask) {
  ConstpropFixture f;
  // 0x12345678#[0..=7] = 0x78
  auto              op = f.add_binary_node(Lnast_node::create_get_mask(), "a", 0x12345678, 0xff);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_get_mask();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0x78);
}

// ── Logical ──────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsLogAndBothTrue) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_log_and(), "a", 1, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

TEST(UpassConstprop, FoldsLogAndOneFalse) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_log_and(), "a", 0, 1);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);
}

TEST(UpassConstprop, FoldsLogOrOneFalse) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_log_or(), "a", 0, 1);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

TEST(UpassConstprop, FoldsLogOrBothFalse) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_log_or(), "a", 0, 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);
}

TEST(UpassConstprop, FoldsLogNotFalse) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_node::create_log_not(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_not();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);  // !0 = 1
}

TEST(UpassConstprop, FoldsLogNotTrue) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_node::create_log_not(), "a", 5);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_log_not();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);  // !5 = 0
}

// ── Convergence ──────────────────────────────────────────────────────────────

// Running the same fold a second time should not mark changed (value already in st).
TEST(UpassConstprop, SecondRunConverges) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_node::create_plus(), "a", 2, 3);
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
  auto             if_nid
      = f.add_if_node("cond", [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_node::create_plus(), "x", 1, 2); });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Lconst(1));  // condition is known-true
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 2. if-only: cond is known-false.
TEST(UpassConstpropIf, KnownFalseConditionNoElse) {
  ConstpropFixture  f;
  auto              if_nid = f.add_if_node("cond", [&](Lnast_nid then_stmts) {
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
  auto if_nid = f.add_if_node("unknown_cond",
                              [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_node::create_plus(), "y", 3, 4); });
  TestableConstprop cp(f.lm);
  // Do NOT seed "unknown_cond" — leave it absent from the symbol table.
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 4. if-else: two branches, known-true condition.
//    Layout: [if: [ref:cond], [stmts:then], [stmts:else]]
TEST(UpassConstpropIf, KnownTrueConditionWithElse) {
  ConstpropFixture f;
  auto             if_nid = f.add_if_node(
      "cond",
      [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_node::create_plus(), "r", 1, 0); },
      [&](Lnast_nid else_stmts) { f.add_binary_under(else_stmts, Lnast_node::create_plus(), "r", 0, 0); });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Lconst(1));
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 5. if-else: known-false condition.
TEST(UpassConstpropIf, KnownFalseConditionWithElse) {
  ConstpropFixture f;
  auto             if_nid = f.add_if_node(
      "cond",
      [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_node::create_plus(), "r", 5, 5); },
      [&](Lnast_nid else_stmts) { f.add_binary_under(else_stmts, Lnast_node::create_plus(), "r", 9, 9); });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Lconst(0));
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 6. Literal const as condition (if (1) { ... }) — should complete cleanly.
TEST(UpassConstpropIf, ConstConditionNoElse) {
  ConstpropFixture f;
  // Build [if: [const:1], [stmts:...]] — constant-true condition literal.
  auto if_nid = f.ln->add_child(f.stmts_nid, Lnast_node::create_if());
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
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_node::create_red_or(), "a", 5);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_or();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);  // 5 != 0 → 1
}

TEST(UpassConstprop, RedOrZero) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_node::create_red_or(), "a", 0);
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
  auto             red_nid = f.ln->add_child(f.stmts_nid, Lnast_node::create_red_or());
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
  auto              op = f.add_unary_node(Lnast_node::create_red_and(), "a", 0b111);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_and();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

TEST(UpassConstprop, RedAndNotAllOnes) {
  ConstpropFixture f;
  // 0b101 = 5: bit 1 is clear → red_and = 0.
  auto              op = f.add_unary_node(Lnast_node::create_red_and(), "a", 0b101);
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
  auto              op = f.add_unary_node(Lnast_node::create_red_xor(), "a", 0b111);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_xor();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 1);  // popcount(7) = 3, odd → 1
}

TEST(UpassConstprop, RedXorEvenParity) {
  ConstpropFixture f;
  // 0b1010 = 10: popcount = 2 (even) → red_xor = 0.
  auto              op = f.add_unary_node(Lnast_node::create_red_xor(), "a", 0b1010);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_red_xor();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), 0);  // popcount(10) = 2, even → 0
}

// ── sext ─────────────────────────────────────────────────────────────────────

// Fixture helper: [sext: ref(dst), const_or_ref(src), const(nbits)]
// Since add_binary_node uses two int64_t literals, we build this manually.
static Lnast_nid add_sext_node(ConstpropFixture& f, std::string_view dst, int64_t src_val, int64_t nbits) {
  auto op = f.ln->add_child(f.stmts_nid, Lnast_node::create_sext());
  f.ln->add_child(op, Lnast_node::create_ref(dst));
  f.ln->add_child(op, Lnast_node::create_const(src_val));
  f.ln->add_child(op, Lnast_node::create_const(nbits));
  return op;
}

TEST(UpassConstprop, SextNoTruncation) {
  ConstpropFixture f;
  // src = 3 (0b011), ebits = 10 → ebits >= bits, so no change: result = 3.
  auto              op = add_sext_node(f, "a", 3, 10);
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
  auto              op = add_sext_node(f, "a", 4, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.process_sext();
  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("a").to_i(), -4);  // 3-bit 0b100 sign-extended = -4
}

// ── Tuple operations ──────────────────────────────────────────────────────────
//
// Tests for process_tuple_add / process_tuple_get / process_tuple_set.
// Each helper builds nodes directly into the fixture's LNAST; no runner is
// involved so we test the pass methods in isolation.

// tuple_add + tuple_get: first positional field (index 0).
TEST(UpassConstpropTuple, TupleAddAndGetFirstField) {
  ConstpropFixture f;
  auto             add_op = f.add_tuple_add_node("___t0", {3, 7});
  auto             get_op = f.add_tuple_get_node("dst", "___t0", "0");

  TestableConstprop cp(f.lm);
  cp.position(add_op);
  cp.process_tuple_add();  // ___t0 = (3, 7)

  cp.position(get_op);
  cp.process_tuple_get();  // dst = ___t0[0] = 3

  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("dst").to_i(), 3);
}

// tuple_add + tuple_get: second positional field (index 1).
TEST(UpassConstpropTuple, TupleAddAndGetSecondField) {
  ConstpropFixture f;
  auto             add_op = f.add_tuple_add_node("___t0", {3, 7});
  auto             get_op = f.add_tuple_get_node("dst", "___t0", "1");

  TestableConstprop cp(f.lm);
  cp.position(add_op);
  cp.process_tuple_add();

  cp.position(get_op);
  cp.process_tuple_get();  // dst = ___t0[1] = 7

  EXPECT_TRUE(cp.has_changed());
  EXPECT_EQ(cp.get_result("dst").to_i(), 7);
}

// tuple_get on a source variable not in the symbol table: no store, no mark_changed.
TEST(UpassConstpropTuple, TupleGetMissingSourceNoStore) {
  ConstpropFixture f;
  auto             get_op = f.add_tuple_get_node("dst", "undefined_tuple", "0");

  TestableConstprop cp(f.lm);
  cp.position(get_op);
  cp.process_tuple_get();

  EXPECT_TRUE(cp.get_result("dst").is_invalid());
  EXPECT_FALSE(cp.has_changed());
}

// tuple_get with a runtime ref index not in the symbol table: can't fold → no store.
TEST(UpassConstpropTuple, TupleGetUnknownRuntimeIndexNoStore) {
  ConstpropFixture f;
  auto             add_op = f.add_tuple_add_node("___t0", {42});
  auto             get_op = f.add_tuple_get_ref_idx_node("dst", "___t0", "runtime_idx");

  TestableConstprop cp(f.lm);
  cp.position(add_op);
  cp.process_tuple_add();  // ___t0 = (42) — bundle populated

  cp.begin_iteration();  // reset changed flag
  cp.position(get_op);
  cp.process_tuple_get();  // runtime_idx not in st → cannot fold

  EXPECT_TRUE(cp.get_result("dst").is_invalid());
  EXPECT_FALSE(cp.has_changed());
}

// Second run of tuple_add + tuple_get must not fire mark_changed (convergence).
TEST(UpassConstpropTuple, TupleGetConvergesOnRepeat) {
  ConstpropFixture f;
  auto             add_op = f.add_tuple_add_node("___t0", {5});
  auto             get_op = f.add_tuple_get_node("dst", "___t0", "0");

  TestableConstprop cp(f.lm);
  cp.position(add_op);
  cp.process_tuple_add();

  cp.position(get_op);
  cp.process_tuple_get();
  ASSERT_TRUE(cp.has_changed());
  ASSERT_EQ(cp.get_result("dst").to_i(), 5);

  // Second iteration: same nodes, same values → no change.
  cp.begin_iteration();
  cp.position(add_op);
  cp.process_tuple_add();
  cp.position(get_op);
  cp.process_tuple_get();
  EXPECT_FALSE(cp.has_changed());
}

// tuple_set writes a scalar value into the bundle and marks changed.
TEST(UpassConstpropTuple, TupleSetWritesField) {
  ConstpropFixture f;
  auto             set_op = f.add_tuple_set_node("___t0", "0", 99);

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_tuple_set();

  EXPECT_TRUE(cp.has_changed());
  // The symbol table key "___t0.0" should now hold 99.
  EXPECT_EQ(cp.get_result("___t0.0").to_i(), 99);
}

// tuple_set with a __bits attribute field must be silently skipped (no mark_changed).
// Attribute nodes begin with "__" and must not trigger infinite convergence loops.
TEST(UpassConstpropTuple, TupleSetAttributeFieldSkipped) {
  ConstpropFixture f;
  auto             set_op = f.add_tuple_set_node("x", "__bits", 8);

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_tuple_set();

  EXPECT_FALSE(cp.has_changed());
}

// Second run of tuple_set with an identical value must not fire mark_changed.
TEST(UpassConstpropTuple, TupleSetConvergesOnRepeat) {
  ConstpropFixture f;
  auto             set_op = f.add_tuple_set_node("___t0", "0", 42);

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_tuple_set();
  ASSERT_TRUE(cp.has_changed());

  cp.begin_iteration();
  cp.position(set_op);
  cp.process_tuple_set();
  EXPECT_FALSE(cp.has_changed());  // value unchanged → no mark_changed
}

// tuple_set followed by tuple_get propagates the written value end-to-end.
TEST(UpassConstpropTuple, TupleSetThenGetRoundTrip) {
  ConstpropFixture f;
  auto             set_op = f.add_tuple_set_node("___t0", "0", 77);
  auto             get_op = f.add_tuple_get_node("dst", "___t0", "0");

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_tuple_set();  // ___t0[0] = 77

  cp.position(get_op);
  cp.process_tuple_get();  // dst = ___t0[0] = 77

  EXPECT_EQ(cp.get_result("dst").to_i(), 77);
}

// ── Attribute Operations ────────────────────────────────────────────────────

// AttrSetAndGet was removed: after the upstream runner rewrite, process_attr_set
// and process_attr_get are handled by the runner's A_OP/C_OP dispatch macros and
// are no longer custom-implemented in constprop (Slice 1 copies attr_set verbatim
// per upass.md §3). Testing round-trip storage here would test behavior constprop
// no longer owns.

TEST(UpassConstpropAttr, AttrSetBitsSkipped) {
  // __bits is a Pyrope bitwidth annotation — constprop must skip it
  // to avoid non-convergence (same guard as tuple_set).
  ConstpropFixture f;
  auto set_op = f.add_attr_set_node("x", "__bits", 8);

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_attr_set();

  // x.__bits must NOT be stored in the symbol table.
  EXPECT_FALSE(cp.has_changed());
}

// AttrSetConvergesOnRepeat was removed: same reason as AttrSetAndGet above —
// process_attr_set is a runner-level no-op in constprop; mark_changed() is
// never called, so testing its convergence behaviour here is meaningless.

TEST(UpassConstpropAttr, AttrGetMissingSourceNoStore) {
  // attr_get on a variable with no stored attribute leaves dst unknown.
  ConstpropFixture f;
  auto get_op = f.add_attr_get_node("dst", "undefined_var", "myattr");

  TestableConstprop cp(f.lm);
  cp.position(get_op);
  cp.process_attr_get();

  EXPECT_TRUE(cp.get_result("dst").is_invalid());
  EXPECT_FALSE(cp.has_changed());
}

// ── Tuple Concat ────────────────────────────────────────────────────────────

TEST(UpassConstpropTuple, TupleConcatMergesTwoTuples) {
  // dst = lhs ++ rhs  where lhs=(3,7) and rhs=(11)
  // After concat + get, dst[0]=3, dst[1]=7, dst[2]=11.
  ConstpropFixture f;
  auto lhs_op    = f.add_tuple_add_node("lhs", {3, 7});
  auto rhs_op    = f.add_tuple_add_node("rhs", {11});
  auto concat_op = f.add_tuple_concat_node("dst", "lhs", "rhs");
  auto get0_op   = f.add_tuple_get_node("r0", "dst", "0");
  auto get1_op   = f.add_tuple_get_node("r1", "dst", "1");
  auto get2_op   = f.add_tuple_get_node("r2", "dst", "2");

  TestableConstprop cp(f.lm);
  cp.position(lhs_op);    cp.process_tuple_add();
  cp.position(rhs_op);    cp.process_tuple_add();
  cp.position(concat_op); cp.process_tuple_concat();
  cp.position(get0_op);   cp.process_tuple_get();
  cp.position(get1_op);   cp.process_tuple_get();
  cp.position(get2_op);   cp.process_tuple_get();

  EXPECT_EQ(cp.get_result("r0").to_i(), 3);
  EXPECT_EQ(cp.get_result("r1").to_i(), 7);
  EXPECT_EQ(cp.get_result("r2").to_i(), 11);
}

TEST(UpassConstpropTuple, TupleConcatUnknownLhsNoStore) {
  // If lhs bundle is unknown, dst must stay unknown (soundness).
  ConstpropFixture f;
  auto rhs_op    = f.add_tuple_add_node("rhs", {5});
  auto concat_op = f.add_tuple_concat_node("dst", "unknown_lhs", "rhs");

  TestableConstprop cp(f.lm);
  cp.position(rhs_op);    cp.process_tuple_add();
  cp.position(concat_op); cp.process_tuple_concat();

  EXPECT_FALSE(cp.has_changed());
}

// ── Delay Assign ────────────────────────────────────────────────────────────

TEST(UpassConstpropDelay, DelayAssignLeavesDestUnknown) {
  // delay_assign reads from a previous clock cycle — not statically foldable.
  // dst must remain unknown in the symbol table.
  ConstpropFixture f;
  auto delay_op = f.add_delay_assign_node("dst", "src", 1);

  TestableConstprop cp(f.lm);
  cp.seed("src", Lconst(99));   // src is known — but dst still can't be folded
  cp.position(delay_op);
  cp.process_delay_assign();

  EXPECT_TRUE(cp.get_result("dst").is_invalid());
  EXPECT_FALSE(cp.has_changed());
}

// ── §5.a Symbol_table query helpers ─────────────────────────────────────────
//
// These tests exercise the four helpers added in Slice 1:
//   is_known_const, is_reg, is_input, is_output
// They are deliberately lightweight: the helpers are thin wrappers over
// existing ST primitives and prefix checks, so the test surface is small.

TEST(UpassConstpropST, IsKnownConstWithValidValue) {
  // A concrete, no-unknown Lconst stored in the ST → is_known_const is true.
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);
  cp.seed("x", Lconst(42));
  EXPECT_TRUE(cp.st_is_known_const("x"));
}

TEST(UpassConstpropST, IsKnownConstAbsentReturnsFalse) {
  // Nothing stored for "x" → is_known_const returns false.
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);
  EXPECT_FALSE(cp.st_is_known_const("x"));
}

TEST(UpassConstpropST, IsKnownConstInvalidLconstReturnsFalse) {
  // Lconst::invalid() (the "not-yet-known" sentinel) → is_known_const false.
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);
  cp.seed("x", Lconst::invalid());
  EXPECT_FALSE(cp.st_is_known_const("x"));
}

TEST(UpassConstpropST, IsRegDetectsHashPrefix) {
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);
  EXPECT_TRUE(cp.st_is_reg("#clk"));
  EXPECT_FALSE(cp.st_is_reg("clk"));
  EXPECT_FALSE(cp.st_is_reg("$inp"));
  EXPECT_FALSE(cp.st_is_reg(""));
}

TEST(UpassConstpropST, IsInputDetectsDollarPrefix) {
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);
  EXPECT_TRUE(cp.st_is_input("$data"));
  EXPECT_FALSE(cp.st_is_input("data"));
  EXPECT_FALSE(cp.st_is_input("#reg"));
  EXPECT_FALSE(cp.st_is_input(""));
}

TEST(UpassConstpropST, IsOutputDetectsPercentPrefix) {
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);
  EXPECT_TRUE(cp.st_is_output("%out"));
  EXPECT_FALSE(cp.st_is_output("out"));
  EXPECT_FALSE(cp.st_is_output("$inp"));
  EXPECT_FALSE(cp.st_is_output(""));
}

TEST(UpassConstpropST, ClassifyDropsKnownConstTemp) {
  // classify_statement should return drop() for a plain variable whose value
  // is fully known in the symbol table.
  ConstpropFixture f;
  auto assign_op = f.ln->add_child(f.stmts_nid, Lnast_node::create_assign());
  f.ln->add_child(assign_op, Lnast_node::create_ref("tmp"));
  f.ln->add_child(assign_op, Lnast_node::create_const(7));

  TestableConstprop cp(f.lm);
  cp.seed("tmp", Lconst(7));  // simulate process_assign having already run
  cp.position(assign_op);
  EXPECT_EQ(cp.classify_statement().kind, upass::Emit_kind::drop_subtree);
}

TEST(UpassConstpropST, ClassifyKeepsRegEvenIfKnownConst) {
  // classify_statement must never drop a register assignment — regs carry
  // timing semantics that make the assignment itself observable.
  ConstpropFixture f;
  auto assign_op = f.ln->add_child(f.stmts_nid, Lnast_node::create_assign());
  f.ln->add_child(assign_op, Lnast_node::create_ref("#reg"));
  f.ln->add_child(assign_op, Lnast_node::create_const(3));

  TestableConstprop cp(f.lm);
  cp.seed("#reg", Lconst(3));
  cp.position(assign_op);
  EXPECT_EQ(cp.classify_statement().kind, upass::Emit_kind::emit);
}

}  // namespace
