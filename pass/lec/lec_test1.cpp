//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <unistd.h>

#include <string>

#include "eprp_utils.hpp"
#include "ezminisat.hpp"
#include "ezsat.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "lgraph.hpp"
#include "pass_lec.hpp"

using testing::HasSubstr;

class LecMainTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(LecMainTest, NoLGraphTest) {
  // Attempt 2 to add to Git for review

  ezMiniSAT sat;

  std::vector<int> vx = sat.vec_var("x", 2);
  std::vector<int> vy = sat.vec_var("y", 2);
  // std::vector<int> t = sat.vec_xor(vx, vy);

  std::vector<int>  modelExpressions;
  std::vector<bool> modelValues;

  sat.vec_append(modelExpressions, vx);
  sat.vec_append(modelExpressions, vy);

  std::vector<int> z1 = sat.vec_not(sat.vec_or(sat.vec_not(vx), vy));
  std::vector<int> z2 = sat.vec_not(sat.vec_or(sat.vec_not(vx), sat.vec_not(vy)));

  std::vector<int> result_out = sat.vec_xor(z1, sat.vec_and(vx, vy));

  fmt::print("Formula: (!(!a || b))^(a && b) \n");

  if (!sat.solve(modelExpressions, modelValues, result_out)) {
    fmt::print("SAT solver failed to find a model!\n");
  } else {
    fmt::print("SAT solver found a model!\n");
    for (auto val : modelValues) {
      fmt::print("{}", val ? 1 : 0);
    }
    fmt::print("\n");
  }

  fmt::print("Formula: (!(!a || !b))^(a && b) \n");

  std::vector<int> result_out2 = sat.vec_xor(z2, sat.vec_and(vx, vy));

  if (!sat.solve(modelExpressions, modelValues, result_out2)) {
    fmt::print("SAT solver failed to find a model!\n");
  } else {
    fmt::print("SAT solver found a model!\n");
    for (auto val : modelValues) {
      fmt::print("{}", val ? 1 : 0);
    }
    fmt::print("\n");
  }

  // x = sat.vec_model_get_unsigned(modelExpressions, modelValues, sat.vec_var("x", 32));

  // FIXME: Add the ezlib calls to check that
  //
  //  (!(!a || !b )) ^ (a&&b) is not satisfiable
  //  (!(!a ||  b )) ^ (a&&b) is satisfiable

  EXPECT_TRUE(true);  // FIXME: replace sat/not sat result
}
