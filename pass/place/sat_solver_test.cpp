// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "sat_solver.hpp"

#include <cvc5/cvc5.h>

#include <array>

#include "gtest/gtest.h"
#include "sat/cadical/ccadical.h"
namespace livehd::placement {
TEST(SatSolver, IncrementalAssumptionsModelsAndPrivateState) {
  for (auto backend : {Sat_backend::cadical, Sat_backend::cms_cnf, Sat_backend::cms_xor}) {
    for (unsigned repetition = 0; repetition < 3; ++repetition) {
      Sat_solver solver(backend);
      int        a = solver.new_variable(), b = solver.new_variable(), activation = solver.new_variable();
      solver.parity(std::array{a, b}, false);  // coordinate-bit equality (XNOR)
      solver.clause({-activation, a});
      EXPECT_EQ(solver.solve(std::array{activation}), Sat_status::satisfiable);
      EXPECT_TRUE(solver.value(a));
      EXPECT_TRUE(solver.value(b));
      EXPECT_EQ(solver.solve(std::array{activation, -b}), Sat_status::unsatisfiable);
      EXPECT_THROW(solver.value(a), std::logic_error);
      EXPECT_EQ(solver.solve(std::array{-activation, -a}), Sat_status::satisfiable);
      EXPECT_FALSE(solver.value(a));
      EXPECT_FALSE(solver.value(b));
      EXPECT_EQ(solver.solve({}, 100, 0), Sat_status::unknown);
      EXPECT_THROW(solver.value(a), std::logic_error);
      EXPECT_EQ(solver.solve(std::array{activation}), Sat_status::satisfiable);
      cvc5::TermManager tm;
      cvc5::Solver      formal(tm);
      formal.setLogic("QF_BV");
      auto x = tm.mkConst(tm.mkBitVectorSort(8), "x");
      formal.assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {x, tm.mkBitVector(8, 7)}));
      EXPECT_TRUE(formal.checkSat().isSat());
      auto* abc = ccadical_init();
      ccadical_add(abc, 1);
      ccadical_add(abc, 0);
      EXPECT_EQ(ccadical_solve(abc), 10);
      ccadical_release(abc);
    }
  }
}
TEST(SatSolver, ParityIsNotCoordinateEquality) {
  for (auto backend : {Sat_backend::cadical, Sat_backend::cms_cnf, Sat_backend::cms_xor}) {
    Sat_solver solver(backend);
    const int  a = solver.new_variable(), b = solver.new_variable(), c = solver.new_variable();
    solver.parity(std::array{a, b, c}, true);
    EXPECT_EQ(solver.solve(std::array{a, b, c}), Sat_status::satisfiable);
    EXPECT_EQ(solver.solve(std::array{a, -b, -c}), Sat_status::satisfiable);
    EXPECT_EQ(solver.solve(std::array{-a, -b, -c}), Sat_status::unsatisfiable);
  }
}
}  // namespace livehd::placement
