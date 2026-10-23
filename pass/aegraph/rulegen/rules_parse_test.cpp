//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// rulegen: the parser, every lint that fails generation, the emitters' determinism, and the
// fuzz / example / bv-proof drivers on inline rules (a correct rule passes, a wrong one is caught).

#include <string>

#include "gtest/gtest.h"
#include "rulegen/rule_test_support.hpp"
#include "rulegen/rules.hpp"
#include "rulegen/sexp.hpp"

using namespace livehd::aegraph::rulegen;
namespace tst = livehd::aegraph::rulegen::testing;

namespace {

// bool01 is implemented in the runtime; the rest are declared only (milestone G).
const char* const kAnalyses = R"(
(analysis bool01 (x) :type bool  :spec (and (<= 0 x) (<= x 1)))
(analysis range  (x) :type Range :spec (and (<= LO x) (<= x HI)))
(analysis known  (x) :type Known_bits :spec (= (And x MASK) VAL))
(pred known_zero_outside (x m) :uses known :spec (= (And x (Not m)) 0))
(pred nonneg        (x) :uses range :spec (<= 0 x))
(pred range_bounded (x) :uses range :spec (and (<= LO x) (<= x HI)))
)";

tst::Parsed parse_text(const std::string& text) { return tst::parse_all("t/group.rules", text.c_str(), kAnalyses); }

// The diagnostics of parsing + linting `text` (errors and warnings, as printed).
Diags lint_text(const std::string& text) {
  auto p = parse_text(text);
  Diags diags = p.diags;
  // bool01 is implemented in the runtime; the other inline declarations are declared only.
  p.analyses.impl       = {"bool01"};
  p.analyses.impl_known = true;
  if (diags.empty()) {
    lint({p.file}, p.analyses, {}, diags);
  }
  return diags;
}

