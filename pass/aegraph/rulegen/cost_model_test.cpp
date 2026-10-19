//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// The cost profiles (rulegen/cost_model.cpp): the file syntax, what a price depends on (width, operand count,
// constants), the diagnostics, the per-profile cost lint of a `simplify`, and the shipped profiles' shape.

#include "rulegen/cost_model.hpp"

#include <array>
#include <string>

#include "gtest/gtest.h"
#include "rulegen/rule_test_support.hpp"
#include "rulegen/rules.hpp"
#include "runtime/cost.hpp"

namespace cost = livehd::aegraph::cost;
namespace rg   = livehd::aegraph::rulegen;
namespace tst  = livehd::aegraph::rulegen::testing;

namespace {

cost::Profile parse(const std::string& text, cost::Diags& diags) { return cost::parse_profile(text, "t/p.cost", diags); }

bool mentions(const cost::Diags& diags, const std::string& what) {
  for (const auto& d : diags) {
    if (d.find(what) != std::string::npos) {
      return true;
    }
  }
  return false;
}

const char* const kProfile = R"(
(profile p :default-width 16 :doc "t")
(cost p (SHL _ (k)) 0)
(cost p (Mult (k c) ..) :when (pow2? c) 1)
(cost p (Mult (k c) ..) (* W (popcount c)))
(cost p Mult (* Wmax (- Wsum Wmax)))
(cost p (Get_mask _ (k lo) _) (+ N lo))
(cost p Mux (* W (- A 1)))
(cost p _ W)
)";

cost::Operand var(int64_t width, uint32_t bank = 0) { return {bank, width, false, 0}; }
cost::Operand konst(int64_t value, uint32_t bank = 1) { return {bank, 8, true, value}; }

}  // namespace

TEST(CostModel, ParsesAProfileAndPricesByPatternFirstMatchWins) {
  cost::Diags diags;
  const auto  p = parse(kProfile, diags);
  ASSERT_TRUE(diags.empty()) << diags.front();
  cost::lint_profile(p, diags);
  ASSERT_TRUE(diags.empty()) << diags.front();
  EXPECT_EQ(p.name, "p");
  EXPECT_EQ(p.default_width, 16);

  const std::array<cost::Operand, 2> shl_const{var(8, 0), konst(3, 1)};
  EXPECT_EQ(cost::price(p, Ntype_op::SHL, 8, shl_const), 0) << "a shift by a constant is wiring";
  const std::array<cost::Operand, 2> shl_var{var(8, 0), var(3, 1)};
  EXPECT_EQ(cost::price(p, Ntype_op::SHL, 8, shl_var), 8) << "falls to the catch-all: W";

  const std::array<cost::Operand, 2> mult_pow2{var(8, 0), konst(8, 0)};
  EXPECT_EQ(cost::price(p, Ntype_op::Mult, 12, mult_pow2), 1) << "(k c) bound, guard holds";
  const std::array<cost::Operand, 2> mult_7{var(8, 0), konst(7, 0)};
  EXPECT_EQ(cost::price(p, Ntype_op::Mult, 12, mult_7), 12 * 3) << "guard fails: the next entry, W * popcount(7)";
  const std::array<cost::Operand, 2> mult_vv{var(8, 0), var(6, 0)};
  EXPECT_EQ(cost::price(p, Ntype_op::Mult, 14, mult_vv), 8 * 6) << "no constant: Wmax * (Wsum - Wmax)";
}

TEST(CostModel, PricesAreWidthAndWordAware) {
  cost::Diags diags;
  const auto  p = parse(kProfile, diags);
  ASSERT_TRUE(diags.empty());
  const std::array<cost::Operand, 3> mux{var(1, 0), var(8, 1), var(8, 2)};
  EXPECT_EQ(cost::price(p, Ntype_op::Mux, 8, mux), 16);
  EXPECT_EQ(cost::price(p, Ntype_op::Mux, 64, mux), 128) << "W grows the price";
  const std::array<cost::Operand, 3> window{var(100, 0), konst(0, 1), konst(10, 2)};
  EXPECT_EQ(cost::price(p, Ntype_op::Get_mask, 10, window), 1 + 0) << "N is the 64-bit words of the result";
  EXPECT_EQ(cost::price(p, Ntype_op::Get_mask, 100, window), 2 + 0);
  const std::array<cost::Operand, 3> window_hi{var(100, 0), konst(5, 1), konst(10, 2)};
  EXPECT_EQ(cost::price(p, Ntype_op::Get_mask, 10, window_hi), 1 + 5) << "a binder from the second operand";
}

