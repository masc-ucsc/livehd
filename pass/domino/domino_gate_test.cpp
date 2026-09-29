// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "domino_gate.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <map>
#include <print>
#include <random>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

using livehd::domino::cofactor;
using livehd::domino::dual;
using livehd::domino::Fit_reason;
using livehd::domino::Gate_limits;
using livehd::domino::kInfCost;
using livehd::domino::prime_implicants;
using livehd::domino::Rail;
using livehd::domino::Sp_factorer;
using livehd::domino::Sp_formula;
using livehd::domino::support_mask;
using livehd::domino::Truth;
using livehd::domino::truth_and;
using livehd::domino::truth_const;
using livehd::domino::truth_not;
using livehd::domino::truth_or;
using livehd::domino::truth_var;
using livehd::domino::truth_xor;
using livehd::domino::Unate;
using livehd::domino::unateness;

Truth v(int j, int n) { return truth_var(j, n); }

// Every point of the formula agrees with the table.
void expect_formula_matches(const Sp_formula& f, const Truth& t, int n) {
  for (int x = 0; x < (1 << n); ++x) {
    ASSERT_EQ(f.evaluate(static_cast<uint32_t>(x)), t.get(x)) << "at x=" << x << " formula " << f.to_string();
  }
}

// Positive unate SOP from a list of variable masks.
Truth sop(const std::vector<uint32_t>& cubes, int n) {
  Truth t = truth_const(false, n);
  for (auto c : cubes) {
    Truth cube = truth_const(true, n);
    for (int j = 0; j < n; ++j) {
      if ((c >> j) & 1u) {
        cube = truth_and(cube, v(j, n));
      }
    }
    t = truth_or(t, cube);
  }
  return t;
}

// ---------------------------------------------------------------------------

TEST(Truth, VarCofactorDual) {
  const int n  = 3;
  Truth     ab = truth_and(v(0, n), v(1, n));
  EXPECT_TRUE(ab.get(0b011));
  EXPECT_FALSE(ab.get(0b101));
  EXPECT_EQ(support_mask(ab, n), 0b011u);
  // cofactor a=1 of ab is b; a=0 is 0
  EXPECT_EQ(cofactor(ab, n, 0, true), v(1, n));
  EXPECT_EQ(cofactor(ab, n, 0, false), truth_const(false, n));
  // dual of AND is OR and back
  EXPECT_EQ(dual(ab, n), truth_or(v(0, n), v(1, n)));
  EXPECT_EQ(dual(dual(ab, n), n), ab);
  // bits round trip: f = a (n=2) is "1010"
  EXPECT_EQ(livehd::domino::to_bits(v(0, 2), 2), "1010");
  EXPECT_EQ(livehd::domino::truth_from_bits("1010", 2), v(0, 2));
}

TEST(Truth, Unateness) {
  const int n    = 3;
  auto      and3 = truth_and(truth_and(v(0, n), v(1, n)), v(2, n));
  auto      u    = unateness(and3, n);
  EXPECT_EQ(u[0], Unate::positive);
  EXPECT_EQ(u[2], Unate::positive);
  u = unateness(truth_not(and3, n), n);  // NAND: negative in all
  EXPECT_EQ(u[0], Unate::negative);
  EXPECT_EQ(u[1], Unate::negative);
  u = unateness(truth_xor(v(0, n), v(1, n)), n);  // XOR: binate, c independent
  EXPECT_EQ(u[0], Unate::binate);
  EXPECT_EQ(u[1], Unate::binate);
  EXPECT_EQ(u[2], Unate::independent);
  // MUX: s ? a : b  == s*a + !s*b ; binate in s, positive in a and b
  auto mux = truth_or(truth_and(v(2, n), v(0, n)), truth_and(truth_not(v(2, n), n), v(1, n)));
  u        = unateness(mux, n);
  EXPECT_EQ(u[0], Unate::positive);
  EXPECT_EQ(u[1], Unate::positive);
  EXPECT_EQ(u[2], Unate::binate);
}

