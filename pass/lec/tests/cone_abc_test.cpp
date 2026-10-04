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

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "cvc5/cvc5.h"
#include "gtest/gtest.h"

using namespace cvc5;
using livehd::lec::abc_prove_unsat;
using livehd::lec::abc_prove_unsat_batch;
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

// An independent gate-level array multiplier. The faulty version drops one
// carry term, so arithmetic proof must never equate its output to MUL.
Term gate_product(TermManager& tm, const Term& a, const Term& b, uint32_t width, bool signed_a, bool signed_b,
                  bool faulty_carry = false) {
  const auto bit = [&](const Term& value, uint32_t index, bool sign) {
    const auto n = value.getSort().getBitVectorSize();
    if (index >= n && !sign) {
      return tm.mkBitVector(1, 0);
    }
    const auto i = std::min(index, n - 1);
    return tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {i, i}), {value});
  };
  std::vector<Term> sum(width, tm.mkBitVector(1, 0));
  for (uint32_t j = 0; j < width; ++j) {
    Term carry = tm.mkBitVector(1, 0);
    for (uint32_t i = 0; i < width; ++i) {
      const Term part
          = i < j ? tm.mkBitVector(1, 0) : tm.mkTerm(Kind::BITVECTOR_AND, {bit(a, i - j, signed_a), bit(b, j, signed_b)});
      const Term propagate = tm.mkTerm(Kind::BITVECTOR_XOR, {sum[i], part});
      const Term generate  = tm.mkTerm(Kind::BITVECTOR_AND, {sum[i], part});
      sum[i]               = tm.mkTerm(Kind::BITVECTOR_XOR, {propagate, carry});
      carry                = faulty_carry && j == 1 && i == 2
                                 ? generate
                                 : tm.mkTerm(Kind::BITVECTOR_OR, {generate, tm.mkTerm(Kind::BITVECTOR_AND, {propagate, carry})});
    }
  }
  Term product = sum.back();
  for (size_t i = sum.size() - 1; i-- > 0;) {
    product = tm.mkTerm(Kind::BITVECTOR_CONCAT, {product, sum[i]});
  }
  return product;
}

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

