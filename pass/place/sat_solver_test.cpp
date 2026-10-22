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
      EXPECT_TRUE(solver.failed(activation));
      EXPECT_TRUE(solver.failed(-b));
      EXPECT_FALSE(solver.failed(-activation));
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
TEST(SatSolver, ChoiceCostBoundsAreExactAndIncremental) {
  for (auto backend : {Sat_backend::cadical, Sat_backend::cms_cnf, Sat_backend::cms_xor}) {
    Sat_solver solver(backend);
    const int  a = solver.new_variable(), b = solver.new_variable(), c = solver.new_variable(), d = solver.new_variable();
    solver.clause({-a, -b});
    solver.clause({-c, -d});
    Sat_choice_cost costs(solver,
                          {
                              {{a, 3},  {b, 8}},
                              {{c, 5}, {d, 17}}
    });
    // Exhaustively check both one-hot groups, including disabled endpoints.
    for (int first = 0; first < 3; ++first) {
      for (int second = 0; second < 3; ++second) {
        const unsigned value = (first == 1 ? 3 : first == 2 ? 8 : 0) + (second == 1 ? 5 : second == 2 ? 17 : 0);
        for (unsigned bound = 0; bound <= 26; ++bound) {
          const int guard         = costs.at_most(bound);
          const int assumptions[] = {first == 1 ? a : -a, first == 2 ? b : -b, second == 1 ? c : -c, second == 2 ? d : -d, guard};
          EXPECT_EQ(solver.solve(assumptions), value <= bound ? Sat_status::satisfiable : Sat_status::unsatisfiable)
              << first << ',' << second << " bound=" << bound;
        }
      }
    }
  }
}
TEST(SatSolver, ChoiceCostHandlesHighBitsAndRejectsOverflow) {
  Sat_solver      solver(Sat_backend::cadical);
  const int       a = solver.new_variable(), b = solver.new_variable();
  const uint64_t  high = uint64_t(1) << 63;
  Sat_choice_cost costs(solver, {{{a, high}}, {{b, 7}}});
  EXPECT_EQ(solver.solve(std::array{a, b, costs.at_most(high + 6)}), Sat_status::unsatisfiable);
  EXPECT_EQ(solver.solve(std::array{a, b, costs.at_most(high + 7)}), Sat_status::satisfiable);
  EXPECT_EQ(solver.solve(std::array{-a, b, costs.at_most(7)}), Sat_status::satisfiable);
  EXPECT_THROW((Sat_choice_cost(solver, {{{a, high}}, {{b, high}}})), std::overflow_error);
}
}  // namespace livehd::placement
