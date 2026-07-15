// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Differential validation of the cone bit-blaster against cvc5 ITSELF. Every
// case asks the same question two ways -- "is this diff UNSAT?" -- of cvc5 and
// of abc_prove_unsat, and fails on ANY disagreement.
//
// This is the soundness test that matters: lec subtracts an ABC-Proven cut from
// the cvc5 obligation, so a blaster whose adder/shifter/comparator semantics
// drift from cvc5's by even one bit could retire a cut cvc5 would refute. Using
// the SMT solver as the oracle is the only check that covers the whole op set
// without re-deriving the semantics a second time (and getting them wrong the
// same way twice).

#include "cone_abc.hpp"

#include <unistd.h>
#include <sys/wait.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "cvc5/cvc5.h"
#include "gtest/gtest.h"

using namespace cvc5;
using livehd::lec::abc_prove_unsat;
using livehd::lec::Cone_verdict;

namespace {

constexpr int64_t kLimit = 0;  // ABC default effort: these cones are tiny

// The cvc5 oracle: UNSAT (the two sides always agree) or SAT.
bool cvc5_says_unsat(TermManager& tm, const Term& diff) {
  Solver s(tm);
  s.setLogic("QF_BV");
  s.assertFormula(diff);
  Result r = s.checkSat();
  EXPECT_FALSE(r.isUnknown()) << "oracle went unknown on a tiny query";
  return r.isUnsat();
}

// Assert the blaster and cvc5 reach the SAME conclusion about `diff`.
void expect_agree(TermManager& tm, const Term& diff, const std::string& what) {
  const bool         unsat = cvc5_says_unsat(tm, diff);
  livehd::lec::Cone_stats st;
  const Cone_verdict v = abc_prove_unsat(diff, kLimit, &st);
  ASSERT_NE(v, Cone_verdict::Unsupported) << what << ": blaster bailed on " << st.why;
  ASSERT_NE(v, Cone_verdict::Unknown) << what << ": ABC gave up on a tiny cone";
  if (unsat) {
    EXPECT_EQ(v, Cone_verdict::Proven) << what << ": cvc5 says UNSAT, ABC says " << cone_verdict_name(v);
  } else {
    EXPECT_EQ(v, Cone_verdict::Refuted) << what << ": cvc5 says SAT, ABC says " << cone_verdict_name(v);
  }
}

Term distinct(TermManager& tm, const Term& a, const Term& b) { return tm.mkTerm(Kind::DISTINCT, {a, b}); }

}  // namespace

// ---- identities the blaster must reproduce exactly (all UNSAT) --------------
TEST(ConeAbc, ArithmeticIdentities) {
  TermManager tm;
  Sort        bv8 = tm.mkBitVectorSort(8);
  Term        a   = tm.mkConst(bv8, "a");
  Term        b   = tm.mkConst(bv8, "b");
  Term        one = tm.mkBitVector(8, 1);

  expect_agree(tm, distinct(tm, a, a), "a == a");
  expect_agree(tm, distinct(tm, tm.mkTerm(Kind::BITVECTOR_ADD, {a, b}), tm.mkTerm(Kind::BITVECTOR_ADD, {b, a})),
               "add commutes");
  expect_agree(tm,
               distinct(tm, tm.mkTerm(Kind::BITVECTOR_SUB, {tm.mkTerm(Kind::BITVECTOR_ADD, {a, b}), b}), a),
               "(a+b)-b == a");
  expect_agree(tm, distinct(tm, tm.mkTerm(Kind::BITVECTOR_MULT, {a, b}), tm.mkTerm(Kind::BITVECTOR_MULT, {b, a})),
               "mul commutes");
  expect_agree(tm, distinct(tm, tm.mkTerm(Kind::BITVECTOR_NOT, {tm.mkTerm(Kind::BITVECTOR_NOT, {a})}), a), "~~a == a");
  expect_agree(tm, distinct(tm, tm.mkTerm(Kind::BITVECTOR_NEG, {tm.mkTerm(Kind::BITVECTOR_NEG, {a})}), a), "--a == a");
  // A real difference: a != a+1 for every a (bvadd wraps, so this is UNSAT-free).
  expect_agree(tm, distinct(tm, a, tm.mkTerm(Kind::BITVECTOR_ADD, {a, one})), "a vs a+1");
}

