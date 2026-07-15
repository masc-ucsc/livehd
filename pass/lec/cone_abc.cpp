// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//
// Bit-blast a cvc5 Boolean term into an ABC AIG and prove it UNSAT. See
// cone_abc.hpp for why the obligation is consumed as a TERM (no LGraph
// re-walk, hence no width/semantics drift versus encode.cpp).

#include "cone_abc.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

// clang-format off
// ABC headers must stay in dependency order: abc.h defines Abc_Frame_t (used by
// main.h) and the word/namespace macros. Do not sort. ABC's headers must never
// reach a LiveHD header -- they #define short names (word, ...) that poison
// cvc5/hhds in any TU that includes them.
extern "C" {
#include "base/abc/abc.h"
#include "base/main/abcapis.h"
#include "base/main/main.h"
#include "proof/fraig/fraig.h"
#include "misc/extra/extra.h"
}
// clang-format on

namespace livehd::lec {

namespace {

using Bit = Abc_Obj_t*;  // AIG signal: node pointer with the complement bit in bit 0

// ABC's frame is a process-global singleton (mainFrame.c s_GlobalFrame), so
// materialize it once, lazily. Never Abc_Stop(): pass.abc / pass.liberty may
// share the frame within one lhd process, and lec's engine forks each get their
// own copy-on-write image that dies with the child.
void ensure_abc_started() {
  [[maybe_unused]] static const bool started = [] {
    Abc_Start();
    return true;
  }();
}

std::string kind_name(cvc5::Kind k) {
  std::ostringstream os;
  os << k;
  return os.str();
}

// Blasts a term DAG into `ntk`. Every term is visited once (memoized), so the
// shared structure a cut's two sides have in common collapses in the AIG the
// same way it does in the cvc5 term graph.
class Blaster {
public:
  Blaster(Abc_Ntk_t* ntk, const Cone_merge_map* merge)
      : ntk_(ntk), man_(static_cast<Abc_Aig_t*>(ntk->pManFunc)), one_(Abc_AigConst1(ntk)), merge_(merge) {}

  // Returns the AIG bit for `root` (Boolean), or nullptr if the term left the
  // blastable fragment (then why() names the offending kind).
  Bit run(const cvc5::Term& root) {
    walk(root);
    if (bad_) {
      return nullptr;
    }
    auto it = bl_.find(root);
    if (it == bl_.end()) {
      fail("root-not-boolean");
      return nullptr;
    }
    return it->second;
  }

  [[nodiscard]] bool               bad() const { return bad_; }
  [[nodiscard]] const std::string& why() const { return why_; }
  [[nodiscard]] int                pis() const { return npi_; }

private:
  // ---- AIG primitives ------------------------------------------------------
  Bit one() const { return one_; }
  Bit zero() const { return Abc_ObjNot(one_); }
  Bit inv(Bit a) const { return Abc_ObjNot(a); }
  Bit and2(Bit a, Bit b) { return Abc_AigAnd(man_, a, b); }
  Bit or2(Bit a, Bit b) { return Abc_AigOr(man_, a, b); }
  Bit xor2(Bit a, Bit b) { return Abc_AigXor(man_, a, b); }
  Bit mux(Bit c, Bit t, Bit e) { return Abc_AigMux(man_, c, t, e); }

  Bit new_pi(uint64_t id, uint32_t bit) {
    Abc_Obj_t*  pi   = Abc_NtkCreatePi(ntk_);
    std::string name = "v" + std::to_string(id) + "_b" + std::to_string(bit);
    Abc_ObjAssignName(pi, const_cast<char*>(name.c_str()), nullptr);
    ++npi_;
    return pi;
  }

  // Resolve a symbol to its merge representative (identity when unmerged).
  cvc5::Term canon(const cvc5::Term& t) const {
    if (merge_ == nullptr) {
      return t;
    }
    auto it = merge_->find(t);
    return it == merge_->end() ? t : it->second;
  }

  void fail(std::string_view what) {
    if (!bad_) {
      bad_ = true;
      why_ = std::string{what};
    }
  }

  // ---- multi-bit helpers (all vectors are LSB-first) ------------------------
  std::vector<Bit> add(const std::vector<Bit>& a, const std::vector<Bit>& b, Bit cin) {
    std::vector<Bit> r(a.size());
    Bit              c = cin;
    for (size_t i = 0; i < a.size(); ++i) {
      Bit ab = xor2(a[i], b[i]);
      r[i]   = xor2(ab, c);
      c      = or2(and2(a[i], b[i]), and2(c, ab));
    }
    return r;
  }

