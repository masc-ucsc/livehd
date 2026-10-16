// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "term_sim.hpp"

#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"

using namespace livehd::lec;
using cvc5::Kind;

namespace {

struct Fixture : testing::Test {
  cvc5::TermManager tm;
  std::mt19937_64   rng{0x7465726d73696dULL};

  cvc5::Term var(uint32_t w, const std::string& name) { return tm.mkConst(tm.mkBitVectorSort(w), name); }

  // cvc5's own value of `t` once every leaf is replaced by its simulator value.
  cvc5::Term cvc5_value(const Term_sim& sim, const cvc5::Term& t) {
    std::vector<cvc5::Term> from, to;
    for (size_t i = 0; i < sim.leaves().size(); ++i) {
      if (!sim.leaves()[i].is_array) {
        from.push_back(sim.leaves()[i].term);
        to.push_back(sim.leaf_value(i));
      }
    }
    cvc5::Solver s(tm);
    return s.simplify(t.substitute(from, to));
  }

  void randomize(Term_sim& sim) {
    for (size_t i = 0; i < sim.leaves().size(); ++i) {
      if (sim.leaves()[i].is_array) {
        sim.set_array_seed(i, rng());
        continue;
      }
      const auto mode = rng() % 6;
      auto*      w    = sim.leaf_words(i);
      for (uint32_t k = 0; k < sim.leaf_nwords(i); ++k) {
        w[k] = mode == 0 ? 0 : mode == 1 ? ~uint64_t{0} : mode == 2 ? (rng() % 4) : rng();
      }
    }
  }

  // A random bit-vector expression of width w over `pool` (per width).
  cvc5::Term expr(uint32_t w, int depth, std::vector<std::vector<cvc5::Term>>& pool, const std::vector<uint32_t>& widths) {
    auto leaf = [&](uint32_t ww) {
      auto& p = pool[ww];
      if (p.empty() || rng() % 4 == 0) {
        p.push_back(var(ww, "v" + std::to_string(ww) + "_" + std::to_string(p.size())));
      }
      return p[rng() % p.size()];
    };
    if (depth == 0 || rng() % 5 == 0) {
      return rng() % 7 == 0 ? tm.mkBitVector(w, rng() & (w >= 64 ? ~uint64_t{0} : ((uint64_t{1} << w) - 1))) : leaf(w);
    }
    auto sub = [&](uint32_t ww) { return expr(ww, depth - 1, pool, widths); };
    auto cmp = [&]() {
      static const Kind ks[] = {Kind::BITVECTOR_ULT,
                                Kind::BITVECTOR_ULE,
                                Kind::BITVECTOR_UGT,
                                Kind::BITVECTOR_UGE,
                                Kind::BITVECTOR_SLT,
                                Kind::BITVECTOR_SLE,
                                Kind::BITVECTOR_SGT,
                                Kind::BITVECTOR_SGE,
                                Kind::EQUAL,
                                Kind::DISTINCT};
      const uint32_t    ww   = widths[rng() % widths.size()];
      return tm.mkTerm(ks[rng() % std::size(ks)], {sub(ww), sub(ww)});
    };
    switch (rng() % 20) {
      case 0: {
        static const Kind ks[] = {Kind::BITVECTOR_ADD,
                                  Kind::BITVECTOR_SUB,
                                  Kind::BITVECTOR_MULT,
                                  Kind::BITVECTOR_AND,
                                  Kind::BITVECTOR_OR,
                                  Kind::BITVECTOR_XOR,
                                  Kind::BITVECTOR_NAND,
                                  Kind::BITVECTOR_NOR,
                                  Kind::BITVECTOR_XNOR};
        return tm.mkTerm(ks[rng() % std::size(ks)], {sub(w), sub(w)});
      }
      case 1: {
        static const Kind ks[]
            = {Kind::BITVECTOR_UDIV, Kind::BITVECTOR_UREM, Kind::BITVECTOR_SDIV, Kind::BITVECTOR_SREM, Kind::BITVECTOR_SMOD};
        return tm.mkTerm(ks[rng() % std::size(ks)], {sub(w), sub(w)});
      }
      case 2: {
        static const Kind ks[] = {Kind::BITVECTOR_SHL, Kind::BITVECTOR_LSHR, Kind::BITVECTOR_ASHR};
        return tm.mkTerm(ks[rng() % std::size(ks)], {sub(w), sub(w)});
      }
      case 3: return tm.mkTerm(rng() % 2 ? Kind::BITVECTOR_NOT : Kind::BITVECTOR_NEG, {sub(w)});
      case 4: return tm.mkTerm(Kind::ITE, {cmp(), sub(w), sub(w)});
      case 5:
        if (w > 1) {
          const uint32_t lo = static_cast<uint32_t>(rng() % (w - 1)) + 1;
          return tm.mkTerm(Kind::BITVECTOR_CONCAT, {sub(w - lo), sub(lo)});
        }
        break;
      case 6: {
        const uint32_t extra = static_cast<uint32_t>(rng() % 70);
        const uint32_t from  = w + extra;
        const uint32_t lo    = static_cast<uint32_t>(rng() % (extra + 1));
        return tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {lo + w - 1, lo}), {sub(from)});
      }
      case 7:
        if (w > 1) {
          const uint32_t from = static_cast<uint32_t>(rng() % (w - 1)) + 1;
          return tm.mkTerm(tm.mkOp(rng() % 2 ? Kind::BITVECTOR_ZERO_EXTEND : Kind::BITVECTOR_SIGN_EXTEND, {w - from}), {sub(from)});
        }
        break;
      case 8:
        if (w % 2 == 0) {
          return tm.mkTerm(tm.mkOp(Kind::BITVECTOR_REPEAT, {2}), {sub(w / 2)});
        }
        break;
      case 9:
        return tm.mkTerm(tm.mkOp(rng() % 2 ? Kind::BITVECTOR_ROTATE_LEFT : Kind::BITVECTOR_ROTATE_RIGHT,
                                 {static_cast<uint32_t>(rng() % (2 * w + 1))}),
                         {sub(w)});
      case 10:
        if (w == 1) {
          static const Kind ks[] = {Kind::BITVECTOR_COMP, Kind::BITVECTOR_ULTBV, Kind::BITVECTOR_SLTBV};
          const uint32_t    ww   = widths[rng() % widths.size()];
          return tm.mkTerm(ks[rng() % std::size(ks)], {sub(ww), sub(ww)});
        }
        break;
      case 11:
        if (w == 1) {
          const uint32_t ww = widths[rng() % widths.size()];
          return tm.mkTerm(rng() % 2 ? Kind::BITVECTOR_REDOR : Kind::BITVECTOR_REDAND, {sub(ww)});
        }
        break;
      default: break;
    }
    return tm.mkTerm(Kind::BITVECTOR_ADD, {sub(w), sub(w)});
  }
};

}  // namespace