// ---- shifts: the saturating >= width semantics are easy to get wrong --------
TEST(ConeAbc, Shifts) {
  TermManager tm;
  Sort        bv8 = tm.mkBitVectorSort(8);
  Term        a   = tm.mkConst(bv8, "a");
  Term        s   = tm.mkConst(bv8, "s");

  expect_agree(tm, distinct(tm, tm.mkTerm(Kind::BITVECTOR_SHL, {a, tm.mkBitVector(8, 0)}), a), "a<<0 == a");
  // Every dynamic shift amount, including the >= width saturation cases.
  expect_agree(tm,
               distinct(tm, tm.mkTerm(Kind::BITVECTOR_SHL, {a, s}), tm.mkTerm(Kind::BITVECTOR_SHL, {a, s})),
               "shl self");
  expect_agree(tm,
               distinct(tm, tm.mkTerm(Kind::BITVECTOR_LSHR, {a, s}), tm.mkTerm(Kind::BITVECTOR_LSHR, {a, s})),
               "lshr self");
  expect_agree(tm,
               distinct(tm, tm.mkTerm(Kind::BITVECTOR_ASHR, {a, s}), tm.mkTerm(Kind::BITVECTOR_ASHR, {a, s})),
               "ashr self");
  // shl by 1 == a+a (exercises the barrel shifter against the adder)
  expect_agree(tm,
               distinct(tm, tm.mkTerm(Kind::BITVECTOR_SHL, {a, tm.mkBitVector(8, 1)}), tm.mkTerm(Kind::BITVECTOR_ADD, {a, a})),
               "a<<1 == a+a");
  // Not equal in general: lshr vs ashr differ exactly when a is negative.
  expect_agree(tm, distinct(tm, tm.mkTerm(Kind::BITVECTOR_LSHR, {a, s}), tm.mkTerm(Kind::BITVECTOR_ASHR, {a, s})),
               "lshr vs ashr");
}

// ---- signed vs unsigned comparison boundaries ------------------------------
TEST(ConeAbc, Comparisons) {
  TermManager tm;
  Sort        bv6 = tm.mkBitVectorSort(6);
  Term        a   = tm.mkConst(bv6, "a");
  Term        b   = tm.mkConst(bv6, "b");

  expect_agree(tm,
               distinct(tm,
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_ULT, {a, b}), a, b}),
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_UGT, {b, a}), a, b})),
               "ult(a,b) == ugt(b,a)");
  expect_agree(tm,
               distinct(tm,
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_ULE, {a, b}), a, b}),
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::NOT, {tm.mkTerm(Kind::BITVECTOR_ULT, {b, a})}), a, b})),
               "ule(a,b) == !ult(b,a)");
  expect_agree(tm,
               distinct(tm,
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_SLT, {a, b}), a, b}),
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_SGT, {b, a}), a, b})),
               "slt(a,b) == sgt(b,a)");
  // signed and unsigned MUST disagree somewhere (the sign-bit boundary)
  expect_agree(tm,
               distinct(tm,
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_SLT, {a, b}), a, b}),
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_ULT, {a, b}), a, b})),
               "slt vs ult");
  expect_agree(tm,
               distinct(tm,
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_SGE, {a, b}), a, b}),
                        tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::NOT, {tm.mkTerm(Kind::BITVECTOR_SLT, {a, b})}), a, b})),
               "sge(a,b) == !slt(a,b)");
}