TEST(Truth, PrimeImplicants) {
  const int n   = 3;
  auto      maj = sop({0b011, 0b101, 0b110}, n);
  auto      p   = prime_implicants(maj, n);
  ASSERT_EQ(p.size(), 3u);
  EXPECT_EQ(p[0], 0b011u);
  EXPECT_EQ(p[1], 0b101u);
  EXPECT_EQ(p[2], 0b110u);
  // primes of the dual are the prime implicates: maj is self-dual
  EXPECT_EQ(prime_implicants(dual(maj, n), n), p);
  // OR3 has one implicate of size 3
  auto or3 = sop({0b001, 0b010, 0b100}, n);
  auto q   = prime_implicants(dual(or3, n), n);
  ASSERT_EQ(q.size(), 1u);
  EXPECT_EQ(q[0], 0b111u);
}

// ---------------------------------------------------------------------------

TEST(Factor, BasicGates) {
  Sp_factorer F;
  const int   n    = 3;
  auto        and2 = sop({0b011}, n);
  auto        f    = F.factor(and2, n, 4);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->transistors, 2);
  EXPECT_EQ(f->stack, 2);
  EXPECT_EQ(f->width, 1);
  EXPECT_EQ(f->to_string(), "ab");
  expect_formula_matches(*f, and2, n);

  auto or2 = sop({0b001, 0b010}, n);
  f        = F.factor(or2, n, 4);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->transistors, 2);
  EXPECT_EQ(f->stack, 1);
  EXPECT_EQ(f->width, 2);
  EXPECT_EQ(f->to_string(), "a+b");

  // AO21: ab + c
  auto ao21 = sop({0b011, 0b100}, n);
  f         = F.factor(ao21, n, 4);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->transistors, 3);
  EXPECT_EQ(f->stack, 2);
  EXPECT_EQ(f->to_string(), "ab+c");
  expect_formula_matches(*f, ao21, n);

  // majority: 5 transistors, a(b+c)+bc or a permutation of it, stack 2
  auto maj = sop({0b011, 0b101, 0b110}, n);
  f        = F.factor(maj, n, 4);
  ASSERT_TRUE(f.has_value());
  EXPECT_EQ(f->transistors, 5);
  EXPECT_EQ(f->stack, 2);
  expect_formula_matches(*f, maj, n);
  // the SOP itself is 6; a stack bound of 1 is impossible (primes have size 2)
  EXPECT_FALSE(F.factor(maj, n, 1).has_value());
  EXPECT_EQ(F.cost(maj, n, 1), kInfCost);
}

TEST(Factor, StackBoundChangesShape) {
  // f = a b c + d: stack 3 needs the SOP (4 transistors); stack 2 is impossible.
  Sp_factorer F;
  const int   n = 4;
  auto        f = sop({0b0111, 0b1000}, n);
  auto        r = F.factor(f, n, 3);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->transistors, 4);
  EXPECT_EQ(r->stack, 3);
  EXPECT_FALSE(F.factor(f, n, 2).has_value());
  // g = (a+b)(c+d): stack 2, 4 transistors; its SOP would be 8.
  auto g = sop({0b0101, 0b0110, 0b1001, 0b1010}, n);
  r      = F.factor(g, n, 2);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->transistors, 4);
  EXPECT_EQ(r->stack, 2);
  EXPECT_EQ(r->width, 2);
  expect_formula_matches(*r, g, n);
}

TEST(Factor, PathAndSharing) {
  // ab + bc + cd: 5 transistors (b(a+c)+cd or ab+c(b+d)), stack 2.
  Sp_factorer F;
  const int   n = 4;
  auto        f = sop({0b0011, 0b0110, 0b1100}, n);
  auto        r = F.factor(f, n, 4);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->transistors, 5);
  EXPECT_EQ(r->stack, 2);
  expect_formula_matches(*r, f, n);
  // threshold-2 of 4 (any two of a,b,c,d): (a+b)(c+d)+ab+cd = 8
  auto t2 = sop({0b0011, 0b0101, 0b1001, 0b0110, 0b1010, 0b1100}, n);
  r       = F.factor(t2, n, 4);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->transistors, 8);
  EXPECT_EQ(r->stack, 2);
  expect_formula_matches(*r, t2, n);
}

