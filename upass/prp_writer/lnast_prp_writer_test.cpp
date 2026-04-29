//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "gtest/gtest.h"
#include "lnast.hpp"
#include "lnast_manager.hpp"
#include "lnast_prp_writer.hpp"
#include "prp2lnast.hpp"
#include "upass_runner.hpp"

namespace {

// ── Helpers ──────────────────────────────────────────────────────────────────

// Run upass with the given pass names on `ln`, take the staging LNAST,
// and return the Pyrope 3.0 text produced by Lnast_prp_writer.
static std::string run_and_emit(std::shared_ptr<Lnast> ln,
                                const std::vector<std::string>& passes = {}) {
  auto lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, passes);
  runner.run();
  auto staging = runner.take_staging();
  if (!staging) return "";

  std::ostringstream oss;
  Lnast_prp_writer writer(oss, staging);
  writer.write_all();
  return oss.str();
}

// Build:  top → stmts → assign(lhs, rhs_const)
static std::shared_ptr<Lnast> make_assign_lnast(std::string_view lhs,
                                                 std::string_view rhs_val,
                                                 std::string_view top_name = "test") {
  auto ln = std::make_shared<Lnast>(std::string(top_name));
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());
  auto asgn  = ln->add_child(stmts, Lnast_node::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref(lhs));
  ln->add_child(asgn, Lnast_node::create_const(rhs_val));
  return ln;
}

}  // namespace

// ── Test 1: plain assign survives and is emitted as `mut x = 3` ─────────────
TEST(LnastPrpWriter, AssignEmittedAsMut) {
  // Output port (%out) — upass keeps it; writer strips % prefix.
  auto ln = make_assign_lnast("%out", "3", "assign_test");
  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_NE(output.find("out"), std::string::npos)  << output;
  EXPECT_NE(output.find("3"),   std::string::npos)  << output;
  EXPECT_EQ(output.find('%'),   std::string::npos)  << "% prefix must be stripped: " << output;
}

// ── Test 2: add_trivial pattern — constprop DCEs all-tmp intermediates ───────
// LNAST:  assign x=1, assign y=2, ___c = x+y, eq ___1 = ___c==3, cassert ___1
// After constprop: all three-underscore temps (___c, ___1) are eliminated
// because they are provably constant and only used in further constant folds.
// The comb block header must appear; no ___ temp tokens should remain.
TEST(LnastPrpWriter, TmpsAreDCEdByConstprop) {
  auto ln = std::make_shared<Lnast>("add_trivial");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // x = 1
  auto ax = ln->add_child(stmts, Lnast_node::create_assign());
  ln->add_child(ax, Lnast_node::create_ref("x"));
  ln->add_child(ax, Lnast_node::create_const("1"));

  // y = 2
  auto ay = ln->add_child(stmts, Lnast_node::create_assign());
  ln->add_child(ay, Lnast_node::create_ref("y"));
  ln->add_child(ay, Lnast_node::create_const("2"));

  // ___c = x + y
  auto plus = ln->add_child(stmts, Lnast_node::create_plus());
  ln->add_child(plus, Lnast_node::create_ref("___c"));
  ln->add_child(plus, Lnast_node::create_ref("x"));
  ln->add_child(plus, Lnast_node::create_ref("y"));

  // cassert ___c == 3  →  eq node: ___1 = ___c == 3
  auto eq = ln->add_child(stmts, Lnast_node::create_eq());
  ln->add_child(eq, Lnast_node::create_ref("___1"));
  ln->add_child(eq, Lnast_node::create_ref("___c"));
  ln->add_child(eq, Lnast_node::create_const("3"));

  auto ca = ln->add_child(stmts, Lnast_node::create_cassert());
  ln->add_child(ca, Lnast_node::create_ref("___1"));

  auto output = run_and_emit(ln, {"constprop"});

  // The comb block header must appear.
  EXPECT_NE(output.find("comb add_trivial"), std::string::npos) << output;
  // All tmps were DCE'd — no tmp refs in output.
  EXPECT_EQ(output.find("___"), std::string::npos) << "Tmps should be DCE'd: " << output;
}

