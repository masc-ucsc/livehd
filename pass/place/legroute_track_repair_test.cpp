// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "legroute_track_repair.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "gtest/gtest.h"

namespace livehd::placement::lr {
namespace {
Track_repair_limits budget() {
  Track_repair_limits limits;
  limits.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  return limits;
}
bool compatible(const Track_repair_problem& p, const std::vector<size_t>& selected) {
  if (selected.size() != p.candidate_counts.size()) {
    return false;
  }
  for (size_t r = 0; r < selected.size(); ++r) {
    if (selected[r] >= p.candidate_counts[r]) {
      return false;
    }
  }
  for (const auto& q : p.incompatible) {
    if (selected[q[0]] == q[1] && selected[q[2]] == q[3]) {
      return false;
    }
  }
  return true;
}
Track_repair_problem relocation_chain() {
  Track_repair_problem p;
  p.candidate_counts.assign(8, 2);
  p.candidate_counts.push_back(1);
  p.preferred.assign(9, 0);
  // Eight existing intervals may stay on track r or move to r+1. The failed
  // ninth request requires track 0: all eight must move together to admit it.
  const auto track = [](size_t r, size_t c) { return r == 8 ? size_t{0} : r + c; };
  for (size_t r = 0; r < p.candidate_counts.size(); ++r) {
    for (size_t s = r + 1; s < p.candidate_counts.size(); ++s) {
      for (size_t a = 0; a < p.candidate_counts[r]; ++a) {
        for (size_t b = 0; b < p.candidate_counts[s]; ++b) {
          if (track(r, a) == track(s, b)) {
            p.incompatible.push_back({r, a, s, b});
          }
        }
      }
    }
  }
  return p;
}
}  // namespace

TEST(LegrouteTrackRepair, SolvesNineRequestRelocationWithoutMakingHintsConstraints) {
  const auto p               = relocation_chain();
  const auto original_counts = p.candidate_counts;
  const auto original_pairs  = p.incompatible;
  const auto result          = solve_track_repair(p, budget());
  ASSERT_EQ(result.status, Sat_status::satisfiable) << result.reason;
  EXPECT_TRUE(compatible(p, result.selected));
  EXPECT_EQ(result.selected, (std::vector<size_t>{1, 1, 1, 1, 1, 1, 1, 1, 0}));
  EXPECT_EQ(result.candidate_literals, 17);
  EXPECT_EQ(result.variables, 25);
  EXPECT_EQ(result.solve_calls, 1);
  EXPECT_EQ(p.candidate_counts, original_counts);
  EXPECT_EQ(p.incompatible, original_pairs);
  EXPECT_EQ(p.preferred, (std::vector<std::optional<size_t>>(9, 0)));
  EXPECT_GE(result.build_seconds, 0);
  EXPECT_GE(result.solve_seconds, 0);
  EXPECT_GE(result.decode_seconds, 0);
  EXPECT_GE(result.total_seconds, result.build_seconds + result.solve_seconds + result.decode_seconds);
  EXPECT_TRUE(std::isfinite(result.total_seconds));
}

TEST(LegrouteTrackRepair, FinitePigeonholeIsUnsatRatherThanDroppingARequest) {
  Track_repair_problem p;
  p.candidate_counts = {2, 2, 2};
  for (size_t a = 0; a < 3; ++a) {
    for (size_t b = a + 1; b < 3; ++b) {
      for (size_t track = 0; track < 2; ++track) {
        p.incompatible.push_back({a, track, b, track});
      }
    }
  }
  const auto result = solve_track_repair(p, budget());
  EXPECT_EQ(result.status, Sat_status::unsatisfiable);
  EXPECT_EQ(result.reason, "finite_domain_unsatisfiable");
  EXPECT_TRUE(result.selected.empty());
  EXPECT_EQ(result.solve_calls, 1);
}

TEST(LegrouteTrackRepair, EncodingMatchesIndependentExhaustiveFiniteChoices) {
  for (size_t instance = 0; instance < 16; ++instance) {
    Track_repair_problem p;
    p.candidate_counts = {2, 3, 2};
    for (size_t r = 0; r < 3; ++r) {
      for (size_t s = r + 1; s < 3; ++s) {
        for (size_t a = 0; a < p.candidate_counts[r]; ++a) {
          for (size_t b = 0; b < p.candidate_counts[s]; ++b) {
            if ((r * 31 + s * 17 + a * 13 + b * 7 + instance * 3) % 11 < instance % 8) {
              p.incompatible.push_back({r, a, s, b});
            }
          }
        }
      }
    }
    if (instance % 2) {
      p.incompatible.push_back({1, instance % 3, 1, instance % 3});
    }
    bool feasible = false;
    for (size_t a = 0; a < 2; ++a) {
      for (size_t b = 0; b < 3; ++b) {
        for (size_t c = 0; c < 2; ++c) {
          feasible |= compatible(p, {a, b, c});
        }
      }
    }
    const auto result = solve_track_repair(p, budget());
    EXPECT_EQ(result.status, feasible ? Sat_status::satisfiable : Sat_status::unsatisfiable) << instance << ':' << result.reason;
    if (feasible) {
      EXPECT_TRUE(compatible(p, result.selected)) << instance;
    } else {
      EXPECT_TRUE(result.selected.empty());
    }
  }
}

TEST(LegrouteTrackRepair, EmptyDomainAndForbiddenChoicesKeepExactlyOneSemantics) {
  Track_repair_problem p;
  p.candidate_counts = {0};
  EXPECT_EQ(solve_track_repair(p, budget()).status, Sat_status::unsatisfiable);
  p.candidate_counts = {2};
  p.preferred        = {0};
  p.incompatible     = {
      {0, 0, 0, 0},
      {0, 0, 0, 0}
  };
  auto result = solve_track_repair(p, budget());
  ASSERT_EQ(result.status, Sat_status::satisfiable);
  EXPECT_EQ(result.selected, (std::vector<size_t>{1}));
  p.incompatible.push_back({0, 1, 0, 1});
  EXPECT_EQ(solve_track_repair(p, budget()).status, Sat_status::unsatisfiable);
  result = solve_track_repair({}, budget());
  EXPECT_EQ(result.status, Sat_status::satisfiable);
  EXPECT_TRUE(result.selected.empty());
  EXPECT_EQ(result.variables, 0);
  EXPECT_EQ(result.clauses, 0);
}

TEST(LegrouteTrackRepair, DeadlineAndConstructionCapsNeverPublishAnAllocation) {
  const auto p = relocation_chain();
  for (int condition = 0; condition < 6; ++condition) {
    auto limits = budget();
    switch (condition) {
      case 0: limits.deadline = {}; break;
      case 1: limits.max_requests = 8; break;
      case 2: limits.max_candidates_per_request = 1; break;
      case 3: limits.max_candidates = 16; break;
      case 4: limits.max_clauses = 1; break;
      case 5: limits.conflicts = 0; break;
    }
    const auto result = solve_track_repair(p, limits);
    EXPECT_EQ(result.status, Sat_status::unknown) << condition;
    EXPECT_TRUE(result.selected.empty());
    EXPECT_EQ(result.solve_calls, 0);
  }
  Track_repair_problem one;
  one.candidate_counts = {4};
  auto limits          = budget();
  limits.max_clauses   = 8;
  EXPECT_EQ(solve_track_repair(one, limits).reason, "clause_limit");
  limits.max_clauses = 9;
  const auto result  = solve_track_repair(one, limits);
  EXPECT_EQ(result.status, Sat_status::satisfiable);
  EXPECT_EQ(result.clauses, 9);
  EXPECT_EQ(result.variables, 7);
  limits.max_clauses = std::numeric_limits<size_t>::max();
  EXPECT_EQ(solve_track_repair(one, limits).status, Sat_status::satisfiable);
}

TEST(LegrouteTrackRepair, RejectsMalformedCallerIndices) {
  Track_repair_problem p;
  p.candidate_counts = {2};
  p.preferred        = {0, 1};
  EXPECT_THROW(solve_track_repair(p, budget()), std::invalid_argument);
  p.preferred = {2};
  EXPECT_THROW(solve_track_repair(p, budget()), std::invalid_argument);
  p.preferred.clear();
  p.incompatible = {
      {0, 0, 1, 0}
  };
  EXPECT_THROW(solve_track_repair(p, budget()), std::invalid_argument);
  p.incompatible = {
      {0, 2, 0, 0}
  };
  EXPECT_THROW(solve_track_repair(p, budget()), std::invalid_argument);
  p.incompatible.clear();
  auto limits      = budget();
  limits.conflicts = std::numeric_limits<unsigned>::max();
  EXPECT_THROW(solve_track_repair(p, limits), std::invalid_argument);
}
}  // namespace livehd::placement::lr
