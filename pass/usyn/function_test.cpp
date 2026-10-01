// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "function.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::usyn {
namespace {
Truth_table truth(uint32_t n, const std::function<bool(uint32_t)>& f) {
  Truth_table t(n);
  for (uint32_t x = 0; x < (1U << n); ++x) {
    t.set(x, f(x));
  }
  return t;
}

// Independent recursive evaluation: does not use Gate_formula::evaluate or
// the table-to-cover logic, including for complemented output selection.
bool eval(const Gate_formula& f, uint32_t x) {
  const auto visit = [&](auto&& self, uint32_t id) -> bool {
    const auto& n = f.nodes.at(id);
    switch (n.kind) {
      case Gate_formula::Kind::constant: return n.inverted;
      case Gate_formula::Kind::literal : return ((x & (1U << n.variable)) != 0) ^ n.inverted;
      case Gate_formula::Kind::series  : return self(self, n.left) && self(self, n.right);
      case Gate_formula::Kind::parallel: return self(self, n.left) || self(self, n.right);
    }
    return false;
  };
  return visit(visit, static_cast<uint32_t>(f.nodes.size() - 1)) ^ f.output_inverted;
}
}  // namespace

TEST(Function, PackedKernelsMatchScalarOraclesAcrossWordBoundaries) {
  for (uint32_t n = 0; n <= max_logical_inputs; ++n) {
    SCOPED_TRACE(n);
    Gate_formula formula;
    formula.nodes.push_back({Gate_formula::Kind::constant, 0, 0, 0, true});
    for (uint32_t j = 0; j < n; ++j) {
      const auto last = static_cast<uint32_t>(formula.nodes.size() - 1);
      formula.nodes.push_back({Gate_formula::Kind::literal, 0, 0, j, (j % 3) == 0});
      formula.nodes.push_back({j % 2 ? Gate_formula::Kind::parallel : Gate_formula::Kind::series, last, last + 1});
    }
    formula.output_inverted = n % 2;
    const auto expected     = truth(n, [&](auto x) { return eval(formula, x); });
    Budget     packed{formula.nodes.size() * (expected.words.size() + 1)};
    const auto table = formula.evaluate_table(n, packed);
    ASSERT_TRUE(table);
    EXPECT_EQ(*table, expected);
    EXPECT_TRUE(valid_truth_table(*table));
    EXPECT_EQ(packed.remaining, 0U);
    Budget short_work{formula.nodes.size() * (expected.words.size() + 1) - 1};
    EXPECT_FALSE(formula.evaluate_table(n, short_work));

    // Include binate functions and absent variables, with independent scalar
    // cofactors so the oracle does not reuse the packed masks or shifts.
    for (const auto& f : {expected, truth(n, [](auto x) { return ((x * 2654435761U) >> 29) & 1; })}) {
      uint32_t support = 0, negative = 0, binate = 0;
      for (uint32_t j = 0; j < n; ++j) {
        bool up = false, down = false;
        for (uint32_t x = 0; x < (1U << n); ++x) {
          if (x & (1U << j)) {
            continue;
          }
          const auto lo = f.get(x), hi = f.get(x | (1U << j));
          up   |= !lo && hi;
          down |= lo && !hi;
        }
        if (up || down) {
          support |= 1U << j;
        }
        if (down) {
          (up ? binate : negative) |= 1U << j;
        }
      }
      Budget     work{1 + n * f.words.size()};
      const auto analysis = analyze_function(f, work);
      ASSERT_EQ(analysis.status, Status::feasible);
      EXPECT_EQ(analysis.support, support);
      EXPECT_EQ(analysis.negative, negative);
      EXPECT_EQ(analysis.binate, binate);
    }

    const uint32_t phase  = 0xa55aU & ((1U << n) - 1);
    const auto     f      = truth(n, [&](auto x) { return std::popcount(x ^ phase) >= int(n / 2); });
    const auto     care   = truth(n, [](auto x) { return (x % 7) == 0; });
    auto           scalar = truth(n, [&](auto x) { return f.get(x ^ phase) && care.get(x ^ phase); });
    for (uint32_t j = 0; j < n; ++j) {
      for (uint32_t x = 0; x < (1U << n); ++x) {
        if (x & (1U << j)) {
          scalar.set(x, scalar.get(x) || scalar.get(x ^ (1U << j)));
        }
      }
    }
    const auto completion = truth(n, [&](auto x) { return scalar.get(x ^ phase); });
    Budget     work{(n + 16) * f.words.size()};
    const auto result = monotone_completion(f, care, phase, work);
    ASSERT_EQ(result.status, Status::feasible);
    EXPECT_EQ(result.table, completion);
    EXPECT_TRUE(valid_truth_table(result.table));
  }
}

