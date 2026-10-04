// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "cone_expand.hpp"

#include "cone_abc.hpp"
#include "gtest/gtest.h"

namespace {
using cvc5::Kind;
using cvc5::Term;
using Definitions = std::vector<std::pair<Term, Term>>;

void expect_equivalent(cvc5::TermManager& tm, const Definitions& definitions, const Term& original, const Term& expanded) {
  cvc5::Solver solver(tm);
  solver.setLogic("QF_AUFBV");
  for (const auto& [symbol, value] : definitions) {
    solver.assertFormula(tm.mkTerm(Kind::EQUAL, {symbol, value}));
  }
  solver.assertFormula(tm.mkTerm(Kind::DISTINCT, {original, expanded}));
  EXPECT_TRUE(solver.checkSat().isUnsat());
}

TEST(ConeExpand, DynamicMemoryReadMatchesBankAndExposesARealFault) {
  cvc5::TermManager tm;
  const auto        address = tm.mkConst(tm.mkBitVectorSort(2), "address");
  const auto        read    = tm.mkConst(tm.mkBitVectorSort(8), "read");
  auto              array   = tm.mkConst(tm.mkArraySort(address.getSort(), read.getSort()), "base");
  std::vector<Term> data;
  for (unsigned i = 0; i < 4; ++i) {
    data.push_back(tm.mkConst(read.getSort(), "word" + std::to_string(i)));
    array = tm.mkTerm(Kind::STORE, {array, tm.mkBitVector(2, i), data.back()});
  }
  Term bank = data[3];
  for (unsigned i = 3; i-- > 0;) {
    bank = tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::EQUAL, {address, tm.mkBitVector(2, i)}), data[i], bank});
  }
  const Definitions definitions{
      {read, tm.mkTerm(Kind::SELECT, {array, address})}
  };
  const auto correct  = tm.mkTerm(Kind::DISTINCT, {read, bank});
  const auto fault    = tm.mkTerm(Kind::DISTINCT, {read, tm.mkTerm(Kind::BITVECTOR_XOR, {bank, tm.mkBitVector(8, 1)})});
  const auto expanded = livehd::lec::expand_cone_definitions(tm, definitions, {correct, fault});
  expect_equivalent(tm, definitions, correct, expanded[0]);
  expect_equivalent(tm, definitions, fault, expanded[1]);
  const auto verdicts = livehd::lec::abc_prove_unsat_batch(expanded, 10000, 1000);
  EXPECT_EQ(verdicts[0], livehd::lec::Cone_verdict::Proven);
  EXPECT_NE(verdicts[1], livehd::lec::Cone_verdict::Proven);
  cvc5::Solver solver(tm);
  solver.setLogic("QF_AUFBV");
  solver.assertFormula(tm.mkTerm(Kind::EQUAL, {definitions[0].first, definitions[0].second}));
  solver.assertFormula(fault);
  EXPECT_TRUE(solver.checkSat().isSat());
}

TEST(ConeExpand, CyclicAndConflictingDefinitionsRemainSound) {
  cvc5::TermManager tm;
  const auto        a    = tm.mkConst(tm.mkBitVectorSort(4), "a");
  const auto        b    = tm.mkConst(a.getSort(), "b");
  const auto        c    = tm.mkConst(a.getSort(), "c");
  const auto        root = tm.mkTerm(Kind::DISTINCT, {a, c});
  const Definitions cycle{
      {a, tm.mkTerm(Kind::BITVECTOR_NOT, {b})},
      {b, tm.mkTerm(Kind::BITVECTOR_NOT, {a})}
  };
  const auto expanded = livehd::lec::expand_cone_definitions(tm, cycle, {root});
  expect_equivalent(tm, cycle, root, expanded[0]);
  const Definitions conflict{
      {a, b},
      {a, c}
  };
  EXPECT_EQ(livehd::lec::expand_cone_definitions(tm, conflict, {root})[0], root);
}

TEST(ConeExpand, DefinitionChangesParticipateInProofCacheKeys) {
  cvc5::TermManager tm;
  const auto        a      = tm.mkConst(tm.mkBitVectorSort(4), "a");
  const auto        read   = tm.mkConst(a.getSort(), "read");
  const auto        root   = tm.mkTerm(Kind::DISTINCT, {read, a});
  const auto        before = livehd::lec::expand_cone_definitions(tm,
                                                                  {
                                                               {read, a}
  },
                                                                  {root})[0];
  const auto        after  = livehd::lec::expand_cone_definitions(tm,
                                                                  {
                                                              {read, tm.mkTerm(Kind::BITVECTOR_NOT, {a})}
  },
                                                                  {root})[0];
  EXPECT_NE(livehd::lec::cone_digest(before), livehd::lec::cone_digest(after));
}
}  // namespace