TEST(ConeAbc, ArithmeticTreesPreserveSignednessTruncationAndCarryFaults) {
  for (const uint32_t width : {5U, 9U, 17U}) {
    for (int signs = 0; signs < 4; ++signs) {
      TermManager tm;
      const Term  a      = tm.mkConst(tm.mkBitVectorSort(4), "a");
      const Term  b      = tm.mkConst(tm.mkBitVectorSort(4), "b");
      const auto  extend = [&](const Term& value, bool sign) {
        return tm.mkTerm(tm.mkOp(sign ? Kind::BITVECTOR_SIGN_EXTEND : Kind::BITVECTOR_ZERO_EXTEND, {width - 4}), {value});
      };
      const Term product = tm.mkTerm(Kind::BITVECTOR_MULT, {extend(a, signs & 1), extend(b, signs & 2)});
      expect_agree(tm, distinct(tm, product, gate_product(tm, a, b, width, signs & 1, signs & 2)), "array multiplier");
      const Term fault = distinct(tm, product, gate_product(tm, a, b, width, signs & 1, signs & 2, true));
      ASSERT_FALSE(cvc5_says_unsat(tm, fault)) << "fixture must expose its faulty carry";
      expect_agree(tm, fault, "faulty array multiplier");
    }
  }
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

// ---- the fragment boundary: arrays are ABSTRACTED, one way only -------------
TEST(ConeAbc, ArraySelectIsAbstractedSoundly) {
  TermManager tm;
  Sort        bv4 = tm.mkBitVectorSort(4);
  Sort        arr = tm.mkArraySort(bv4, bv4);
  Term        m   = tm.mkConst(arr, "m");
  Term        m2  = tm.mkConst(arr, "m2");
  Term        i   = tm.mkConst(bv4, "i");
  Term        rd  = tm.mkTerm(Kind::SELECT, {m, i});
  Term        rd2 = tm.mkTerm(Kind::SELECT, {m2, i});

  livehd::lec::Cone_stats st;
  // SELECT is outside the blastable fragment, so it becomes a free input KEYED
  // BY THE TERM. The SAME select on both sides is then the same input, and the
  // cut proves -- which is the whole point: a whole-array register file written
  // from a function of ITSELF puts a select in every one of its obligations, and
  // declining the cone left the array cut for cvc5's array theory alone.
  EXPECT_EQ(abc_prove_unsat(distinct(tm, rd, rd), kLimit, &st), Cone_verdict::Proven);

  // The abstraction is an OVER-approximation, so the other direction must never
  // be reported: two DIFFERENT arrays read at one index really are unequal in
  // general, but a bit-level "difference" over free inputs is not evidence of
  // one. Unknown (the cut stays with cvc5), never Refuted.
  EXPECT_EQ(abc_prove_unsat(distinct(tm, rd, rd2), kLimit, &st), Cone_verdict::Unknown);

  // An ARRAY-SORTED root is still outside the fragment: it is not a value the
  // blaster can hand back a bit for.
  Term arr_eq = tm.mkTerm(Kind::DISTINCT, {m, m2});
  livehd::lec::Cone_stats st2;
  EXPECT_EQ(abc_prove_unsat(arr_eq, kLimit, &st2), Cone_verdict::Unsupported);
  EXPECT_FALSE(st2.why.empty());
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

// A cone ABC cannot finish (wide multiplier commutativity) must not starve the
// cones queued behind it: the batch abandons it to cvc5 after its stall budget,
// a fresh child resumes at the next cone, and the index bookkeeping lands every
// verdict on its own cone -- all well inside the deadline.
TEST(ConeAbc, BatchResumesPastAStalledCone) {
  TermManager tm;
  Sort        wide = tm.mkBitVectorSort(32);
  Sort        bv8  = tm.mkBitVectorSort(8);
  Term        x    = tm.mkConst(wide, "x");
  Term        y    = tm.mkConst(wide, "y");
  Term        a    = tm.mkConst(bv8, "a");
  Term        b    = tm.mkConst(bv8, "b");
  const Term  hard = distinct(tm, tm.mkTerm(Kind::BITVECTOR_MULT, {x, y}), tm.mkTerm(Kind::BITVECTOR_MULT, {y, x}));
  const Term  add  = distinct(tm, tm.mkTerm(Kind::BITVECTOR_ADD, {a, b}), tm.mkTerm(Kind::BITVECTOR_ADD, {b, a}));
  const Term  sat  = distinct(tm, a, tm.mkTerm(Kind::BITVECTOR_ADD, {a, tm.mkBitVector(8, 1)}));
  const Term  inv  = distinct(tm, tm.mkTerm(Kind::BITVECTOR_NOT, {tm.mkTerm(Kind::BITVECTOR_NOT, {a})}), a);

  constexpr int64_t                    deadline_ms = 3000;  // a 250 ms stall budget per cone
  std::vector<livehd::lec::Cone_stats> stats;
  const auto                           t0       = std::chrono::steady_clock::now();
  const auto                           verdicts = abc_prove_unsat_batch({hard, add, hard, sat, inv}, kLimit, deadline_ms, &stats);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

  ASSERT_EQ(verdicts.size(), 5U);
  EXPECT_NE(verdicts[0], Cone_verdict::Refuted) << "commutativity is UNSAT";
  EXPECT_NE(verdicts[2], Cone_verdict::Refuted) << "commutativity is UNSAT";
  EXPECT_EQ(verdicts[1], Cone_verdict::Proven) << "a+b == b+a behind a stalled cone";
  EXPECT_EQ(verdicts[3], Cone_verdict::Refuted) << "a vs a+1 behind a stalled cone";
  EXPECT_EQ(verdicts[4], Cone_verdict::Proven) << "~~a == a after two stalls";
  EXPECT_LT(ms, deadline_ms) << "a stalled cone held the batch to its deadline";
}

TEST(ConeAbc, WideMemoryReadMuxesPreserveDataAndAddress) {
  TermManager       tm;
  const Term        address = tm.mkConst(tm.mkBitVectorSort(4), "read_address");
  std::vector<Term> words;
  for (unsigned i = 0; i < 16; ++i) {
    words.push_back(tm.mkConst(tm.mkBitVectorSort(32), "word" + std::to_string(i)));
  }
  Term priority = words.back();
  for (unsigned i = 15; i-- > 0;) {
    priority = tm.mkTerm(Kind::ITE, {tm.mkTerm(Kind::EQUAL, {address, tm.mkBitVector(4, i)}), words[i], priority});
  }
  auto level = words;
  for (unsigned bit = 0; bit < 4; ++bit) {
    const Term selector
        = tm.mkTerm(Kind::EQUAL, {tm.mkTerm(tm.mkOp(Kind::BITVECTOR_EXTRACT, {bit, bit}), {address}), tm.mkBitVector(1, 1)});
    std::vector<Term> next;
    for (size_t i = 0; i < level.size(); i += 2) {
      next.push_back(tm.mkTerm(Kind::ITE, {selector, level[i + 1], level[i]}));
    }
    level = std::move(next);
  }
  expect_agree(tm, distinct(tm, priority, level[0]), "priority and binary memory read muxes");
  const Term fault = tm.mkTerm(Kind::BITVECTOR_XOR, {level[0], tm.mkBitVector(32, 1)});
  expect_agree(tm, distinct(tm, priority, fault), "faulty wide memory read mux");
}

TEST(ConeAbc, ArithmeticPortfolioPreservesCounterexamples) {
  TermManager tm;
  const auto  input   = tm.mkBitVectorSort(4);
  const auto  a       = tm.mkConst(input, "portfolio_a");
  const auto  b       = tm.mkConst(input, "portfolio_b");
  const auto  extend  = [&](const Term& t) { return tm.mkTerm(tm.mkOp(Kind::BITVECTOR_SIGN_EXTEND, {5}), {t}); };
  const auto  product = tm.mkTerm(Kind::BITVECTOR_MULT, {extend(a), extend(b)});
  const auto  good    = distinct(tm, product, gate_product(tm, a, b, 9, true, true));
  const auto  bad     = distinct(tm, product, gate_product(tm, a, b, 9, true, true, true));
  ASSERT_TRUE(cvc5_says_unsat(tm, good));
  ASSERT_FALSE(cvc5_says_unsat(tm, bad));
  // Two arithmetic cones receive 1000 ms each and exercise the bounded race.
  const auto results = abc_prove_unsat_batch({good, bad}, 10000, 4000);
  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(results[0], Cone_verdict::Proven);
  EXPECT_EQ(results[1], Cone_verdict::Refuted);
}
