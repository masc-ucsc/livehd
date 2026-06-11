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
  // 2b/A — the symbol table is runner-owned; this standalone harness has no
  // runner, so it owns a table and wires it the way the runner would
  // (including the per-tree function scope the runner pushes at run()).
  explicit TestableConstprop(std::shared_ptr<upass::Lnast_manager>& _lm) : uPass_constprop(_lm) {
    set_runner_symbol_table(&own_st_);
    own_st_.function_scope(_lm->get_top_module_name());
  }
  Symbol_table own_st_;

  void position(const Lnast_nid& nid) { move_to_nid(nid); }

  Const get_result(std::string_view name) const {
    if (!st().has_trivial(name)) {
      return *Dlop::invalid();
    }
    return st().get_trivial(name);
  }

  // Pre-populate the symbol table so process_if() can look up conditions.
  void seed(std::string_view name, const Const& val) { st().set(name, val); }
  void seed(std::string_view name, const spool_ptr<Dlop>& val) { st().set(name, *val); }

  // Expose §5.a ST query helpers for testing.
  bool st_is_known_const(std::string_view name) const { return st().is_known_const(name); }

  // Directly write a trivial value into the symbol table (for ST tests).
  void st_set(std::string_view name, const Const& val) { st().set(name, val); }
  void st_set(std::string_view name, const spool_ptr<Dlop>& val) { st().set(name, *val); }

  // Expose classify_statement_impl() for white-box testing.
  upass::Emit_decision classify() { return classify_statement_impl(); }

  // 2b/E4 — drive a PUSH-form hook from the cursor the way the runner's
  // resolve_node_operands would: first ref child = dst name (live bundle or
  // throwaway), remaining children = operands (const → make_const value,
  // ref → table bundle or empty view).
  template <typename M>
  void push_from_cursor(M method) {
    const auto here = lm->save_cursor();
    if (!lm->has_child()) {
      return;
    }
    lm->move_to_child();
    std::string dst{lm->current_text()};
    std::shared_ptr<Bundle>     dstb = st().get_bundle_for_write(dst);
    if (!dstb) {
      dstb = std::make_shared<Bundle>(dst);
    }
    std::vector<upass::Operand> operands;
    std::vector<std::string>    names;  // keep string_views alive
    names.reserve(8);
    while (lm->move_to_sibling()) {
      if (Lnast_ntype::is_const(lm->get_raw_ntype())) {
        Const v = *Dlop::from_pyrope(lm->current_text());
        operands.push_back(upass::Operand{std::string_view{}, Bundle::make_const(v, upass::Kind::unknown), false});
      } else {
        names.emplace_back(lm->current_text());
        auto b = st().get_bundle(names.back());
        if (!b) {
          b = std::make_shared<Bundle>(names.back());
        }
        operands.push_back(upass::Operand{names.back(), std::move(b), false});
      }
    }
    lm->restore_cursor(here);
    (this->*method)(dst, *dstb, upass::Src_span{operands});
  }
};

// Build a fresh Lnast and Lnast_manager for each test.
struct ConstpropFixture {
  std::shared_ptr<Lnast>                ln;
  std::shared_ptr<upass::Lnast_manager> lm;
  Lnast_nid                             stmts_nid;

  ConstpropFixture() {
    ln             = std::make_shared<Lnast>("test");
    auto root_nid_ = ln->set_root(Lnast_ntype::create_top());
    stmts_nid      = ln->add_child(root_nid_, Lnast_ntype::create_stmts());
    lm             = std::make_shared<upass::Lnast_manager>(ln);
  }

