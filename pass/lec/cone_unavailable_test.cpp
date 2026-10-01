// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cone_abc.hpp"
#include "gtest/gtest.h"

namespace livehd::lec {
TEST(ConeUnavailable, RetainsSatisfiableUnsatisfiableAndInvalidObligations) {
  cvc5::TermManager             tm;
  const std::vector<cvc5::Term> terms{tm.mkBoolean(true), tm.mkBoolean(false), cvc5::Term{}};
  for (const auto& term : terms) {
    Cone_stats stats{123, 456, "old result"};
    EXPECT_EQ(abc_prove_unsat(term, 0, &stats), Cone_verdict::Unsupported);
    EXPECT_EQ(stats.ands, 0);
    EXPECT_EQ(stats.pis, 0);
    EXPECT_NE(stats.why.find("unavailable"), std::string::npos);
  }
  std::vector<Cone_stats> stats(10);
  const auto              verdicts = abc_prove_unsat_batch(terms, 0, 1, &stats);
  ASSERT_EQ(verdicts.size(), terms.size());
  ASSERT_EQ(stats.size(), terms.size());
  for (size_t i = 0; i < verdicts.size(); ++i) {
    EXPECT_EQ(verdicts[i], Cone_verdict::Unsupported);
    EXPECT_NE(stats[i].why.find("unavailable"), std::string::npos);
  }
  EXPECT_TRUE(abc_prove_unsat_batch({}, 0, 1, &stats).empty());
  EXPECT_TRUE(stats.empty());
  EXPECT_FALSE(cone_digest(terms.front()).empty());
  EXPECT_NE(cone_digest(terms.front()), cone_digest(terms[1]));
}
}  // namespace livehd::lec