TEST(CostModel, BankedOpsMatchTheirConstantsInAnyOrderAndPositionalOpsByPin) {
  cost::Diags diags;
  const auto  p = parse(R"(
(profile q)
(cost q (Sum (k c) ..) c)
(cost q (Mux _ (k) _) 5)
(cost q _ 100)
)",
                       diags);
  ASSERT_TRUE(diags.empty()) << diags.front();
  const std::array<cost::Operand, 3> a{var(8, 0), konst(7, 0), var(8, 0)};
  EXPECT_EQ(cost::price(p, Ntype_op::Sum, 8, a), 7) << "the constant is found wherever it sits in the bank";
  const std::array<cost::Operand, 2> none{var(8, 0), var(8, 0)};
  EXPECT_EQ(cost::price(p, Ntype_op::Sum, 8, none), 100);
  const std::array<cost::Operand, 3> mux_f{var(1, 0), konst(0, 1), var(8, 2)};
  EXPECT_EQ(cost::price(p, Ntype_op::Mux, 8, mux_f), 5) << "positional: the constant is the false arm";
  const std::array<cost::Operand, 3> mux_t{var(1, 0), var(8, 1), konst(0, 2)};
  EXPECT_EQ(cost::price(p, Ntype_op::Mux, 8, mux_t), 100) << "the true arm is not the pattern's constant";
}

TEST(CostModel, AnExpressionWithNoValueFallsThroughAndPricesClamp) {
  cost::Diags diags;
  const auto  p = parse(R"(
(profile q)
(cost q Rem (log2 (- W 8)))
(cost q Sum (* W W W W W W W W W W W W W W W W))
(cost q Not (- 0 5))
(cost q _ 2)
)",
                       diags);
  ASSERT_TRUE(diags.empty()) << diags.front();
  const std::array<cost::Operand, 2> two{var(8, 0), var(8, 0)};
  EXPECT_EQ(cost::price(p, Ntype_op::Rem, 8, two), 2) << "log2(0) has no value: the entry does not match";
  EXPECT_EQ(cost::price(p, Ntype_op::Rem, 16, two), 3);
  EXPECT_EQ(cost::price(p, Ntype_op::Sum, 16, two), 2) << "an overflow has no value either";
  EXPECT_EQ(cost::price(p, Ntype_op::Not, 8, two), 0) << "negative prices clamp to 0";
  EXPECT_EQ(cost::price(p, Ntype_op::Sum, 15, two), cost::kMaxPrice) << "and a huge one to kMaxPrice";
}

TEST(CostModel, DiagnosticsNameTheLineAndTheMistake) {
  const auto bad = [](const std::string& text, const std::string& what) {
    cost::Diags diags;
    const auto  p = parse(text, diags);
    if (diags.empty()) {
      cost::lint_profile(p, diags);
    }
    EXPECT_TRUE(mentions(diags, what)) << text << "\n  expected: " << what
                                       << "\n  got: " << (diags.empty() ? "nothing" : diags.front());
  };
  bad("(cost p _ 1)", "no (profile NAME) form");
  bad("(profile p)\n(cost q _ 1)", "this file is `p`");
  bad("(profile p)\n(cost p Bogus 1)\n(cost p _ 1)", "unknown op `Bogus`");
  bad("(profile p)\n(cost p Sum (* W Z))\n(cost p _ 1)", "unbound name `Z`");
  bad("(profile p)\n(cost p Sum (frob W))\n(cost p _ 1)", "unknown builtin `frob`");
  bad("(profile p)\n(cost p Sum (min W))\n(cost p _ 1)", "takes 2+ argument");
  bad("(profile p)\n(cost p (Sum (k c) (k c)) 1)\n(cost p _ 1)", "bound twice");
  bad("(profile p)\n(cost p (Sum x) 1)\n(cost p _ 1)", "pattern item");
  bad("(profile p)\n(cost p Sum 1)", "catch-all");
  bad("(profile p)\n(cost p Sum 1)\n(cost p Sum 2)\n(cost p _ 1)", "unreachable");
  bad("(profile p)\n(cost p _ 1)\n(cost p Sum 2)", "unreachable");
  bad("(profile p :default-width 0)\n(cost p _ 1)", ":default-width");
  bad("(profile p)\n(profile p)\n(cost p _ 1)", "second (profile");
}