  // Build: <op_type> ref("dst") const(lhs) const(rhs)
  // Returns the nid of the operator node.
  Lnast_nid add_binary_node(Lnast_ntype::Lnast_ntype_int op_type, std::string_view dst, int64_t lhs, int64_t rhs) {
    auto op = ln->add_child(stmts_nid, op_type);
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_const(lhs));
    ln->add_child(op, Lnast_node::create_const(rhs));
    return op;
  }

  // Build: <op_type> ref("dst") const(operand)
  Lnast_nid add_unary_node(Lnast_ntype::Lnast_ntype_int op_type, std::string_view dst, int64_t operand) {
    auto op = ln->add_child(stmts_nid, op_type);
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
    auto if_nid = ln->add_child(stmts_nid, Lnast_ntype::create_if());
    ln->add_child(if_nid, Lnast_node::create_ref(cond_var));
    auto then_stmts = ln->add_child(if_nid, Lnast_ntype::create_stmts());
    if (build_then) {
      build_then(then_stmts);
    }
    if (build_else) {
      auto else_stmts = ln->add_child(if_nid, Lnast_ntype::create_stmts());
      build_else(else_stmts);
    }
    return if_nid;
  }

  // Convenience: add a binary op node under an arbitrary parent stmts nid.
  Lnast_nid add_binary_under(Lnast_nid parent_stmts, Lnast_ntype::Lnast_ntype_int op_type, std::string_view dst, int64_t lhs,
                             int64_t rhs) {
    auto op = ln->add_child(parent_stmts, op_type);
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_const(lhs));
    ln->add_child(op, Lnast_node::create_const(rhs));
    return op;
  }

  // Build: [tuple_add: ref(dst), const(vals[0]), const(vals[1]), ...]
  // Each entry is a positional constant field.
  Lnast_nid add_tuple_add_node(std::string_view dst, std::initializer_list<int64_t> vals) {
    auto op = ln->add_child(stmts_nid, Lnast_ntype::create_tuple_add());
    ln->add_child(op, Lnast_node::create_ref(dst));
    for (auto v : vals) {
      ln->add_child(op, Lnast_node::create_const(v));
    }
    return op;
  }

  // Build: [tuple_get: ref(dst), ref(src), const(field)]
  // field is a string like "0", "1", "foo".
  Lnast_nid add_tuple_get_node(std::string_view dst, std::string_view src, std::string_view field) {
    auto op = ln->add_child(stmts_nid, Lnast_ntype::create_tuple_get());
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_ref(src));
    ln->add_child(op, Lnast_node::create_const(field));
    return op;
  }

  // Build: [tuple_get: ref(dst), ref(src), ref(idx_var)]
  // idx_var is a runtime variable (not a compile-time const).
  Lnast_nid add_tuple_get_ref_idx_node(std::string_view dst, std::string_view src, std::string_view idx_var) {
    auto op = ln->add_child(stmts_nid, Lnast_ntype::create_tuple_get());
    ln->add_child(op, Lnast_node::create_ref(dst));
    ln->add_child(op, Lnast_node::create_ref(src));
    ln->add_child(op, Lnast_node::create_ref(idx_var));
    return op;
  }

  // Build: [tuple_set: ref(tuple_var), const(field), const(value)]
  // field is a string key; value is a compile-time integer.
  Lnast_nid add_tuple_set_node(std::string_view tuple_var, std::string_view field, int64_t value) {
    auto op = ln->add_child(stmts_nid, Lnast_ntype::create_store());
    ln->add_child(op, Lnast_node::create_ref(tuple_var));
    ln->add_child(op, Lnast_node::create_const(field));
    ln->add_child(op, Lnast_node::create_const(value));
    return op;
  }
};

// ── Arithmetic ──────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsPlus) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_plus(), "a", 2, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_plus);
  EXPECT_EQ(cp.get_result("a").to_i(), 5);
}

TEST(UpassConstprop, FoldsMinus) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_minus(), "a", 10, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_minus);
  EXPECT_EQ(cp.get_result("a").to_i(), 7);
}

TEST(UpassConstprop, FoldsMult) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_mult(), "a", 3, 4);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_mult);
  EXPECT_EQ(cp.get_result("a").to_i(), 12);
}

TEST(UpassConstprop, FoldsDiv) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_div(), "a", 8, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_div);
  EXPECT_EQ(cp.get_result("a").to_i(), 4);
}

TEST(UpassConstprop, FoldsMod) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_mod(), "a", 7, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_mod);
  EXPECT_EQ(cp.get_result("a").to_i(), 1);
}

// ── Shift ────────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsShl) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_shl(), "a", 1, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_shl);
  EXPECT_EQ(cp.get_result("a").to_i(), 8);  // 1 << 3 = 8
}

TEST(UpassConstprop, FoldsSra) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_sra(), "a", 16, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_sra);
  EXPECT_EQ(cp.get_result("a").to_i(), 4);  // 16 >> 2 = 4
}

// ── Bitwise ──────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsBitAnd) {
  ConstpropFixture  f;
  // 0b1010 & 0b1100 = 0b1000 = 8
  auto              op = f.add_binary_node(Lnast_ntype::create_bit_and(), "a", 0b1010, 0b1100);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_bit_and);
  EXPECT_EQ(cp.get_result("a").to_i(), 0b1000);
}