TEST(Function, PackedFormulaRejectsMalformedInputAndHonorsCancellation) {
  Gate_formula formula;
  Budget       work{1000};
  EXPECT_FALSE(formula.evaluate_table(0, work));
  formula.nodes.push_back({Gate_formula::Kind::literal, 0, 0, 6});
  EXPECT_FALSE(formula.evaluate_table(6, work));
  EXPECT_FALSE(formula.evaluate_table(17, work));
  formula.nodes.push_back({Gate_formula::Kind::series, 0, 1});
  EXPECT_FALSE(formula.evaluate_table(7, work));
  formula.nodes.pop_back();
  Budget denied{1000};
  denied.admission = [] { return false; };
  EXPECT_FALSE(formula.evaluate_table(7, denied));
  EXPECT_TRUE(denied.resource_exhausted);
}

TEST(Function, WideSupportAndNegativePhaseAreExact) {
  const auto t = truth(16, [](auto x) { return (x & 1) && !(x & (1U << 15)); });
  Budget     work{3000000};
  const auto a = analyze_function(t, work);
  ASSERT_EQ(a.status, Status::feasible);
  EXPECT_EQ(a.support, 0x8001U);
  EXPECT_EQ(a.negative, 0x8000U);
  EXPECT_EQ(a.binate, 0U);
  const auto gate = synthesize_gate(t, {}, work);
  ASSERT_TRUE(gate.formula) << gate.reason;
  EXPECT_EQ(gate.cost.transistors, 2U);
  for (uint32_t x = 0; x < 65536; ++x) {
    ASSERT_EQ(eval(*gate.formula, x), (x & 1) && !(x & 0x8000));
  }
  EXPECT_THROW(Truth_table(17), std::invalid_argument);
}

TEST(Function, EveryThreeVariableFunctionHasAnEquivalentRailNetwork) {
  for (uint32_t bits = 0; bits < 256; ++bits) {
    const auto t = truth(3, [bits](auto x) { return (bits >> x) & 1; });
    Budget     work{200000};
    const auto gate = synthesize_gate(t, {}, work);
    ASSERT_EQ(gate.status, Status::feasible) << bits << ':' << gate.reason;
    ASSERT_TRUE(gate.formula);
    ASSERT_TRUE(gate.formula->metrics());
    for (uint32_t x = 0; x < 8; ++x) {
      ASSERT_EQ(eval(*gate.formula, x), ((bits >> x) & 1) != 0) << bits << ':' << x;
    }
  }
}

TEST(Function, ParallelMetricUsesFactoredWidthNotDischargePaths) {
  const auto t = truth(4, [](auto x) { return (x & 3) && (x & 12); });
  Budget     work{100000};
  const auto gate = synthesize_gate(t, {8, 2, 2}, work);
  ASSERT_TRUE(gate.formula) << gate.reason;
  EXPECT_EQ(gate.cost.transistors, 4U);
  EXPECT_EQ(gate.cost.stack, 2U);
  EXPECT_EQ(gate.cost.branches, 2U);
  for (uint32_t x = 0; x < 16; ++x) {
    EXPECT_EQ(eval(*gate.formula, x), (x & 3) && (x & 12));
  }
}

TEST(Function, MixedRailsDoNotDoubleLogicalInputsOrImposeATransistorCeiling) {
  const auto parity = truth(4, [](auto x) { return (std::popcount(x) & 1) != 0; });
  Budget     work{1000000};
  const auto gate = synthesize_gate(parity, {4, 4, 10}, work);
  ASSERT_TRUE(gate.formula) << gate.reason;
  EXPECT_GT(gate.cost.transistors, 16U);
  EXPECT_EQ(gate.cost.positive & gate.cost.negative, 15U);
  EXPECT_EQ(std::popcount(gate.cost.support), 4);
  Budget     small{100000};
  const auto rejected = synthesize_gate(parity, {3, 4, 10}, small);
  EXPECT_FALSE(rejected.formula);
  EXPECT_EQ(rejected.reason, "logical input limit");
}

