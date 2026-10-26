// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "timing_comparison.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement::late {
namespace {
Result observation(const std::vector<double>& slacks) {
  Result result;
  result.certified                      = true;
  result.clocks["main"].period_ps       = 1000;
  result.clocks["main"].timed_endpoints = slacks.size();
  for (size_t i = 0; i < slacks.size(); ++i) {
    Pin_result pin;
    pin.name           = "e" + std::to_string(i);
    pin.endpoint       = true;
    pin.transitions[0] = {100., 100. + slacks[i], 20., 2., {}};
    result.pins.push_back(pin);
  }
  return result;
}
TEST(TimingComparison, IdentityMatchingKeepsNumericalAndModelCoverageSeparate) {
  auto reference = observation({-10, -5, 0, 200}), predicted = reference;
  predicted.pins[0].transitions[0].arrival_ps = 101;
  std::reverse(predicted.pins.begin(), predicted.pins.end());
  auto result = compare_timing(predicted, reference);
  EXPECT_TRUE(result.comparison_gates_pass);
  EXPECT_TRUE(result.supported_coverage_pass);
  EXPECT_EQ(result.fields[0].compared, 4);
  EXPECT_EQ(result.fields[0].maximum, 1);
  EXPECT_EQ(result.fields[0].mean, .25);
  EXPECT_EQ(result.ranking.reference_critical, 3);
  EXPECT_EQ(result.ranking.prediction_critical, 3);
  EXPECT_EQ(result.ranking.jaccard, 1);
  EXPECT_EQ(result.ranking.tau_b, 1);
  predicted.certified = false;
  result              = compare_timing(predicted, reference);
  EXPECT_TRUE(result.comparison_gates_pass);
  EXPECT_FALSE(result.supported_coverage_pass);
}
TEST(TimingComparison, DeclaredLimitsAreAppliedToEveryCommonTransition) {
  auto reference = observation({0, 10, 20}), predicted = reference;
  predicted.pins[0].transitions[0].arrival_ps  = 110;
  predicted.pins[1].transitions[0].required_ps = 118;
  predicted.pins[2].transitions[0].slew_ps     = 22;
  auto result                                  = compare_timing(predicted, reference);
  EXPECT_EQ(result.fields[0].tolerance_failures, 1);
  EXPECT_EQ(result.fields[1].tolerance_failures, 1);
  EXPECT_EQ(result.fields[2].tolerance_failures, 0);
  EXPECT_FALSE(result.numerical_gates_pass);
  ASSERT_EQ(result.fields[0].failure_examples.size(), 1);
  EXPECT_EQ(result.fields[0].failure_examples[0].reference, 100);
  EXPECT_EQ(result.fields[0].failure_examples[0].tolerance, 5);
  predicted.pins[2].transitions[0].slew_ps = 22.001;
  EXPECT_EQ(compare_timing(predicted, reference).fields[2].tolerance_failures, 1);
  reference.pins[0].transitions[0].arrival_ps = -1000;
  predicted.pins[0].transitions[0].arrival_ps = -951;
  EXPECT_EQ(compare_timing(predicted, reference).fields[0].tolerance_failures, 0);
  EXPECT_FALSE(result.fields[3].tolerance_failures);  // no invented load threshold
}
TEST(TimingComparison, CriticalMembershipAndRankingAreIndependentOfWorstSlack) {
  const auto reference = observation({0, 10, 20});
  const auto reversed  = compare_timing(observation({20, 10, 0}), reference);
  EXPECT_EQ(reversed.ranking.jaccard, 1);
  EXPECT_EQ(reversed.ranking.tau_b, -1);
  EXPECT_FALSE(reversed.ranking_gates_pass);
  const auto membership = compare_timing(observation({500, 50, 0}), observation({0, 50, 500}));
  EXPECT_EQ(membership.ranking.reference_critical, 2);
  EXPECT_EQ(membership.ranking.prediction_critical, 2);
  EXPECT_EQ(membership.ranking.union_size, 3);
  EXPECT_NEAR(*membership.ranking.jaccard, 1. / 3, 1e-15);
}
TEST(TimingComparison, TiesAndSinglePointDoNotBecomePassingRanks) {
  const auto tied = compare_timing(observation({0, 10, 10}), observation({0, 0, 10}));
  ASSERT_TRUE(tied.ranking.tau_b);
  EXPECT_DOUBLE_EQ(*tied.ranking.tau_b, .5);
  const auto flat = compare_timing(observation({0, 0, 0}), observation({0, 0, 0}));
  EXPECT_EQ(flat.ranking.jaccard, 1);
  EXPECT_FALSE(flat.ranking.tau_b);
  EXPECT_TRUE(flat.numerical_gates_pass);
  EXPECT_FALSE(flat.comparison_gates_pass);
  const auto single = compare_timing(observation({0}), observation({0}));
  EXPECT_FALSE(single.ranking.tau_b);
  EXPECT_FALSE(single.ranking.unavailable.empty());
}
TEST(TimingComparison, MissingValuesAndMultiplePeriodsRemainExplicit) {
  auto reference = observation({0, 10, 20}), predicted = reference;
  predicted.pins[0].transitions[0].required_ps.reset();
  auto result = compare_timing(predicted, reference);
  EXPECT_EQ(result.fields[1].coverage_disagreements, 1);
  EXPECT_EQ(result.ranking.coverage_disagreements, 1);
  EXPECT_FALSE(result.ranking.jaccard);
  EXPECT_FALSE(result.ranking.tau_b);
  EXPECT_FALSE(result.comparison_gates_pass);
  predicted                 = reference;
  reference.clocks["other"] = {.period_ps = 2000, .timed_endpoints = 1};
  predicted.clocks          = reference.clocks;
  result                    = compare_timing(predicted, reference);
  EXPECT_FALSE(result.ranking.period_ps);
  EXPECT_FALSE(result.ranking.jaccard);
  EXPECT_FALSE(result.comparison_gates_pass);
}
TEST(TimingComparison, RejectsInvalidNumericStatesAndPinIdentities) {
  auto reference = observation({0, 10, 20}), predicted = reference;
  predicted.pins[0].transitions[0].arrival_ps = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(compare_timing(predicted, reference), std::invalid_argument);
  predicted                                = reference;
  predicted.pins[0].transitions[0].slew_ps = -1;
  EXPECT_THROW(compare_timing(predicted, reference), std::invalid_argument);
  predicted              = reference;
  predicted.pins[0].name = "unknown";
  EXPECT_THROW(compare_timing(predicted, reference), std::invalid_argument);
  predicted = reference;
  predicted.pins.push_back(predicted.pins.back());
  EXPECT_THROW(compare_timing(predicted, reference), std::invalid_argument);
  predicted                               = reference;
  predicted.characterized_boundary_policy = true;
  EXPECT_THROW(compare_timing(predicted, reference), std::invalid_argument);
  predicted                         = reference;
  predicted.bounded_boundary_policy = true;
  EXPECT_THROW(compare_timing(predicted, reference), std::invalid_argument);
}
TEST(TimingComparison, FenwickTauAgreesWithIndependentPairwiseOracleIncludingTies) {
  std::mt19937 generator(271828);
  for (unsigned trial = 0; trial < 100; ++trial) {
    std::vector<double> a(8), b(8);
    for (unsigned i = 0; i < 8; ++i) {
      a[i] = generator() % 5;
      b[i] = generator() % 5;
    }
    int        score = 0, tied_a = 0, tied_b = 0, count = 0;
    const auto sign = [](double x) { return (x > 0) - (x < 0); };
    for (unsigned i = 0; i < 8; ++i) {
      for (unsigned j = i + 1; j < 8; ++j) {
        const auto x = sign(a[i] - a[j]), y = sign(b[i] - b[j]);
        score  += x * y;
        tied_a += x == 0;
        tied_b += y == 0;
        ++count;
      }
    }
    const double denominator = std::sqrt(double(count - tied_a) * (count - tied_b));
    const auto   result      = compare_timing(observation(b), observation(a));
    if (denominator) {
      ASSERT_TRUE(result.ranking.tau_b);
      EXPECT_NEAR(*result.ranking.tau_b, score / denominator, 1e-15);
    } else {
      EXPECT_FALSE(result.ranking.tau_b);
    }
  }
}
}  // namespace
}  // namespace livehd::placement::late