TEST(UpassConstprop, FoldsBitOr) {
  ConstpropFixture  f;
  // 0b1010 | 0b0101 = 0b1111 = 15
  auto              op = f.add_binary_node(Lnast_ntype::create_bit_or(), "a", 0b1010, 0b0101);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_bit_or);
  EXPECT_EQ(cp.get_result("a").to_i(), 0b1111);
}

TEST(UpassConstprop, FoldsBitXor) {
  ConstpropFixture  f;
  // 0b1010 ^ 0b1100 = 0b0110 = 6
  auto              op = f.add_binary_node(Lnast_ntype::create_bit_xor(), "a", 0b1010, 0b1100);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_bit_xor);
  EXPECT_EQ(cp.get_result("a").to_i(), 0b0110);
}

TEST(UpassConstprop, FoldsBitNot) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_ntype::create_bit_not(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_bit_not);
  // ~0 = -1 in two's complement
  EXPECT_EQ(cp.get_result("a").to_i(), -1);
}

TEST(UpassConstprop, FoldsGetMask) {
  ConstpropFixture  f;
  // 0x12345678#[0..=7] = 0x78
  auto              op = f.add_binary_node(Lnast_ntype::create_get_mask(), "a", 0x12345678, 0xff);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_get_mask);
  EXPECT_EQ(cp.get_result("a").to_i(), 0x78);
}

// ── Logical ──────────────────────────────────────────────────────────────────

TEST(UpassConstprop, FoldsLogAndBothTrue) {
  // log_and operands must be bool per Pyrope's type rule (`1 and 2` is a
  // type error; source must write `1 != 0 and 2 != 0`). Feed bool literals.
  ConstpropFixture f;
  auto             op = f.ln->add_child(f.stmts_nid, Lnast_ntype::create_log_and());
  f.ln->add_child(op, Lnast_node::create_ref("a"));
  f.ln->add_child(op, Lnast_node::create_const("true"));
  f.ln->add_child(op, Lnast_node::create_const("true"));

  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_log_and);
  EXPECT_TRUE(cp.get_result("a").is_known_true());
}

TEST(UpassConstprop, FoldsLogAndOneFalse) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_log_and(), "a", 0, 1);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_log_and);
  EXPECT_TRUE(cp.get_result("a").is_known_false());
}

TEST(UpassConstprop, FoldsLogOrOneFalse) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_log_or(), "a", 0, 1);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_log_or);
  EXPECT_TRUE(cp.get_result("a").is_known_true());
}

TEST(UpassConstprop, FoldsLogOrBothFalse) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_log_or(), "a", 0, 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_log_or);
  EXPECT_TRUE(cp.get_result("a").is_known_false());
}

TEST(UpassConstprop, FoldsLogNotFalse) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_ntype::create_log_not(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_log_not);
  EXPECT_TRUE(cp.get_result("a").is_known_true());  // !0 = true
}

TEST(UpassConstprop, FoldsLogNotTrue) {
  // log_not operand must be bool per Pyrope's type rule.
  ConstpropFixture f;
  auto             op = f.ln->add_child(f.stmts_nid, Lnast_ntype::create_log_not());
  f.ln->add_child(op, Lnast_node::create_ref("a"));
  f.ln->add_child(op, Lnast_node::create_const("true"));

  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_log_not);
  EXPECT_TRUE(cp.get_result("a").is_known_false());  // !true = false
}

// ── Convergence ──────────────────────────────────────────────────────────────