  std::vector<Bit> bv_not(std::vector<Bit> a) {
    for (auto& b : a) {
      b = inv(b);
    }
    return a;
  }

  std::vector<Bit> neg(const std::vector<Bit>& a) {
    std::vector<Bit> zeros(a.size(), zero());
    return add(bv_not(a), zeros, one());  // ~a + 1
  }

  std::vector<Bit> mul(const std::vector<Bit>& a, const std::vector<Bit>& b) {
    const size_t     w = a.size();
    std::vector<Bit> acc(w, zero());
    for (size_t j = 0; j < w; ++j) {
      std::vector<Bit> part(w, zero());
      for (size_t i = j; i < w; ++i) {
        part[i] = and2(a[i - j], b[j]);
      }
      acc = add(acc, part, zero());
    }
    return acc;  // mod 2^w, exactly SMT-LIB bvmul
  }

  // Barrel shifter. `fill` is what enters the vacated positions (0 for shl/lshr,
  // the sign bit for ashr) and is also the saturated result when the shift
  // amount is >= the width -- the SMT-LIB semantics.
  std::vector<Bit> shift(const std::vector<Bit>& a, const std::vector<Bit>& amt, bool left, Bit fill) {
    const size_t w = a.size();
    size_t       stages = 0;
    while ((size_t{1} << stages) < w) {
      ++stages;
    }
    std::vector<Bit> cur = a;
    for (size_t k = 0; k < stages && k < amt.size(); ++k) {
      const size_t     sh = size_t{1} << k;
      std::vector<Bit> nxt(w);
      for (size_t i = 0; i < w; ++i) {
        Bit src;
        if (left) {
          src = i >= sh ? cur[i - sh] : fill;
        } else {
          src = i + sh < w ? cur[i + sh] : fill;
        }
        nxt[i] = mux(amt[k], src, cur[i]);
      }
      cur = std::move(nxt);
    }
    // Any amount bit above the addressable range saturates the whole result.
    Bit ovf = zero();
    for (size_t k = stages; k < amt.size(); ++k) {
      ovf = or2(ovf, amt[k]);
    }
    for (size_t i = 0; i < w; ++i) {
      cur[i] = mux(ovf, fill, cur[i]);
    }
    return cur;
  }

  // a < b, unsigned: scanning LSB->MSB keeps the verdict of the most
  // significant differing bit.
  Bit ult(const std::vector<Bit>& a, const std::vector<Bit>& b) {
    Bit lt = zero();
    for (size_t i = 0; i < a.size(); ++i) {
      lt = mux(xor2(a[i], b[i]), and2(inv(a[i]), b[i]), lt);
    }
    return lt;
  }

  // a < b, signed: differing sign bits decide it (a negative => smaller).
  Bit slt(const std::vector<Bit>& a, const std::vector<Bit>& b) {
    const size_t msb = a.size() - 1;
    return mux(xor2(a[msb], b[msb]), a[msb], ult(a, b));
  }

  Bit eq_bits(const std::vector<Bit>& a, const std::vector<Bit>& b) {
    Bit e = one();
    for (size_t i = 0; i < a.size(); ++i) {
      e = and2(e, inv(xor2(a[i], b[i])));
    }
    return e;
  }

  // ---- traversal -----------------------------------------------------------
  bool done(const cvc5::Term& t) const { return bv_.count(t) != 0 || bl_.count(t) != 0; }

