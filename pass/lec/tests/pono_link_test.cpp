// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Milestone-1 build de-risk (lec.md build sequence step 3): prove the full
// cvc5 -> smt-switch -> Pono stack links, and validate the L1 discharge
// mechanism end-to-end *before* the encoder exists.
//
// It builds the exact transition system the query layer will: a combinational
// miter latched into a 1-bit state var (`bad' = (out_a != out_b)`, init
// `bad = 0`), with SafetyProperty `Not(bad)`. k-induction proves it for an
// equal pair (TRUE) and refutes an off-by-one pair (FALSE); BMC also finds the
// off-by-one counterexample.

#include "core/fts.h"
#include "core/prop.h"
#include "core/proverresult.h"
#include "engines/prover.h"
#include "gtest/gtest.h"
#include "smt/available_solvers.h"
#include "utils/make_provers.h"

using namespace pono;
using namespace smt;

namespace {

// out_a = a + 1; out_b = a + (equal ? 1 : 2). Latch the miter, run `eng`.
ProverResult run_miter(bool equal, Engine eng) {
  SmtSolver s = create_solver(CVC5, false, true, true);

  FunctionalTransitionSystem fts(s);
  Sort                       bv8   = s->make_sort(BV, 8);
  Term                       a     = fts.make_inputvar("a", bv8);
  Term                       out_a = s->make_term(BVAdd, a, s->make_term(1, bv8));
  Term                       out_b = s->make_term(BVAdd, a, s->make_term(equal ? 1 : 2, bv8));
  Term                       bad   = s->make_term(Distinct, out_a, out_b);

  Term bad_state = fts.make_statevar("__lec_bad", s->make_sort(BOOL));
  fts.set_init(s->make_term(Equal, bad_state, s->make_term(false)));
  fts.assign_next(bad_state, bad);

  SafetyProperty prop(s, s->make_term(Not, bad_state));
  auto           prover = make_prover(eng, prop, fts, s, PonoOptions());
  prover->initialize();
  return prover->check_until(2);
}

}  // namespace

TEST(PonoLink, KInductionProvesEqual) { EXPECT_EQ(run_miter(true, Engine::KIND), TRUE); }

TEST(PonoLink, KInductionRefutesUnequal) { EXPECT_EQ(run_miter(false, Engine::KIND), FALSE); }

TEST(PonoLink, BmcRefutesUnequal) { EXPECT_EQ(run_miter(false, Engine::BMC), FALSE); }