// Re-running the same fold is idempotent: the value already in the symbol
// table is recomputed to the same constant.
TEST(UpassConstprop, SecondRunConverges) {
  ConstpropFixture  f;
  auto              op = f.add_binary_node(Lnast_ntype::create_plus(), "a", 2, 3);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_plus);
  ASSERT_EQ(cp.get_result("a").to_i(), 5);

  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_plus);
  EXPECT_EQ(cp.get_result("a").to_i(), 5);
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
      = f.add_if_node("cond", [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_ntype::create_plus(), "x", 1, 2); });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Dlop::create_integer(1));  // condition is known-true
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 2. if-only: cond is known-false.
TEST(UpassConstpropIf, KnownFalseConditionNoElse) {
  ConstpropFixture  f;
  auto              if_nid = f.add_if_node("cond", [&](Lnast_nid then_stmts) {
    f.add_binary_under(then_stmts, Lnast_ntype::create_plus(), "x", 10, 20);
  });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Dlop::create_integer(0));  // condition is known-false
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 3. if-only: condition variable not in symbol table (unknown).
TEST(UpassConstpropIf, UnknownConditionNoElse) {
  ConstpropFixture f;
  auto if_nid = f.add_if_node("unknown_cond",
                              [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_ntype::create_plus(), "y", 3, 4); });
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
      [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_ntype::create_plus(), "r", 1, 0); },
      [&](Lnast_nid else_stmts) { f.add_binary_under(else_stmts, Lnast_ntype::create_plus(), "r", 0, 0); });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Dlop::create_integer(1));
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 5. if-else: known-false condition.
TEST(UpassConstpropIf, KnownFalseConditionWithElse) {
  ConstpropFixture f;
  auto             if_nid = f.add_if_node(
      "cond",
      [&](Lnast_nid then_stmts) { f.add_binary_under(then_stmts, Lnast_ntype::create_plus(), "r", 5, 5); },
      [&](Lnast_nid else_stmts) { f.add_binary_under(else_stmts, Lnast_ntype::create_plus(), "r", 9, 9); });
  TestableConstprop cp(f.lm);
  cp.seed("cond", Dlop::create_integer(0));
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// 6. Literal const as condition (if (1) { ... }) — should complete cleanly.
TEST(UpassConstpropIf, ConstConditionNoElse) {
  ConstpropFixture f;
  // Build [if: [const:1], [stmts:...]] — constant-true condition literal.
  auto             if_nid = f.ln->add_child(f.stmts_nid, Lnast_ntype::create_if());
  f.ln->add_child(if_nid, Lnast_node::create_const(1));
  auto then_stmts = f.ln->add_child(if_nid, Lnast_ntype::create_stmts());
  f.add_binary_under(then_stmts, Lnast_ntype::create_plus(), "z", 7, 8);
  TestableConstprop cp(f.lm);
  cp.position(if_nid);
  EXPECT_NO_THROW(cp.process_if());
}

// ── Reduce ops ───────────────────────────────────────────────────────────────

// red_or: 1 if any bit set, 0 if zero.
TEST(UpassConstprop, RedOrNonZero) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_ntype::create_red_or(), "a", 5);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_red_or);
  EXPECT_TRUE(cp.get_result("a").is_known_true());  // 5 != 0 → true
}

TEST(UpassConstprop, RedOrZero) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_ntype::create_red_or(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_red_or);
  EXPECT_TRUE(cp.get_result("a").is_known_false());  // 0 == 0 → false
}

TEST(UpassConstprop, RedOrUnknownInputNoStore) {
  // Input is an unknown ref — result should NOT be stored.
  // Build: [red_or: ref("b"), ref("unknown_x")]
  ConstpropFixture f;
  auto             red_nid = f.ln->add_child(f.stmts_nid, Lnast_ntype::create_red_or());
  f.ln->add_child(red_nid, Lnast_node::create_ref("b"));
  f.ln->add_child(red_nid, Lnast_node::create_ref("unknown_x"));
  TestableConstprop cp(f.lm);
  cp.position(red_nid);
  cp.push_from_cursor(&uPass_constprop::process_red_or);
  // "b" must not appear in the symbol table.
  EXPECT_TRUE(cp.get_result("b").is_invalid());
}

// red_and: 1 only when all bits are 1 (value is all-ones mask).
TEST(UpassConstprop, RedAndAllOnes) {
  ConstpropFixture  f;
  // 0b111 = 7: all bits set → red_and = true.
  auto              op = f.add_unary_node(Lnast_ntype::create_red_and(), "a", 0b111);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_red_and);
  EXPECT_TRUE(cp.get_result("a").is_known_true());
}

TEST(UpassConstprop, RedAndNotAllOnes) {
  ConstpropFixture  f;
  // 0b101 = 5: bit 1 is clear → red_and = false.
  auto              op = f.add_unary_node(Lnast_ntype::create_red_and(), "a", 0b101);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_red_and);
  EXPECT_TRUE(cp.get_result("a").is_known_false());
}

// red_xor: parity of set bits (true if popcount is odd).
TEST(UpassConstprop, RedXorOddParity) {
  ConstpropFixture  f;
  // 0b101 = 5: popcount = 2 (even) → red_xor = false
  // Let's use 0b111 = 7: popcount = 3 (odd) → red_xor = true.
  auto              op = f.add_unary_node(Lnast_ntype::create_red_xor(), "a", 0b111);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_red_xor);
  EXPECT_TRUE(cp.get_result("a").is_known_true());  // popcount(7) = 3, odd → true
}

