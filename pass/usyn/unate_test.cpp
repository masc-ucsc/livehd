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

namespace {
// Initial credits of a budget: what it consumed plus what remains.
uint64_t initial(const Budget& b) { return b.consumed + b.remaining; }
}  // namespace

TEST(BudgetFloor, SpendHasAndAvailableRecordTheirRequirements) {
  Budget b{100};
  EXPECT_TRUE(b.spend(30));
  EXPECT_EQ(b.credit_floor(), (Credit_floor{30, 30, false, 0}));
  EXPECT_TRUE(b.has(50));  // needs 30 + 50 credits
  EXPECT_EQ(b.floor, 80U);
  EXPECT_TRUE(b.available());  // needs only 31
  EXPECT_EQ(b.floor, 80U);
  EXPECT_TRUE(b.spend(0));
  EXPECT_TRUE(b.spend(70));
  EXPECT_EQ(b.credit_floor(), (Credit_floor{100, 100, false, 0}));
  // A spend refused for lack of work happens only under exactly these credits.
  EXPECT_FALSE(b.spend(1));
  EXPECT_TRUE(b.exhausted);
  EXPECT_FALSE(b.resource_exhausted);
  EXPECT_EQ(b.credit_floor(), (Credit_floor{100, 100, true, 100}));
  EXPECT_TRUE(b.credit_floor().reproduces(100));
  EXPECT_FALSE(b.credit_floor().reproduces(101));
  // Sticky exhaustion adds no new fact.
  EXPECT_FALSE(b.spend(0));
  EXPECT_FALSE(b.available());

  Budget c{10};
  EXPECT_FALSE(c.has(11));  // a false answer binds the run
  EXPECT_TRUE(c.bound);
  EXPECT_FALSE(c.exhausted);
  EXPECT_TRUE(c.spend(10));
  EXPECT_EQ(c.credit_floor(), (Credit_floor{10, 10, true, 10}));

  Budget d{10};
  EXPECT_TRUE(d.spend(4));
  EXPECT_FALSE(d.spend(7));  // the refused amount is not consumed
  EXPECT_EQ(d.credit_floor(), (Credit_floor{4, 4, true, 10}));
}

TEST(BudgetFloor, AdmissionRefusalIsNotAReproductionFact) {
  Budget b{100};
  b.admission_interval = 1;
  b.admission          = [] { return false; };
  EXPECT_FALSE(b.spend(1));
  EXPECT_TRUE(b.resource_exhausted);
  EXPECT_TRUE(b.exhausted);
  EXPECT_FALSE(b.bound);  // resource refusals are never reused, so they bind nothing
  EXPECT_EQ(b.consumed, 0U);
  Budget child = b.slice(50);
  EXPECT_TRUE(child.resource_exhausted);
  b.absorb(child);
  EXPECT_TRUE(b.resource_exhausted);
}

