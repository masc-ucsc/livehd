// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "array_blast.hpp"

#include <random>

#include "fresh_solver.hpp"
#include "gtest/gtest.h"
using namespace livehd::lec;
using cvc5::Kind;

namespace {
struct Fixture {
  cvc5::TermManager tm;
  cvc5::Sort        idx  = tm.mkBitVectorSort(2);
  cvc5::Sort        elem = tm.mkBitVectorSort(8);
  cvc5::Sort        arr  = tm.mkArraySort(idx, elem);
  cvc5::Term        t(Kind k, std::initializer_list<cvc5::Term> a) { return tm.mkTerm(k, std::vector<cvc5::Term>(a)); }
  // A blasted and a plain Fresh_solver must agree on `f`.
  void same_result(const cvc5::Term& f, bool expect_sat) {
    for (uint32_t bits : {0u, 5u}) {
      Fresh_solver s(tm);
      s.setLogic("QF_ABV");
      s.set_profile("internal", Solver_query_ctx{.has_mem = true, .witness = true});
      s.set_array_blast(bits);
      s.assertFormula(f);
      const auto r = s.checkSat();
      EXPECT_EQ(r.isSat(), expect_sat) << "blast bits " << bits << ": " << f;
      EXPECT_EQ(r.isUnsat(), !expect_sat) << "blast bits " << bits << ": " << f;
    }
  }
};
}  // namespace

TEST(ArrayBlast, ReadOverWriteAndExtensionalityAreValid) {
  Fixture f;
  auto    a = f.tm.mkConst(f.arr, "a"), b = f.tm.mkConst(f.arr, "b");
  auto    i = f.tm.mkConst(f.idx, "i"), j = f.tm.mkConst(f.idx, "j"), v = f.tm.mkConst(f.elem, "v");
  auto    c = f.tm.mkConst(f.tm.getBooleanSort(), "c");
  // select(store(a,i,v),j) == ite(i==j, v, select(a,j))
  auto rw = f.t(Kind::EQUAL,
                {f.t(Kind::SELECT, {f.t(Kind::STORE, {a, i, v}), j}),
                 f.t(Kind::ITE, {f.t(Kind::EQUAL, {i, j}), v, f.t(Kind::SELECT, {a, j})})});
  f.same_result(f.t(Kind::NOT, {rw}), false);
  // a == store(a, i, select(a, i))
  f.same_result(f.t(Kind::DISTINCT, {a, f.t(Kind::STORE, {a, i, f.t(Kind::SELECT, {a, i})})}), false);
  // ite over arrays, then a read
  auto mix = f.t(Kind::SELECT, {f.t(Kind::ITE, {c, a, f.t(Kind::STORE, {b, i, v})}), i});
  f.same_result(f.t(Kind::AND, {f.t(Kind::NOT, {c}), f.t(Kind::DISTINCT, {mix, v})}), false);
  // extensionality: two arrays equal in every entry are equal
  std::vector<cvc5::Term> eqs;
  for (uint64_t e = 0; e < 4; ++e) {
    auto at = f.tm.mkBitVector(2, e);
    eqs.push_back(f.t(Kind::EQUAL, {f.t(Kind::SELECT, {a, at}), f.t(Kind::SELECT, {b, at})}));
  }
  eqs.push_back(f.t(Kind::DISTINCT, {a, b}));
  f.same_result(f.tm.mkTerm(Kind::AND, eqs), false);
}

TEST(ArrayBlast, ConstantArraysAndSatisfiableQueries) {
  Fixture f;
  auto    zero = f.tm.mkConstArray(f.arr, f.tm.mkBitVector(8, 0));
  auto    a = f.tm.mkConst(f.arr, "a"), i = f.tm.mkConst(f.idx, "i"), v = f.tm.mkConst(f.elem, "v");
  auto    st = f.t(Kind::STORE, {zero, f.tm.mkBitVector(2, 1), f.tm.mkBitVector(8, 7)});
  // the stored constant reads back, every other entry stays 0
  f.same_result(f.t(Kind::AND,
                    {f.t(Kind::EQUAL, {f.t(Kind::SELECT, {st, i}), v}), f.t(Kind::DISTINCT, {v, f.tm.mkBitVector(8, 0)}),
                     f.t(Kind::DISTINCT, {v, f.tm.mkBitVector(8, 7)})}),
                false);
  f.same_result(f.t(Kind::AND, {f.t(Kind::EQUAL, {a, st}), f.t(Kind::EQUAL, {f.t(Kind::SELECT, {a, i}), f.tm.mkBitVector(8, 7)})}),
                true);
}

