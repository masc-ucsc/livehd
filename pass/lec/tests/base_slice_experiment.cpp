// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <chrono>
#include <cstdio>

#include "fresh_solver.hpp"
#include "solve_stats.hpp"

using namespace livehd::lec;
int main() {
  std::puts("mode,status,wall_us,solvers,resource_units");
  for (bool slicing : {false, true}) {
    cvc5::TermManager tm;
    Cvc5_stats        stats;
    auto              solver = std::make_unique<Fresh_solver>(tm, Fresh_solver::Attach{}, [&](const cvc5::Solver& finished) {
      capture_cvc5_stats(finished, &stats);
      ++stats.solvers;
    });
    solver->setLogic("QF_BV");
    solver->set_profile("internal", {.witness = true});
    solver->set_base_slicing(slicing);
    auto bv = tm.mkBitVectorSort(16);
    auto x = tm.mkConst(bv, "x"), next = tm.mkConst(bv, "next");
    auto rhs = tm.mkTerm(cvc5::Kind::BITVECTOR_ADD, {x, tm.mkBitVector(16, 1)});
    for (int i = 0; i < 5000; ++i) {
      auto input = tm.mkConst(bv, "input" + std::to_string(i));
      auto state = tm.mkConst(bv, "state" + std::to_string(i));
      auto value = tm.mkTerm(cvc5::Kind::BITVECTOR_MULT, {input, input});
      solver->assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {state, value}));
    }
    solver->assertFormula(tm.mkTerm(cvc5::Kind::EQUAL, {next, rhs}));
    solver->push();
    solver->assertFormula(tm.mkTerm(cvc5::Kind::DISTINCT, {next, rhs}));
    const auto start = std::chrono::steady_clock::now();
    solver->set_deadline(start + std::chrono::seconds(2));
    const auto result = solver->checkSat();
    const auto us     = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    solver.reset();
    std::printf("%s,%s,%lld,%llu,%llu\n",
                slicing ? "sliced" : "full",
                result.toString().c_str(),
                static_cast<long long>(us),
                static_cast<unsigned long long>(stats.solvers),
                static_cast<unsigned long long>(stats.resource_units));
    if (!result.isUnsat()) {
      return 1;
    }
  }
}