  bool supported(const cvc5::Term& t) {
    const cvc5::Sort srt = t.getSort();
    if (!srt.isBitVector() && !srt.isBoolean()) {
      fail("sort:" + srt.toString());
      return false;
    }
    switch (t.getKind()) {
      case cvc5::Kind::CONSTANT:
      case cvc5::Kind::VARIABLE:
      case cvc5::Kind::CONST_BITVECTOR:
      case cvc5::Kind::CONST_BOOLEAN:
      case cvc5::Kind::ITE:
      case cvc5::Kind::NOT:
      case cvc5::Kind::AND:
      case cvc5::Kind::OR:
      case cvc5::Kind::XOR:
      case cvc5::Kind::IMPLIES:
      case cvc5::Kind::EQUAL:
      case cvc5::Kind::DISTINCT:
      case cvc5::Kind::BITVECTOR_NOT:
      case cvc5::Kind::BITVECTOR_AND:
      case cvc5::Kind::BITVECTOR_OR:
      case cvc5::Kind::BITVECTOR_XOR:
      case cvc5::Kind::BITVECTOR_NAND:
      case cvc5::Kind::BITVECTOR_NOR:
      case cvc5::Kind::BITVECTOR_XNOR:
      case cvc5::Kind::BITVECTOR_CONCAT:
      case cvc5::Kind::BITVECTOR_EXTRACT:
      case cvc5::Kind::BITVECTOR_ZERO_EXTEND:
      case cvc5::Kind::BITVECTOR_SIGN_EXTEND:
      case cvc5::Kind::BITVECTOR_ADD:
      case cvc5::Kind::BITVECTOR_SUB:
      case cvc5::Kind::BITVECTOR_NEG:
      case cvc5::Kind::BITVECTOR_MULT:
      case cvc5::Kind::BITVECTOR_SHL:
      case cvc5::Kind::BITVECTOR_LSHR:
      case cvc5::Kind::BITVECTOR_ASHR:
      case cvc5::Kind::BITVECTOR_ULT:
      case cvc5::Kind::BITVECTOR_ULE:
      case cvc5::Kind::BITVECTOR_UGT:
      case cvc5::Kind::BITVECTOR_UGE:
      case cvc5::Kind::BITVECTOR_SLT:
      case cvc5::Kind::BITVECTOR_SLE:
      case cvc5::Kind::BITVECTOR_SGT:
      case cvc5::Kind::BITVECTOR_SGE: return true;
      default:
        // Arrays (SELECT/STORE), uninterpreted functions and division live
        // outside the blastable fragment: the cut stays with cvc5.
        fail(kind_name(t.getKind()));
        return false;
    }
  }

  void walk(const cvc5::Term& root) {
    std::vector<std::pair<cvc5::Term, bool>> st;
    st.emplace_back(root, false);
    while (!st.empty() && !bad_) {
      auto entry = st.back();
      if (done(entry.first)) {
        st.pop_back();
        continue;
      }
      if (!entry.second) {
        if (!supported(entry.first)) {
          return;
        }
        st.back().second = true;
        for (size_t i = 0; i < entry.first.getNumChildren(); ++i) {
          st.emplace_back(entry.first[i], false);
        }
      } else {
        st.pop_back();
        emit(entry.first);
      }
    }
  }

  const std::vector<Bit>& bits_of(const cvc5::Term& t) {
    auto it = bv_.find(t);
    if (it == bv_.end()) {
      fail("missing-bv-child");
      static const std::vector<Bit> empty;
      return empty;
    }
    return it->second;
  }

  Bit bool_of(const cvc5::Term& t) {
    auto it = bl_.find(t);
    if (it == bl_.end()) {
      fail("missing-bool-child");
      return zero();
    }
    return it->second;
  }

  void emit(const cvc5::Term& t) {
    if (t.getSort().isBitVector()) {
      auto v = emit_bv(t);
      if (!bad_) {
        bv_.emplace(t, std::move(v));
      }
    } else {
      Bit b = emit_bool(t);
      if (!bad_) {
        bl_.emplace(t, b);
      }
    }
  }