TEST(CostModel, TheShippedProfilesAreRegisteredAndTotal) {
  for (const char* name : {"synth", "sim"}) {
    const auto* p = livehd::aegraph::rt::find_cost_profile(name);
    ASSERT_NE(p, nullptr) << name;
    EXPECT_EQ(p->name, name);
    cost::Diags diags;
    cost::lint_profile(*p, diags);
    EXPECT_TRUE(diags.empty()) << name << ": " << (diags.empty() ? "" : diags.front());
  }
  EXPECT_EQ(livehd::aegraph::rt::find_cost_profile("nonesuch"), nullptr);
}

// The point of having two profiles, pinned on the shipped tables: wiring is free in hardware and an instruction
// in the simulator, and the simulator quantizes to 64-bit words where the hardware pays per bit.
TEST(CostModel, TheShippedProfilesDisagreeWhereTheirTargetsDo) {
  const auto* synth = livehd::aegraph::rt::find_cost_profile("synth");
  const auto* sim   = livehd::aegraph::rt::find_cost_profile("sim");
  ASSERT_NE(synth, nullptr);
  ASSERT_NE(sim, nullptr);

  const std::array<cost::Operand, 3> window{var(100, 0), konst(3, 1), konst(4, 2)};
  EXPECT_EQ(cost::price(*synth, Ntype_op::Get_mask, 1, window), 0) << "a bit select is a wire";
  EXPECT_GT(cost::price(*sim, Ntype_op::Get_mask, 1, window), 0) << "and a shift plus a mask in the simulator";

  const std::array<cost::Operand, 3> mux8{var(1, 0), var(8, 1), var(8, 2)};
  const std::array<cost::Operand, 3> mux60{var(1, 0), var(60, 1), var(60, 2)};
  EXPECT_LT(cost::price(*synth, Ntype_op::Mux, 8, mux8), cost::price(*synth, Ntype_op::Mux, 60, mux60))
      << "hardware pays per bit";
  EXPECT_EQ(cost::price(*sim, Ntype_op::Mux, 8, mux8), cost::price(*sim, Ntype_op::Mux, 60, mux60)) << "a word is a word";
  const std::array<cost::Operand, 3> mux70{var(1, 0), var(70, 1), var(70, 2)};
  EXPECT_GT(cost::price(*sim, Ntype_op::Mux, 70, mux70), cost::price(*sim, Ntype_op::Mux, 60, mux60)) << "until it spills a word";

  const std::array<cost::Operand, 2> mult_pow2{var(8, 0), konst(8, 0)};
  const std::array<cost::Operand, 2> shl{var(8, 0), konst(3, 1)};
  EXPECT_EQ(cost::price(*synth, Ntype_op::Mult, 12, mult_pow2), cost::price(*synth, Ntype_op::SHL, 12, shl));
  EXPECT_GE(cost::price(*sim, Ntype_op::Mult, 12, mult_pow2), cost::price(*sim, Ntype_op::SHL, 12, shl))
      << "a simplify may not be dearer in any profile (the rule lint)";

  const std::array<cost::Operand, 2> mult_vv{var(16, 0), var(16, 0)};
  EXPECT_GT(cost::price(*synth, Ntype_op::Mult, 32, mult_vv), 100 * cost::price(*sim, Ntype_op::Mult, 32, mult_vv) / 100)
      << "an array multiplier is hundreds of gates, one imul in the simulator";
}

// ---- the per-profile cost lint of a rule -------------------------------------------------------