// Native evaluation agrees with cvc5 on every QF_BV operator, at word-sized,
// sub-word and multi-word widths, including division by zero and over-wide
// shift amounts (biased leaf values hit both).
TEST_F(Fixture, MatchesCvc5OnRandomBitVectorTerms) {
  const std::vector<uint32_t>          widths{1, 3, 8, 63, 64, 65, 100, 130};
  std::vector<std::vector<cvc5::Term>> pool(300);
  for (int round = 0; round < 60; ++round) {
    std::vector<cvc5::Term> roots;
    for (int k = 0; k < 6; ++k) {
      roots.push_back(expr(widths[rng() % widths.size()], 4, pool, widths));
    }
    Term_sim    sim(tm);
    std::string why;
    ASSERT_TRUE(sim.build({}, roots, why)) << why;
    for (int trial = 0; trial < 6; ++trial) {
      randomize(sim);
      ASSERT_TRUE(sim.eval());
      for (size_t r = 0; r < roots.size(); ++r) {
        const auto expect = cvc5_value(sim, roots[r]);
        ASSERT_EQ(sim.root_value(r), expect) << roots[r];
      }
    }
  }
}

// Definitions (EQUAL(symbol, term)) are followed, other assertions are
// constraints, and memories read their stores or seeded power-on content;
// pins() reproduces the evaluation exactly in a solver.
TEST_F(Fixture, DefinitionsConstraintsAndMemories) {
  const auto              asort = tm.mkArraySort(tm.mkBitVectorSort(4), tm.mkBitVectorSort(16));
  const auto              m0    = tm.mkConst(asort, "m0");
  const auto              m1    = tm.mkConst(asort, "m1");
  const auto              a     = var(4, "a");
  const auto              b     = var(4, "b");
  const auto              d     = var(16, "d");
  const auto              st1   = var(16, "st1");
  const auto              zero  = tm.mkConstArray(asort, tm.mkBitVector(16, 7));
  std::vector<cvc5::Term> base{
      tm.mkTerm(Kind::EQUAL, {m1, tm.mkTerm(Kind::STORE, {m0, a, d})}),
      tm.mkTerm(Kind::EQUAL, {st1, tm.mkTerm(Kind::SELECT, {m1, b})}),
      tm.mkTerm(Kind::DISTINCT, {a, tm.mkBitVector(4, 15)}),  // a constraint, not a definition
  };
  const auto  read_b   = tm.mkTerm(Kind::SELECT, {m1, b});
  const auto  const_rd = tm.mkTerm(Kind::SELECT, {tm.mkTerm(Kind::STORE, {zero, a, d}), b});
  const auto  root     = tm.mkTerm(Kind::BITVECTOR_ADD, {st1, read_b, const_rd});
  Term_sim    sim(tm);
  std::string why;
  ASSERT_TRUE(sim.build(base, {root}, why)) << why;
  EXPECT_EQ(sim.constraints(), 1u);
  EXPECT_TRUE(sim.has_arrays());
  EXPECT_EQ(sim.leaf_index(st1), -1) << "a defined symbol is not free";
  int rejected = 0;
  for (int trial = 0; trial < 40; ++trial) {
    randomize(sim);
    if (!sim.eval(true)) {
      ++rejected;
      continue;
    }
    cvc5::Solver s(tm);
    s.setLogic("QF_ABV");
    s.setOption("produce-models", "true");
    s.setOption("arrays-exp", "true");  // constant arrays
    for (const auto& f : base) {
      s.assertFormula(f);
    }
    for (const auto& p : sim.pins()) {
      s.assertFormula(p);
    }
    s.assertFormula(tm.mkTerm(Kind::DISTINCT, {root, sim.root_value(0)}));
    ASSERT_TRUE(s.checkSat().isUnsat()) << "the pinned evaluation must determine the root";
  }
  EXPECT_GT(rejected, 0) << "a = 15 violates the constraint and must be rejected";
  EXPECT_LT(rejected, 40);
}