bool mentions(const Diags& diags, const std::string& what) {
  for (const auto& d : diags) {
    if (d.find(what) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---- reader ------------------------------------------------------------------------

TEST(RulegenSexp, ReadsAtomsListsStringsAttrsAndComments) {
  const auto top = parse_sexps("; c\n(a b [c \"d e\"] Hotmux{proven} -1)\n(x)", "f");
  ASSERT_EQ(top.size(), 2u);
  EXPECT_EQ(top[0].items.size(), 5u);
  EXPECT_EQ(top[0].items[2].open, '[');
  EXPECT_EQ(top[0].items[2].items[1].text, "d e");
  EXPECT_EQ(top[0].items[3].attrs, std::vector<std::string>{"proven"});
  EXPECT_EQ(top[1].line, 3);
  EXPECT_EQ(to_text(top[0]), "(a b [c \"d e\"] Hotmux{proven} -1)");
  EXPECT_THROW((void)parse_sexps("(a", "f"), Syntax_error);
  EXPECT_THROW((void)parse_sexps("a)", "f"), Syntax_error);
}

// ---- parser ------------------------------------------------------------------------

TEST(RulegenParse, EveryConstructOfTheDocSyntax) {
  const auto p = parse_text(R"(
(simplify sum_drop_zero (Sum (as 0 ..r) (bs ..s)) => (Sum (as ..r) (bs ..s)) :example (Sum $x 0))
(simplify sum_cancel 60 (Sum (as x ..r) (bs x ..s)) => (Sum (as ..r) (bs ..s)) :example (Sum (as $x $y) (bs $x)))
(alt mult_pow2_shl (Mult (as x (k c))) :when (pow2? c) => (SHL x (k (log2 c)))
     :expect (synth rhs) :example (Mult $x 8) :fuel 64 :proof bv)
(simplify getmask_getmask
  (Get_mask (Get_mask x (k l1) (k h1)) (k l2) (k h2))
  :let (lo (+ l1 l2)) (hi (min h1 (+ l1 h2))) :when (< lo hi)
  => (Get_mask x (k lo) (k hi)) :example (Get_mask (Get_mask $x 1 7) 1 3) :doc "window of window")
(simplify named (Get_mask a: x lo: 0 hi: (k h)) => x :example (Get_mask $x 0 4))
(choice mult_unknown (Mult (as x ?q)) => 0 [?q := 0] | x [?q := 1] :example (Mult $x 3))
)");
  ASSERT_TRUE(p.diags.empty()) << p.diags.front();
  ASSERT_EQ(p.file.rules.size(), 6u);
  const auto& r0 = p.file.rules[0];
  EXPECT_EQ(r0.prio, 0);
  EXPECT_TRUE(r0.lhs.banked);
  ASSERT_EQ(r0.lhs.banks.size(), 2u);
  EXPECT_TRUE(r0.lhs.banks[0].has_rest);
  EXPECT_EQ(r0.lhs.banks[0].rest, "r");
  EXPECT_EQ(r0.lhs.banks[0].elems.size(), 1u);
  EXPECT_EQ(p.file.rules[1].prio, 60);
  const auto& r2 = p.file.rules[2];
  EXPECT_EQ(r2.tag, Rule::Tag::alt);
  ASSERT_EQ(r2.expects.size(), 1u);
  EXPECT_EQ(r2.expects[0].profile, "synth");
  EXPECT_EQ(r2.fuel, 64);
  EXPECT_EQ(r2.proof, "bv");
  EXPECT_EQ(p.file.rules[3].lets.size(), 2u);
  EXPECT_EQ(p.file.rules[3].whens.size(), 1u);
  EXPECT_EQ(p.file.rules[3].doc, "window of window");
  // pin order: Get_mask is (a, lo, hi) = pids 0, 2, 3
  const auto& named = p.file.rules[4].lhs;
  ASSERT_EQ(named.pids.size(), 3u);
  EXPECT_EQ(named.pids[1], 2u);
  EXPECT_EQ(named.pids[2], 3u);
  const auto& choice = p.file.rules[5];
  ASSERT_EQ(choice.arms.size(), 2u);
  EXPECT_EQ(choice.arms[0].value, 0);
  EXPECT_EQ(choice.arms[1].value, 1);
  EXPECT_EQ(choice.arms[1].lit, "?q");
}

TEST(RulegenParse, AnalysesDecl) {
  const auto p = parse_text("");
  ASSERT_TRUE(p.diags.empty());
  ASSERT_EQ(p.analyses.list.size(), 6u);
  EXPECT_FALSE(p.analyses.find("bool01")->is_pred);
  EXPECT_TRUE(p.analyses.find("nonneg")->is_pred);
  EXPECT_EQ(p.analyses.find("nonneg")->uses, "range");
  EXPECT_EQ(p.analyses.find("known")->type, "Known_bits");
  ASSERT_TRUE(p.analyses.find("range")->spec.has_value());
  EXPECT_EQ(expr_text(*p.analyses.find("bool01")->spec), "(and (<= 0 x) (<= x 1))");
}

TEST(RulegenParse, SyntaxErrorsAreDiagnostics) {
  EXPECT_FALSE(parse_text("(simplify a (Not x)").diags.empty());
  EXPECT_TRUE(mentions(parse_text("(frob a (Not x) => x)").diags, "unknown form"));
  EXPECT_TRUE(mentions(parse_text("(simplify a (Not x))").diags, "no `=>`"));
  EXPECT_TRUE(mentions(parse_text("(simplify a (Bogus x) => x)").diags, "unknown op"));
  EXPECT_TRUE(mentions(parse_text("(simplify a (Not x y) => x)").diags, "takes 1 operands"));
}

// ---- lints -------------------------------------------------------------------------

TEST(RulegenLint, ACleanRuleHasNoDiagnostics) {
  EXPECT_TRUE(lint_text("(simplify ok (Not (Not x)) => x :example (Not (Not $x)))").empty());
}

TEST(RulegenLint, UnknownOpAndPin) {
  EXPECT_TRUE(mentions(lint_text("(simplify a (Bogus x) => x :example (Not $x))"), "unknown op `Bogus`"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Flop x) => x :example (Not $x))"), "not a pure e-graph op"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (SHL a: x zz: 0) => x :example (SHL $x 0))"), "unknown pin `zz`"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Not (as x)) => x :example (Not $x))"), "no operand banks"));
}