// ---------------------------------------------------------------------------
// Brute force: the exact minimum-literal monotone formula for every monotone
// function of n <= 4 variables under every stack bound, by closure of the
// literals under AND (stacks add) and OR (stacks max). The DP must match.

struct Exact {
  int                                     n;
  std::map<std::pair<uint32_t, int>, int> best;  // (table, stack<=d) -> min literals

  explicit Exact(int n_) : n(n_) {
    const int      pts = 1 << n;
    const uint32_t all = pts == 32 ? 0xffffffffu : ((1u << pts) - 1);
    auto           put = [&](uint32_t t, int d, int c) {
      if (t == 0 || t == all) {
        return;  // constants are not gates
      }
      auto key = std::make_pair(t, d);
      auto it  = best.find(key);
      if (it == best.end() || c < it->second) {
        best[key] = c;
      }
    };
    for (int j = 0; j < n; ++j) {
      uint32_t t = 0;
      for (int x = 0; x < pts; ++x) {
        if ((x >> j) & 1) {
          t |= (1u << x);
        }
      }
      for (int d = 1; d <= n; ++d) {
        put(t, d, 1);
      }
    }
    // Relax until no state improves. Costs are >= 1, so the fixpoint is the exact minimum.
    for (bool changed = true; changed;) {
      changed   = false;
      auto snap = best;  // iterate over a snapshot; insert into best
      for (const auto& [ka, ca] : snap) {
        for (const auto& [kb, cb] : snap) {
          const auto [ta, da] = ka;
          const auto [tb, db] = kb;
          // AND: stacks add
          if (da + db <= n) {
            const uint32_t t = ta & tb;
            for (int d = da + db; d <= n; ++d) {
              auto key = std::make_pair(t, d);
              auto it  = best.find(key);
              if (t != 0 && t != all && (it == best.end() || ca + cb < it->second)) {
                best[key] = ca + cb;
                changed   = true;
              }
            }
          }
          // OR: stacks max
          {
            const uint32_t t = ta | tb;
            for (int d = std::max(da, db); d <= n; ++d) {
              auto key = std::make_pair(t, d);
              auto it  = best.find(key);
              if (t != 0 && t != all && (it == best.end() || ca + cb < it->second)) {
                best[key] = ca + cb;
                changed   = true;
              }
            }
          }
        }
      }
    }
  }
  int cost(uint32_t t, int d) const {
    auto it = best.find({t, d});
    return it == best.end() ? kInfCost : it->second;
  }
};

Truth from_u32(uint32_t t, int n) {
  Truth out;
  for (int x = 0; x < (1 << n); ++x) {
    out.set(x, ((t >> x) & 1u) != 0);
  }
  return out;
}

// Returns the number of (function, stack bound) pairs where the factorer's cost
// differs from the brute force. `exact_small` false runs the heuristic search
// on these small functions too, which measures the heuristic itself.
int check_exact(int n, bool exact_small) {
  Exact       ex(n);
  Sp_factorer F;
  F.set_exact_small(exact_small);
  int count = 0, mismatches = 0;
  for (const auto& [key, c] : ex.best) {
    const auto [t, d] = key;
    if (d != n) {
      continue;
    }
    ++count;
    const Truth f = from_u32(t, n);
    for (int dd = 1; dd <= n; ++dd) {
      const int ours = F.cost(f, n, dd);
      if (ours != ex.cost(t, dd)) {
        ++mismatches;
        std::println("heuristic mismatch: n={} f={} stack<={} ours={} exact={}",
                     n,
                     livehd::domino::to_bits(f, n),
                     dd,
                     ours,
                     ex.cost(t, dd));
        if (exact_small) {
          ADD_FAILURE() << "n=" << n << " f=" << livehd::domino::to_bits(f, n) << " stack<=" << dd << " ours=" << ours
                        << " exact=" << ex.cost(t, dd);
        }
      }
      EXPECT_GE(ours, ex.cost(t, dd)) << "cheaper than the exact minimum: the formula must be wrong";
      if (ours < kInfCost) {
        auto r = F.factor(f, n, dd);
        EXPECT_TRUE(r.has_value()) << "cost finite but no formula";
        if (!r.has_value()) {
          continue;
        }
        EXPECT_EQ(r->transistors, ours);
        EXPECT_LE(r->stack, dd);
        expect_formula_matches(*r, f, n);
      }
    }
  }
  // Dedekind numbers minus the two constants: 4, 18, 166
  const int expected = n == 2 ? 4 : n == 3 ? 18 : 166;
  EXPECT_EQ(count, expected);
  return mismatches;
}