TEST(Function, OversizedPhysicalNetworkAndExhaustionAreDifferentFromInvalidInput) {
  const auto parity = truth(3, [](auto x) { return (std::popcount(x) & 1) != 0; });
  Budget     work{100000};
  const auto no_fit = synthesize_gate(parity, {3, 1, 1}, work);
  EXPECT_FALSE(no_fit.formula);
  EXPECT_EQ(no_fit.status, Status::search_exhausted);  // heuristic, not a proof of impossibility
  Budget     none{0};
  const auto exhausted = synthesize_gate(parity, {}, none);
  EXPECT_TRUE(exhausted.search_exhausted);
  EXPECT_FALSE(exhausted.formula);
  auto malformed = parity;
  malformed.words.push_back(1);
  EXPECT_EQ(synthesize_gate(malformed, {}, work).status, Status::invalid);
  EXPECT_EQ(synthesize_gate(parity, {8, 0, 10}, work).status, Status::invalid);
}

TEST(Function, VerifiedIncumbentSurvivesLaterAdmissionFailure) {
  const auto t      = truth(2, [](auto x) { return (x & 3) == 3; });
  uint32_t   checks = 0;
  Budget     work{100000};
  work.admission_interval = 1;
  work.admission          = [&]() { return ++checks < 65; };
  const auto gate         = synthesize_gate(t, {}, work);
  ASSERT_TRUE(gate.formula) << gate.reason;
  EXPECT_TRUE(gate.search_exhausted);
  EXPECT_EQ(gate.status, Status::feasible);
  for (uint32_t x = 0; x < 4; ++x) {
    EXPECT_EQ(eval(*gate.formula, x), x == 3);
  }
}

TEST(Function, DivisorImageRejectsConflictsAndRecordsUnreachableAssignments) {
  const auto                     a    = truth(2, [](auto x) { return x & 1; });
  const auto                     b    = truth(2, [](auto x) { return x & 2; });
  const auto                     root = truth(2, [](auto x) { return (x & 3) != 0; });
  const std::vector<Truth_table> divisors{a, a.complement(), b};
  Budget                         work{10000};
  const auto                     derived = derive_divisor_function(root, divisors, work);
  ASSERT_EQ(derived.status, Status::feasible);
  for (uint32_t d = 0; d < 8; ++d) {
    const bool reachable = ((d & 1) != 0) != ((d & 2) != 0);
    EXPECT_EQ(derived.care.get(d), reachable);
    if (reachable) {
      EXPECT_EQ(derived.function.get(d), (d & 5) != 0);
    }
  }
  const std::vector<Truth_table> insufficient{a};
  EXPECT_EQ(derive_divisor_function(root, insufficient, work).status, Status::unsupported);
  Budget none{0};
  EXPECT_EQ(derive_divisor_function(root, divisors, none).status, Status::search_exhausted);
}

TEST(Function, DivisorConflictWitnessSeparatesOppositeRootAssignments) {
  const auto       a = truth(3, [](auto x) { return x & 1; });
  const std::array divisors{a};
  for (uint32_t bits = 0; bits < 256; ++bits) {
    const auto root = truth(3, [&](auto x) { return (bits >> x) & 1; });
    Budget     work{10000};
    const auto image = derive_divisor_function(root, divisors, work, true);
    if (image.status == Status::unsupported) {
      ASSERT_TRUE(image.conflict);
      const auto [x, y] = *image.conflict;
      ASSERT_LT(x, 8U);
      ASSERT_LT(y, 8U);
      EXPECT_EQ(a.get(x), a.get(y));
      EXPECT_NE(root.get(x), root.get(y));
    } else {
      EXPECT_EQ(image.status, Status::feasible);
      EXPECT_FALSE(image.conflict);
      for (uint32_t x = 0; x < 8; ++x) {
        EXPECT_EQ(image.function.get(a.get(x)), root.get(x));
      }
    }
  }
  const auto root = truth(3, [](auto x) { return x & 2; });
  Budget     measured{1000};
  EXPECT_EQ(derive_divisor_function(root, divisors, measured).status, Status::unsupported);
  Budget     limited{1000 - measured.remaining};
  const auto refused = derive_divisor_function(root, divisors, limited, true);
  EXPECT_EQ(refused.status, Status::search_exhausted);
  EXPECT_FALSE(refused.conflict);
}