namespace {

const char* const kTwo = R"(
(profile cheap)
(cost cheap (SHL _ (k)) 0)
(cost cheap Mult 10)
(cost cheap _ 1)
)";
const char* const kTwoB = R"(
(profile dear)
(cost dear (SHL _ (k)) 20)
(cost dear Mult 10)
(cost dear _ 1)
)";

rg::Diags lint_with_profiles(const std::string& rules) {
  const char* analyses = "(analysis bool01 (x) :type bool :spec (and (<= 0 x) (<= x 1)))";
  auto        p        = tst::parse_all("t/g.rules", rules.c_str(), analyses);
  rg::Diags   diags    = p.diags;
  std::vector<cost::Profile> profiles;
  profiles.push_back(parse(kTwo, diags));
  profiles.push_back(parse(kTwoB, diags));
  p.analyses.impl       = {"bool01"};
  p.analyses.impl_known = true;
  if (diags.empty()) {
    rg::lint({p.file}, p.analyses, {}, profiles, diags);
  }
  return diags;
}

}  // namespace

TEST(CostLint, ASimplifyMayNotCostMoreThanItReplacesInAnyProfile) {
  // Mult -> SHL: free in `cheap`, 20 against 10 in `dear`: dearer in one profile, so it must be an alt.
  const std::string mult_shl = "s (Mult (as x (k c))) :when (pow2? c) (> c 1) => (SHL x (k (log2 c))) :example (Mult $x 8))";
  const auto        bad      = lint_with_profiles("(simplify " + mult_shl);
  ASSERT_FALSE(bad.empty());
  EXPECT_TRUE(mentions(bad, "costs more than it replaces under profile `dear`")) << bad.front();
  EXPECT_TRUE(mentions(bad, "make it alt"));
  EXPECT_TRUE(lint_with_profiles("(alt " + mult_shl).empty());
  // Dearer in neither: a simplify.
  EXPECT_TRUE(lint_with_profiles("(simplify s (Not (Not x)) => x :example (Not (Not $x)))").empty());
}

TEST(CostLint, ExpectNamesMustBeADefinedProfile) {
  EXPECT_TRUE(lint_with_profiles("(alt a (Not x) => (Xor x -1) :expect (cheap lhs) (dear lhs) :example (Not $x))").empty());
  EXPECT_TRUE(mentions(lint_with_profiles("(alt a (Not x) => (Xor x -1) :expect (synth lhs) :example (Not $x))"),
                       "unknown cost profile `synth` (cheap or dear)"));
}

// ---- :expect, through the engine -----------------------------------------------------------------

TEST(CostExpect, TheProfileDecidesWhichSideIsKept) {
  const char* analyses = "(analysis bool01 (x) :type bool :spec (and (<= 0 x) (<= x 1)))";
  const auto  p        = tst::parse_all("t/g.rules", R"(
(simplify mult_pow2_shl 30
  (Mult (as x (k c)))
  :when (pow2? c) (> c 1)
  => (SHL x (k (log2 c)))
  :expect (synth rhs) (sim rhs)
  :example (Mult $x 8))
(alt not_xor_alt 5
  (Not x)
  => (Xor x -1)
  :expect (synth lhs) (sim lhs)
  :example (Not $x))
)",
                                 analyses);
  ASSERT_TRUE(p.diags.empty()) << p.diags.front();
  EXPECT_EQ(tst::expect_side(p.file, p.file.rules[0], "synth", "rhs"), "");
  EXPECT_EQ(tst::expect_side(p.file, p.file.rules[0], "sim", "rhs"), "");
  EXPECT_EQ(tst::expect_side(p.file, p.file.rules[1], "synth", "lhs"), "");
  EXPECT_EQ(tst::expect_side(p.file, p.file.rules[1], "sim", "lhs"), "");
  EXPECT_NE(tst::expect_side(p.file, p.file.rules[0], "synth", "lhs").find("takes the right-hand side"), std::string::npos);
  EXPECT_NE(tst::expect_side(p.file, p.file.rules[1], "sim", "rhs").find("keeps the left-hand side"), std::string::npos);
  EXPECT_NE(tst::expect_side(p.file, p.file.rules[1], "synth", "rhs").find("keeps the left-hand side"), std::string::npos)
      << "a wrong :expect is a failing test: the tie keeps the original";
}