  std::vector<Bit> emit_bv(const cvc5::Term& t) {
    const uint32_t w = t.getSort().getBitVectorSize();
    switch (t.getKind()) {
      case cvc5::Kind::CONST_BITVECTOR: {
        const std::string s = t.getBitVectorValue(2);  // MSB-first, exactly w chars
        std::vector<Bit>  r(w);
        for (uint32_t i = 0; i < w; ++i) {
          r[i] = s[w - 1 - i] == '1' ? one() : zero();
        }
        return r;
      }
      case cvc5::Kind::CONSTANT:
      case cvc5::Kind::VARIABLE: {
        // A free symbol: shared state, a primary input, or a memory dout. This
        // is the cone's boundary -- unconstrained here, which is what makes the
        // ABC obligation at least as strong as cvc5's. A merged symbol reuses its
        // representative's inputs (a DISCHARGED assumption -- see the header).
        const cvc5::Term rep = canon(t);
        if (auto it = sym_pi_.find(rep); it != sym_pi_.end()) {
          return it->second;
        }
        std::vector<Bit> r(w);
        for (uint32_t i = 0; i < w; ++i) {
          r[i] = new_pi(rep.getId(), i);
        }
        sym_pi_.emplace(rep, r);
        return r;
      }
      case cvc5::Kind::BITVECTOR_NOT: return bv_not(bits_of(t[0]));
      case cvc5::Kind::BITVECTOR_NEG: return neg(bits_of(t[0]));
      case cvc5::Kind::BITVECTOR_AND:
      case cvc5::Kind::BITVECTOR_OR:
      case cvc5::Kind::BITVECTOR_XOR:
      case cvc5::Kind::BITVECTOR_NAND:
      case cvc5::Kind::BITVECTOR_NOR:
      case cvc5::Kind::BITVECTOR_XNOR: {
        const auto       k = t.getKind();
        std::vector<Bit> acc = bits_of(t[0]);
        for (size_t c = 1; c < t.getNumChildren(); ++c) {
          const auto& o = bits_of(t[c]);
          for (uint32_t i = 0; i < w; ++i) {
            switch (k) {
              case cvc5::Kind::BITVECTOR_AND:
              case cvc5::Kind::BITVECTOR_NAND: acc[i] = and2(acc[i], o[i]); break;
              case cvc5::Kind::BITVECTOR_OR:
              case cvc5::Kind::BITVECTOR_NOR: acc[i] = or2(acc[i], o[i]); break;
              default: acc[i] = xor2(acc[i], o[i]); break;
            }
          }
        }
        if (k == cvc5::Kind::BITVECTOR_NAND || k == cvc5::Kind::BITVECTOR_NOR || k == cvc5::Kind::BITVECTOR_XNOR) {
          acc = bv_not(std::move(acc));
        }
        return acc;
      }
      case cvc5::Kind::BITVECTOR_CONCAT: {
        // cvc5 concat is MSB-first: the LAST child holds the low bits.
        std::vector<Bit> r;
        r.reserve(w);
        for (size_t c = t.getNumChildren(); c-- > 0;) {
          const auto& o = bits_of(t[c]);
          r.insert(r.end(), o.begin(), o.end());
        }
        return r;
      }
      case cvc5::Kind::BITVECTOR_EXTRACT: {
        const uint32_t hi = t.getOp()[0].getUInt32Value();
        const uint32_t lo = t.getOp()[1].getUInt32Value();
        const auto&    a  = bits_of(t[0]);
        if (bad_ || hi >= a.size() || lo > hi) {
          fail("extract-range");
          return {};
        }
        return std::vector<Bit>(a.begin() + lo, a.begin() + hi + 1);
      }
      case cvc5::Kind::BITVECTOR_ZERO_EXTEND:
      case cvc5::Kind::BITVECTOR_SIGN_EXTEND: {
        std::vector<Bit> r = bits_of(t[0]);
        if (bad_ || r.empty()) {
          fail("extend-empty");
          return {};
        }
        const Bit pad = t.getKind() == cvc5::Kind::BITVECTOR_SIGN_EXTEND ? r.back() : zero();
        r.resize(w, pad);
        return r;
      }
      case cvc5::Kind::BITVECTOR_ADD: {
        std::vector<Bit> acc = bits_of(t[0]);
        for (size_t c = 1; c < t.getNumChildren(); ++c) {
          acc = add(acc, bits_of(t[c]), zero());
        }
        return acc;
      }
      case cvc5::Kind::BITVECTOR_SUB: {
        std::vector<Bit> acc = bits_of(t[0]);
        for (size_t c = 1; c < t.getNumChildren(); ++c) {
          acc = add(acc, neg(bits_of(t[c])), zero());
        }
        return acc;
      }
      case cvc5::Kind::BITVECTOR_MULT: {
        std::vector<Bit> acc = bits_of(t[0]);
        for (size_t c = 1; c < t.getNumChildren(); ++c) {
          acc = mul(acc, bits_of(t[c]));
        }
        return acc;
      }
      case cvc5::Kind::BITVECTOR_SHL: return shift(bits_of(t[0]), bits_of(t[1]), /*left=*/true, zero());
      case cvc5::Kind::BITVECTOR_LSHR: return shift(bits_of(t[0]), bits_of(t[1]), /*left=*/false, zero());
      case cvc5::Kind::BITVECTOR_ASHR: {
        const auto& a = bits_of(t[0]);
        if (bad_ || a.empty()) {
          fail("ashr-empty");
          return {};
        }
        return shift(a, bits_of(t[1]), /*left=*/false, a.back());
      }
      case cvc5::Kind::ITE: {
        const Bit   c = bool_of(t[0]);
        const auto& x = bits_of(t[1]);
        const auto& y = bits_of(t[2]);
        if (bad_) {
          return {};
        }
        std::vector<Bit> r(w);
        for (uint32_t i = 0; i < w; ++i) {
          r[i] = mux(c, x[i], y[i]);
        }
        return r;
      }
      default: fail(kind_name(t.getKind())); return {};
    }
  }