TEST(RulegenLint, UnboundVariables) {
  EXPECT_TRUE(mentions(lint_text("(simplify a (Not (Not x)) => y :example (Not (Not $x)))"), "unbound variable `y`"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Not (Not x)) => (SHL x (k z)) :example (Not (Not $x)))"), "unbound variable `z`"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Not (Not x)) :when (< q 2) => x :example (Not (Not $x)))"), "unbound variable `q`"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Mult (as x ..r)) => (Mult (as ..s)) :example (Mult $x 2))"), "not a rest binder"));
}

TEST(RulegenLint, StructuralCostLint) {
  // A simplify must not grow the graph ...
  EXPECT_TRUE(mentions(lint_text("(simplify grow (Not x) => (Not (Not x)) :example (Not $x))"), "make it alt"));
  // ... nor build an op priced above its left-hand root.
  EXPECT_TRUE(mentions(lint_text("(simplify pricey (SHL x (k c)) => (Mult x (k c)) :example (SHL $x 3))"), "priced above"));
  // The same shapes are fine as alt.
  EXPECT_TRUE(lint_text("(alt grow (Not x) => (Not (Not x)) :example (Not $x))").empty());
}

TEST(RulegenLint, FreshQuestionMarkAndCreateConstOfIt) {
  EXPECT_TRUE(mentions(lint_text("(simplify a (Not (Not x)) => ?z :example (Not (Not $x)))"), "fresh `?z`"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Not (Not x)) => 0b?1 :example (Not (Not $x)))"), "create_const of ?"));
  EXPECT_TRUE(lint_text("(simplify a (Or (as ?q 0)) => ?q :example (Or $x 0))").empty()) << "a bound ? may be reused";
}

TEST(RulegenLint, ChoiceArms) {
  EXPECT_TRUE(mentions(lint_text("(choice c (Mult (as x ?q)) => 0 | x [?q := 1] :example (Mult $x 3))"),
                       "choice arm without a ?-assignment"));
  EXPECT_TRUE(mentions(lint_text("(choice c (Mult (as x y)) => 0 [?q := 0] :example (Mult $x 3))"), "binds no ?literal"));
  EXPECT_TRUE(mentions(lint_text("(choice c (Mult (as x ?q)) => x [?q := x] :example (Mult $x 3))"), "milestone (J)"));
  EXPECT_TRUE(mentions(lint_text("(simplify c (Mult (as x ?q)) => 0 [?q := 0] :example (Mult $x 3))"), "only `choice` arms"));
  EXPECT_TRUE(lint_text("(choice c (Mult (as x ?q)) => 0 [?q := 0] | x [?q := 1] :example (Mult $x 3))").empty());
}

TEST(RulegenLint, AttributesProfilesProofsAndExamples) {
  EXPECT_TRUE(
      mentions(lint_text("(alt a (Not x) => (Not{bogus} x) :example (Not $x))"), "unregistered contract attribute {bogus}"));
  EXPECT_TRUE(mentions(lint_text("(alt a (Not x) => (Not{proven} x) :example (Not $x))"), "milestone F"));
  EXPECT_TRUE(mentions(lint_text("(alt a (Not x) => (Xor x -1) :expect (nonesuch rhs) :example (Not $x))"),
                       "unknown cost profile `nonesuch`"));
  EXPECT_TRUE(mentions(lint_text("(alt a (Not x) => (Xor x -1) :expect (synth middle) :example (Not $x))"), "lhs or rhs"));
  EXPECT_TRUE(mentions(lint_text("(alt a (Not x) => (Xor x -1) :proof magic :example (Not $x))"), ":proof is"));
  EXPECT_TRUE(mentions(lint_text("(alt a (Not x) => (Xor x -1))"), "no :example"));
  EXPECT_TRUE(mentions(lint_text("(alt a (Not x) => (Xor x -1) :example (Not x))"), "example inputs are spelled $name"));
}

