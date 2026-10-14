// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "solver_profile.hpp"

#include "effort.hpp"
#include "fresh_solver.hpp"
#include "gtest/gtest.h"
#include "solve_stats.hpp"
using namespace livehd::lec;
using cvc5::Kind;
TEST(SolverProfile, EligibleProfilesSolveAndRespectModels) {
  for (const auto& profile : solver_profiles()) {
    SCOPED_TRACE(profile.name);
#ifndef LIVEHD_CVC5_SOURCE_BUILD
    if (profile.needs_models_off) {
      continue;
    }
#endif
    try {
      cvc5::TermManager tm;
      cvc5::Solver      solver(tm);
      Solver_query_ctx  ctx{.witness = !profile.needs_models_off};
      solver.setLogic(profile.name == "intblast" ? "QF_AUFBVNIA" : "QF_BV");
      configure_solver(solver, profile, ctx);
      auto x = tm.mkConst(tm.mkBitVectorSort(8), "x"), y = tm.mkConst(tm.mkBitVectorSort(8), "y");
      auto eq = tm.mkTerm(Kind::EQUAL, {tm.mkTerm(Kind::BITVECTOR_MULT, {x, y}), tm.mkBitVector(8, 15)});
      solver.assertFormula(eq);
      ASSERT_TRUE(solver.checkSat().isSat());
      if (ctx.witness) {
        EXPECT_TRUE(solver.getValue(eq).getBooleanValue());
      }
      EXPECT_EQ(solver.getOption("incremental"), profile.incremental ? "true" : "false");
    } catch (const std::exception& error) {
      ADD_FAILURE() << profile.name << ": " << error.what();
    }
  }
}
TEST(SolverProfile, ArrayGatingAndFreshAssertionScopes) {
  Solver_query_ctx memory{.has_mem = true, .witness = false};
  EXPECT_FALSE(profile_eligible(solver_profile("eager-kissat"), memory));
  cvc5::TermManager tm;
  Fresh_solver      solver(tm);
  solver.setLogic("QF_ABV");
  solver.set_profile("internal", memory);
  auto bv = tm.mkBitVectorSort(8), arr = tm.mkArraySort(bv, bv);
  auto a = tm.mkConst(arr, "a"), i = tm.mkConst(bv, "i"), v = tm.mkConst(bv, "v");
  auto roundtrip = tm.mkTerm(Kind::SELECT, {tm.mkTerm(Kind::STORE, {a, i, v}), i});
  solver.push();
  solver.assertFormula(tm.mkTerm(Kind::DISTINCT, {roundtrip, v}));
  EXPECT_TRUE(solver.checkSat().isUnsat());
  solver.pop();
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {v, tm.mkBitVector(8, 42)}));
  solver.setOption("produce-models", "true");
  EXPECT_TRUE(solver.checkSat().isSat());
  EXPECT_EQ(solver.getValue(v).getBitVectorValue(10), "42");
}
TEST(SolverProfile, SamplerUsesFreshSolversWithoutChangingCoverage) {
  cvc5::TermManager tm;
  Fresh_solver      solver(tm);
  solver.setLogic("QF_BV");
  Input_sampler sampler(tm);
  auto          a = tm.mkConst(tm.mkBitVectorSort(8), "a");
  sampler.add_input(a);
  const auto out = sampler.run(solver, tm.mkTerm(Kind::DISTINCT, {a, a}), Effort::Quick);
  EXPECT_FALSE(out.mismatch);
  EXPECT_EQ(out.report.reached, Effort::Quick);
  EXPECT_TRUE(solver.checkSat().isSat());
}
TEST(SolverProfile, LazyArithmeticNeverReportsFalseSat) {
  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  solver.setLogic("QF_BV");
  configure_solver(solver, solver_profile("lazy"), {.witness = true, .timeout_ms = 1000});
  auto bv = tm.mkBitVectorSort(16);
  auto a = tm.mkConst(bv, "a"), b = tm.mkConst(bv, "b"), c = tm.mkConst(bv, "c");
  auto lhs = tm.mkTerm(Kind::BITVECTOR_MULT, {a, tm.mkTerm(Kind::BITVECTOR_ADD, {b, c})});
  auto rhs = tm.mkTerm(Kind::BITVECTOR_ADD, {tm.mkTerm(Kind::BITVECTOR_MULT, {a, b}), tm.mkTerm(Kind::BITVECTOR_MULT, {a, c})});
  solver.assertFormula(tm.mkTerm(Kind::DISTINCT, {lhs, rhs}));
  EXPECT_FALSE(solver.checkSat().isSat());  // Unknown is honest; SAT would be unsound.
}