  Bit emit_bool(const cvc5::Term& t) {
    switch (t.getKind()) {
      case cvc5::Kind::CONST_BOOLEAN: return t.getBooleanValue() ? one() : zero();
      case cvc5::Kind::CONSTANT:
      case cvc5::Kind::VARIABLE: {
        const cvc5::Term rep = canon(t);
        auto             it  = sym_bit_.find(rep);
        if (it == sym_bit_.end()) {
          it = sym_bit_.emplace(rep, new_pi(rep.getId(), 0)).first;
        }
        return it->second;
      }
      case cvc5::Kind::NOT: return inv(bool_of(t[0]));
      case cvc5::Kind::AND: {
        Bit acc = one();
        for (size_t c = 0; c < t.getNumChildren(); ++c) {
          acc = and2(acc, bool_of(t[c]));
        }
        return acc;
      }
      case cvc5::Kind::OR: {
        Bit acc = zero();
        for (size_t c = 0; c < t.getNumChildren(); ++c) {
          acc = or2(acc, bool_of(t[c]));
        }
        return acc;
      }
      case cvc5::Kind::XOR: {
        Bit acc = bool_of(t[0]);
        for (size_t c = 1; c < t.getNumChildren(); ++c) {
          acc = xor2(acc, bool_of(t[c]));
        }
        return acc;
      }
      case cvc5::Kind::IMPLIES: return or2(inv(bool_of(t[0])), bool_of(t[1]));
      case cvc5::Kind::ITE: return mux(bool_of(t[0]), bool_of(t[1]), bool_of(t[2]));
      case cvc5::Kind::EQUAL:
      case cvc5::Kind::DISTINCT: {
        if (t.getNumChildren() != 2) {
          fail("nary-equality");  // n-ary DISTINCT is pairwise; the encoder never builds one
          return zero();
        }
        Bit e;
        if (t[0].getSort().isBitVector()) {
          const auto& a = bits_of(t[0]);
          const auto& b = bits_of(t[1]);
          if (bad_ || a.size() != b.size()) {
            fail("equality-width");
            return zero();
          }
          e = eq_bits(a, b);
        } else if (t[0].getSort().isBoolean()) {
          e = inv(xor2(bool_of(t[0]), bool_of(t[1])));
        } else {
          fail("equality-sort:" + t[0].getSort().toString());
          return zero();
        }
        return t.getKind() == cvc5::Kind::EQUAL ? e : inv(e);
      }
      case cvc5::Kind::BITVECTOR_ULT: return ult(bits_of(t[0]), bits_of(t[1]));
      case cvc5::Kind::BITVECTOR_UGT: return ult(bits_of(t[1]), bits_of(t[0]));
      case cvc5::Kind::BITVECTOR_ULE: return inv(ult(bits_of(t[1]), bits_of(t[0])));
      case cvc5::Kind::BITVECTOR_UGE: return inv(ult(bits_of(t[0]), bits_of(t[1])));
      case cvc5::Kind::BITVECTOR_SLT: return slt(bits_of(t[0]), bits_of(t[1]));
      case cvc5::Kind::BITVECTOR_SGT: return slt(bits_of(t[1]), bits_of(t[0]));
      case cvc5::Kind::BITVECTOR_SLE: return inv(slt(bits_of(t[1]), bits_of(t[0])));
      case cvc5::Kind::BITVECTOR_SGE: return inv(slt(bits_of(t[0]), bits_of(t[1])));
      default: fail(kind_name(t.getKind())); return zero();
    }
  }

  Abc_Ntk_t*  ntk_ = nullptr;
  Abc_Aig_t*  man_ = nullptr;
  Bit         one_ = nullptr;
  bool        bad_ = false;
  int         npi_ = 0;
  std::string why_;

