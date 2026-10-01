// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Bit-demand transfer functions of pass/lec/observe.cpp. The analysis only
// PROPOSES which flop bits the inductive miter may free (query.cpp re-encodes
// and the solver re-checks every kept obligation), so an over-approximation is
// always safe; these cases pin that it is also PRECISE on the shapes the
// encoder emits, and never UNDER-approximates (a missing bit here costs a
// proof, a missing bit in the other direction would be caught by the solver).

#include "observe.hpp"

#include <cvc5/cvc5.h>

#include "gtest/gtest.h"

namespace {

using livehd::lec::Bit_demand;
using livehd::lec::Bit_set;
using livehd::lec::Ground_eval;

struct Fixture {
  cvc5::TermManager tm;
  cvc5::Term        x = tm.mkConst(tm.mkBitVectorSort(8), "x");
  cvc5::Term        y = tm.mkConst(tm.mkBitVectorSort(8), "y");

  cvc5::Term bv(uint32_t w, uint64_t v) { return tm.mkBitVector(w, v); }
  cvc5::Term ex(const cvc5::Term& t, uint32_t hi, uint32_t lo) {
    return tm.mkTerm(tm.mkOp(cvc5::Kind::BITVECTOR_EXTRACT, {hi, lo}), {t});
  }
  cvc5::Term zext(const cvc5::Term& t, uint32_t n) { return tm.mkTerm(tm.mkOp(cvc5::Kind::BITVECTOR_ZERO_EXTEND, {n}), {t}); }