TEST(Factor, ExactTableMatchesBruteForce) {
  EXPECT_EQ(check_exact(2, true), 0);
  EXPECT_EQ(check_exact(3, true), 0);
  EXPECT_EQ(check_exact(4, true), 0);
}

// The heuristic search (what runs for 5..8 variables) measured on <= 4
// variables. Zero mismatches means the candidate set is complete there.
TEST(Factor, HeuristicMatchesBruteForceOnSmallFunctions) {
  EXPECT_EQ(check_exact(2, false), 0);
  EXPECT_EQ(check_exact(3, false), 0);
  const int m4 = check_exact(4, false);
  EXPECT_EQ(m4, 0) << m4 << " of 664 (function, stack) pairs of 4 variables are not minimal under the heuristic";
}

// ---------------------------------------------------------------------------

TEST(Factor, RandomLargeFunctionsEvaluate) {
  Sp_factorer  F;
  std::mt19937 rng(12345);
  const auto   t0     = std::chrono::steady_clock::now();
  int          fitted = 0;
  for (int iter = 0; iter < 200; ++iter) {
    const int             n = 6 + static_cast<int>(rng() % 3);  // 6..8
    std::vector<uint32_t> cubes;
    const int             ncubes = 2 + static_cast<int>(rng() % 5);
    for (int i = 0; i < ncubes; ++i) {
      uint32_t  c    = 0;
      const int size = 1 + static_cast<int>(rng() % 4);
      for (int k = 0; k < size; ++k) {
        c |= 1u << (rng() % static_cast<uint32_t>(n));
      }
      cubes.push_back(c);
    }
    const Truth f = sop(cubes, n);
    if (livehd::domino::is_const(f, n)) {
      continue;
    }
    for (int d = 2; d <= 4; ++d) {
      auto r = F.factor(f, n, d);
      if (!r.has_value()) {
        // impossible only when a prime is wider than d
        int maxp = 0;
        for (auto p : prime_implicants(f, n)) {
          maxp = std::max(maxp, std::popcount(p));
        }
        EXPECT_GT(maxp, d);
        continue;
      }
      ++fitted;
      EXPECT_LE(r->stack, d);
      EXPECT_EQ(r->transistors, F.cost(f, n, d));
      expect_formula_matches(*r, f, n);
      // never worse than the SOP itself
      int sop_lits = 0;
      for (auto p : prime_implicants(f, n)) {
        sop_lits += std::popcount(p);
      }
      EXPECT_LE(r->transistors, sop_lits);
    }
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  EXPECT_GT(fitted, 100);
  EXPECT_LT(ms, 15000) << "factoring 200 random 6-8 variable functions took " << ms << " ms";
}

// ---------------------------------------------------------------------------

TEST(Fit, RailsAndReasons) {
  Sp_factorer F;
  Gate_limits lim{.k = 8, .stack = 4, .budget = 16};
  const int   n = 3;

  // AND2 positive rail
  auto and2 = truth_and(v(0, n), v(1, n));
  auto g    = F.fit(and2, n, lim, Rail::pos);
  EXPECT_TRUE(g.ok());
  EXPECT_EQ(g.support, 0b011u);
  EXPECT_EQ(g.leaf_rail[0], Rail::pos);
  EXPECT_EQ(g.formula->transistors, 2);
  EXPECT_EQ(g.formula->stack, 2);

  // NAND2 as a gate: negative unate in both, network a'+b' (1 stack)
  g = F.fit(truth_not(and2, n), n, lim, Rail::pos);
  EXPECT_TRUE(g.ok());
  EXPECT_EQ(g.leaf_rail[0], Rail::neg);
  EXPECT_EQ(g.leaf_rail[1], Rail::neg);
  EXPECT_EQ(g.formula->to_string(), "a+b");
  EXPECT_EQ(g.formula->stack, 1);
  // ...which is the same question as the negative rail of AND2
  auto g2 = F.fit(and2, n, lim, Rail::neg);
  EXPECT_TRUE(g2.ok());
  EXPECT_EQ(g2.formula->to_string(), "a+b");

  // AOI21 = !(ab + c): all negative, network (a+b)c over the negated rails
  auto aoi = truth_not(sop({0b011, 0b100}, n), n);
  g        = F.fit(aoi, n, lim, Rail::pos);
  EXPECT_TRUE(g.ok());
  EXPECT_EQ(g.formula->transistors, 3);
  EXPECT_EQ(g.formula->stack, 2);
  EXPECT_EQ(g.leaf_rail[2], Rail::neg);
  expect_formula_matches(*g.formula, sop({0b101, 0b110}, n), n);  // (a+b)c

  // XOR: binate, no single gate in either rail
  auto x = truth_xor(v(0, n), v(1, n));
  g      = F.fit(x, n, lim, Rail::pos);
  EXPECT_EQ(g.reason, Fit_reason::binate);
  EXPECT_EQ(g.binate_mask, 0b011u);
  EXPECT_EQ(F.fit(x, n, lim, Rail::neg).reason, Fit_reason::binate);

  // MUX: binate in the select only
  auto mux = truth_or(truth_and(v(2, n), v(0, n)), truth_and(truth_not(v(2, n), n), v(1, n)));
  g        = F.fit(mux, n, lim, Rail::pos);
  EXPECT_EQ(g.reason, Fit_reason::binate);
  EXPECT_EQ(g.binate_mask, 0b100u);

  // constants
  EXPECT_EQ(F.fit(truth_const(true, n), n, lim).reason, Fit_reason::constant);
  EXPECT_EQ(F.fit(truth_const(false, n), n, lim).reason, Fit_reason::constant);
}

TEST(Fit, WideGatesAndTheDualStackRule) {
  Sp_factorer F;
  Gate_limits lim{.k = 8, .stack = 4, .budget = 16};
  const int   n    = 8;
  Truth       or8  = truth_const(false, n);
  Truth       and8 = truth_const(true, n);
  for (int j = 0; j < n; ++j) {
    or8  = truth_or(or8, v(j, n));
    and8 = truth_and(and8, v(j, n));
  }
  // OR8 positive: 8 parallel transistors, stack 1
  auto g = F.fit(or8, n, lim, Rail::pos);
  ASSERT_TRUE(g.ok());
  EXPECT_EQ(g.formula->transistors, 8);
  EXPECT_EQ(g.formula->stack, 1);
  EXPECT_EQ(g.formula->width, 8);
  // its twin (NOR8 over negated rails) is an 8-deep stack: fails D=4
  g = F.fit(or8, n, lim, Rail::neg);
  EXPECT_EQ(g.reason, Fit_reason::stack);
  EXPECT_EQ(g.max_prime, 8);
  // AND8 the other way round
  EXPECT_EQ(F.fit(and8, n, lim, Rail::pos).reason, Fit_reason::stack);
  g = F.fit(and8, n, lim, Rail::neg);
  ASSERT_TRUE(g.ok());
  EXPECT_EQ(g.formula->stack, 1);
  // fan-in limit
  Gate_limits k4{.k = 4, .stack = 4, .budget = 16};
  EXPECT_EQ(F.fit(or8, n, k4, Rail::pos).reason, Fit_reason::fanin);
  // budget limit: majority needs 5, give it 4 (then 5)
  Gate_limits tight{.k = 8, .stack = 4, .budget = 4};
  auto        maj = sop({0b011, 0b101, 0b110}, 3);
  g               = F.fit(maj, 3, tight, Rail::pos);
  EXPECT_EQ(g.reason, Fit_reason::budget);
  EXPECT_FALSE(g.formula.has_value());
  tight.budget = 5;
  g            = F.fit(maj, 3, tight, Rail::pos);
  ASSERT_TRUE(g.ok());
  EXPECT_EQ(g.formula->transistors, 5);
}

TEST(Fit, StudyDesignPointExamples) {
  // The worked examples of pass/domino/README.md section 1.5.
  Sp_factorer F;
  Gate_limits lim;  // k=8, D=4, B=16
  const int   n = 4;
  // q = (a & b) | (c & ~d): negative in d only, network ab + cd' , 4 transistors
  auto        q = truth_or(truth_and(v(0, n), v(1, n)), truth_and(v(2, n), truth_not(v(3, n), n)));
  auto        g = F.fit(q, n, lim, Rail::pos);
  ASSERT_TRUE(g.ok());
  EXPECT_EQ(g.leaf_rail[3], Rail::neg);
  EXPECT_EQ(g.leaf_rail[0], Rail::pos);
  EXPECT_EQ(g.formula->transistors, 4);
  EXPECT_EQ(g.formula->stack, 2);
  EXPECT_EQ(g.formula->to_string(), "ab+cd");
  // its twin: !q = (a'+b')(c'+d), 4 transistors, stack 2
  g = F.fit(q, n, lim, Rail::neg);
  ASSERT_TRUE(g.ok());
  EXPECT_EQ(g.formula->transistors, 4);
  EXPECT_EQ(g.formula->stack, 2);
  EXPECT_EQ(g.leaf_rail[3], Rail::pos);
}

}  // namespace

