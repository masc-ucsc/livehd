// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "effort.hpp"

#include <stdexcept>

#include "gtest/gtest.h"

using namespace livehd::lec;
using cvc5::Kind;

TEST(Effort, PresetsAndLevels) {
  EXPECT_EQ(parse_effort(""), Effort::Legacy);
  EXPECT_EQ(parse_effort("proof"), Effort::Legacy);
  EXPECT_EQ(parse_effort("quick-check"), Effort::Quick);
  EXPECT_EQ(parse_effort("test"), Effort::Test);
  EXPECT_EQ(parse_effort("long-check"), Effort::Long);
  EXPECT_EQ(parse_effort("verify"), Effort::Verify);
  EXPECT_THROW(parse_effort("fast"), std::invalid_argument);
  EXPECT_TRUE(is_sampled(Effort::Quick) && is_sampled(Effort::Test) && is_sampled(Effort::Long));
  EXPECT_FALSE(is_sampled(Effort::Verify) || is_sampled(Effort::Legacy));
  EXPECT_LT(effort_vectors(Effort::Quick), effort_vectors(Effort::Test));
  EXPECT_LT(effort_vectors(Effort::Test), effort_vectors(Effort::Long));
  // The reached level is the highest one whose vector count is met.
  EXPECT_EQ(effort_reached(0), Effort::Legacy);
  EXPECT_EQ(effort_reached(effort_vectors(Effort::Quick) - 1), Effort::Legacy);
  EXPECT_EQ(effort_reached(effort_vectors(Effort::Quick)), Effort::Quick);
  EXPECT_EQ(effort_reached(effort_vectors(Effort::Test)), Effort::Test);
  EXPECT_EQ(effort_reached(effort_vectors(Effort::Long) * 2), Effort::Long);
}

struct SamplerFixture : testing::Test {
  cvc5::TermManager tm;
  cvc5::Solver      solver{tm};
  Input_sampler     sampler{tm};
  SamplerFixture() {
    solver.setLogic("QF_BV");
    solver.setOption("incremental", "true");
    solver.setOption("produce-models", "true");
    solver.setOption("bv-solver", "bitblast-internal");
  }
  cvc5::Term variable(unsigned width, const std::string& name) { return tm.mkConst(tm.mkBitVectorSort(width), name); }
};

// Two copies of a 2-cycle design: y = a + b per cycle. Equal designs never
// mismatch; every query is UNSAT and the requested level is reached.
TEST_F(SamplerFixture, EquivalentDesignReachesRequestedLevel) {
  std::vector<cvc5::Term> diffs;
  for (int cyc = 0; cyc < 2; ++cyc) {
    const auto a = variable(16, "a" + std::to_string(cyc));
    const auto b = variable(16, "b" + std::to_string(cyc));
    sampler.add_input(a);
    sampler.add_input(tm.mkTerm(tm.mkOp(Kind::BITVECTOR_ZERO_EXTEND, {4}), {b}));  // free core under an extension
    const auto ref  = tm.mkTerm(Kind::BITVECTOR_ADD, {a, b});
    const auto impl = tm.mkTerm(Kind::BITVECTOR_ADD, {b, a});
    diffs.push_back(tm.mkTerm(Kind::DISTINCT, {ref, impl}));
  }
  EXPECT_EQ(sampler.input_count(), 4u);
  sampler.set_cycles(2);
  const auto bad    = tm.mkTerm(Kind::OR, diffs);
  const auto result = sampler.run(solver, bad, Effort::Quick);
  EXPECT_FALSE(result.mismatch);
  EXPECT_EQ(result.report.reached, Effort::Quick) << result.reason;
  EXPECT_GE(result.report.vectors, effort_vectors(Effort::Quick));
  EXPECT_GT(result.report.partial, 0u) << "the schedule must include partially pinned queries";
  EXPECT_EQ(result.report.unknown, 0u);
  EXPECT_TRUE(solver.checkSat().isSat()) << "sampling must balance its assertion stack";
}

// A bug only on ONE operand value (a == 0x1234): random vectors essentially
// never hit it, the partially pinned queries let the solver find it.
TEST_F(SamplerFixture, PartialPinningFindsRareMismatch) {
  const auto a = variable(16, "a");
  const auto b = variable(16, "b");
  sampler.add_input(a);
  sampler.add_input(b);
  const auto ref  = tm.mkTerm(Kind::BITVECTOR_ADD, {a, b});
  const auto hit  = tm.mkTerm(Kind::EQUAL, {a, tm.mkBitVector(16, 0x1234)});
  const auto impl = tm.mkTerm(Kind::ITE, {hit, tm.mkTerm(Kind::BITVECTOR_SUB, {a, b}), ref});
  const auto bad  = tm.mkTerm(Kind::DISTINCT, {ref, impl});
  const auto out  = sampler.run(solver, bad, Effort::Test);
  ASSERT_TRUE(out.mismatch);
  EXPECT_TRUE(out.result.isSat());
  EXPECT_EQ(solver.getValue(a).getBitVectorValue(16), "1234");
  solver.pop();  // the caller owns the live mismatch model
  EXPECT_TRUE(solver.checkSat().isSat());
}