TEST(BudgetFloor, SliceSizeRulesLiftTheChildRequirement) {
  {
    // Unbound child: its size must stay >= its floor, (r - reserve)/d >= floor.
    Budget p{1000};
    EXPECT_TRUE(p.spend(100));
    {
      Work_slice s(p, unlimited_work, 4);
      EXPECT_EQ(s.work.remaining, 225U);
      EXPECT_TRUE(s.work.spend(50));
      EXPECT_EQ(s.work.credit_floor(), (Credit_floor{50, 50, false, 0}));
    }
    EXPECT_EQ(p.credit_floor(), (Credit_floor{150, 100 + 4 * 50, false, 0}));
  }
  {
    // Bound, cap-limited child: its exact size needs remaining >= d*cap.
    Budget p{1000};
    {
      Work_slice s(p, 60, 4);
      EXPECT_EQ(s.work.remaining, 60U);
      EXPECT_FALSE(s.work.spend(61));
      EXPECT_TRUE(s.work.bound);
    }
    EXPECT_EQ(p.credit_floor(), (Credit_floor{0, 240, false, 0}));
    EXPECT_FALSE(p.exhausted);  // a child's exhaustion stays in the child
  }
  {
    // Bound, remaining-limited child: only these parent credits give its size.
    Budget p{100};
    {
      Work_slice s(p, 60, 4);
      EXPECT_EQ(s.work.remaining, 25U);
      EXPECT_FALSE(s.work.spend(26));
    }
    EXPECT_TRUE(p.bound);
    EXPECT_EQ(p.credit_floor().credits, 100U);
  }
  {
    // A reserve is subtracted before dividing: (1000 - 360) / 32 = 20.
    Budget p{1000};
    {
      Work_slice s(p, unlimited_work, 32, 360);
      EXPECT_EQ(s.work.remaining, 20U);
      EXPECT_TRUE(s.work.spend(10));
    }
    EXPECT_EQ(p.floor, 360U + 32 * 10);
    Budget none{100};
    {
      Work_slice s(none, unlimited_work, 1, 200);
      EXPECT_EQ(s.work.remaining, 0U);
    }
    EXPECT_EQ(none.credit_floor(), (Credit_floor{0, 0, false, 0}));  // an idle child needs nothing
  }
  {
    // A zero-cap child keeps its size under any credits, even when bound.
    Budget p{10};
    {
      Work_slice s(p, 0, 2);
      EXPECT_FALSE(s.work.has());
    }
    EXPECT_EQ(p.credit_floor(), (Credit_floor{0, 0, false, 0}));
  }
}

TEST(BudgetFloor, NestedSlicesPropagateOnlyThroughRemainingLimitedSizes) {
  {
    Budget p{10000};
    {
      Work_slice child(p, unlimited_work, 2);  // 5000
      {
        Work_slice grand(child.work, 100);  // cap-limited
        EXPECT_FALSE(grand.work.spend(101));
      }
      EXPECT_FALSE(child.work.bound);
      EXPECT_EQ(child.work.floor, 100U);
      EXPECT_TRUE(child.work.spend(10));
    }
    EXPECT_EQ(p.credit_floor(), (Credit_floor{10, 200, false, 0}));
  }
  {
    Budget p{1000};
    {
      Work_slice child(p, 300);  // cap-limited
      {
        Work_slice grand(child.work, unlimited_work, 2);  // remaining-limited: 150
        EXPECT_FALSE(grand.work.spend(151));
      }
      EXPECT_TRUE(child.work.bound);
    }
    EXPECT_FALSE(p.bound);
    EXPECT_EQ(p.floor, 300U);
  }
  {
    // A parent spending while its child is open can be overdrawn by the child.
    Budget p{100};
    Budget c = p.slice(80);
    EXPECT_TRUE(p.spend(50));
    EXPECT_TRUE(c.spend(80));
    p.absorb(c);
    EXPECT_TRUE(p.exhausted);
    EXPECT_TRUE(p.bound);
    EXPECT_EQ(p.remaining, 0U);
    EXPECT_EQ(p.consumed, 100U);
  }
  {
    // An exhausted parent's slice starts exhausted and bound.
    Budget p{5};
    EXPECT_FALSE(p.spend(6));
    const auto c = p.slice(3);
    EXPECT_TRUE(c.exhausted);
    EXPECT_TRUE(c.bound);
  }
}

TEST(BudgetFloor, CreditShareAnswersRecordOnlyWhatTheyNeed) {
  {
    Budget       b{600};
    Credit_share share(b, 100, 3);  // cap-limited: min(100, 200)
    EXPECT_TRUE(share.exceeds(50));
    EXPECT_EQ(b.floor, 3U * 51);
    EXPECT_FALSE(share.exceeds(100));  // no credits make a capped value exceed its cap
    EXPECT_FALSE(b.bound);
    EXPECT_EQ(share.clamp(80), 80U);
    EXPECT_EQ(b.floor, 3U * 80);
    EXPECT_EQ(share.clamp(120), 100U);
    EXPECT_EQ(b.floor, 3U * 100);
    EXPECT_FALSE(b.bound);
  }
  {
    Budget       b{150};
    Credit_share share(b, 100, 3);  // remaining-limited: 50
    EXPECT_TRUE(share.exceeds(10));
    EXPECT_EQ(b.floor, 33U);
    EXPECT_FALSE(b.bound);
    EXPECT_FALSE(share.exceeds(60));  // more credits would exceed 60
    EXPECT_TRUE(b.bound);
  }
  {
    Budget       b{150};
    Credit_share share(b, 100, 3);
    EXPECT_EQ(share.clamp(40), 40U);
    EXPECT_FALSE(b.bound);
    EXPECT_EQ(share.clamp(70), 50U);  // the exact remaining-limited value
    EXPECT_TRUE(b.bound);
  }
}