namespace {

// Worst cases for the search: the symmetric threshold functions of eight
// variables (T4 has 70 prime implicants). Under the real gate budgets the
// branch-and-bound must reject or solve each of them quickly; this bounds what
// the mapper pays per distinct cut function.
TEST(Factor, EightVariableThresholdsUnderBudgetAreFast) {
  Sp_factorer F;
  const int   n = 8;
  for (int budget : {16, 24}) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int th = 1; th <= n; ++th) {
      Truth f;
      for (int x = 0; x < (1 << n); ++x) {
        f.set(x, std::popcount(static_cast<unsigned>(x)) >= th);
      }
      for (int d : {4, 8}) {
        const auto t1 = std::chrono::steady_clock::now();
        auto       r  = F.factor(f, n, d, budget);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count();
        std::println("  T{}(8) stack<={} budget {}: {} ms, memo {}, cost {}",
                     th,
                     d,
                     budget,
                     ms,
                     F.memo_size(),
                     r ? r->transistors : -1);
        if (th > d) {
          EXPECT_FALSE(r.has_value());  // the smallest prime has th literals
          continue;
        }
        if (r.has_value()) {
          EXPECT_LE(r->stack, d);
          EXPECT_LE(r->transistors, budget);
          expect_formula_matches(*r, f, n);
        }
        // OR8 / AND8 are 8 transistors; T2(8) is 24 (an n log n construction).
        if (th == 1 || th == 8) {
          ASSERT_TRUE(r.has_value());
          EXPECT_EQ(r->transistors, 8);
        }
        if (th == 2 && d == 8) {
          EXPECT_EQ(r.has_value(), budget >= 24);
        }
      }
    }
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
#ifdef NDEBUG
    const long limit_ms = 8000;  // -c opt: measured ~4 s for both budgets together
#else
    const long limit_ms = 40000;  // -c dbg is 5-6x slower; the whole test must stay under 60 s
#endif
    EXPECT_LT(ms, limit_ms) << "budget " << budget << " took " << ms << " ms";
  }
}

}  // namespace