  const Cone_merge_map*                           merge_ = nullptr;
  std::unordered_map<cvc5::Term, std::vector<Bit>> bv_;
  std::unordered_map<cvc5::Term, Bit>              bl_;
  std::unordered_map<cvc5::Term, std::vector<Bit>> sym_pi_;   // representative -> its PIs
  std::unordered_map<cvc5::Term, Bit>              sym_bit_;  // representative -> its PI (bool)
};

}  // namespace

// ---- canonical cone digest --------------------------------------------------
namespace {

uint64_t mix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ULL;
  x  = (x ^ (x >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  x  = (x ^ (x >> 27U)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31U);
}

uint64_t hcomb(uint64_t h, uint64_t v) { return mix64(h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6U) + (h >> 2U))); }

uint64_t hstr(uint64_t h, std::string_view s) {
  for (const char c : s) {
    h = (h ^ static_cast<unsigned char>(c)) * 0x100000001b3ULL;  // FNV-1a
  }
  return mix64(h);
}

// The bits of a term that DEFINE it, as text. Kind + sort pin the shape; the
// leaf payloads pin the identity (a symbol is its name -- two cones over the
// same boundary symbol are the same obligation, which is the whole point).
//
// Returns false when the term has NO stable identity, which makes the whole
// cone undigestable (and so uncacheable). See the anonymous-symbol case.
//
// `name_seq`/`sym_ix` disambiguate symbols that SHARE a name -- see the
// same-name case below; they must persist across one cone_digest walk.
bool node_payload(const cvc5::Term& t, std::string& p, std::unordered_map<std::string, uint32_t>& name_seq,
                  std::unordered_map<cvc5::Term, uint32_t>& sym_ix) {
  p = std::to_string(static_cast<int>(t.getKind())) + "|" + t.getSort().toString();
  switch (t.getKind()) {
    case cvc5::Kind::CONSTANT:
    case cvc5::Kind::VARIABLE: {
      // An UNNAMED symbol has no identity we may persist: cvc5 prints it as
      // "var_<id>", an allocation-order number that differs between processes.
      // Baking that into a stored key would let two DIFFERENT cones collide on
      // one digest and silently transfer a PROVEN -- the single failure mode
      // this pass must never have. Refuse to digest instead; the cone is simply
      // re-proven every run. (Same ruling as semdiff's anonymous state cells,
      // semdiff.cpp: a per-run debug nid is not a cross-process identity.)
      if (!t.hasSymbol()) {
        return false;
      }
      // A name is NOT an identity: mkConst does not hash-cons on it, and the ind
      // engine encodes BOTH designs with an empty prefix (query.cpp), so each
      // side's memory read dout and comb-box output are DISTINCT symbols with
      // the SAME name (":rd0", "cb:..."). Keying on the name alone would give
      // DISTINCT(f(a), f(b)) -- SAT, two free vars -- the same digest as
      // DISTINCT(f(a), f(a)) -- UNSAT -- and replay a PROVEN onto a cone nobody
      // proved. Number the symbols within each name group, in first-encounter
      // order of this deterministic walk: equal digests then mean the terms are
      // equal up to a BIJECTIVE renaming inside each group, which preserves
      // satisfiability, so a PROVEN still transfers.
      const std::string sym = t.getSymbol();
      auto [it, fresh]      = sym_ix.try_emplace(t, 0U);
      if (fresh) {
        it->second = name_seq[sym]++;
      }
      p += "|v:" + sym + "#" + std::to_string(it->second);
      break;
    }
    case cvc5::Kind::CONST_BITVECTOR: p += "|k:" + t.getBitVectorValue(2); break;
    case cvc5::Kind::CONST_BOOLEAN: p += t.getBooleanValue() ? "|b:1" : "|b:0"; break;
    default: break;
  }
  if (t.hasOp()) {  // EXTRACT/ZERO_EXTEND/... indices are part of the operator
    const cvc5::Op op = t.getOp();
    for (size_t i = 0; i < op.getNumIndices(); ++i) {
      p += "|i:" + cvc5::Op(op)[i].toString();
    }
  }
  return true;
}

}  // namespace

std::string cone_digest(const cvc5::Term& t) {
  if (t.isNull()) {
    return {};
  }
  // Two independent lanes -> 128 bits. A collision would silently transfer a
  // PROVEN between two DIFFERENT obligations, so 64 bits (birthday-bound ~2^32
  // cones) is not enough margin to rely on.
  std::unordered_map<cvc5::Term, std::pair<uint64_t, uint64_t>> memo;
  std::unordered_map<std::string, uint32_t>                     name_seq;  // name -> next free index
  std::unordered_map<cvc5::Term, uint32_t>                      sym_ix;    // symbol -> its index in its name group
  std::vector<std::pair<cvc5::Term, bool>>                      st;
  st.emplace_back(t, false);
  while (!st.empty()) {
    auto entry = st.back();
    if (memo.count(entry.first) != 0) {
      st.pop_back();
      continue;
    }
    if (!entry.second) {
      st.back().second = true;
      for (size_t i = 0; i < entry.first.getNumChildren(); ++i) {
        st.emplace_back(entry.first[i], false);
      }
      continue;
    }
    st.pop_back();
    std::string pay;
    if (!node_payload(entry.first, pay, name_seq, sym_ix)) {
      return {};  // no stable identity anywhere in the DAG => never cache this cone
    }
    uint64_t a = hstr(0xcbf29ce484222325ULL, pay);
    uint64_t b = hstr(0x9ae16a3b2f90404fULL, pay);
    for (size_t i = 0; i < entry.first.getNumChildren(); ++i) {
      const auto it = memo.find(entry.first[i]);
      if (it == memo.end()) {
        return {};  // cannot happen (post-order), but never hash a partial DAG
      }
      a = hcomb(a, it->second.first);
      b = hcomb(b, it->second.second ^ 0x5851f42d4c957f2dULL);
    }
    memo.emplace(entry.first, std::make_pair(a, b));
  }
  const auto it = memo.find(t);
  if (it == memo.end()) {
    return {};
  }
  char buf[33];
  std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(it->second.first),
                static_cast<unsigned long long>(it->second.second));
  return std::string{buf, 32};
}