TEST(Function, FormulaRejectsCyclesSharedTransistorsAndGarbage) {
  using K = Gate_formula::Kind;
  Gate_formula f;
  f.nodes = {
      {K::literal, 0, 0, 0},
      {K::parallel, 0, 0}
  };
  EXPECT_FALSE(f.metrics());  // one physical transistor cannot be two branches
  f.nodes[1].right = 1;
  EXPECT_FALSE(f.metrics());
  f.nodes = {
      {K::literal, 0, 0, 0},
      {K::literal, 0, 0, 1}
  };
  EXPECT_FALSE(f.metrics());  // unused serialized node
}

TEST(Function, EveryTwoInputPartialFunctionHasTheExactPhaseCompletionDecision) {
  for (uint32_t code = 0; code < 81; ++code) {
    Truth_table f(2), care(2);
    auto        ternary = code;
    for (uint32_t x = 0; x < 4; ++x) {
      const auto value  = ternary % 3;
      ternary          /= 3;
      care.set(x, value != 2);
      f.set(x, value == 1);
    }
    for (uint32_t phase = 0; phase < 4; ++phase) {
      bool exists = false;
      for (uint32_t candidate = 0; candidate < 16; ++candidate) {
        bool matches = true;
        for (uint32_t x = 0; x < 4; ++x) {
          matches &= !care.get(x) || (((candidate >> x) & 1) != 0) == f.get(x);
          for (uint32_t y = 0; y < 4; ++y) {
            // Independent order-relation check, without the closure routine.
            if (((x ^ phase) & (y ^ phase)) == (x ^ phase)) {
              matches &= ((candidate >> x) & 1) <= ((candidate >> y) & 1);
            }
          }
        }
        exists |= matches;
      }
      Budget     work{1000};
      const auto c = monotone_completion(f, care, phase, work);
      ASSERT_EQ(c.status, exists ? Status::feasible : Status::unsupported) << code << ':' << phase;
      if (exists) {
        for (uint32_t x = 0; x < 4; ++x) {
          if (care.get(x)) {
            EXPECT_EQ(c.table.get(x), f.get(x));
          }
        }
      }
    }
  }
}

TEST(Function, CareImageCanMakeXorMonotoneWithoutChangingReachableBehavior) {
  const auto f    = truth(2, [](auto x) { return x == 1 || x == 2; });
  const auto care = truth(2, [](auto x) { return x != 3; });
  Budget     work{1000};
  const auto completion = monotone_completion(f, care, 0, work);
  ASSERT_EQ(completion.status, Status::feasible);
  EXPECT_EQ(completion.table.words[0], 14U);  // OR, not full XOR
  Budget none{0};
  EXPECT_EQ(monotone_completion(f, care, 0, none).status, Status::search_exhausted);
}

TEST(Function, BoundedCompletionsPreserveEveryTwoInputPartialFunction) {
  for (uint32_t code = 0; code < 81; ++code) {
    Truth_table f(2), care(2);
    auto        ternary = code;
    for (uint32_t x = 0; x < 4; ++x) {
      const auto value  = ternary % 3;
      ternary          /= 3;
      care.set(x, value != 2);
      f.set(x, value == 1);
    }
    Budget     work{10000};
    const auto alternatives = function_completions(f, care, 4, work);
    ASSERT_EQ(alternatives.status, Status::feasible) << code;
    EXPECT_FALSE(alternatives.search_exhausted);
    EXPECT_LE(alternatives.phases, 4U);
    EXPECT_LE(alternatives.tables.size(), 10U);
    for (const auto& table : alternatives.tables) {
      EXPECT_TRUE(valid_truth_table(table));
      for (uint32_t x = 0; x < 4; ++x) {
        EXPECT_TRUE(!care.get(x) || table.get(x) == f.get(x)) << code << ':' << x;
      }
    }
    if (care == Truth_table(2, true)) {
      EXPECT_EQ(alternatives.phases, 0U);
      ASSERT_EQ(alternatives.tables.size(), 1U);
      EXPECT_EQ(alternatives.tables.front(), f);
    }
    // Independently enumerate all total functions for the first (positive)
    // phase. Their intersection/union are the least/greatest completions.
    uint32_t least = 15, greatest = 0;
    bool     exists = false;
    for (uint32_t bits = 0; bits < 16; ++bits) {
      bool valid = true;
      for (uint32_t x = 0; x < 4; ++x) {
        valid &= !care.get(x) || (((bits >> x) & 1) != 0) == f.get(x);
        for (uint32_t y = 0; y < 4; ++y) {
          if ((x & y) == x) {
            valid &= ((bits >> x) & 1) <= ((bits >> y) & 1);
          }
        }
      }
      if (valid) {
        exists    = true;
        least    &= bits;
        greatest |= bits;
      }
    }
    std::vector<uint32_t> expected{static_cast<uint32_t>(f.words[0] & care.words[0]),
                                   static_cast<uint32_t>((f.words[0] | ~care.words[0]) & 15)};
    if (exists) {
      expected.push_back(least);
      expected.push_back(greatest);
    }
    std::sort(expected.begin(), expected.end());
    expected.erase(std::unique(expected.begin(), expected.end()), expected.end());
    const auto            first_phase = function_completions(f, care, 1, work);
    std::vector<uint32_t> actual;
    for (const auto& table : first_phase.tables) {
      actual.push_back(static_cast<uint32_t>(table.words[0]));
    }
    std::sort(actual.begin(), actual.end());
    EXPECT_EQ(actual, expected) << code;
  }
}