// ---- extract / concat / extend: the bit-order traps -------------------------
TEST(ConeAbc, SliceAndExtend) {
  TermManager tm;
  Sort        bv8 = tm.mkBitVectorSort(8);
  Term        a   = tm.mkConst(bv8, "a");

  Op   hi  = tm.mkOp(Kind::BITVECTOR_EXTRACT, {7, 4});
  Op   lo  = tm.mkOp(Kind::BITVECTOR_EXTRACT, {3, 0});
  Term rebuilt = tm.mkTerm(Kind::BITVECTOR_CONCAT, {tm.mkTerm(hi, {a}), tm.mkTerm(lo, {a})});
  expect_agree(tm, distinct(tm, rebuilt, a), "concat(a[7:4],a[3:0]) == a");

  // zero_extend then extract the pad must be 0; sign_extend must copy bit 7.
  Op   ze  = tm.mkOp(Kind::BITVECTOR_ZERO_EXTEND, {4});
  Op   se  = tm.mkOp(Kind::BITVECTOR_SIGN_EXTEND, {4});
  Op   top = tm.mkOp(Kind::BITVECTOR_EXTRACT, {11, 8});
  expect_agree(tm, distinct(tm, tm.mkTerm(top, {tm.mkTerm(ze, {a})}), tm.mkBitVector(4, 0)), "zero_extend pad == 0");
  expect_agree(tm,
               distinct(tm,
                        tm.mkTerm(top, {tm.mkTerm(se, {a})}),
                        tm.mkTerm(Kind::ITE,
                                  {tm.mkTerm(Kind::EQUAL, {tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {7, 7}), {a}), tm.mkBitVector(1, 1)}),
                                   tm.mkBitVector(4, 15),
                                   tm.mkBitVector(4, 0)})),
               "sign_extend pad == replicated a[7]");
  // The classic trap: sign_extend != zero_extend whenever a is negative.
  expect_agree(tm, distinct(tm, tm.mkTerm(ze, {a}), tm.mkTerm(se, {a})), "zero_extend vs sign_extend");
}

// ---- the fragment boundary: arrays must bail, never mis-prove ---------------
TEST(ConeAbc, ArraysAreUnsupported) {
  TermManager tm;
  Sort        bv4 = tm.mkBitVectorSort(4);
  Sort        arr = tm.mkArraySort(bv4, bv4);
  Term        m   = tm.mkConst(arr, "m");
  Term        i   = tm.mkConst(bv4, "i");
  Term        rd  = tm.mkTerm(Kind::SELECT, {m, i});

  livehd::lec::Cone_stats st;
  // Trivially UNSAT for cvc5, but SELECT is outside the fragment: the blaster
  // must decline (leaving the cut to cvc5), not claim a proof.
  EXPECT_EQ(abc_prove_unsat(distinct(tm, rd, rd), kLimit, &st), Cone_verdict::Unsupported);
  EXPECT_FALSE(st.why.empty());
}

// ---- randomized differential fuzz over the whole supported op set -----------
namespace {

Term random_expr(TermManager& tm, std::mt19937& rng, const std::vector<Term>& leaves, int depth) {
  std::uniform_int_distribution<int> leaf_pick(0, static_cast<int>(leaves.size()) - 1);
  if (depth <= 0) {
    return leaves[leaf_pick(rng)];
  }
  std::uniform_int_distribution<int> op_pick(0, 12);
  const Term                         x = random_expr(tm, rng, leaves, depth - 1);
  const Term                         y = random_expr(tm, rng, leaves, depth - 1);
  switch (op_pick(rng)) {
    case 0: return tm.mkTerm(Kind::BITVECTOR_ADD, {x, y});
    case 1: return tm.mkTerm(Kind::BITVECTOR_SUB, {x, y});
    case 2: return tm.mkTerm(Kind::BITVECTOR_AND, {x, y});
    case 3: return tm.mkTerm(Kind::BITVECTOR_OR, {x, y});
    case 4: return tm.mkTerm(Kind::BITVECTOR_XOR, {x, y});
    case 5: return tm.mkTerm(Kind::BITVECTOR_NOT, {x});
    case 6: return tm.mkTerm(Kind::BITVECTOR_NEG, {x});
    case 7: return tm.mkTerm(Kind::BITVECTOR_MULT, {x, y});
    case 8: return tm.mkTerm(Kind::BITVECTOR_SHL, {x, y});
    case 9: return tm.mkTerm(Kind::BITVECTOR_LSHR, {x, y});
    case 10: return tm.mkTerm(Kind::BITVECTOR_ASHR, {x, y});
    case 11: return tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_ULT, {x, y}), x, y});
    default: return tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::BITVECTOR_SLT, {x, y}), y, x});
  }
}

}  // namespace