TEST(RulegenLint, NamesGuardsAndAnalyses) {
  EXPECT_TRUE(mentions(lint_text("(alt Bad (Not x) => (Xor x -1) :example (Not $x))"), "lower_snake_case"));
  EXPECT_TRUE(mentions(lint_text("(alt a (Not x) => (Xor x -1) :example (Not $x))\n"
                                 "(alt a (Not y) => (Xor y -1) :example (Not $x))"),
                       "already used"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Mult (as x (k c))) :when (frobnicate c) => x :example (Mult $x 1))"),
                       "unknown builtin"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Mult (as x (k c))) :when (+ c 1) => x :example (Mult $x 1))"), "truth value"));
  EXPECT_TRUE(mentions(lint_text("(simplify a (Mult (as x (k c))) :when (nonneg x) => x :example (Mult $x 1))"),
                       "no runtime implementation"));
  EXPECT_TRUE(lint_text("(simplify a (Xor (as x 1)) :when (bool01 x) => (EQ x 0) :example (Xor (EQ $x 0) 1))").empty());
  const auto overlap = lint_text("(alt a (Not x) => (Xor x -1) :example (Not $x))\n(alt b (Not y) => (Xor y 0) :example (Not $x))");
  EXPECT_TRUE(mentions(overlap, "overlaps `a`"));
}

// ---- emitters ----------------------------------------------------------------------

TEST(RulegenEmit, RestOnlyBankDoesNotEmitUnsignedComparisonWithZero) {
  const auto p = parse_text(R"(
(simplify sum_not_self (Sum (as x (Not x) ..r) (bs ..s))
  => (Sum (as -1 ..r) (bs ..s))
  :example (Sum (as $x (Not $x) $y) (bs $z)))
)");
  ASSERT_TRUE(p.diags.empty());
  const auto cpp = emit_rules_cpp(p.file, p.analyses);
  EXPECT_EQ(cpp.find(" < 0)"), std::string::npos);
  EXPECT_NE(cpp.find(" < 2)"), std::string::npos);
}

TEST(RulegenEmit, DeterministicAndLineMapped) {
  const char* text = R"(
(simplify not_not 30 (Not (Not x)) => x :example (Not (Not $x)))
(simplify mult_pow2_shl 30 (Mult (as x (k c))) :when (pow2? c) (> c 1) => (SHL x (k (log2 c))) :example (Mult $x 8))
(choice mult_unknown (Mult (as x ?q)) => 0 [?q := 0] | x [?q := 1] :example (Mult $x 3))
)";
  const auto a = tst::parse_all("pass/aegraph/rules/g.rules", text, kAnalyses);
  const auto b = tst::parse_all("pass/aegraph/rules/g.rules", text, kAnalyses);
  ASSERT_TRUE(a.diags.empty());
  EXPECT_EQ(emit_rules_cpp(a.file, a.analyses), emit_rules_cpp(b.file, b.analyses));
  EXPECT_EQ(emit_fuzz_test(a.file, a.analyses), emit_fuzz_test(b.file, b.analyses));
  EXPECT_EQ(emit_prove_test(a.file, a.analyses), emit_prove_test(b.file, b.analyses));
  EXPECT_EQ(emit_rules_md({a.file}, a.analyses, {}), emit_rules_md({b.file}, b.analyses, {}));

  const auto cpp = emit_rules_cpp(a.file, a.analyses);
  EXPECT_NE(cpp.find("#line 2 \"pass/aegraph/rules/g.rules\""), std::string::npos);
  EXPECT_NE(cpp.find("AEG_RULE(not_not, simplify, 30, Ntype_op::Not)"), std::string::npos);
  EXPECT_NE(cpp.find("AEG_RULE(mult_unknown, choice, 0, Ntype_op::Mult)"), std::string::npos);
  EXPECT_NE(cpp.find("ctx.admits(q_q, INT64_C(0))"), std::string::npos) << "the implicit (admits ?q C) guard";
  EXPECT_EQ(cpp.find("random"), std::string::npos);

  const auto fuzz = emit_fuzz_test(a.file, a.analyses);
  EXPECT_NE(fuzz.find("not_not_equal"), std::string::npos);
  EXPECT_NE(fuzz.find("not_not_example_fires"), std::string::npos);
  const auto md = emit_rules_md({a.file}, a.analyses, {});
  EXPECT_NE(md.find("| `not_not` | simplify | 30 | Not | bv-swept | g.rules:2 |"), std::string::npos);
}