TEST(SolverProfile, CompilerPrivateBudgetsAreReproducible) {
  for (const auto backend : {"minisat", "cadical"}) {
    std::string verdict;
    int64_t     resources = 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
      cvc5::TermManager tm;
      cvc5::Solver      solver(tm);
      solver.setLogic("QF_BV");
      configure_solver(solver, solver_profile("internal"), {.witness = false, .compiler_budget = true});
      solver.setOption("sat-solver", backend);
      solver.setOption("rlimit-per", "10000");
      auto bv = tm.mkBitVectorSort(16);
      auto x = tm.mkConst(bv, "x"), y = tm.mkConst(bv, "y");
      auto product = tm.mkTerm(Kind::BITVECTOR_MULT, {x, y});
      solver.assertFormula(tm.mkTerm(
          Kind::AND,
          {tm.mkTerm(Kind::EQUAL, {product, tm.mkBitVector(16, 15)}), tm.mkTerm(Kind::DISTINCT, {x, tm.mkBitVector(16, 0)})}));
      const auto result = solver.checkSat();
      Cvc5_stats stats;
      capture_cvc5_stats(solver, &stats);
      ASSERT_GT(stats.resource_units, 0) << backend;
      if (!attempt) {
        verdict   = result.toString();
        resources = stats.resource_units;
      }
      EXPECT_EQ(result.toString(), verdict) << backend;
      EXPECT_EQ(stats.resource_units, resources) << backend;
    }
  }
}

TEST(SolverProfile, SlicedBaseCannotRefuteWithoutFullConstraintsAndModel) {
  cvc5::TermManager tm;
  auto              bv = tm.mkBitVectorSort(8);
  auto              x = tm.mkConst(bv, "x"), other = tm.mkConst(bv, "other");
  Fresh_solver      solver(tm);
  solver.setLogic("QF_BV");
  solver.set_profile("internal", {.witness = true});
  solver.set_base_slicing(true);
  for (int i = 0; i < 300; ++i) {
    auto pad = tm.mkConst(bv, "pad" + std::to_string(i));
    solver.assertFormula(tm.mkTerm(Kind::EQUAL, {pad, tm.mkBitVector(8, i % 256)}));
  }
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {other, tm.mkBitVector(8, 42)}));
  solver.push();
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {x, tm.mkBitVector(8, 7)}));
  ASSERT_TRUE(solver.checkSat().isSat());
  EXPECT_EQ(solver.getValue(other).getBitVectorValue(10), "42");
  solver.pop();
  // A contradictory component outside the goal may be omitted by slicing,
  // but its weaker SAT must not become a false mismatch.
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {other, tm.mkBitVector(8, 43)}));
  solver.push();
  EXPECT_TRUE(solver.checkSatAssuming(tm.mkTerm(Kind::EQUAL, {x, tm.mkBitVector(8, 7)})).isUnsat());
  solver.pop();
  solver.push();
  EXPECT_TRUE(solver.checkSatAssuming(tm.mkTerm(Kind::DISTINCT, {other, tm.mkBitVector(8, 42)})).isUnsat());
  solver.pop();
}

#ifdef LIVEHD_CVC5_SOURCE_BUILD
TEST(SolverProfile, CryptoMiniSatTimeoutCannotBecomeFalseSat) {
  for (const auto cap : {1, 3, 5, 20, 80, 250}) {
    cvc5::TermManager tm;
    cvc5::Solver      solver(tm);
    solver.setLogic("QF_BV");
    configure_solver(solver, solver_profile("eager-cms"), {.witness = false, .timeout_ms = cap});
    auto bv = tm.mkBitVectorSort(16);
    auto a = tm.mkConst(bv, "a"), b = tm.mkConst(bv, "b");
    auto product = tm.mkTerm(Kind::BITVECTOR_MULT, {tm.mkTerm(Kind::BITVECTOR_UDIV, {a, b}), b});
    auto sum     = tm.mkTerm(Kind::BITVECTOR_ADD, {product, tm.mkTerm(Kind::BITVECTOR_UREM, {a, b})});
    solver.assertFormula(
        tm.mkTerm(Kind::AND, {tm.mkTerm(Kind::DISTINCT, {b, tm.mkBitVector(16, 0)}), tm.mkTerm(Kind::DISTINCT, {sum, a})}));
    EXPECT_FALSE(solver.checkSat().isSat()) << "CryptoMiniSat timeout=" << cap;
  }
}
#endif

TEST(SolverProfile, PropagationOnlyDoesNotPoisonLaterFullCheck) {
  cvc5::TermManager tm;
  cvc5::Solver      solver(tm);
  solver.setLogic("QF_BV");
  configure_solver(solver, solver_profile("lazy"), {.witness = true});
  solver.setOption("bv-propagate", "true");
  auto bv = tm.mkBitVectorSort(16);
  auto x = tm.mkConst(bv, "x"), y = tm.mkConst(bv, "y");
  auto goal = tm.mkTerm(Kind::EQUAL, {tm.mkTerm(Kind::BITVECTOR_ADD, {x, y}), tm.mkBitVector(16, 15)});
  solver.assertFormula(goal);
  ASSERT_TRUE(solver.checkSat().isSat());
  EXPECT_TRUE(solver.getValue(goal).getBooleanValue());
}