// Random terms, cvc5 as the oracle: ANY disagreement is a blaster bug. Narrow
// (5-bit) so the oracle stays instant while still crossing every sign boundary.
TEST(ConeAbc, RandomDifferentialFuzz) {
  TermManager tm;
  Sort        bv5 = tm.mkBitVectorSort(5);
  std::vector<Term> leaves{tm.mkConst(bv5, "a"),
                           tm.mkConst(bv5, "b"),
                           tm.mkConst(bv5, "c"),
                           tm.mkBitVector(5, 0),
                           tm.mkBitVector(5, 1),
                           tm.mkBitVector(5, 31)};
  std::mt19937 rng(12345);  // fixed seed: a failure must be reproducible
  for (int i = 0; i < 120; ++i) {
    const Term t1 = random_expr(tm, rng, leaves, 3);
    const Term t2 = random_expr(tm, rng, leaves, 3);
    expect_agree(tm, distinct(tm, t1, t2), "fuzz#" + std::to_string(i));
    if (::testing::Test::HasFatalFailure() || ::testing::Test::HasNonfatalFailure()) {
      return;  // first disagreement is the signal; keep the log readable
    }
  }
}

// Self-equality of a random term is ALWAYS UNSAT -- the blaster must prove every
// one of these (the shape a matched cone takes once both sides agree).
TEST(ConeAbc, RandomSelfEquivalenceAlwaysProven) {
  TermManager tm;
  Sort        bv5 = tm.mkBitVectorSort(5);
  std::vector<Term> leaves{tm.mkConst(bv5, "a"), tm.mkConst(bv5, "b"), tm.mkBitVector(5, 3)};
  std::mt19937      rng(777);
  for (int i = 0; i < 40; ++i) {
    const Term t = random_expr(tm, rng, leaves, 3);
    EXPECT_EQ(abc_prove_unsat(distinct(tm, t, t), kLimit, nullptr), Cone_verdict::Proven) << "self-equivalence #" << i;
  }
}

// ---- cone_digest: the cache key's soundness properties ----------------------
// The digest decides whether a stored PROVEN is replayed for a cone, so it has
// exactly two obligations: identical obligations must agree (or the cache never
// hits), and DIFFERENT obligations must never collide (or a PROVEN is
// transferred to a cone nobody proved -- a false PROVEN).

TEST(ConeDigest, StableAcrossProcesses) {
  // The whole premise of persisting the digest: a fresh process re-deriving the
  // same obligation must land on the same key. Computed in a forked child with
  // its own TermManager (and its own term ids) and compared to the parent's.
  TermManager tm;
  Sort        bv8 = tm.mkBitVectorSort(8);
  Term        a   = tm.mkConst(bv8, "s_flop_a");
  Term        b   = tm.mkConst(bv8, "port_b");
  Term        t   = distinct(tm, tm.mkTerm(Kind::BITVECTOR_ADD, {a, b}), tm.mkTerm(Kind::BITVECTOR_MULT, {b, a}));
  const std::string parent = livehd::lec::cone_digest(t);
  ASSERT_EQ(parent.size(), 32u) << "a named-symbol cone must be digestable";

  int fds[2];
  ASSERT_EQ(pipe(fds), 0);
  const pid_t pid = fork();
  ASSERT_GE(pid, 0);
  if (pid == 0) {
    close(fds[0]);
    TermManager ctm;  // a DIFFERENT term universe: ids/pointers cannot match
    Sort        cbv = ctm.mkBitVectorSort(8);
    Term        ca  = ctm.mkConst(cbv, "s_flop_a");
    Term        cb  = ctm.mkConst(cbv, "port_b");
    Term        ct  = ctm.mkTerm(Kind::DISTINCT, {ctm.mkTerm(Kind::BITVECTOR_ADD, {ca, cb}),
                                                  ctm.mkTerm(Kind::BITVECTOR_MULT, {cb, ca})});
    const std::string d = livehd::lec::cone_digest(ct);
    (void)!write(fds[1], d.c_str(), d.size());
    close(fds[1]);
    _exit(0);
  }
  close(fds[1]);
  std::string child;
  char        buf[64];
  for (ssize_t n = 0; (n = read(fds[0], buf, sizeof buf)) > 0;) {
    child.append(buf, static_cast<size_t>(n));
  }
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  EXPECT_EQ(child, parent) << "the digest must not depend on the process that built the term";
}