std::string_view cone_verdict_name(Cone_verdict v) {
  switch (v) {
    case Cone_verdict::Proven: return "PROVEN";
    case Cone_verdict::Refuted: return "DIFF";
    case Cone_verdict::Unsupported: return "unsupported";
    default: return "unknown";
  }
}

namespace {
Cone_verdict prove_one(const cvc5::Term& diff, int64_t backtrack_limit, Cone_stats* st, const Cone_merge_map* merge) {
  if (diff.isNull() || !diff.getSort().isBoolean()) {
    return Cone_verdict::Unsupported;
  }
  ensure_abc_started();

  Abc_Ntk_t* ntk = Abc_NtkAlloc(ABC_NTK_STRASH, ABC_FUNC_AIG, 1);
  ntk->pName     = Extra_UtilStrsav(const_cast<char*>("lec_cone"));

  Blaster b(ntk, merge);
  Bit     po_bit = b.run(diff);
  if (b.bad() || po_bit == nullptr) {
    if (st != nullptr) {
      st->why = b.why();
    }
    Abc_NtkDelete(ntk);
    return Cone_verdict::Unsupported;
  }
  Abc_Obj_t* po = Abc_NtkCreatePo(ntk);
  Abc_ObjAddFanin(po, po_bit);
  Abc_ObjAssignName(po, const_cast<char*>("diff"), nullptr);
  if (st != nullptr) {
    st->pis  = b.pis();
    st->ands = Abc_NtkNodeNum(ntk);
  }
  if (Abc_NtkCheck(ntk) == 0) {
    Abc_NtkDelete(ntk);
    return Cone_verdict::Unsupported;
  }

  // Structural hashing alone often settles a cut (the two sides collapse to the
  // same AIG node): const-0 output = UNSAT, const-1 = always different.
  const int cst = Abc_NtkMiterIsConstant(ntk);
  if (cst == 1) {
    Abc_NtkDelete(ntk);
    return Cone_verdict::Proven;
  }
  if (cst == 0) {
    Abc_NtkDelete(ntk);
    return Cone_verdict::Refuted;
  }

  // This is an OPPORTUNISTIC pre-pass, not a last-resort prover: a cone that is
  // not easy for the bit-level engine must fall to cvc5 FAST, because cvc5 is
  // still going to be asked the same question. That makes ABC's `cec` settings
  // exactly wrong here -- Ivy_FraigProve escalates its SAT budget geometrically
  // (miter 5000*2^iter, node 2*8^iter) across nItersMax iterations and only
  // consults nTotalBacktrackLimit BETWEEN iterations, so with nItersMax=5 a
  // single hard cone (16-bit multiplier reassociation is the standard example)
  // runs effectively forever and no global limit can stop it.
  //
  // One non-escalating iteration instead: rewrite + fraig + one miter SAT call
  // capped at `backtrack_limit` conflicts. Easy cones (a tech-mapped netlist
  // against its RTL -- the case this exists for) collapse in fraiging; hard ones
  // hit the cap in bounded time and go back to cvc5.
  Prove_Params_t params;
  Prove_ParamsSetDefault(&params);
  params.nItersMax = 1;
  if (backtrack_limit > 0) {
    params.nMiteringLimitStart  = static_cast<int>(backtrack_limit);
    params.nTotalBacktrackLimit = backtrack_limit;
  }
  const int rv = Abc_NtkIvyProve(&ntk, &params);  // NOTE: replaces ntk
  Abc_NtkDelete(ntk);
  if (rv == 1) {
    return Cone_verdict::Proven;
  }
  return rv == 0 ? Cone_verdict::Refuted : Cone_verdict::Unknown;
}
}  // namespace