namespace {
// A random program over one budget: every answer it observes goes to `trace`,
// never a raw `remaining`. Replaying it under credits that reproduce the first
// run must observe the identical trace and consume identical work.
struct Budget_program {
  uint32_t seed;
  void     run(Budget& b, std::vector<uint64_t>& trace) const {
    uint32_t   state = seed;
    const auto next  = [&](uint32_t bound) {
      state = state * 1664525U + 1013904223U;
      return (state >> 8) % bound;
    };
    body(b, trace, next, 0);
  }
  template <class Next>
  static void body(Budget& b, std::vector<uint64_t>& trace, Next& next, uint32_t depth) {
    const auto steps = 1 + next(6);
    for (uint32_t i = 0; i < steps; ++i) {
      switch (next(depth < 3 ? 6 : 4)) {
        case 0:
        case 1: trace.push_back(b.spend(next(40))); break;
        case 2: trace.push_back(b.has(next(300))); break;
        case 3: {
          Credit_share share(b, next(4) ? next(200) : unlimited_work, 1 + next(3));
          trace.push_back(share.exceeds(next(120)));
          trace.push_back(share.clamp(next(160)));
          break;
        }
        default: {
          const uint64_t cap = next(3) ? next(250) : unlimited_work;
          Work_slice     slice(b, cap, 1 + next(4), next(3) ? 0 : next(200));
          body(slice.work, trace, next, depth + 1);
          trace.push_back(slice.work.consumed);
        }
      }
    }
  }
};
}  // namespace

TEST(BudgetFloor, RandomProgramsReplayIdenticallyAtTheirFloorAndAbove) {
  uint32_t bound = 0, unbound = 0, tight = 0;
  for (uint32_t seed = 1; seed <= 3000; ++seed) {
    const Budget_program program{seed};
    for (const uint64_t credits : {60U, 250U, 1000U, 5000U}) {
      Budget                b{credits};
      std::vector<uint64_t> trace;
      program.run(b, trace);
      const auto recorded = b.credit_floor();
      ASSERT_LE(recorded.work, recorded.floor);
      ASSERT_LE(recorded.floor, credits);
      ASSERT_EQ(initial(b), credits);
      if (recorded.bound) {
        ++bound;
        Budget                again{credits};
        std::vector<uint64_t> replay;
        program.run(again, replay);
        ASSERT_EQ(replay, trace) << seed;
        ASSERT_EQ(again.credit_floor(), recorded) << seed;
        continue;
      }
      ++unbound;
      for (const uint64_t other : {recorded.floor, recorded.floor + 1, credits, 3 * credits + 7}) {
        ASSERT_TRUE(recorded.reproduces(other));
        Budget                again{other};
        std::vector<uint64_t> replay;
        program.run(again, replay);
        ASSERT_EQ(replay, trace) << "seed " << seed << " credits " << credits << " -> " << other;
        ASSERT_EQ(again.credit_floor(), recorded) << "seed " << seed << " credits " << credits << " -> " << other;
      }
      if (recorded.floor) {
        Budget                below{recorded.floor - 1};
        std::vector<uint64_t> replay;
        program.run(below, replay);
        tight += replay != trace;
      }
    }
  }
  // Both kinds occur, and the floor is usually the exact threshold.
  EXPECT_GT(bound, 1000U);
  EXPECT_GT(unbound, 1000U);
  EXPECT_GT(tight, unbound / 2);
}
}  // namespace livehd::usyn