TEST(ConeDigest, DiscriminatesSymbolsAndIndices) {
  TermManager tm;
  Sort        bv8 = tm.mkBitVectorSort(8);
  Term        p   = tm.mkConst(bv8, "s_p");
  Term        q   = tm.mkConst(bv8, "s_q");
  Term        r   = tm.mkConst(bv8, "s_r");

  // Same SHAPE, different boundary symbol => a different obligation.
  EXPECT_NE(livehd::lec::cone_digest(distinct(tm, p, q)), livehd::lec::cone_digest(distinct(tm, p, r)));
  // Operand order is part of the term.
  EXPECT_NE(livehd::lec::cone_digest(distinct(tm, tm.mkTerm(Kind::BITVECTOR_SUB, {p, q}), r)),
            livehd::lec::cone_digest(distinct(tm, tm.mkTerm(Kind::BITVECTOR_SUB, {q, p}), r)));
  // Operator INDICES are part of the term: a[3:0] and a[7:4] are not the same cut.
  Term lo = tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {3, 0}), {p});
  Term hi = tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {7, 4}), {p});
  Term z  = tm.mkBitVector(4, 0);
  EXPECT_NE(livehd::lec::cone_digest(distinct(tm, lo, z)), livehd::lec::cone_digest(distinct(tm, hi, z)));
  // Constants are part of the term.
  EXPECT_NE(livehd::lec::cone_digest(distinct(tm, p, tm.mkBitVector(8, 1))),
            livehd::lec::cone_digest(distinct(tm, p, tm.mkBitVector(8, 2))));
  // ... and the same obligation twice agrees with itself.
  EXPECT_EQ(livehd::lec::cone_digest(distinct(tm, p, q)), livehd::lec::cone_digest(distinct(tm, p, q)));
}

TEST(ConeDigest, RefusesAnonymousSymbols) {
  // An unnamed cvc5 const prints as var_<allocation id>, which differs between
  // processes -- persisting a key built from it could transfer a PROVEN between
  // two DIFFERENT cones. cone_digest must decline (empty = do not cache) rather
  // than mint an unstable key.
  TermManager tm;
  Sort        bv8  = tm.mkBitVectorSort(8);
  Term        anon = tm.mkConst(bv8);  // no symbol
  Term        named = tm.mkConst(bv8, "s_named");
  ASSERT_FALSE(anon.hasSymbol());
  EXPECT_TRUE(livehd::lec::cone_digest(distinct(tm, anon, named)).empty());
  EXPECT_TRUE(livehd::lec::cone_digest(distinct(tm, tm.mkTerm(Kind::BITVECTOR_ADD, {anon, named}), named)).empty())
      << "an anonymous symbol ANYWHERE in the cone must make the whole cone undigestable";
}

// The ind engine encodes BOTH designs with an EMPTY prefix (query.cpp), so the
// two sides' memory read douts and comb-box outputs are DISTINCT cvc5 symbols
// that carry the SAME name (mkConst does not hash-cons on the name). A digest
// that keyed on the name alone would map a SAT cone onto an UNSAT one's key and
// replay a PROVEN that was never established -- the one thing the cache must not
// do. Same name must NOT imply same symbol.
TEST(ConeDigest, DistinguishesDistinctSymbolsSharingAName) {
  TermManager tm;
  Sort        bv8 = tm.mkBitVectorSort(8);
  Term        a   = tm.mkConst(bv8, "m:32x64#0:rd0");
  Term        b   = tm.mkConst(bv8, "m:32x64#0:rd0");  // exactly what the two sides do
  ASSERT_NE(a, b) << "cvc5 mkConst must mint a fresh symbol per call";

  // DISTINCT(a,b) is SAT (two free vars); DISTINCT(a,a) is UNSAT. If these ever
  // shared a digest, caching the UNSAT one would settle the SAT one.
  const std::string ab = livehd::lec::cone_digest(distinct(tm, a, b));
  const std::string aa = livehd::lec::cone_digest(distinct(tm, a, a));
  EXPECT_NE(ab, aa) << "a SAT cone must never share a key with an UNSAT one";

  // Same through a shared operator shape, which is how it actually appears.
  Term fa = tm.mkTerm(Kind::BITVECTOR_ADD, {a, tm.mkBitVector(8, 1)});
  Term fb = tm.mkTerm(Kind::BITVECTOR_ADD, {b, tm.mkBitVector(8, 1)});
  EXPECT_NE(livehd::lec::cone_digest(distinct(tm, fa, fb)), livehd::lec::cone_digest(distinct(tm, fa, fa)));

  // ... and the disambiguation must stay STABLE: the same term digests the same.
  EXPECT_EQ(livehd::lec::cone_digest(distinct(tm, fa, fb)), livehd::lec::cone_digest(distinct(tm, fa, fb)));
}