Cone_verdict abc_prove_unsat(const cvc5::Term& diff, int64_t backtrack_limit, Cone_stats* st) {
  return prove_one(diff, backtrack_limit, st, nullptr);
}

// One streamed result: which cone, what verdict, and its size (diagnostics).
namespace {
constexpr size_t kRecord = 13;  // u32 index | u8 verdict | u32 pis | u32 ands

void put_u32(unsigned char* p, uint32_t v) {
  p[0] = static_cast<unsigned char>(v & 0xffU);
  p[1] = static_cast<unsigned char>((v >> 8U) & 0xffU);
  p[2] = static_cast<unsigned char>((v >> 16U) & 0xffU);
  p[3] = static_cast<unsigned char>((v >> 24U) & 0xffU);
}

uint32_t get_u32(const unsigned char* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) | (static_cast<uint32_t>(p[2]) << 16U)
         | (static_cast<uint32_t>(p[3]) << 24U);
}
}  // namespace

std::vector<Cone_verdict> abc_prove_unsat_batch(const std::vector<cvc5::Term>& diffs, int64_t backtrack_limit,
                                                int64_t deadline_ms, std::vector<Cone_stats>* st,
                                                const Cone_merge_map* merge) {
  // Unknown = "not proven" = the cut stays in the cvc5 obligation, so every
  // early return below degrades soundly.
  std::vector<Cone_verdict> out(diffs.size(), Cone_verdict::Unknown);
  if (st != nullptr) {
    st->assign(diffs.size(), Cone_stats{});
  }
  if (diffs.empty()) {
    return out;
  }

  int fds[2];
  if (pipe(fds) != 0) {
    return out;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return out;
  }

  if (pid == 0) {  // ---- child: run ABC where a clock can actually reach it ----
    close(fds[0]);
    // ABC chatters on stdout ("Reached global limit on conflicts..."), which
    // would corrupt lhd's machine-readable stdout. The child has nothing else to
    // say there; diagnostics go through the parent.
    if (int devnull = open("/dev/null", O_WRONLY); devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      close(devnull);
    }
    for (size_t i = 0; i < diffs.size(); ++i) {
      Cone_stats         one;
      const Cone_verdict v = prove_one(diffs[i], backtrack_limit, &one, merge);
      unsigned char      rec[kRecord];
      put_u32(rec, static_cast<uint32_t>(i));
      rec[4] = static_cast<unsigned char>(v);
      put_u32(rec + 5, static_cast<uint32_t>(one.pis));
      put_u32(rec + 9, static_cast<uint32_t>(one.ands));
      if (write(fds[1], rec, kRecord) != static_cast<ssize_t>(kRecord)) {
        break;  // parent stopped listening (deadline): nothing left to report
      }
    }
    close(fds[1]);
    _exit(0);  // never run the parent's atexit handlers / flush its buffers
  }

  // ---- parent: collect whatever lands before the deadline -------------------
  close(fds[1]);
  const auto    t0 = std::chrono::steady_clock::now();
  unsigned char rec[kRecord];
  size_t        have   = 0;
  bool          expire = false;
  for (;;) {
    int wait_ms = -1;
    if (deadline_ms > 0) {
      const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      if (spent >= deadline_ms) {
        expire = true;
        break;
      }
      wait_ms = static_cast<int>(deadline_ms - spent);
    }
    pollfd p{fds[0], POLLIN, 0};
    const int pr = poll(&p, 1, wait_ms);
    if (pr == 0) {
      expire = true;
      break;
    }
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    const ssize_t n = read(fds[0], rec + have, kRecord - have);
    if (n <= 0) {
      break;  // EOF: the child finished every cone
    }
    have += static_cast<size_t>(n);
    if (have < kRecord) {
      continue;
    }
    have                = 0;
    const uint32_t idx  = get_u32(rec);
    const auto     v    = static_cast<Cone_verdict>(rec[4]);
    if (idx < out.size() && rec[4] <= static_cast<unsigned char>(Cone_verdict::Unknown)) {
      out[idx] = v;
    }
    if (st != nullptr && idx < st->size()) {
      (*st)[idx].pis  = static_cast<int>(get_u32(rec + 5));
      (*st)[idx].ands = static_cast<int>(get_u32(rec + 9));
    }
  }
  close(fds[0]);
  if (expire) {
    kill(pid, SIGKILL);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return out;
}

}  // namespace livehd::lec
