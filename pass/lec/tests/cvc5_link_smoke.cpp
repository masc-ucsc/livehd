// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Milestone-1 build de-risk: prove the prebuilt cvc5 static lib links and a
// QF_ABV (bit-vectors + arrays) query runs through cvc5's own C++ API. This is
// the link smoke test referenced by lec.md "Build sequence" step 1 -- before
// smt-switch / Pono are layered on top.

#include <cvc5/cvc5.h>

#include "gtest/gtest.h"

using namespace cvc5;

// A satisfiable bit-vector query: exists x:bv8 . x == 1.
TEST(Cvc5Link, BitVectorSat) {
  TermManager tm;
  Solver      solver(tm);
  solver.setLogic("QF_ABV");

  Sort bv8 = tm.mkBitVectorSort(8);
  Term x   = tm.mkConst(bv8, "x");
  Term one = tm.mkBitVector(8, 1);
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {x, one}));

  Result r = solver.checkSat();
  EXPECT_TRUE(r.isSat());
}

// An unsatisfiable bit-vector query: x == 1 AND x == 2 (mod 2^8).
TEST(Cvc5Link, BitVectorUnsat) {
  TermManager tm;
  Solver      solver(tm);
  solver.setLogic("QF_ABV");

  Sort bv8 = tm.mkBitVectorSort(8);
  Term x   = tm.mkConst(bv8, "x");
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {x, tm.mkBitVector(8, 1)}));
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {x, tm.mkBitVector(8, 2)}));

  Result r = solver.checkSat();
  EXPECT_TRUE(r.isUnsat());
}

// Exercise the array theory (the "A" in QF_ABV): store-then-select round-trips.
TEST(Cvc5Link, ArraySelectStore) {
  TermManager tm;
  Solver      solver(tm);
  solver.setLogic("QF_ABV");

  Sort bv8     = tm.mkBitVectorSort(8);
  Sort arr     = tm.mkArraySort(bv8, bv8);
  Term a       = tm.mkConst(arr, "a");
  Term idx     = tm.mkConst(bv8, "i");
  Term val     = tm.mkConst(bv8, "v");
  Term stored  = tm.mkTerm(Kind::STORE, {a, idx, val});
  Term fetched = tm.mkTerm(Kind::SELECT, {stored, idx});
  // select(store(a,i,v), i) must equal v -- assert the negation is unsat.
  solver.assertFormula(tm.mkTerm(Kind::DISTINCT, {fetched, val}));

  Result r = solver.checkSat();
  EXPECT_TRUE(r.isUnsat());
}