TEST(RulegenEmit, ScansHandWrittenRules) {
  const auto found = scan_native(
      "AEG_RULE_GROUP(arith);\n\n// first line\n// second line\n"
      "AEG_NATIVE_RULE(annihilate, simplify, 90, Ntype_op::Mult, Ntype_op::And) {\n}\n"
      "AEG_RULE(not_not, simplify, 30, Ntype_op::Not) {\n}\n",
      "rules/arith.cpp");
  ASSERT_EQ(found.size(), 2u);
  EXPECT_EQ(found[0].name, "annihilate");
  EXPECT_EQ(found[0].group, "arith");
  EXPECT_EQ(found[0].prio, 90);
  EXPECT_EQ(found[0].roots, (std::vector<std::string>{"Mult", "And"}));
  EXPECT_EQ(found[0].doc, "first line second line");
  EXPECT_EQ(found[0].line, 5);
  EXPECT_TRUE(found[0].native);
  EXPECT_FALSE(found[1].native);
  // a name that a pattern rule also uses is a lint error
  auto p = parse_text("(alt annihilate (Not x) => (Xor x -1) :example (Not $x))");
  Diags diags;
  lint({p.file}, p.analyses, found, diags);
  EXPECT_TRUE(mentions(diags, "already used"));
}

// ---- the drivers -------------------------------------------------------------------

TEST(RulegenFuzz, CorrectRulesPassAndAWrongOneIsCaught) {
  const auto p = parse_text(R"(
(simplify f_not_not (Not (Not x)) => x :example (Not (Not $x)))
(simplify f_xor_one (Xor (as x 1)) :when (bool01 x) => (EQ x 0) :example (Xor (EQ $x 0) 1))
(simplify f_mult_shl (Mult (as x (k c))) :when (pow2? c) (> c 1) => (SHL x (k (log2 c))) :example (Mult $x 8))
(simplify f_sext (Sext (Sext x (k a)) (k b)) :when (>= a 1) (>= b 1) => (Sext x (k (min a b))) :example (Sext (Sext $x 6) 4))
(simplify f_sum_cancel (Sum (as x ..r) (bs x ..s)) => (Sum (as ..r) (bs ..s)) :example (Sum (as $x $y) (bs $x)))
(simplify f_wrong (Sum (as x 1)) => x :example (Sum $x 1))
)");
  ASSERT_TRUE(p.diags.empty()) << p.diags.front();
  for (const auto& r : p.file.rules) {
    const auto report = tst::fuzz_equal(r, p.analyses, 7, 300);
    if (r.name == "f_wrong") {
      EXPECT_FALSE(report.failure.empty()) << "an unsound rule must fail the fuzz";
      EXPECT_NE(report.failure.find("f_wrong"), std::string::npos);
    } else {
      EXPECT_TRUE(report.failure.empty()) << report.failure;
      EXPECT_GE(report.samples, 270u) << r.name << " skipped too many samples";
    }
  }
}