TEST(Function, CareCompletionRescuesPhysicalLimitsAndRetainsIncumbentsOnExhaustion) {
  // Reachable codes 0,1,2 select cofactors 0,r,1. Code 3 never occurs.
  Truth_table f(3), care(3);
  f.words[0]    = 0x64;
  care.words[0] = 0x77;
  const Gate_constraints gates{3, 2, 2};
  Budget                 work{1000000};
  EXPECT_FALSE(synthesize_gate(f, gates, work).formula);
  const auto alternatives = function_completions(f, care, 8, work);
  ASSERT_EQ(alternatives.status, Status::feasible);
  bool rescued = false;
  for (const auto& table : alternatives.tables) {
    const auto gate = synthesize_gate(table, gates, work);
    if (!gate.formula) {
      continue;
    }
    rescued = true;
    for (uint32_t x = 0; x < 8; ++x) {
      EXPECT_TRUE(!care.get(x) || eval(*gate.formula, x) == f.get(x));
    }
  }
  EXPECT_TRUE(rescued);
  Budget     limited{12};
  const auto partial = function_completions(f, care, 8, limited);
  EXPECT_EQ(partial.status, Status::feasible);
  EXPECT_TRUE(partial.search_exhausted);
  EXPECT_FALSE(partial.tables.empty());
  for (const auto& table : partial.tables) {
    for (uint32_t x = 0; x < 8; ++x) {
      EXPECT_TRUE(!care.get(x) || table.get(x) == f.get(x));
    }
  }
  Budget empty{0};
  EXPECT_EQ(function_completions(f, care, 8, empty).status, Status::search_exhausted);
  EXPECT_EQ(function_completions(f, care, max_completion_phases + 1, work).status, Status::invalid);
}

TEST(Function, RedundantConsensusCubeCannotBlockALegalDivisor) {
  // Complement: (!a & !c) | (a & b). Greedy expansion may also produce b&!c;
  // that consensus term must not inflate the factored parallel width.
  const auto f = truth(3, [](auto x) {
    const bool a = x & 1, b = x & 2, c = x & 4;
    return !((!a && !c) || (a && b));
  });
  Budget     work{100000};
  const auto result = synthesize_gate(f, {3, 2, 2}, work);
  ASSERT_TRUE(result.formula) << result.reason;
  EXPECT_EQ(result.cost.transistors, 4U);
  for (uint32_t x = 0; x < 8; ++x) {
    EXPECT_EQ(eval(*result.formula, x), f.get(x));
  }
}
TEST(Function, SignedCompletionAlternativesExposeInputsHiddenByZeroAndOneFills) {
  Truth_table f(2), care(2);
  f.set(3, true);
  care.set(0, true);
  care.set(3, true);
  Budget     work{10000};
  const auto result = function_completions(f, care, 4, work);
  ASSERT_EQ(result.status, Status::feasible);
  EXPECT_EQ(result.phases, 4U);
  bool a = false, b = false;
  for (const auto& table : result.tables) {
    a |= table.words[0] == 0xa;
    b |= table.words[0] == 0xc;
  }
  EXPECT_TRUE(a);
  EXPECT_TRUE(b);
  const auto bounded = function_completions(f, care, 1, work);
  EXPECT_EQ(bounded.status, Status::feasible);
  EXPECT_EQ(bounded.phases, 1U);
  EXPECT_TRUE(bounded.search_exhausted);
}
}  // namespace livehd::usyn