// A fully pinned step is SIMULATED (term_sim.hpp): every free symbol gets a
// value -- the inputs and also a power-on state symbol the old cube left free,
// which turned each "fully pinned" query into a real search -- and no solver
// query is issued for it. Here the free state symbol `s` is shared by both
// sides through a definitional assertion chain, as the BMC unrolling writes it.
TEST_F(SamplerFixture, PinnedTracesAreSimulatedWithoutSolverQueries) {
  std::vector<cvc5::Term> diffs;
  cvc5::Term              state = variable(32, "s0");  // power-on state: not an input
  for (int cyc = 0; cyc < 4; ++cyc) {
    const auto a  = variable(32, "a" + std::to_string(cyc));
    const auto st = variable(32, "st" + std::to_string(cyc));
    sampler.add_input(a, cyc, "a");
    solver.assertFormula(tm.mkTerm(Kind::EQUAL, {st, tm.mkTerm(Kind::BITVECTOR_MULT, {state, a})}));
    const auto ref  = tm.mkTerm(Kind::BITVECTOR_ADD, {st, a});
    const auto impl = tm.mkTerm(Kind::BITVECTOR_ADD, {a, st});
    const auto diff = tm.mkTerm(Kind::DISTINCT, {ref, impl});
    diffs.push_back(diff);
    sampler.add_output_diff(cyc, diff);
    state = st;
  }
  sampler.set_cycles(4);
  const auto out = sampler.run(solver, tm.mkTerm(Kind::OR, diffs), Effort::Quick);
  EXPECT_FALSE(out.mismatch);
  EXPECT_EQ(out.report.reached, Effort::Quick) << out.reason;
  EXPECT_NE(out.note.find("simulated"), std::string::npos) << out.note;
  EXPECT_LT(out.report.queries, out.report.traces) << "most steps must be simulations, not solver queries";
}

// The constants a design compares an input FIELD against bias the random
// values: a bug behind a 32-bit opcode (one value in 2^32) is hit by a
// simulated trace at once, and the caller's solver re-derives the mismatch
// with every free symbol pinned so its witness path reads a live model.
TEST_F(SamplerFixture, DecodedFieldConstantsReachARareOpcode) {
  std::vector<cvc5::Term> diffs, hits;
  for (int cyc = 0; cyc < 2; ++cyc) {
    const auto inst = variable(48, "inst" + std::to_string(cyc));
    sampler.add_input(inst, cyc, "inst");
    const auto op   = tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {39, 8}), {inst});
    const auto x    = tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {7, 0}), {inst});
    const auto hit  = tm.mkTerm(Kind::EQUAL, {op, tm.mkBitVector(32, 0xC0FFEE11u)});
    const auto ref  = tm.mkTerm(Kind::ITE, {hit, tm.mkTerm(Kind::BITVECTOR_ADD, {x, x}), x});
    const auto impl = tm.mkTerm(Kind::ITE, {hit, tm.mkTerm(Kind::BITVECTOR_ADD, {x, tm.mkBitVector(8, 1)}), x});
    const auto diff = tm.mkTerm(Kind::DISTINCT, {ref, impl});
    diffs.push_back(diff);
    hits.push_back(hit);
    sampler.add_output_diff(cyc, diff);
  }
  sampler.set_cycles(2);
  const auto out = sampler.run(solver, tm.mkTerm(Kind::OR, diffs), Effort::Test);
  ASSERT_TRUE(out.mismatch) << out.reason << " / " << out.note;
  EXPECT_NE(out.note.find("1 decoded input field(s)"), std::string::npos) << out.note;
  EXPECT_TRUE(out.result.isSat());
  EXPECT_TRUE(solver.getValue(tm.mkTerm(Kind::OR, hits)).getBooleanValue()) << "the witness takes the rare opcode";
  solver.pop();  // the caller owns the live mismatch model
  EXPECT_TRUE(solver.checkSat().isSat());
}

TEST_F(SamplerFixture, ImpossibleAssumptionsNeverCountAsMismatch) {
  // An environment that admits no trace: every query is UNSAT, which is the
  // honest "no mismatch among admitted traces" -- never a refutation.
  const auto a = variable(8, "a");
  sampler.add_input(a);
  solver.assertFormula(tm.mkBoolean(false));
  const auto out = sampler.run(solver, tm.mkTerm(Kind::DISTINCT, {a, a}), Effort::Quick);
  EXPECT_FALSE(out.mismatch);
}

TEST_F(SamplerFixture, TimeoutStopsBeforeTheLevel) {
  const auto a = variable(8, "a");
  sampler.add_input(a);
  const auto out = sampler.run(solver, tm.mkTerm(Kind::DISTINCT, {a, a}), Effort::Long, {}, [] { return true; });
  EXPECT_EQ(out.report.queries, 0u);
  EXPECT_FALSE(out.report.passed());
  EXPECT_EQ(out.reason, "hit formal.timeout");
}

// Timeout during a state lead must not start another query or count the lead.
// The first (simulated) trace finds the lead; the deadline passes before its
// confirmation search starts.
TEST_F(SamplerFixture, TimeoutBeforeLeadConfirmation) {
  const auto a = variable(8, "a");
  sampler.add_input(a);
  sampler.add_state_diff("q", 0, tm.mkTrue());
  int        checks = 0;
  int        polls  = 0;
  const auto out    = sampler.run(
      solver,
      tm.mkFalse(),
      Effort::Test,
      [&](long long) {
        ++checks;
        return solver.checkSat();
      },
      [&] { return polls++ > 0; });
  EXPECT_EQ(checks, 0);
  EXPECT_EQ(out.report.queries, 0u);
  EXPECT_EQ(out.report.leads, 1u);
  EXPECT_EQ(out.report.vectors, 0u);
  EXPECT_FALSE(out.report.passed());
  EXPECT_EQ(out.reason, "hit formal.timeout");
  EXPECT_TRUE(solver.checkSat().isSat());
}
