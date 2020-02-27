//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <unistd.h>

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "eprp_utils.hpp"
#include "lgraph.hpp"
#include "pass_lec.hpp"

#include "ezsat.hpp"
#include "ezminisat.hpp"

using testing::HasSubstr;

class LecMainTest : public ::testing::Test {
protected:
  void SetUp() override {}
};

TEST_F(LecMainTest, NoLGraphTest) {

  ezMiniSAT sat;

  std::vector<int> vx = sat.vec_var("x", 32);
  std::vector<int> vy = sat.vec_var("y", 32);
  std::vector<int> t = sat.vec_xor(vx, vy);

  std::vector<int>  modelExpressions;
  std::vector<bool> modelValues;

  sat.vec_append(modelExpressions, vx);
  sat.vec_append(modelExpressions, vy);

  if(!sat.solve(modelExpressions, modelValues)) {
    fmt::print("SAT solver failed to find a model!\n");
  }else{
    fmt::print("SAT solver found a model!\n");
  }

  // x = sat.vec_model_get_unsigned(modelExpressions, modelValues, sat.vec_var("x", 32));

  // FIXME: Add the ezlib calls to check that
  //
  //  (!(!a || !b )) ^ (a&&b) is not satisfiable
  //  (!(!a ||  b )) ^ (a&&b) is satisfiable

  EXPECT_TRUE(true); // FIXME: replace sat/not sat result
}
