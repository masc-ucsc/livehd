// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Milestone-1 build de-risk (lec.md build sequence step 2): prove the
// rules_foreign_cc smt-switch build links and a bit-vector query runs through
// smt-switch's cvc5 backend (Cvc5SolverFactory). This is the term layer Pono
// and pass/lec are built on.

#include "gtest/gtest.h"
#include "smt-switch/cvc5_factory.h"
#include "smt-switch/smt.h"

using namespace smt;

TEST(SmtSwitchLink, BitVectorSat) {
  SmtSolver s = Cvc5SolverFactory::create(false);
  s->set_logic("QF_BV");

  Sort bv8 = s->make_sort(BV, 8);
  Term x   = s->make_symbol("x", bv8);
  Term one = s->make_term(1, bv8);
  s->assert_formula(s->make_term(Equal, x, one));

  Result r = s->check_sat();
  EXPECT_TRUE(r.is_sat());
}

TEST(SmtSwitchLink, BitVectorUnsat) {
  SmtSolver s = Cvc5SolverFactory::create(false);
  s->set_logic("QF_BV");

  Sort bv8 = s->make_sort(BV, 8);
  Term x   = s->make_symbol("x", bv8);
  s->assert_formula(s->make_term(Equal, x, s->make_term(1, bv8)));
  s->assert_formula(s->make_term(Equal, x, s->make_term(2, bv8)));

  Result r = s->check_sat();
  EXPECT_TRUE(r.is_unsat());
}