// ── Test 3: plus node emits infix a + b ──────────────────────────────────────
TEST(LnastPrpWriter, PlusEmittedAsInfix) {
  auto ln = std::make_shared<Lnast>("infix_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // %out = %a + %b  — ports so upass keeps them
  auto plus = ln->add_child(stmts, Lnast_node::create_plus());
  ln->add_child(plus, Lnast_node::create_ref("%out"));
  ln->add_child(plus, Lnast_node::create_ref("%a"));
  ln->add_child(plus, Lnast_node::create_ref("%b"));

  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_NE(output.find("out"), std::string::npos) << output;
  EXPECT_NE(output.find('+'),   std::string::npos) << output;
  EXPECT_NE(output.find('a'),   std::string::npos) << output;
  EXPECT_NE(output.find('b'),   std::string::npos) << output;
  // Must NOT use call form
  EXPECT_EQ(output.find("add("), std::string::npos) << "Should be infix: " << output;
}

// ── Test 4: eq node emits == ──────────────────────────────────────────────────
TEST(LnastPrpWriter, EqEmittedAsDoubleEquals) {
  auto ln = std::make_shared<Lnast>("eq_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // %out = %a == %b
  auto eq = ln->add_child(stmts, Lnast_node::create_eq());
  ln->add_child(eq, Lnast_node::create_ref("%out"));
  ln->add_child(eq, Lnast_node::create_ref("%a"));
  ln->add_child(eq, Lnast_node::create_ref("%b"));

  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_NE(output.find("=="),   std::string::npos) << output;
  EXPECT_EQ(output.find("eq("), std::string::npos)  << "Should be infix: " << output;
}

// ── Test 5: cassert emits without parentheses ─────────────────────────────────
TEST(LnastPrpWriter, CassertHasNoParens) {
  auto ln = std::make_shared<Lnast>("cassert_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // %cond is a port — survives upass
  auto ca = ln->add_child(stmts, Lnast_node::create_cassert());
  ln->add_child(ca, Lnast_node::create_ref("%cond"));

  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_NE(output.find("cassert"), std::string::npos) << output;
  // cassert must not be followed by '('
  auto pos = output.find("cassert");
  ASSERT_NE(pos, std::string::npos);
  ASSERT_LT(pos + 7, output.size()) << "output too short after 'cassert': " << output;
  EXPECT_NE(output[pos + 7], '(') << "cassert must have no parens: " << output;
}

// ── Test 6: if with const-true condition is pruned ───────────────────────────
TEST(LnastPrpWriter, TrueIfPrunedInOutput) {
  auto ln = std::make_shared<Lnast>("if_prune_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // if true { %out = 4 }
  auto if_nid = ln->add_child(stmts, Lnast_node::create_if());
  ln->add_child(if_nid, Lnast_node::create_const("true"));
  auto then_s = ln->add_child(if_nid, Lnast_node::create_stmts());
  auto asgn   = ln->add_child(then_s, Lnast_node::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref("%out"));
  ln->add_child(asgn, Lnast_node::create_const("4"));

  auto output = run_and_emit(ln, {"constprop"});
  // Slice 7 pruned the if — no `if` keyword in output.
  EXPECT_EQ(output.find("if "), std::string::npos) << "if should be pruned: " << output;
  // The then-branch assignment was spliced in.
  EXPECT_NE(output.find("out"), std::string::npos) << output;
  EXPECT_NE(output.find('4'),   std::string::npos) << output;
}

// ── Test 7: ref strips $ prefix (input port) ─────────────────────────────────
TEST(LnastPrpWriter, InputPortPrefixStripped) {
  auto ln = std::make_shared<Lnast>("prefix_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // %out = $in   (output = input — port-to-port assign, always kept)
  auto asgn = ln->add_child(stmts, Lnast_node::create_assign());
  ln->add_child(asgn, Lnast_node::create_ref("%out"));
  ln->add_child(asgn, Lnast_node::create_ref("$in"));

  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_EQ(output.find('%'), std::string::npos) << "% must be stripped: " << output;
  EXPECT_EQ(output.find('$'), std::string::npos) << "$ must be stripped: " << output;
  EXPECT_NE(output.find("out"), std::string::npos) << output;
  EXPECT_NE(output.find("in"),  std::string::npos) << output;
}

// ── Test 8: if/else with unknown condition — structure preserved ──────────────
// if %cond { %out = 4 } else { %out = 5 }
// %cond is a port — constprop can't fold it → full if/else emitted.
TEST(LnastPrpWriter, UnknownCondIfElsePreserved) {
  auto ln = std::make_shared<Lnast>("if_else_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  auto if_nid = ln->add_child(stmts, Lnast_node::create_if());
  ln->add_child(if_nid, Lnast_node::create_ref("%cond"));

  auto then_s = ln->add_child(if_nid, Lnast_node::create_stmts());
  auto asgn1  = ln->add_child(then_s, Lnast_node::create_assign());
  ln->add_child(asgn1, Lnast_node::create_ref("%out"));
  ln->add_child(asgn1, Lnast_node::create_const("4"));

  auto else_s = ln->add_child(if_nid, Lnast_node::create_stmts());
  auto asgn2  = ln->add_child(else_s, Lnast_node::create_assign());
  ln->add_child(asgn2, Lnast_node::create_ref("%out"));
  ln->add_child(asgn2, Lnast_node::create_const("5"));

  auto output = run_and_emit(ln, {"constprop"});
  EXPECT_NE(output.find("if "),   std::string::npos) << "if must be kept: "   << output;
  EXPECT_NE(output.find("else"),  std::string::npos) << "else must appear: "  << output;
  EXPECT_NE(output.find("cond"),  std::string::npos) << "condition missing: " << output;
  EXPECT_NE(output.find("4"),     std::string::npos) << "then-val missing: "  << output;
  EXPECT_NE(output.find("5"),     std::string::npos) << "else-val missing: "  << output;
}

// ── Test 9: multiple statements emitted in order ──────────────────────────────
// Two port assigns back-to-back — both must appear in the right order.
TEST(LnastPrpWriter, MultipleStatementsInOrder) {
  auto ln = std::make_shared<Lnast>("multi_stmt");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  auto a1 = ln->add_child(stmts, Lnast_node::create_assign());
  ln->add_child(a1, Lnast_node::create_ref("%x"));
  ln->add_child(a1, Lnast_node::create_const("1"));

  auto a2 = ln->add_child(stmts, Lnast_node::create_assign());
  ln->add_child(a2, Lnast_node::create_ref("%y"));
  ln->add_child(a2, Lnast_node::create_const("2"));

  auto output = run_and_emit(ln, {"noop_shared"});
  auto px = output.find("x");
  auto py = output.find("y");
  EXPECT_NE(px, std::string::npos) << output;
  EXPECT_NE(py, std::string::npos) << output;
  // x must appear before y (emission order preserved)
  EXPECT_LT(px, py) << "x must come before y: " << output;
}

// ── Test 10: arithmetic operators emit correct infix symbols ─────────────────
TEST(LnastPrpWriter, ArithmeticOperatorsInfix) {
  auto ln = std::make_shared<Lnast>("arith_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // %r1 = %a - %b
  auto minus = ln->add_child(stmts, Lnast_node::create_minus());
  ln->add_child(minus, Lnast_node::create_ref("%r1"));
  ln->add_child(minus, Lnast_node::create_ref("%a"));
  ln->add_child(minus, Lnast_node::create_ref("%b"));

  // %r2 = %a * %b
  auto mult = ln->add_child(stmts, Lnast_node::create_mult());
  ln->add_child(mult, Lnast_node::create_ref("%r2"));
  ln->add_child(mult, Lnast_node::create_ref("%a"));
  ln->add_child(mult, Lnast_node::create_ref("%b"));

  // %r3 = %a / %b
  auto divn = ln->add_child(stmts, Lnast_node::create_div());
  ln->add_child(divn, Lnast_node::create_ref("%r3"));
  ln->add_child(divn, Lnast_node::create_ref("%a"));
  ln->add_child(divn, Lnast_node::create_ref("%b"));

  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_NE(output.find('-'), std::string::npos) << "minus missing: " << output;
  EXPECT_NE(output.find('*'), std::string::npos) << "mult missing: "  << output;
  EXPECT_NE(output.find('/'), std::string::npos) << "div missing: "   << output;
  // Must not use call form
  EXPECT_EQ(output.find("sub("), std::string::npos) << output;
  EXPECT_EQ(output.find("mul("), std::string::npos) << output;
  EXPECT_EQ(output.find("div("), std::string::npos) << output;
}

// ── Test 11: bitwise and logical operators ────────────────────────────────────
TEST(LnastPrpWriter, BitwiseAndLogicalOperators) {
  auto ln = std::make_shared<Lnast>("bitlog_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // %r1 = %a & %b
  auto band = ln->add_child(stmts, Lnast_node::create_bit_and());
  ln->add_child(band, Lnast_node::create_ref("%r1"));
  ln->add_child(band, Lnast_node::create_ref("%a"));
  ln->add_child(band, Lnast_node::create_ref("%b"));

  // %r2 = %a | %b
  auto bor = ln->add_child(stmts, Lnast_node::create_bit_or());
  ln->add_child(bor, Lnast_node::create_ref("%r2"));
  ln->add_child(bor, Lnast_node::create_ref("%a"));
  ln->add_child(bor, Lnast_node::create_ref("%b"));

  // %r3 = %a and %b
  auto land = ln->add_child(stmts, Lnast_node::create_log_and());
  ln->add_child(land, Lnast_node::create_ref("%r3"));
  ln->add_child(land, Lnast_node::create_ref("%a"));
  ln->add_child(land, Lnast_node::create_ref("%b"));

  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_NE(output.find('&'),     std::string::npos) << "bit_and missing: " << output;
  EXPECT_NE(output.find('|'),     std::string::npos) << "bit_or missing: "  << output;
  EXPECT_NE(output.find(" and "), std::string::npos) << "log_and missing: " << output;
  // Must not use call form
  EXPECT_EQ(output.find("bit_and("), std::string::npos) << output;
  EXPECT_EQ(output.find("log_and("), std::string::npos) << output;
}

// ── Test 12: tuple_add emits (a, b) form ─────────────────────────────────────
TEST(LnastPrpWriter, TupleAddEmitted) {
  auto ln = std::make_shared<Lnast>("tuple_test");
  ln->set_root(Lnast_node::create_top());
  auto stmts = ln->add_child(Lnast_nid::root(), Lnast_node::create_stmts());

  // %t = (%a, %b)
  auto tadd = ln->add_child(stmts, Lnast_node::create_tuple_add());
  ln->add_child(tadd, Lnast_node::create_ref("%t"));
  ln->add_child(tadd, Lnast_node::create_ref("%a"));
  ln->add_child(tadd, Lnast_node::create_ref("%b"));

  auto output = run_and_emit(ln, {"noop_shared"});
  EXPECT_NE(output.find('('), std::string::npos) << "tuple parens missing: " << output;
  EXPECT_NE(output.find('a'), std::string::npos) << output;
  EXPECT_NE(output.find('b'), std::string::npos) << output;
  // Must not use call form
  EXPECT_EQ(output.find("tuple_add("), std::string::npos) << output;
}

// ── Round-trip helpers (Pyrope 3.0 → prp2lnast → uPass → Lnast_prp_writer) ──
//
// These tests exercise the full pipeline: real .prp source is written to a
// temp file, parsed by Prp2lnast into an LNAST, optimised by uPass, and then
// serialised back to Pyrope 3.0 text by Lnast_prp_writer.  They verify that
// the writer can consume parser-generated LNASTs without crashing and that the
// expected high-level output properties hold.

// Write `src` to $TEST_TMPDIR/<name>.prp and return the path.
static std::string write_prp(std::string_view name, std::string_view src) {
  const char* tmpdir = std::getenv("TEST_TMPDIR");
  std::string path   = (tmpdir ? std::string(tmpdir) : std::string("/tmp"))
                       + "/" + std::string(name) + ".prp";
  std::ofstream f(path);
  f << src;
  return path;
}

// Parse a .prp file → run uPass → emit Pyrope 3.0 text.
static std::string round_trip(std::string_view name,
                               std::string_view src,
                               const std::vector<std::string>& passes = {"noop_shared"}) {
  auto path = write_prp(name, src);
  Prp2lnast converter(path, std::string(name), /*parse_only=*/false);
  auto ln = std::shared_ptr<Lnast>(converter.get_lnast().release());

  auto lm = std::make_shared<upass::Lnast_manager>(ln);
  uPass_runner runner(lm, passes);
  runner.run();
  auto staging = runner.take_staging();
  if (!staging) return "";

  std::ostringstream oss;
  Lnast_prp_writer writer(oss, staging);
  writer.write_all();
  return oss.str();
}

// ── Test 13: round-trip — comb block with params is parsed as func_def ────────
// A comb block with parameters is represented as a func_def node in LNAST.
// func_def bodies are deferred to Slice 4, so the writer emits a TODO comment
// in place of the body.  This test verifies:
//   1. The round-trip does not crash.
//   2. The top-level "comb rt_simple" header is emitted.
//   3. The func_def TODO comment is present (expected current behavior).
TEST(LnastPrpWriter, RoundTripCombWithParamsIsFuncDef) {
  const char* src = R"prp(
comb rt_simple(a) -> (out) {
  out = a
}
)prp";

  auto output = round_trip("rt_simple", src, {"noop_shared"});
  ASSERT_FALSE(output.empty()) << "round-trip produced no output";
  EXPECT_NE(output.find("comb rt_simple"), std::string::npos)
      << "comb header missing:\n" << output;
  // func_def bodies are not yet serialised (Slice 4); the writer emits a TODO.
  EXPECT_NE(output.find("TODO: func_def"), std::string::npos)
      << "expected func_def TODO comment:\n" << output;
}

// ── Test 14: round-trip — constprop folds arithmetic, no raw + in output ──────
// `const a = 2 + 3` — constprop folds the plus to 5 and DCEs the tmp.
// The emitted Pyrope must not contain a bare `+` operator (folded away).
TEST(LnastPrpWriter, RoundTripConstpropFoldsArith) {
  const char* src = R"prp(
comb rt_fold() {
  const a = 2 + 3
  cassert a == 5
}
rt_fold()
)prp";

  auto output = round_trip("rt_fold", src, {"constprop"});
  ASSERT_FALSE(output.empty()) << "round-trip produced no output";
  EXPECT_NE(output.find("comb rt_fold"), std::string::npos)
      << "comb header missing:\n" << output;
  // After constprop, the `2 + 3` plus node should be folded — no raw `+` remains.
  EXPECT_EQ(output.find(" + "), std::string::npos)
      << "unexpected `+` in output (should be folded):\n" << output;
}

// ── Test 15: round-trip — if(true) branch is pruned by constprop ─────────────
// `if true { out = 1 } else { out = 2 }` — constprop prunes the dead else.
// The emitted Pyrope must have no `if` keyword.
TEST(LnastPrpWriter, RoundTripIfTruePruned) {
  const char* src = R"prp(
comb rt_if() -> (out) {
  if true {
    out = 1
  } else {
    out = 2
  }
}
rt_if()
)prp";

  auto output = round_trip("rt_if", src, {"constprop"});
  ASSERT_FALSE(output.empty()) << "round-trip produced no output";
  EXPECT_NE(output.find("comb rt_if"), std::string::npos)
      << "comb header missing:\n" << output;
  EXPECT_EQ(output.find("if "), std::string::npos)
      << "if should be pruned but still present:\n" << output;
}