TEST(RulegenFuzz, RefinementModeForQuestionMarkRules) {
  const auto p = parse_text(R"(
(simplify q_and_zero (And (as ?q 0)) => 0 :example (And $x 0))
(simplify q_or_zero_bad (Or (as ?q 0)) => 0 :example (Or $x 0))
(simplify q_keep (Or (as ?q 0)) => ?q :example (Or $x 0))
)");
  ASSERT_TRUE(p.diags.empty()) << (p.diags.empty() ? "" : p.diags.front());
  EXPECT_TRUE(tst::fuzz_equal(p.file.rules[0], p.analyses, 3, 300).failure.empty());
  const auto bad = tst::fuzz_equal(p.file.rules[1], p.analyses, 3, 300);
  EXPECT_FALSE(bad.failure.empty()) << "dropping known bits of a ? is not a refinement";
  EXPECT_TRUE(tst::fuzz_equal(p.file.rules[2], p.analyses, 3, 300).failure.empty());
}

TEST(RulegenFuzz, ChoiceArmsWithTheirAssignmentSubstituted) {
  const auto p = parse_text(R"(
(choice c_good (Mult (as x ?q)) => 0 [?q := 0] | x [?q := 1] :example (Mult $x 3))
(choice c_bad  (Mult (as x ?q)) => 0 [?q := 0] | x [?q := 2] :example (Mult $x 3))
(choice c_sel  (Mux ?q f t) => f [?q := 0] | t [?q := NONZERO] :example (Mux 1 $f $t))
)");
  ASSERT_TRUE(p.diags.empty()) << (p.diags.empty() ? "" : p.diags.front());
  EXPECT_TRUE(tst::fuzz_equal(p.file.rules[0], p.analyses, 5, 200).failure.empty());
  EXPECT_FALSE(tst::fuzz_equal(p.file.rules[1], p.analyses, 5, 200).failure.empty());
  EXPECT_TRUE(tst::fuzz_equal(p.file.rules[2], p.analyses, 5, 200).failure.empty());
}

TEST(RulegenExample, FiresThroughTheRegisteredRule) {
  // `not_not` is a production rule (rules/arith.rules): the example must make it fire when it is the only rule.
  const auto good = parse_text("(simplify not_not 30 (Not (Not x)) => x :example (Not (Not $x)))");
  ASSERT_TRUE(good.diags.empty());
  EXPECT_EQ(tst::example_fires(good.file, good.file.rules[0]), "");
  const auto bad = parse_text("(simplify not_not 30 (Not (Not x)) => x :example (Not $x))");
  EXPECT_NE(tst::example_fires(bad.file, bad.file.rules[0]).find("never made `not_not` fire"), std::string::npos);
  const auto ghost = parse_text("(simplify ghost_rule (Not (Not x)) => x :example (Not (Not $x)))");
  EXPECT_NE(tst::example_fires(ghost.file, ghost.file.rules[0]).find("not in the registry"), std::string::npos);
}

TEST(RulegenProve, ProvesACorrectRuleAndRefutesAWrongOne) {
  const auto p = parse_text(R"(
(simplify p_not_not (Not (Not x)) => x :example (Not (Not $x)))
(simplify p_mult_shl (Mult (as x (k c))) :when (pow2? c) (> c 1) => (SHL x (k (log2 c))) :example (Mult $x 8))
(simplify p_xor_one (Xor (as x 1)) :when (bool01 x) => (EQ x 0) :example (Xor (EQ $x 0) 1))
(simplify p_wrong (Sum (as x 1)) => x :example (Sum $x 1))
)");
  ASSERT_TRUE(p.diags.empty()) << p.diags.front();
  for (const auto& r : p.file.rules) {
    const auto report = tst::prove_bv(r, p.analyses, false, 20.0);
    if (r.name == "p_wrong") {
      EXPECT_FALSE(report.refuted.empty());
      EXPECT_NE(report.refuted.find("counterexample"), std::string::npos) << report.refuted;
      EXPECT_NE(report.refuted.find("w="), std::string::npos) << "the instance is printed";
    } else {
      EXPECT_TRUE(report.refuted.empty()) << report.refuted;
      EXPECT_GT(report.proven, 0u) << r.name << ": " << report.note;
      EXPECT_LE(report.instances, 8u);
    }
  }
}