// reduce(): the constant-folded formula over the symbolic leaves is
// equisatisfiable with the original one under the same pins.
TEST_F(Fixture, ReduceIsEquisatisfiable) {
  const std::vector<uint32_t>          widths{1, 4, 8, 64, 70};
  std::vector<std::vector<cvc5::Term>> pool(300);
  for (int round = 0; round < 40; ++round) {
    const auto  t1   = expr(8, 4, pool, widths);
    const auto  t2   = expr(8, 4, pool, widths);
    const auto  goal = tm.mkTerm(Kind::EQUAL, {t1, t2});
    Term_sim    sim(tm);
    std::string why;
    ASSERT_TRUE(sim.build({}, {goal}, why)) << why;
    randomize(sim);
    ASSERT_TRUE(sim.eval());
    std::vector<bool> symbolic(sim.leaves().size());
    for (size_t i = 0; i < symbolic.size(); ++i) {
      symbolic[i] = rng() % 3 == 0;
    }
    const auto red = sim.reduce(symbolic, {0});
    EXPECT_FALSE(red.vacuous);
    cvc5::Solver full(tm);
    full.setLogic("QF_BV");
    full.assertFormula(goal);
    for (size_t i = 0; i < symbolic.size(); ++i) {
      if (!symbolic[i]) {
        full.assertFormula(tm.mkTerm(Kind::EQUAL, {sim.leaves()[i].term, sim.leaf_value(i)}));
      }
    }
    cvc5::Solver small(tm);
    small.setLogic("QF_BV");
    for (const auto& a : red.assertions) {
      small.assertFormula(a);
    }
    small.assertFormula(red.roots[0]);
    EXPECT_EQ(full.checkSat().isSat(), small.checkSat().isSat()) << goal;
  }
}

// An assertion no assignment satisfies rejects every evaluation and makes
// every reduction vacuous: sampling must never count such a trace.
TEST_F(Fixture, FalseConstraintRejectsEverything) {
  const auto  a = var(8, "a");
  Term_sim    sim(tm);
  std::string why;
  ASSERT_TRUE(sim.build({tm.mkFalse()}, {tm.mkTerm(Kind::EQUAL, {a, a})}, why)) << why;
  randomize(sim);
  EXPECT_FALSE(sim.eval());
  EXPECT_TRUE(sim.reduce(std::vector<bool>(sim.leaves().size(), true), {0}).vacuous);
}

// Outside the fragment (an uninterpreted function) build() refuses, so the
// sampler keeps its solver-only path.
TEST_F(Fixture, RefusesUninterpretedFunctions) {
  const auto  bv8 = tm.mkBitVectorSort(8);
  const auto  f   = tm.mkConst(tm.mkFunctionSort({bv8}, bv8), "f");
  const auto  a   = var(8, "a");
  Term_sim    sim(tm);
  std::string why;
  EXPECT_FALSE(sim.build({}, {tm.mkTerm(Kind::EQUAL, {tm.mkTerm(Kind::APPLY_UF, {f, a}), a})}, why));
  EXPECT_FALSE(why.empty());
}