TEST(UpassConstprop, RedXorEvenParity) {
  ConstpropFixture  f;
  // 0b1010 = 10: popcount = 2 (even) → red_xor = false.
  auto              op = f.add_unary_node(Lnast_ntype::create_red_xor(), "a", 0b1010);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_red_xor);
  EXPECT_TRUE(cp.get_result("a").is_known_false());  // popcount(10) = 2, even → false
}

// popcount: number of set bits, as an integer (not boolean like the reductions).
TEST(UpassConstprop, PopcountSetBits) {
  ConstpropFixture  f;
  // 0b1010_0100 = 0xa4: bits 2, 5, 7 set → popcount = 3.
  auto              op = f.add_unary_node(Lnast_ntype::create_popcount(), "a", 0xa4);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_popcount);
  EXPECT_EQ(cp.get_result("a").to_i(), 3);
}

TEST(UpassConstprop, PopcountZero) {
  ConstpropFixture  f;
  auto              op = f.add_unary_node(Lnast_ntype::create_popcount(), "a", 0);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_popcount);
  EXPECT_EQ(cp.get_result("a").to_i(), 0);
}

// ── sext ─────────────────────────────────────────────────────────────────────

// Fixture helper: [sext: ref(dst), const_or_ref(src), const(nbits)]
// Since add_binary_node uses two int64_t literals, we build this manually.
static Lnast_nid add_sext_node(ConstpropFixture& f, std::string_view dst, int64_t src_val, int64_t nbits) {
  auto op = f.ln->add_child(f.stmts_nid, Lnast_ntype::create_sext());
  f.ln->add_child(op, Lnast_node::create_ref(dst));
  f.ln->add_child(op, Lnast_node::create_const(src_val));
  f.ln->add_child(op, Lnast_node::create_const(nbits));
  return op;
}

TEST(UpassConstprop, SextNoTruncation) {
  ConstpropFixture  f;
  // src = 3 (0b011), ebits = 10 → ebits >= bits, so no change: result = 3.
  auto              op = add_sext_node(f, "a", 3, 10);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_sext);
  EXPECT_EQ(cp.get_result("a").to_i(), 3);
}

TEST(UpassConstprop, SextSignExtendNarrows) {
  ConstpropFixture  f;
  // src = 4 (0b100, 4 bits), ebits = 2.
  // sext_op(2): tests bit 2 of 4 (0b100) = 1 → sign bit is set → negative.
  // Lower bits (0,1) of 4 are 00 → signed 3-bit 0b100 = -4.
  auto              op = add_sext_node(f, "a", 4, 2);
  TestableConstprop cp(f.lm);
  cp.position(op);
  cp.push_from_cursor(&uPass_constprop::process_sext);
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
  cp.push_from_cursor(&uPass_constprop::process_tuple_add);  // ___t0 = (3, 7)

  cp.position(get_op);
  cp.process_tuple_get();  // dst = ___t0[0] = 3

  EXPECT_EQ(cp.get_result("dst").to_i(), 3);
}

// tuple_add + tuple_get: second positional field (index 1).
TEST(UpassConstpropTuple, TupleAddAndGetSecondField) {
  ConstpropFixture f;
  auto             add_op = f.add_tuple_add_node("___t0", {3, 7});
  auto             get_op = f.add_tuple_get_node("dst", "___t0", "1");

  TestableConstprop cp(f.lm);
  cp.position(add_op);
  cp.push_from_cursor(&uPass_constprop::process_tuple_add);

  cp.position(get_op);
  cp.process_tuple_get();  // dst = ___t0[1] = 7

  EXPECT_EQ(cp.get_result("dst").to_i(), 7);
}

// tuple_get on a source variable not in the symbol table: nothing is stored.
TEST(UpassConstpropTuple, TupleGetMissingSourceNoStore) {
  ConstpropFixture f;
  auto             get_op = f.add_tuple_get_node("dst", "undefined_tuple", "0");

  TestableConstprop cp(f.lm);
  cp.position(get_op);
  cp.process_tuple_get();

  EXPECT_TRUE(cp.get_result("dst").is_invalid());
}