  // Demand `bits` of `root`; returns what reached x (target 0) and y (target 1).
  std::pair<std::vector<int>, std::vector<int>> run(const cvc5::Term& root, const std::vector<int>& bits) {
    Bit_demand g;
    g.add_target(x, 0);
    g.add_target(y, 1);
    Bit_set d(Bit_demand::width_of(root));
    for (int b : bits) {
      d.set(b);
    }
    g.demand(root, d);
    Bit_set sx(8), sy(8);
    g.run([&](int id, const Bit_set& nb) { (id == 0 ? sx : sy).merge(nb); });
    return {sx.bits(), sy.bits()};
  }
};

TEST(Observe, ExtractAndConcatRouteBitsExactly) {
  Fixture f;
  // concat(x[7:4], y[3:0]): bit 0..3 come from y[0..3], bits 4..7 from x[4..7].
  auto    t     = f.tm.mkTerm(cvc5::Kind::BITVECTOR_CONCAT, {f.ex(f.x, 7, 4), f.ex(f.y, 3, 0)});
  auto [xs, ys] = f.run(t, {1, 6});
  EXPECT_EQ(xs, (std::vector<int>{6}));
  EXPECT_EQ(ys, (std::vector<int>{1}));
}

TEST(Observe, ConstantAndMaskKillsBits) {
  Fixture f;
  // x & 0x0F: the high nibble of x cannot reach the result.
  auto    t     = f.tm.mkTerm(cvc5::Kind::BITVECTOR_AND, {f.x, f.bv(8, 0x0F)});
  auto [xs, ys] = f.run(t, {0, 1, 2, 3, 4, 5, 6, 7});
  EXPECT_EQ(xs, (std::vector<int>{0, 1, 2, 3}));
  EXPECT_TRUE(ys.empty());
  // x | 0xF0: same, for OR's dominating ones.
  auto u        = f.tm.mkTerm(cvc5::Kind::BITVECTOR_OR, {f.x, f.bv(8, 0xF0)});
  auto [xs2, _] = f.run(u, {0, 1, 2, 3, 4, 5, 6, 7});
  EXPECT_EQ(xs2, (std::vector<int>{0, 1, 2, 3}));
}

TEST(Observe, GroundShiftAmountAndMaskAreFolded) {
  Fixture f;
  // The encoder spells comptime arithmetic as terms cvc5 does not fold:
  // a mask `(zext 4 #b1111) << (zext 5 #b100)` == 0xF0, and x >> (1+2).
  auto    mask  = f.tm.mkTerm(cvc5::Kind::BITVECTOR_SHL, {f.zext(f.bv(4, 0xF), 4), f.zext(f.bv(3, 4), 5)});
  auto    t     = f.tm.mkTerm(cvc5::Kind::BITVECTOR_AND, {f.x, mask});
  auto [xs, ys] = f.run(t, {0, 1, 2, 3, 4, 5, 6, 7});
  EXPECT_EQ(xs, (std::vector<int>{4, 5, 6, 7}));
  auto amt      = f.tm.mkTerm(cvc5::Kind::BITVECTOR_ADD, {f.bv(8, 1), f.bv(8, 2)});
  auto sh       = f.tm.mkTerm(cvc5::Kind::BITVECTOR_LSHR, {f.x, amt});
  auto [xs2, _] = f.run(sh, {0});
  EXPECT_EQ(xs2, (std::vector<int>{3}));

  Ground_eval ge;
  const auto& v = ge.eval(mask);
  ASSERT_TRUE(v.has_value());
  EXPECT_EQ(*v, (Ground_eval::Bits{0, 0, 0, 0, 1, 1, 1, 1}));
  EXPECT_FALSE(ge.eval(t).has_value());  // reads the symbol x
}

TEST(Observe, IteWithComptimeConditionDemandsOneArm) {
  Fixture f;
  auto    cond  = f.tm.mkTerm(cvc5::Kind::EQUAL, {f.bv(4, 3), f.bv(4, 3)});
  auto    t     = f.tm.mkTerm(cvc5::Kind::ITE, {cond, f.x, f.y});
  auto [xs, ys] = f.run(t, {2});
  EXPECT_EQ(xs, (std::vector<int>{2}));
  EXPECT_TRUE(ys.empty());
  // A runtime condition keeps both arms (and every bit of the condition).
  auto rc         = f.tm.mkTerm(cvc5::Kind::EQUAL, {f.ex(f.x, 7, 7), f.bv(1, 1)});
  auto t2         = f.tm.mkTerm(cvc5::Kind::ITE, {rc, f.x, f.y});
  auto [xs2, ys2] = f.run(t2, {2});
  EXPECT_EQ(xs2, (std::vector<int>{2, 7}));
  EXPECT_EQ(ys2, (std::vector<int>{2}));
}

TEST(Observe, ArithmeticIsConservative) {
  Fixture f;
  // Carries move up: bit 3 of x + y needs bits 0..3 of both operands.
  auto    t     = f.tm.mkTerm(cvc5::Kind::BITVECTOR_ADD, {f.x, f.y});
  auto [xs, ys] = f.run(t, {3});
  EXPECT_EQ(xs, (std::vector<int>{0, 1, 2, 3}));
  EXPECT_EQ(ys, (std::vector<int>{0, 1, 2, 3}));
  // An operator the analysis does not model needs every operand bit.
  auto u          = f.tm.mkTerm(cvc5::Kind::BITVECTOR_UDIV, {f.x, f.y});
  auto [xs2, ys2] = f.run(u, {0});
  EXPECT_EQ(xs2.size(), 8U);
  EXPECT_EQ(ys2.size(), 8U);
}

TEST(Observe, FreeUnkeptBitsKeepsTheSharedBitsAndExtracts) {
  Fixture                f;
  const livehd::lec::Val v{f.x, 8, false};
  auto                   r = livehd::lec::free_unkept_bits(f.tm, v, 8, {0, 1, 5}, "fresh_");
  EXPECT_EQ(r.width, 8);
  // The kept positions still read x; the rest are fresh symbols.
  auto [xs, _] = f.run(r.term, {0, 1, 2, 3, 4, 5, 6, 7});
  EXPECT_EQ(xs, (std::vector<int>{0, 1, 5}));
  auto k = livehd::lec::extract_kept(f.tm, f.x, {0, 1, 5});
  EXPECT_EQ(Bit_demand::width_of(k), 3);
  auto [xs2, __] = f.run(k, {0, 1, 2});
  EXPECT_EQ(xs2, (std::vector<int>{0, 1, 5}));
}

}  // namespace
