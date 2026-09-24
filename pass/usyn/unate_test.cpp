// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "unate.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <random>
#include <set>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
Truth_table table(uint32_t n, uint64_t bits) {
  Truth_table t(n);
  t.words[0] = bits;
  return t;
}
}  // namespace
TEST(Unate, EveryFourInputFunctionHasVerifiedTotalCover) {
  for (uint32_t bits = 0; bits < 65536; ++bits) {
    const auto t = table(4, bits);
    Budget     work{100000};
    auto       f = make_form(t, 64, 4, work);
    ASSERT_EQ(f.status, Status::feasible) << bits;
    // Deliberately evaluate without using check_form/covers.
    for (uint32_t x = 0; x < 16; ++x) {
      bool y = false;
      for (auto c : f.cubes) {
        bool term = true;
        for (uint32_t j = 0; j < 4; ++j) {
          if ((c.care >> j) & 1) {
            term &= ((x >> j) & 1) == ((c.ones >> j) & 1);
          }
        }
        y |= term;
      }
      ASSERT_EQ(y, ((bits >> x) & 1) != 0) << bits << ':' << x;
    }
  }
}

TEST(Unate, EveryThreeInputPartialFunctionHasAValidTotalCompletion) {
  for (uint32_t code = 0; code < 6561; ++code) {
    Truth_table onset(3), care(3);
    auto        digits = code;
    for (uint32_t x = 0; x < 8; ++x) {
      const auto digit  = digits % 3;
      digits           /= 3;
      care.set(x, digit != 2);
      onset.set(x, digit == 1);
    }
    Budget     budget{100000};
    const auto form = make_form(onset, care, 24, 3, budget);
    ASSERT_EQ(form.status, Status::feasible) << code;
    for (uint32_t x = 0; x < 8; ++x) {
      bool actual = false;
      for (const auto& cube : form.cubes) {
        actual |= (x & cube.care) == cube.ones;
      }
      if (care.get(x)) {
        ASSERT_EQ(actual, onset.get(x)) << code << ':' << x;
      }
    }
  }
  Budget     budget{10000};
  const auto partial = make_form(table(2, 6), table(2, 7), 2, 1, budget);
  ASSERT_EQ(partial.status, Status::feasible);
  EXPECT_EQ(partial.positive, 3);
  EXPECT_EQ(partial.negative, 0);
  EXPECT_EQ(partial.series, 1);  // XOR on this image completes to OR.
}

TEST(Unate, ConsensusPrimeDoesNotForceSeriesDepth) {
  Truth_table mux(5);
  for (uint32_t x = 0; x < 32; ++x) {
    mux.set(x, (x & 1) ? (x & 6) == 6 : (x & 24) == 24);
  }
  Budget work{100000};
  auto   f = make_form(mux, 6, 3, work);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.series, 3);
  EXPECT_EQ(f.literals, 6);
}

TEST(Unate, DynamicTablesAndExplicitExhaustion) {
  Truth_table parity(9);
  for (uint32_t x = 0; x < 512; ++x) {
    parity.set(x, (__builtin_popcount(x) & 1) != 0);
  }
  Budget enough{10000000};
  auto   f = make_form(parity, 2304, 9, enough);
  ASSERT_EQ(f.status, Status::feasible);
  EXPECT_EQ(f.cubes.size(), 256);
  Budget none{1};
  EXPECT_EQ(make_form(parity, 2304, 9, none).status, Status::search_exhausted);
  EXPECT_TRUE(none.exhausted);
  Budget too_small{1000000};
  EXPECT_EQ(make_form(parity, 4, 9, too_small).status, Status::search_exhausted);
  EXPECT_FALSE(too_small.exhausted);  // greedy complexity failure, not impossibility
}

TEST(UnateSplit, ExactFormIsMinimalAndFactoringAdmitsProductsOfSums) {
  // (a+b)(c+d)(e+f): 8 cubes of 3 literals (24 > 16) as an SOP, 6 factored.
  Truth_table t(6);
  for (uint32_t x = 0; x < 64; ++x) {
    t.set(x, ((x & 3) != 0) && ((x & 12) != 0) && ((x & 48) != 0));
  }
  Budget     b{100000000};
  const auto sop = exact_form(t, 16, 4, false, b);
  const auto fac = exact_form(t, 16, 4, true, b);
  EXPECT_EQ(sop.status, Status::search_exhausted);
  ASSERT_EQ(fac.status, Status::feasible);
  EXPECT_EQ(fac.literals, 24u);
  EXPECT_EQ(fac.cubes.size(), 8u);
  EXPECT_EQ(fac.factored, 6u);
  EXPECT_EQ(fac.series, 3u);
  // Majority: exactly 3 cubes of 2 literals.
  Truth_table m(3);
  for (uint32_t x = 0; x < 8; ++x) {
    m.set(x, std::popcount(x) >= 2);
  }
  const auto maj = exact_form(m, 16, 4, false, b);
  ASSERT_EQ(maj.status, Status::feasible);
  EXPECT_EQ(maj.literals, 6u);
  EXPECT_EQ(maj.cubes.size(), 3u);
}
}  // namespace livehd::usyn