// tuple_get with a runtime ref index not in the symbol table: can't fold → no store.
TEST(UpassConstpropTuple, TupleGetUnknownRuntimeIndexNoStore) {
  ConstpropFixture f;
  auto             add_op = f.add_tuple_add_node("___t0", {42});
  auto             get_op = f.add_tuple_get_ref_idx_node("dst", "___t0", "runtime_idx");

  TestableConstprop cp(f.lm);
  cp.position(add_op);
  cp.push_from_cursor(&uPass_constprop::process_tuple_add);  // ___t0 = (42) — bundle populated

  cp.position(get_op);
  cp.process_tuple_get();  // runtime_idx not in st → cannot fold

  EXPECT_TRUE(cp.get_result("dst").is_invalid());
}

// Re-running tuple_add + tuple_get is idempotent: the field value is stable.
TEST(UpassConstpropTuple, TupleGetConvergesOnRepeat) {
  ConstpropFixture f;
  auto             add_op = f.add_tuple_add_node("___t0", {5});
  auto             get_op = f.add_tuple_get_node("dst", "___t0", "0");

  TestableConstprop cp(f.lm);
  cp.position(add_op);
  cp.push_from_cursor(&uPass_constprop::process_tuple_add);

  cp.position(get_op);
  cp.process_tuple_get();
  ASSERT_EQ(cp.get_result("dst").to_i(), 5);

  // Second run: same nodes, same values → same result.
  cp.position(add_op);
  cp.push_from_cursor(&uPass_constprop::process_tuple_add);
  cp.position(get_op);
  cp.process_tuple_get();
  EXPECT_EQ(cp.get_result("dst").to_i(), 5);
}

// tuple_set writes a scalar value into the bundle and marks changed.
TEST(UpassConstpropTuple, TupleSetWritesField) {
  ConstpropFixture f;
  auto             set_op = f.add_tuple_set_node("___t0", "0", 99);

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_tuple_set();

  // The symbol table key "___t0.0" should now hold 99.
  EXPECT_EQ(cp.get_result("___t0.0").to_i(), 99);
}

// tuple_set with a __bits attribute field must be silently skipped — the
// attribute field is not written into the value bundle.
TEST(UpassConstpropTuple, TupleSetAttributeFieldSkipped) {
  ConstpropFixture f;
  auto             set_op = f.add_tuple_set_node("x", "__bits", 8);

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_tuple_set();

  EXPECT_TRUE(cp.get_result("x.__bits").is_invalid());
}

// Re-running tuple_set with an identical value is idempotent.
TEST(UpassConstpropTuple, TupleSetConvergesOnRepeat) {
  ConstpropFixture f;
  auto             set_op = f.add_tuple_set_node("___t0", "0", 42);

  TestableConstprop cp(f.lm);
  cp.position(set_op);
  cp.process_tuple_set();
  ASSERT_EQ(cp.get_result("___t0.0").to_i(), 42);

  cp.position(set_op);
  cp.process_tuple_set();
  EXPECT_EQ(cp.get_result("___t0.0").to_i(), 42);
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

// ─── Symbol_table query API (§5.a) ──────────────────────────────────────────

// is_known_const returns true when the variable holds a concrete, fully-known Const.
TEST(UpassConstpropST, IsKnownConstWithValidValue) {
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);

  cp.st_set("x", Dlop::create_integer(42));
  EXPECT_TRUE(cp.st_is_known_const("x"));
}

// is_known_const returns false when the variable has never been declared.
TEST(UpassConstpropST, IsKnownConstAbsentReturnsFalse) {
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);

  EXPECT_FALSE(cp.st_is_known_const("no_such_var"));
}

// is_known_const returns false when the stored value is Dlop::invalid().
TEST(UpassConstpropST, IsKnownConstInvalidLconstReturnsFalse) {
  ConstpropFixture  f;
  TestableConstprop cp(f.lm);

  cp.st_set("y", Dlop::invalid());
  EXPECT_FALSE(cp.st_is_known_const("y"));
}

// classify_statement drops an assign whose LHS is a known-const temp variable.
TEST(UpassConstpropST, ClassifyDropsKnownConstTemp) {
  ConstpropFixture f;

  // Build:  ___tmp = 7
  auto assign_op = f.ln->add_child(f.stmts_nid, Lnast_ntype::create_store());
  f.ln->add_child(assign_op, Lnast_node::create_ref("___tmp"));
  f.ln->add_child(assign_op, Lnast_node::create_const("7"));

  TestableConstprop cp(f.lm);
  cp.st_set("___tmp", Dlop::create_integer(7));

  cp.position(assign_op);
  auto decision = cp.classify();
  EXPECT_EQ(decision.kind, upass::Emit_kind::drop_subtree);
}

}  // namespace