TEST(ArrayBlast, ModelsAndValuesFollowTheRewrite) {
  Fixture      f;
  auto         a = f.tm.mkConst(f.arr, "a"), i = f.tm.mkConst(f.idx, "i"), v = f.tm.mkConst(f.elem, "v");
  Fresh_solver s(f.tm);
  s.setLogic("QF_ABV");
  s.set_profile("internal", Solver_query_ctx{.has_mem = true, .witness = true});
  s.set_array_blast(5);
  s.assertFormula(f.t(Kind::EQUAL, {f.t(Kind::SELECT, {a, i}), v}));
  s.assertFormula(f.t(Kind::EQUAL, {i, f.tm.mkBitVector(2, 2)}));
  s.assertFormula(f.t(Kind::EQUAL, {v, f.tm.mkBitVector(8, 99)}));
  ASSERT_TRUE(s.checkSat().isSat());
  // a value of the ORIGINAL array term, read back at the model's index
  auto av = s.getValue(a);
  ASSERT_FALSE(av.isNull());
  cvc5::Solver check(f.tm);
  check.setLogic("QF_ABV");
  check.setOption("arrays-exp", "true");
  EXPECT_TRUE(check.simplify(f.t(Kind::SELECT, {av, f.tm.mkBitVector(2, 2)})).isBitVectorValue());
  EXPECT_EQ(check.simplify(f.t(Kind::SELECT, {av, f.tm.mkBitVector(2, 2)})).getBitVectorValue(10), "99");
  EXPECT_EQ(s.getValue(f.t(Kind::SELECT, {a, i})).getBitVectorValue(10), "99");
}

TEST(ArrayBlast, LargeArraysStayArrays) {
  cvc5::TermManager tm;
  Array_blaster     blaster(tm, 5);
  auto              big = tm.mkConst(tm.mkArraySort(tm.mkBitVectorSort(8), tm.mkBitVectorSort(8)), "big");
  auto              i   = tm.mkConst(tm.mkBitVectorSort(8), "i");
  auto              rd  = tm.mkTerm(Kind::SELECT, {big, i});
  EXPECT_EQ(blaster.rewrite(rd), rd);
  EXPECT_TRUE(blaster.saw_large_array());
  EXPECT_FALSE(blaster.failed());
}

// Random store/select/ite programs over two 4-entry arrays: the blasted and the
// array-theory solvers must agree on every query.
TEST(ArrayBlast, RandomProgramsAgree) {
  Fixture                         f;
  std::mt19937                    rng(7);
  std::vector<cvc5::Term>         arrays = {f.tm.mkConst(f.arr, "a"), f.tm.mkConst(f.arr, "b"),
                                            f.tm.mkConstArray(f.arr, f.tm.mkBitVector(8, 3))};
  std::vector<cvc5::Term>         idxs   = {f.tm.mkConst(f.idx, "i"), f.tm.mkConst(f.idx, "j"), f.tm.mkBitVector(2, 1)};
  std::vector<cvc5::Term>         vals   = {f.tm.mkConst(f.elem, "x"), f.tm.mkConst(f.elem, "y"), f.tm.mkBitVector(8, 5)};
  std::uniform_int_distribution<> pick(0, 2);
  for (int round = 0; round < 12; ++round) {
    std::vector<cvc5::Term> pool = arrays;
    for (int k = 0; k < 4; ++k) {
      auto base = pool[std::uniform_int_distribution<>(0, static_cast<int>(pool.size()) - 1)(rng)];
      if (pick(rng) == 0) {
        auto other = pool[std::uniform_int_distribution<>(0, static_cast<int>(pool.size()) - 1)(rng)];
        pool.push_back(f.t(Kind::ITE, {f.t(Kind::EQUAL, {idxs[pick(rng)], idxs[pick(rng)]}), base, other}));
      } else {
        pool.push_back(f.t(Kind::STORE, {base, idxs[pick(rng)], vals[pick(rng)]}));
      }
    }
    auto lhs = f.t(Kind::SELECT, {pool.back(), idxs[pick(rng)]});
    auto rhs = f.t(Kind::SELECT, {pool[pool.size() - 2], idxs[pick(rng)]});
    for (auto q : {f.t(Kind::DISTINCT, {lhs, rhs}), f.t(Kind::EQUAL, {pool.back(), pool[pool.size() - 2]})}) {
      cvc5::Solver ref(f.tm);
      ref.setLogic("QF_ABV");
      ref.setOption("arrays-exp", "true");
      ref.assertFormula(q);
      const bool sat = ref.checkSat().isSat();
      f.same_result(q, sat);
    }
  }
}
