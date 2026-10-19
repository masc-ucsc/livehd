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

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "fork_race.hpp"
#include "pass/synth/arith.hpp"

// clang-format off
// ABC headers must stay in dependency order: abc.h defines Abc_Frame_t (used by
// main.h) and the word/namespace macros. Do not sort. ABC's headers must never
// reach a LiveHD header -- they #define short names (word, ...) that poison
// cvc5/hhds in any TU that includes them.
extern "C" {
#include "base/abc/abc.h"
#include "base/main/abcapis.h"
#include "base/main/main.h"
#include "aig/gia/gia.h"
#include "proof/cec/cec.h"
#include "proof/fraig/fraig.h"
#include "misc/extra/extra.h"
extern Gia_Man_t* Cec5_ManSimulateTest3(Gia_Man_t*, int, int);
#include "sat/bmc/bmc.h"
#include "aig/saig/saig.h"
#include "proof/pdr/pdr.h"
#include "proof/fra/fra.h"
#include "sat/cnf/cnf.h"
#include "sat/bsat/satSolver.h"
#include "base/main/mainInt.h"
int        Abc_NtkDarBmc3(Abc_Ntk_t* pNtk, Saig_ParBmc_t* pPars, int fOrDecomp);
Aig_Man_t* Abc_NtkToDar(Abc_Ntk_t* pNtk, int fExors, int fRegisters);
}
// clang-format on

namespace livehd::lec {

namespace {

enum class Arithmetic_engine { correspondence, ivy, sweep, ripple, tree, array };

using Bit = Abc_Obj_t*;  // AIG signal: node pointer with the complement bit in bit 0

// ABC's frame is a thread-local singleton (mainFrame.c s_GlobalFrame) that
// Abc_Start() allocates only when it is NULL, so call it every time rather than
// latching a once-flag: pass.abc / pass.liberty Abc_Stop() the frame between lec
// calls, and a stale latch would let a forked child run ABC on a NULL frame (it
// dies silently, the parent reads EOF, every remaining cone stays Unknown).
// Never Abc_Stop() here: those passes may share the frame within one lhd
// process, and lec's engine forks each get their own copy-on-write image that
// dies with the child.
void ensure_abc_started() {
  Abc_Start();
}

std::string kind_name(cvc5::Kind k) {
  std::ostringstream os;
  os << k;
  return os.str();
}

bool has_multiply(const cvc5::Term& root) {
  std::vector<cvc5::Term>        pending{root};
  std::unordered_set<cvc5::Term> seen;
  while (!pending.empty()) {
    auto term = pending.back();
    pending.pop_back();
    if (!seen.insert(term).second) {
      continue;
    }
    if (term.getKind() == cvc5::Kind::BITVECTOR_MULT) {
      return true;
    }
    for (const auto& child : term) {
      pending.push_back(child);
    }
  }
  return false;
}

// Blasts a term DAG into `ntk`. Every term is visited once (memoized), so the
// shared structure a cut's two sides have in common collapses in the AIG the
// same way it does in the cvc5 term graph.
class Blaster {
public:
  Blaster(Abc_Ntk_t* ntk, const Cone_merge_map* merge, Arithmetic_engine engine = Arithmetic_engine::correspondence)
      : ntk_(ntk), man_(static_cast<Abc_Aig_t*>(ntk->pManFunc)), one_(Abc_AigConst1(ntk)), engine_(engine), merge_(merge) {}

  // Returns the AIG bit for `root` (Boolean), or nullptr if the term left the
  // blastable fragment (then why() names the offending kind).
  Bit run(const cvc5::Term& root) {
    native_arithmetic_ = has_multiply(root);
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

  // Sequential use: blast a bit-vector term, and bind a free symbol to given
  // bits (a register output) before anything reads it.
  std::vector<Bit> run_bv(const cvc5::Term& t) {
    walk(t);
    auto it = bv_.find(t);
    return (bad_ || it == bv_.end()) ? std::vector<Bit>{} : it->second;
  }
  void preset(const cvc5::Term& sym, std::vector<Bit> bits) { sym_pi_[canon(sym)] = std::move(bits); }
  // A memory as registers: one bit vector per entry (all 2^addr entries).
  // SELECT / STORE / ITE over it then blast exactly instead of abstracting.
  using Entries = std::vector<std::vector<Bit>>;
  void preset_array(const cvc5::Term& sym, Entries e) { arr_[canon(sym)] = std::move(e); }
  Entries run_arr(const cvc5::Term& t) {
    walk(t);
    auto it = arr_.find(t);
    return (bad_ || it == arr_.end()) ? Entries{} : it->second;
  }
  const std::vector<Bit>* sym_bits(const cvc5::Term& sym) const {
    auto it = sym_pi_.find(canon(sym));
    return it == sym_pi_.end() ? nullptr : &it->second;
  }

  [[nodiscard]] bool               bad() const { return bad_; }
  [[nodiscard]] const std::string& why() const { return why_; }
  [[nodiscard]] int                pis() const { return npi_; }
  // True when some subterm was replaced by a free input (opaque_leaf). The AIG
  // is then an OVER-approximation: UNSAT still transfers, but SAT does NOT --
  // the counterexample may assign the abstracted leaves inconsistently. Callers
  // must degrade such a SAT to Unknown, never report it as a difference.
  [[nodiscard]] bool               abstracted() const { return abstracted_; }
  [[nodiscard]] bool               arithmetic() const { return native_arithmetic_; }

  std::vector<std::pair<Bit, Bit>> comparison_bits(const cvc5::Term& root) const {
    std::vector<std::pair<Bit, Bit>> result;
    if (root.getKind() != cvc5::Kind::DISTINCT || root.getNumChildren() != 2 || !root[0].getSort().isBitVector()) {
      return result;
    }
    auto a = bv_.find(root[0]);
    auto c = bv_.find(root[1]);
    if (a == bv_.end() || c == bv_.end() || a->second.size() != c->second.size()) {
      return result;
    }
    for (size_t i = 0; i < a->second.size(); ++i) {
      result.emplace_back(a->second[i], c->second[i]);
    }
    return result;
  }

private:
  // ---- AIG primitives ------------------------------------------------------
  Bit one() const { return one_; }
  Bit zero() const { return Abc_ObjNot(one_); }
  Bit inv(Bit a) const { return Abc_ObjNot(a); }
  Bit and2(Bit a, Bit b) { return Abc_AigAnd(man_, a, b); }
  Bit or2(Bit a, Bit b) { return Abc_AigOr(man_, a, b); }
  Bit xor2(Bit a, Bit b) { return Abc_AigXor(man_, a, b); }
  Bit mux(Bit c, Bit t, Bit e) { return Abc_AigMux(man_, c, t, e); }

  // Named after the symbol (or, for an abstracted subterm, its structural
  // digest) so the same input keeps its name in the next design version.
  Bit new_pi(const cvc5::Term& rep, uint32_t bit) {
    Abc_Obj_t*  pi   = Abc_NtkCreatePi(ntk_);
    std::string base = rep.hasSymbol() ? rep.getSymbol() : std::string{};
    if (base.empty()) {
      const std::string d = cone_digest(rep);
      base                = d.empty() ? "v" + std::to_string(rep.getId()) : "o" + d;
    }
    std::string name = base + "_b" + std::to_string(bit);
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
  struct Ops {
    Blaster& b;
    Bit      zero() { return b.zero(); }
    Bit      one() { return b.one(); }
    Bit      inv(Bit a) { return b.inv(a); }
    Bit      and_(Bit a, Bit c) { return b.and2(a, c); }
    Bit      or_(Bit a, Bit c) { return b.or2(a, c); }
    Bit      xor_(Bit a, Bit c) { return b.xor2(a, c); }
  };

  bool ripple_arithmetic() const { return engine_ == Arithmetic_engine::tree || engine_ == Arithmetic_engine::array; }

  std::vector<Bit> add(const std::vector<Bit>& a, const std::vector<Bit>& b, Bit cin) {
    if (native_arithmetic_ && a.size() >= 16 && !ripple_arithmetic()) {
      Ops ops{*this};
      return synth::arith::prefix_add(ops, a, b, cin).sum;
    }
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
    Ops ops{*this};
    return synth::arith::build_mul(engine_ == Arithmetic_engine::tree    ? synth::arith::Mult_kind::tree
                                   : engine_ == Arithmetic_engine::array ? synth::arith::Mult_kind::array
                                                                         : synth::arith::Mult_kind::csa,
                                   (engine_ == Arithmetic_engine::ripple || ripple_arithmetic()) ? synth::arith::Adder_kind::rca
                                                                                                 : synth::arith::Adder_kind::prefix,
                                   0,
                                   ops,
                                   a,
                                   b,
                                   static_cast<int>(a.size()));
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
  bool done(const cvc5::Term& t) const { return bv_.count(t) != 0 || bl_.count(t) != 0 || arr_.count(t) != 0; }

  // An array term built from preset memories by STORE / ITE (memoized).
  bool arr_ok(const cvc5::Term& a) {
    if (arr_.empty() || !a.getSort().isArray()) {
      return false;
    }
    if (auto it = arr_okm_.find(a); it != arr_okm_.end()) {
      return it->second;
    }
    bool ok = false;
    switch (a.getKind()) {
      case cvc5::Kind::CONSTANT:
      case cvc5::Kind::VARIABLE: ok = arr_.contains(canon(a)); break;
      case cvc5::Kind::STORE: ok = arr_ok(a[0]); break;
      case cvc5::Kind::ITE: ok = arr_ok(a[1]) && arr_ok(a[2]); break;
      default: break;
    }
    arr_okm_[a] = ok;
    return ok;
  }

  // A subterm outside the blastable fragment (an array SELECT, a UF
  // application, a divide) but whose OWN sort is BitVector/Boolean is
  // abstracted to a free primary input KEYED BY THE TERM: identical terms share
  // an input, distinct ones get independent ones. Replacing a subterm by a free
  // variable is an OVER-approximation, so UNSAT on the abstraction is UNSAT on
  // the original -- the only direction this pass may conclude (a SAT verdict is
  // already just a lead: the cut goes to cvc5 either way, and the caller marks
  // the obligations whose SAT is not a sound refutation). Without this one
  // `select(a_cur, i)` anywhere in a cone made the whole cut "unsupported",
  // which is what kept a whole-array register file written from a function of
  // ITSELF (`ex_mask_rf_q <= f(ex_mask_rf_q)`) out of reach: every bulk-update
  // obligation reads the array it is writing.
  bool opaque_leaf(const cvc5::Term& t) {
    const cvc5::Sort srt = t.getSort();
    if (!srt.isBitVector() && !srt.isBoolean()) {
      return false;  // an Array-sorted term is not a value: let supported() refuse it
    }
    switch (t.getKind()) {
      case cvc5::Kind::SELECT: return !arr_ok(t[0]);
      case cvc5::Kind::APPLY_UF:
      case cvc5::Kind::BITVECTOR_UDIV:
      case cvc5::Kind::BITVECTOR_UREM:
      case cvc5::Kind::BITVECTOR_SDIV:
      case cvc5::Kind::BITVECTOR_SREM:
      case cvc5::Kind::BITVECTOR_SMOD: return true;
      default: return false;
    }
  }

  void emit_opaque(const cvc5::Term& t) {
    abstracted_        = true;
    const cvc5::Term rep = canon(t);
    if (t.getSort().isBitVector()) {
      auto it = sym_pi_.find(rep);
      if (it == sym_pi_.end()) {
        const uint32_t   w = t.getSort().getBitVectorSize();
        std::vector<Bit> r(w);
        for (uint32_t i = 0; i < w; ++i) {
          r[i] = new_pi(rep, i);
        }
        it = sym_pi_.emplace(rep, std::move(r)).first;
      }
      bv_.emplace(t, it->second);
    } else {
      auto it = sym_bit_.find(rep);
      if (it == sym_bit_.end()) {
        it = sym_bit_.emplace(rep, new_pi(rep, 0)).first;
      }
      bl_.emplace(t, it->second);
    }
  }

  bool supported(const cvc5::Term& t) {
    const cvc5::Sort srt = t.getSort();
    if (srt.isArray() && arr_ok(t)) {
      return true;
    }
    if (t.getKind() == cvc5::Kind::SELECT && arr_ok(t[0])) {
      return true;
    }
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
        if (opaque_leaf(entry.first)) {  // abstract: a leaf, so do NOT descend into it
          st.pop_back();
          emit_opaque(entry.first);
          continue;
        }
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

  // index == k, for an index given as bits (LSB first)
  Bit index_is(const std::vector<Bit>& ix, size_t k) {
    Bit e = one();
    for (size_t i = 0; i < ix.size(); ++i) {
      e = and2(e, ((k >> i) & 1) != 0 ? ix[i] : inv(ix[i]));
    }
    return (ix.size() < 64 && (k >> ix.size()) != 0) ? zero() : e;
  }

  void emit_arr(const cvc5::Term& t) {
    Entries r;
    switch (t.getKind()) {
      case cvc5::Kind::CONSTANT:
      case cvc5::Kind::VARIABLE: r = arr_.at(canon(t)); break;
      case cvc5::Kind::STORE: {
        r              = arr_.at(t[0]);
        const auto& ix = bits_of(t[1]);
        const auto& v  = bits_of(t[2]);
        for (size_t k = 0; k < r.size() && !bad_; ++k) {
          const Bit hit = index_is(ix, k);
          for (size_t i = 0; i < r[k].size() && i < v.size(); ++i) {
            r[k][i] = mux(hit, v[i], r[k][i]);
          }
        }
        break;
      }
      case cvc5::Kind::ITE: {
        const Bit c = bool_of(t[0]);
        r           = arr_.at(t[1]);
        const auto& e = arr_.at(t[2]);
        for (size_t k = 0; k < r.size(); ++k) {
          for (size_t i = 0; i < r[k].size(); ++i) {
            r[k][i] = mux(c, r[k][i], e[k][i]);
          }
        }
        break;
      }
      default: fail(kind_name(t.getKind())); return;
    }
    if (!bad_) {
      arr_.emplace(t, std::move(r));
    }
  }

  void emit(const cvc5::Term& t) {
    if (t.getSort().isArray()) {
      emit_arr(t);
      return;
    }
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
          r[i] = new_pi(rep, i);
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
        if (!native_arithmetic_) {
          auto acc = bits_of(t[0]);
          for (size_t c = 1; c < t.getNumChildren(); ++c) {
            acc = add(acc, bits_of(t[c]), zero());
          }
          return acc;
        }
        std::vector<synth::arith::Sum_operand<Bit>> operands;
        for (size_t c = 0; c < t.getNumChildren(); ++c) {
          operands.push_back({bits_of(t[c]), false, false});
        }
        Ops ops{*this};
        return synth::arith::build_sum(
            w >= 16 && !ripple_arithmetic() ? synth::arith::Adder_kind::prefix : synth::arith::Adder_kind::rca,
            0,
            ops,
            operands,
            static_cast<int>(w));
      }
      case cvc5::Kind::BITVECTOR_SUB: {
        std::vector<Bit> acc = bits_of(t[0]);
        for (size_t c = 1; c < t.getNumChildren(); ++c) {
          acc = native_arithmetic_ ? add(acc, bv_not(bits_of(t[c])), one()) : add(acc, neg(bits_of(t[c])), zero());
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
      case cvc5::Kind::SELECT: {  // a preset memory: a mux over its entries
        const auto& e  = arr_.at(t[0]);
        const auto& ix = bits_of(t[1]);
        std::vector<Bit> r(w, zero());
        for (size_t k = 0; k < e.size() && !bad_; ++k) {
          const Bit hit = index_is(ix, k);
          for (uint32_t i = 0; i < w && i < e[k].size(); ++i) {
            r[i] = or2(r[i], and2(hit, e[k][i]));
          }
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
          it = sym_bit_.emplace(rep, new_pi(rep, 0)).first;
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
  bool        bad_        = false;
  bool        abstracted_ = false;
  bool              native_arithmetic_ = false;
  Arithmetic_engine engine_            = Arithmetic_engine::correspondence;
  int         npi_ = 0;
  std::string why_;

  const Cone_merge_map*                           merge_ = nullptr;
  std::unordered_map<cvc5::Term, std::vector<Bit>> bv_;
  std::unordered_map<cvc5::Term, Bit>              bl_;
  std::unordered_map<cvc5::Term, std::vector<Bit>> sym_pi_;   // representative -> its PIs
  std::unordered_map<cvc5::Term, Bit>              sym_bit_;  // representative -> its PI (bool)
  std::unordered_map<cvc5::Term, Entries>          arr_;      // memory terms, as entries
  std::unordered_map<cvc5::Term, bool>             arr_okm_;
};

}  // namespace

namespace {
Cone_verdict prove_one(const cvc5::Term& diff, int64_t backtrack_limit, Cone_stats* st, const Cone_merge_map* merge,
                       Arithmetic_engine engine = Arithmetic_engine::correspondence) {
  if (diff.isNull() || !diff.getSort().isBoolean()) {
    return Cone_verdict::Unsupported;
  }
  ensure_abc_started();

  Abc_Ntk_t* ntk = Abc_NtkAlloc(ABC_NTK_STRASH, ABC_FUNC_AIG, 1);
  ntk->pName     = Extra_UtilStrsav(const_cast<char*>("lec_cone"));

  Blaster b(ntk, merge, engine);
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
  // A SAT/"always different" answer is only reportable when the blast was
  // EXACT: with an abstracted subterm the AIG over-approximates, so SAT proves
  // nothing (see Blaster::abstracted). Proven transfers either way.
  const bool exact = !b.abstracted();
  const int  cst   = Abc_NtkMiterIsConstant(ntk);
  if (cst == 1) {
    Abc_NtkDelete(ntk);
    return Cone_verdict::Proven;
  }
  if (cst == 0) {
    Abc_NtkDelete(ntk);
    return exact ? Cone_verdict::Refuted : Cone_verdict::Unknown;
  }

  const bool wide_mux_cone = b.pis() >= 512 && Abc_NtkNodeNum(ntk) >= 1000;
  if ((b.arithmetic() || wide_mux_cone) && engine != Arithmetic_engine::ivy) {
    // The current CEC engine finds internal correspondences before solving the
    // output miter. The legacy Ivy prover below can miss those correspondences
    // between a mapped multiplier and its exact arithmetic expansion.
    Gia_Man_t*       gia = Gia_ManStart(Abc_NtkObjNumMax(ntk));
    std::vector<int> literals(static_cast<size_t>(Abc_NtkObjNumMax(ntk)));
    literals[Abc_ObjId(Abc_AigConst1(ntk))] = 1;
    Abc_Obj_t* obj;
    int        index;
    Abc_NtkForEachPi(ntk, obj, index) { literals[Abc_ObjId(obj)] = Gia_ManAppendCi(gia); }
    Abc_NtkForEachNode(ntk, obj, index) {
      const int a              = literals[Abc_ObjFaninId0(obj)] ^ Abc_ObjFaninC0(obj);
      const int c              = literals[Abc_ObjFaninId1(obj)] ^ Abc_ObjFaninC1(obj);
      literals[Abc_ObjId(obj)] = Gia_ManAppendAnd(gia, a, c);
    }
    if (engine == Arithmetic_engine::sweep) {
      Gia_Man_t* single = Gia_ManDup(gia);
      Gia_ManAppendCo(single, literals[Abc_ObjId(Abc_ObjRegular(po_bit))] ^ Abc_ObjIsComplement(po_bit));
      Gia_Man_t* swept  = Cec5_ManSimulateTest3(single, backtrack_limit > 0 ? static_cast<int>(backtrack_limit) : 10000, 0);
      const bool proven = swept != nullptr && Gia_ManPoNum(swept) == 1 && Gia_ManPoIsConst0(swept, 0);
      if (swept != nullptr) {
        Gia_ManStop(swept);
      }
      Gia_ManStop(single);
      if (proven) {
        Gia_ManStop(gia);
        Abc_NtkDelete(ntk);
        return Cone_verdict::Proven;
      }
    }
    const auto comparisons = b.comparison_bits(diff);
    if (!comparisons.empty()) {
      for (const auto& [a, c] : comparisons) {
        Gia_ManAppendCo(gia, literals[Abc_ObjId(Abc_ObjRegular(a))] ^ Abc_ObjIsComplement(a));
        Gia_ManAppendCo(gia, literals[Abc_ObjId(Abc_ObjRegular(c))] ^ Abc_ObjIsComplement(c));
      }
    } else {
      Abc_NtkForEachPo(ntk, obj, index) {
        Gia_ManAppendCo(gia, literals[Abc_ObjFaninId0(obj)] ^ Abc_ObjFaninC0(obj));
        Gia_ManAppendCo(gia, 0);  // CEC consumes pairs: diff must equal constant zero.
      }
    }
    Cec_ParCec_t cec;
    Cec_ManCecSetDefaultParams(&cec);
    cec.fSilent             = 1;
    // Start with cheap correspondence checks. CEC raises this limit as needed;
    // spending the entire cone limit on each initial candidate can stall large
    // multipliers before their useful internal correspondences are found.
    const int initial_limit = 100;
    cec.nBTLimit = backtrack_limit > 0 ? static_cast<int>(std::min<int64_t>(initial_limit, backtrack_limit)) : initial_limit;
    const int cec_result = Cec_ManVerify(gia, &cec);
    Gia_ManStop(gia);
    if (cec_result >= 0) {
      Abc_NtkDelete(ntk);
      return cec_result == 1 ? Cone_verdict::Proven : exact ? Cone_verdict::Refuted : Cone_verdict::Unknown;
    }
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
  return (rv == 0 && exact) ? Cone_verdict::Refuted : Cone_verdict::Unknown;
}
struct Arithmetic_result {
  Cone_verdict verdict = Cone_verdict::Unknown;
  Cone_stats   stats;
};

Cone_verdict prove_arithmetic_portfolio(const cvc5::Term& diff, int64_t backtrack_limit, int64_t deadline_ms, Cone_stats* stats,
                                        const Cone_merge_map* merge) {
  // Each engine sees the original exact obligation. They share one wall
  // deadline; a completed proof cancels the others. No candidate match or
  // simulation result is itself sufficient to prove equivalence.
  const std::array engines{Arithmetic_engine::correspondence,
                           Arithmetic_engine::ivy,
                           Arithmetic_engine::sweep,
                           Arithmetic_engine::ripple,
                           Arithmetic_engine::tree,
                           Arithmetic_engine::array};
  const auto       race = fork_race<Arithmetic_result>(
      static_cast<int>(engines.size()),
      [&](int i) {
        Arithmetic_result result;
        result.verdict = prove_one(diff, backtrack_limit, &result.stats, merge, engines[i]);
        return result;
      },
      [](const Arithmetic_result& result) {
        return std::to_string(static_cast<int>(result.verdict)) + " " + std::to_string(result.stats.pis) + " "
               + std::to_string(result.stats.ands);
      },
      [](std::string_view blob, Arithmetic_result& result) {
        std::istringstream input{std::string{blob}};
        int                verdict;
        if (!(input >> verdict >> result.stats.pis >> result.stats.ands) || verdict < 0
            || verdict > static_cast<int>(Cone_verdict::Unknown)) {
          return false;
        }
        result.verdict = static_cast<Cone_verdict>(verdict);
        return true;
      },
      [](int, const Arithmetic_result& result) {
        return result.verdict == Cone_verdict::Proven || result.verdict == Cone_verdict::Refuted;
      },
      deadline_ms);
  if (!race.forked) {
    return prove_one(diff, backtrack_limit, stats, merge);
  }
  if (race.winner >= 0) {
    const auto& result = race.results[race.winner];
    if (stats != nullptr) {
      *stats = result.stats;
    }
    return result.verdict;
  }
  return Cone_verdict::Unknown;
}

}  // namespace

Cone_verdict abc_prove_unsat(const cvc5::Term& diff, int64_t backtrack_limit, Cone_stats* st) {
  return prove_one(diff, backtrack_limit, st, nullptr);
}

// One streamed result: which cone, what verdict, and its size (diagnostics).
namespace {
constexpr size_t kRecord = 13;  // u32 index | u8 verdict | u32 pis | u32 ands

// A cone ABC has not finished in this long is not easy for it (see prove_one):
// it goes back to cvc5 and the batch moves on. A larger formal.timeout buys
// every cone a proportionally longer stall (kConeStallShare of the deadline).
constexpr int64_t kConeStallMs    = 250;
constexpr int64_t kConeStallShare = 20;

// The child's first record: ABC is up, so the per-cone stall clock starts now
// rather than at fork (a loaded machine must not stall a cone before it begins).
// Until it lands only the overall deadline bounds the wait, and the parent
// starts ABC once before forking so every child inherits the frame (kReady is
// then near-immediate instead of paying Abc_Start per fork).
constexpr uint32_t kReady = 0xffffffffU;

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

// One batch pass. stall_override_ms > 0 replaces the per-cone stall budget;
// stalled (when non-null) collects the cones the stall timer killed.
std::vector<Cone_verdict> batch_pass(const std::vector<cvc5::Term>& diffs, int64_t backtrack_limit, int64_t deadline_ms,
                                     std::vector<Cone_stats>* st, const Cone_merge_map* merge, int64_t stall_override_ms,
                                     std::vector<size_t>* stalled);
}  // namespace

std::vector<Cone_verdict> abc_prove_unsat_batch(const std::vector<cvc5::Term>& diffs, int64_t backtrack_limit,
                                                int64_t deadline_ms, std::vector<Cone_stats>* st,
                                                const Cone_merge_map* merge) {
  const auto          t0 = std::chrono::steady_clock::now();
  std::vector<size_t> stalled;
  auto                out = batch_pass(diffs, backtrack_limit, deadline_ms, st, merge, 0, &stalled);
  if (deadline_ms <= 0 || stalled.empty()) {
    return out;
  }
  // Only where ABC is clearly effective on this obligation and the stalls are
  // outliers; on a cone set ABC cannot crack, the retry would just burn the
  // rest of the carve-out before cvc5 gets its turn.
  const auto proven = static_cast<size_t>(std::count(out.begin(), out.end(), Cone_verdict::Proven));
  if (proven < 4 * stalled.size()) {
    return out;
  }
  // SECOND CHANCE. The stall budget (deadline / kConeStallShare) keeps one hard
  // cone from starving the queue, but a pass that then finishes early leaves
  // the rest of its deadline unused while the stalled cone goes to cvc5, which
  // is usually far worse at exactly these cones. Measured on XiangShan's
  // RenameTable_1 (124 cones): at a 30 s def budget one cone stalled at its
  // 375 ms share, the pass ended after 1.6 s of 7.5 s, and cvc5 then spent
  // 40 s without a verdict; at 150 s the same cone took ABC under 2 s. Retry
  // the stalled cones with the leftover deadline split evenly among them.
  const auto left
      = deadline_ms - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  const int64_t share = left / static_cast<int64_t>(stalled.size());
  if (share <= std::max<int64_t>(kConeStallMs, deadline_ms / kConeStallShare)) {
    return out;  // no more per cone than the first attempt had
  }
  std::vector<cvc5::Term> retry;
  retry.reserve(stalled.size());
  for (const auto idx : stalled) {
    retry.push_back(diffs[idx]);
  }
  std::vector<Cone_stats> retry_st;
  const auto              again = batch_pass(retry, backtrack_limit, left, st != nullptr ? &retry_st : nullptr, merge, share, nullptr);
  for (size_t k = 0; k < stalled.size(); ++k) {
    if (again[k] != Cone_verdict::Unknown) {
      out[stalled[k]] = again[k];
      if (st != nullptr && k < retry_st.size()) {
        (*st)[stalled[k]] = retry_st[k];
      }
    }
  }
  return out;
}

namespace {
std::vector<Cone_verdict> batch_pass(const std::vector<cvc5::Term>& diffs, int64_t backtrack_limit, int64_t deadline_ms,
                                     std::vector<Cone_stats>* st, const Cone_merge_map* merge, int64_t stall_override_ms,
                                     std::vector<size_t>* stalled) {
  // Unknown = "not proven" = the cut stays in the cvc5 obligation, so every
  // early return below degrades soundly.
  std::vector<Cone_verdict> out(diffs.size(), Cone_verdict::Unknown);
  if (st != nullptr) {
    st->assign(diffs.size(), Cone_stats{});
  }
  if (diffs.empty()) {
    return out;
  }

  // A cone that is not easy for ABC must fall to cvc5 FAST (prove_one), but
  // rewriting/fraiging has no clock of its own: one hard cone -- a 129-bit
  // popcount spelled two ways, which cvc5 then proves in milliseconds -- held
  // the child to the whole deadline and starved every cone queued behind it.
  // So each cone also gets a stall budget (kConeStallMs, or a share of a longer
  // deadline): on a stall the child is killed, that cone stays Unknown, and a
  // fresh child resumes with the next.
  const int64_t cone_ms
      = deadline_ms <= 0        ? 0
        : stall_override_ms > 0 ? std::min<int64_t>(deadline_ms, stall_override_ms)
                                : std::min<int64_t>(deadline_ms, std::max<int64_t>(kConeStallMs, deadline_ms / kConeStallShare));
  // A word-level arithmetic proof needs longer than a cheap SAT sweep. Reserve
  // time for every other cone, while letting one arithmetic cone use a useful
  // fraction of the existing batch deadline.
  std::vector<bool> arithmetic(diffs.size(), false);
  for (size_t i = 0; i < diffs.size(); ++i) {
    arithmetic[i] = has_multiply(diffs[i]);
  }
  const int64_t arithmetic_ms = deadline_ms > 0 ? std::max(cone_ms, deadline_ms / static_cast<int64_t>(2 * diffs.size())) : 0;
  const auto t0      = std::chrono::steady_clock::now();
  auto       elapsed = [](std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count();
  };

  ensure_abc_started();  // in the parent: every child inherits the frame copy-on-write

  size_t next = 0;  // the first cone no child has reported yet
  while (next < diffs.size()) {
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
      ensure_abc_started();
      unsigned char ready[kRecord] = {};
      put_u32(ready, kReady);
      if (write(fds[1], ready, kRecord) != static_cast<ssize_t>(kRecord)) {
        _exit(0);
      }
      for (size_t i = next; i < diffs.size(); ++i) {
        Cone_stats         one;
        const Cone_verdict v = arithmetic[i] && arithmetic_ms >= 1000
                                   ? prove_arithmetic_portfolio(diffs[i], backtrack_limit, arithmetic_ms - 100, &one, merge)
                                   : prove_one(diffs[i], backtrack_limit, &one, merge);
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

    // ---- parent: collect whatever lands before the deadline / a stall -------
    close(fds[1]);
    auto          cone_t0 = std::chrono::steady_clock::now();
    unsigned char rec[kRecord];
    size_t        have   = 0;
    bool          expire = false;
    bool          stall  = false;
    bool          ready  = false;  // the stall clock runs only once the child reports kReady
    bool          eof    = false;  // the child closed its pipe: it finished, or it died
    for (;;) {
      int wait_ms = -1;
      if (deadline_ms > 0) {
        const auto left = deadline_ms - elapsed(t0);
        const auto cone = (next < arithmetic.size() && arithmetic[next] ? arithmetic_ms : cone_ms) - elapsed(cone_t0);
        if (left <= 0) {
          expire = true;
          break;
        }
        if (ready && cone <= 0) {
          stall = true;
          break;
        }
        wait_ms = static_cast<int>(ready ? std::min(left, cone) : left);
      }
      pollfd    p{fds[0], POLLIN, 0};
      const int pr = poll(&p, 1, wait_ms);
      if (pr == 0) {
        continue;  // the checks above say which clock ran out
      }
      if (pr < 0) {
        if (errno == EINTR) {
          continue;
        }
        break;
      }
      const ssize_t n = read(fds[0], rec + have, kRecord - have);
      if (n <= 0) {
        next = diffs.size();  // EOF: the child finished every cone (checked after waitpid)
        eof  = true;
        break;
      }
      have += static_cast<size_t>(n);
      if (have < kRecord) {
        continue;
      }
      have               = 0;
      const uint32_t idx = get_u32(rec);
      if (idx == kReady) {
        ready   = true;
        cone_t0 = std::chrono::steady_clock::now();
        continue;
      }
      const auto v = static_cast<Cone_verdict>(rec[4]);
      if (idx < out.size() && rec[4] <= static_cast<unsigned char>(Cone_verdict::Unknown)) {
        out[idx] = v;
      }
      if (st != nullptr && idx < st->size()) {
        (*st)[idx].pis  = static_cast<int>(get_u32(rec + 5));
        (*st)[idx].ands = static_cast<int>(get_u32(rec + 9));
      }
      next    = static_cast<size_t>(idx) + 1;
      cone_t0 = std::chrono::steady_clock::now();
    }
    close(fds[0]);
    if (expire || stall) {
      kill(pid, SIGKILL);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    // The only kill is ours (deadline / stall). Any other signal or a non-zero
    // exit is an ABC crash: it must not read as "every cone stays Unknown".
    if (eof && (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0))) {
      std::fprintf(stderr,
                   "livehd:error: lec ABC cone-proof child died (%s %d): this is a crash, not an undecided proof\n",
                   WIFSIGNALED(status) ? "signal" : "exit",
                   WIFSIGNALED(status) ? WTERMSIG(status) : WEXITSTATUS(status));
      std::abort();
    }
    if (expire || !stall) {
      break;
    }
    if (stalled != nullptr) {
      stalled->push_back(next);
    }
    ++next;  // the stalled cone stays Unknown for cvc5
  }
  return out;
}
}  // namespace

namespace {
// Constant bits, LSB first; empty if `t` is not a bit-vector constant.
std::vector<Bit> const_bits(Abc_Ntk_t* ntk, const cvc5::Term& t) {
  if (t.isNull() || t.getKind() != cvc5::Kind::CONST_BITVECTOR) {
    return {};
  }
  const std::string v   = t.getBitVectorValue(2);
  Bit               one = Abc_AigConst1(ntk);
  std::vector<Bit>  r(v.size());
  for (size_t i = 0; i < v.size(); ++i) {
    r[i] = v[v.size() - 1 - i] == '1' ? one : Abc_ObjNot(one);
  }
  return r;
}

// ---- Cross-version reuse ---------------------------------------------------
// A property's sequential AIG reduced to its cone of influence, with inputs
// and registers named after design signals so a later version can be matched
// by name. Literal 2*v+c; var 0 is constant 0; then inputs, registers, ANDs.
struct Lit_model {
  std::vector<std::string>        pis, regs;  // sorted names
  std::vector<int>                next;       // per register
  std::vector<std::array<int, 2>> ands;
  int                             target = 0;  // bad: assumes held so far, property fails
  int                             ok     = 1;  // assumes held so far
  bool                            exact  = false;
  int  nvars() const { return 1 + static_cast<int>(pis.size() + regs.size() + ands.size()); }
  int  reg_var(size_t i) const { return 1 + static_cast<int>(pis.size() + i); }
};

std::string esc_name(std::string_view s) {
  std::string o;
  for (unsigned char c : s) {
    if (c <= ' ' || c == '%' || c > '~') {
      o += std::format("%{:02x}", c);
    } else {
      o += static_cast<char>(c);
    }
  }
  return o;
}

std::string model_text(const Lit_model& m) {
  std::string s = std::format("lhdm1 {} {} {} {} {} {}\n", m.pis.size(), m.regs.size(), m.ands.size(), m.target, m.ok,
                              m.exact ? 1 : 0);
  for (const auto& p : m.pis) {
    s += esc_name(p) + "\n";
  }
  for (size_t i = 0; i < m.regs.size(); ++i) {
    s += esc_name(m.regs[i]) + " " + std::to_string(m.next[i]) + "\n";
  }
  for (const auto& a : m.ands) {
    s += std::to_string(a[0]) + " " + std::to_string(a[1]) + "\n";
  }
  return s;
}

bool model_parse(const std::string& s, Lit_model& m) {
  std::istringstream in{s};
  std::string        tag;
  size_t             np = 0, nr = 0, na = 0;
  int                ex = 0;
  if (!(in >> tag >> np >> nr >> na >> m.target >> m.ok >> ex) || tag != "lhdm1") {
    return false;
  }
  m.exact = ex != 0;
  m.pis.resize(np);
  m.regs.resize(nr);
  m.next.resize(nr);
  m.ands.resize(na);
  for (auto& p : m.pis) {
    in >> p;
  }
  for (size_t i = 0; i < nr; ++i) {
    in >> m.regs[i] >> m.next[i];
  }
  for (auto& a : m.ands) {
    in >> a[0] >> a[1];
  }
  return static_cast<bool>(in);
}

std::string read_file(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) {
    return {};
  }
  std::string s;
  char        buf[65536];
  size_t      n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
    s.append(buf, n);
  }
  std::fclose(f);
  return s;
}

void write_file(const std::string& path, const std::string& s) {  // atomic: tmp + rename
  const std::string tmp = path + ".tmp" + std::to_string(getpid());
  std::FILE*        f   = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    return;
  }
  const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
  if (std::fclose(f) == 0 && ok) {
    std::rename(tmp.c_str(), path.c_str());
  } else {
    std::remove(tmp.c_str());
  }
}

// The cone of `target` (and `ok`) in `ntk`. `reg_name` names each latch
// output; a name used twice makes the model unusable (empty).
std::optional<Lit_model> extract_model(Abc_Ntk_t* ntk, Bit target, Bit ok,
                                       const std::unordered_map<Abc_Obj_t*, std::string>& reg_name, bool exact) {
  const Abc_Obj_t*                  c1 = Abc_AigConst1(ntk);
  std::vector<Abc_Obj_t*>           pis, bos, ands;
  std::unordered_set<Abc_Obj_t*>    seen;
  std::vector<std::pair<Abc_Obj_t*, bool>> st;  // (object, fanins done)
  auto push = [&](Bit b) {
    Abc_Obj_t* o = Abc_ObjRegular(b);
    if (o != c1 && !seen.contains(o)) {
      st.emplace_back(o, false);
    }
  };
  push(target);
  push(ok);
  while (!st.empty()) {
    auto [o, done] = st.back();
    st.pop_back();
    if (seen.contains(o)) {
      continue;
    }
    if (Abc_ObjIsPi(o)) {
      seen.insert(o);
      pis.push_back(o);
    } else if (Abc_ObjIsBo(o)) {
      seen.insert(o);
      bos.push_back(o);
      push(Abc_ObjChild0(Abc_ObjFanin0(Abc_ObjFanin0(o))));  // bo <- latch <- bi <- next
    } else if (Abc_ObjIsNode(o)) {
      if (done) {
        seen.insert(o);
        ands.push_back(o);
      } else {
        st.emplace_back(o, true);
        push(Abc_ObjChild0(o));
        push(Abc_ObjChild1(o));
      }
    } else {
      return std::nullopt;
    }
  }
  Lit_model                               m;
  std::unordered_map<const Abc_Obj_t*, int> var;
  std::vector<std::pair<std::string, Abc_Obj_t*>> pn, rn;
  for (auto* p : pis) {
    pn.emplace_back(Abc_ObjName(p), p);
  }
  for (auto* b : bos) {
    auto it = reg_name.find(b);
    if (it == reg_name.end()) {
      return std::nullopt;
    }
    rn.emplace_back(it->second, b);
  }
  std::sort(pn.begin(), pn.end());
  std::sort(rn.begin(), rn.end());
  for (size_t i = 0; i + 1 < pn.size(); ++i) {
    if (pn[i].first == pn[i + 1].first) {
      return std::nullopt;
    }
  }
  for (size_t i = 0; i + 1 < rn.size(); ++i) {
    if (rn[i].first == rn[i + 1].first) {
      return std::nullopt;
    }
  }
  int v = 1;
  for (auto& [n, o] : pn) {
    m.pis.push_back(n);
    var[o] = v++;
  }
  for (auto& [n, o] : rn) {
    m.regs.push_back(n);
    var[o] = v++;
  }
  auto lit = [&](Bit b) {
    const Abc_Obj_t* o = Abc_ObjRegular(b);
    const int        c = Abc_ObjIsComplement(b) ? 1 : 0;
    return o == c1 ? 1 ^ c : 2 * var.at(o) + c;
  };
  for (auto* a : ands) {  // post-order: fanins first
    m.ands.push_back({lit(Abc_ObjChild0(a)), lit(Abc_ObjChild1(a))});
    var[a] = v++;
  }
  for (auto& [n, o] : rn) {
    m.next.push_back(lit(Abc_ObjChild0(Abc_ObjFanin0(Abc_ObjFanin0(o)))));
  }
  m.target = lit(target);
  m.ok     = lit(ok);
  m.exact  = exact;
  return m;
}

// Builds an ABC network from models that share inputs by name. `out` gets the
// literal-to-AIG map of each model and returns the single output.
Abc_Ntk_t* build_ntk(const std::vector<const Lit_model*>& ms,
                     const std::function<Bit(Abc_Aig_t*, const std::vector<std::vector<Bit>>&)>& out) {
  Abc_Ntk_t* ntk = Abc_NtkAlloc(ABC_NTK_STRASH, ABC_FUNC_AIG, 1);
  ntk->pName     = Extra_UtilStrsav(const_cast<char*>("lhd_reuse"));
  auto*      man = static_cast<Abc_Aig_t*>(ntk->pManFunc);
  const Bit  one = Abc_AigConst1(ntk);
  std::map<std::string, Bit> pi;
  for (const auto* m : ms) {
    for (const auto& n : m->pis) {
      if (!pi.contains(n)) {
        Abc_Obj_t* p = Abc_NtkCreatePi(ntk);
        Abc_ObjAssignName(p, const_cast<char*>(n.c_str()), nullptr);
        pi[n] = p;
      }
    }
  }
  std::vector<std::vector<Bit>>        val(ms.size());
  std::vector<std::vector<Abc_Obj_t*>> bis(ms.size());
  for (size_t k = 0; k < ms.size(); ++k) {
    const auto& m = *ms[k];
    val[k].assign(static_cast<size_t>(m.nvars()), nullptr);
    val[k][0] = Abc_ObjNot(one);
    size_t v  = 1;
    for (const auto& n : m.pis) {
      val[k][v++] = pi[n];
    }
    for (size_t i = 0; i < m.regs.size(); ++i) {
      Abc_Obj_t*        l  = Abc_NtkCreateLatch(ntk);
      Abc_Obj_t*        bi = Abc_NtkCreateBi(ntk);
      Abc_Obj_t*        bo = Abc_NtkCreateBo(ntk);
      const std::string nm = "m" + std::to_string(k) + ":" + m.regs[i];
      Abc_ObjAddFanin(l, bi);
      Abc_ObjAddFanin(bo, l);
      Abc_LatchSetInit0(l);
      Abc_ObjAssignName(bo, const_cast<char*>(nm.c_str()), nullptr);
      Abc_ObjAssignName(l, const_cast<char*>(nm.c_str()), const_cast<char*>("_L"));
      Abc_ObjAssignName(bi, const_cast<char*>(nm.c_str()), const_cast<char*>("_in"));
      bis[k].push_back(bi);
      val[k][v++] = bo;
    }
    auto bit = [&](int l) { return Abc_ObjNotCond(val[k][static_cast<size_t>(l >> 1)], l & 1); };
    for (const auto& a : m.ands) {
      val[k][v++] = Abc_AigAnd(man, bit(a[0]), bit(a[1]));
    }
    for (size_t i = 0; i < m.regs.size(); ++i) {
      Abc_ObjAddFanin(bis[k][i], bit(m.next[i]));
    }
  }
  Abc_Obj_t* po = Abc_NtkCreatePo(ntk);
  Abc_ObjAddFanin(po, out(man, val));
  Abc_ObjAssignName(po, const_cast<char*>("target"), nullptr);
  Abc_NtkOrderCisCos(ntk);  // ToDar: inputs before latch outputs, outputs before latch inputs
  if (Abc_NtkCheck(ntk) == 0) {
    Abc_NtkDelete(ntk);
    return nullptr;
  }
  return ntk;
}

Bit model_bit(const std::vector<Bit>& val, int l) { return Abc_ObjNotCond(val[static_cast<size_t>(l >> 1)], l & 1); }

// A cube to block: (register index, value). The invariant is "no cube holds".
using Cube = std::vector<std::pair<int, bool>>;

Bit inv_bit(Abc_Aig_t* man, const std::vector<Bit>& val, const Lit_model& m, const std::vector<Cube>& inv) {
  const Bit one = Abc_ObjNot(val[0]);  // var 0 is constant 0
  Bit       all = one;
  for (const auto& c : inv) {
    Bit cube = one;
    for (auto [r, v] : c) {
      const Bit q = val[static_cast<size_t>(m.reg_var(static_cast<size_t>(r)))];
      cube        = Abc_AigAnd(man, cube, v ? q : Abc_ObjNot(q));
    }
    all = Abc_AigAnd(man, all, Abc_ObjNot(cube));
  }
  return all;
}

Abc_Ntk_t* model_ntk(const Lit_model& m, const std::vector<Cube>& inv = {}) {
  return build_ntk({&m}, [&](Abc_Aig_t* man, const std::vector<std::vector<Bit>>& val) {
    const Bit t = model_bit(val[0], m.target);
    return inv.empty() ? t : Abc_AigAnd(man, t, inv_bit(man, val[0], m, inv));
  });
}

std::vector<Cube> inv_parse(const std::string& s, const Lit_model& m) {
  std::unordered_map<std::string, int> ix;
  for (size_t i = 0; i < m.regs.size(); ++i) {
    ix[esc_name(m.regs[i])] = static_cast<int>(i);
  }
  std::vector<Cube>  out;
  std::istringstream in{s};
  std::string        line;
  while (std::getline(in, line)) {
    if (line.starts_with("#")) {
      continue;
    }
    std::istringstream ls{line};
    std::string        tok;
    Cube               c;
    bool               known = true;
    while (ls >> tok) {
      auto it = tok.size() > 2 ? ix.find(tok.substr(2)) : ix.end();
      if (it == ix.end()) {
        known = false;  // the register is gone: drop the whole cube
        break;
      }
      c.emplace_back(it->second, tok[0] == '1');
    }
    if (known && !c.empty()) {
      out.push_back(std::move(c));
    }
  }
  return out;
}

std::string inv_text(const std::vector<Cube>& inv, const Lit_model& m) {
  std::string s = "#inv\n";  // present even when empty: the property needed no strengthening
  for (const auto& c : inv) {
    for (auto [r, v] : c) {
      s += std::string(v ? "1=" : "0=") + esc_name(m.regs[static_cast<size_t>(r)]) + " ";
    }
    s += "\n";
  }
  return s;
}

// Houdini: keep the cubes whose blocking clauses hold at reset and are
// inductive together. Returns true when they also exclude `target`, i.e. the
// property holds in every cycle.
bool houdini(const Lit_model& m, std::vector<Cube>& inv) {
  std::erase_if(inv, [](const Cube& c) {  // reset state is all-zero: a cube of zeros blocks it
    return std::all_of(c.begin(), c.end(), [](auto rv) { return !rv.second; });
  });
  Abc_Ntk_t* ntk = model_ntk(m);
  if (ntk == nullptr) {
    return false;
  }
  Aig_Man_t* aig = Abc_NtkToDar(ntk, 0, 1);
  Abc_NtkDelete(ntk);
  if (aig == nullptr || Aig_ManRegNum(aig) != static_cast<int>(m.regs.size())) {
    if (aig != nullptr) {
      Aig_ManStop(aig);
    }
    return false;
  }
  // One frame as a combinational circuit: latch outputs become inputs and
  // latch inputs outputs, so each output gets its own variable, none forced.
  const int nreg = Aig_ManRegNum(aig);
  const int npi  = Saig_ManPiNum(aig);
  const int npo  = Saig_ManPoNum(aig);
  Aig_ManSetRegNum(aig, 0);
  Cnf_Dat_t*  cnf = Cnf_Derive(aig, Aig_ManCoNum(aig));
  auto*       s   = static_cast<sat_solver*>(Cnf_DataWriteIntoSolver(cnf, 1, 0));
  bool        proved = false;
  if (s != nullptr && nreg == static_cast<int>(m.regs.size())) {
    auto lo = [&](int r) { return cnf->pVarNums[Aig_ObjId(Aig_ManCi(aig, npi + r))]; };
    auto li = [&](int r) { return cnf->pVarNums[Aig_ObjId(Aig_ManCo(aig, npo + r))]; };
    const int base = cnf->nVars;
    sat_solver_setnvars(s, base + static_cast<int>(inv.size()));
    for (size_t j = 0; j < inv.size(); ++j) {
      std::vector<lit> cl{Abc_Var2Lit(base + static_cast<int>(j), 1)};
      for (auto [r, v] : inv[j]) {
        cl.push_back(Abc_Var2Lit(lo(r), v ? 1 : 0));  // register differs from the cube
      }
      sat_solver_addclause(s, cl.data(), cl.data() + cl.size());
    }
    std::vector<char> alive(inv.size(), 1);
    for (bool changed = true; changed;) {
      changed = false;
      for (size_t j = 0; j < inv.size(); ++j) {
        if (!alive[j]) {
          continue;
        }
        std::vector<lit> as;
        for (size_t k = 0; k < inv.size(); ++k) {
          if (alive[k]) {
            as.push_back(Abc_Var2Lit(base + static_cast<int>(k), 0));
          }
        }
        for (auto [r, v] : inv[j]) {
          as.push_back(Abc_Var2Lit(li(r), v ? 0 : 1));  // next state inside the cube
        }
        if (sat_solver_solve(s, as.data(), as.data() + as.size(), 100000, 0, 0, 0) != l_False) {
          alive[j] = 0;
          changed  = true;
        }
      }
    }
    std::vector<Cube> keep;
    std::vector<lit>  as;
    for (size_t k = 0; k < inv.size(); ++k) {
      if (alive[k]) {
        keep.push_back(inv[k]);
        as.push_back(Abc_Var2Lit(base + static_cast<int>(k), 0));
      }
    }
    as.push_back(Abc_Var2Lit(cnf->pVarNums[Aig_ObjId(Aig_ManCo(aig, 0))], 0));
    proved = sat_solver_solve(s, as.data(), as.data() + as.size(), 100000, 0, 0, 0) == l_False;
    inv    = std::move(keep);
  }
  if (s != nullptr) {
    sat_solver_delete(s);
  }
  Cnf_DataFree(cnf);
  Aig_ManStop(aig);
  return proved;
}

// PDR on the model, strengthened by `inv` (already inductive, so the
// reachable states are the same). On a proof, `inv` becomes the full invariant.
bool pdr_prove(const Lit_model& m, std::vector<Cube>& inv, int timeout_s) {
  Abc_Ntk_t* ntk = model_ntk(m, inv);
  if (ntk == nullptr) {
    return false;
  }
  Aig_Man_t* aig = Abc_NtkToDar(ntk, 0, 1);
  Abc_NtkDelete(ntk);
  if (aig == nullptr) {
    return false;
  }
  Pdr_Par_t pars;
  Pdr_ManSetDefaultParams(&pars);
  pars.nTimeOut = std::max(1, timeout_s);
  pars.fSilent  = 1;
  Abc_FrameSetInv(nullptr);
  const int rv = Pdr_ManSolve(aig, &pars);
  if (rv == 1) {
    if (Vec_Int_t* v = Abc_FrameGetGlobalFrame()->pAbcWlcInv; v != nullptr && Vec_IntSize(v) > 0) {
      int* p = Vec_IntArray(v);
      int  n = p[0], at = 1;
      for (int c = 0; c < n; ++c) {
        const int len = p[at++];
        Cube      cube;
        for (int i = 0; i < len; ++i, ++at) {
          if (p[at] >= 0) {
            cube.emplace_back(Abc_Lit2Var(p[at]), !Abc_LitIsCompl(p[at]));
          }
        }
        if (!cube.empty()) {
          inv.push_back(std::move(cube));
        }
      }
    }
  }
  Aig_ManStop(aig);
  return rv == 1;
}

// Same register names, and every next-state function and the target equal
// as combinational functions of inputs and registers (one SAT call). Same
// reset state (all zero) and same transitions: sequentially equivalent.
// Returns 0 (not shown equal), 1 (equal by structure), 2 (equal by CEC).
int comb_equal(const Lit_model& a, const Lit_model& b) {
  if (a.target <= 1 && a.target == b.target) {
    return 1;  // the bad state is structurally impossible in both
  }
  if (a.regs != b.regs) {
    return 0;
  }
  // One AIG over shared inputs and registers; CEC (SAT sweeping, Mishchenko
  // et al.) proves each output pair equal: the targets, then every next state.
  Gia_Man_t* gia = Gia_ManStart(a.nvars() + b.nvars() + 16);
  Gia_ManHashAlloc(gia);
  std::map<std::string, int> in;
  auto input = [&](const std::string& n) {
    auto it = in.find(n);
    if (it == in.end()) {
      it = in.emplace(n, Gia_ManAppendCi(gia)).first;
    }
    return it->second;
  };
  auto blast = [&](const Lit_model& m) {
    std::vector<int> val(static_cast<size_t>(m.nvars()));
    val[0]   = 0;
    size_t v = 1;
    for (const auto& n : m.pis) {
      val[v++] = input("i:" + n);
    }
    for (const auto& n : m.regs) {
      val[v++] = input("r:" + n);
    }
    auto lit = [&](int l) { return Abc_LitNotCond(val[static_cast<size_t>(l >> 1)], l & 1); };
    for (const auto& x : m.ands) {
      val[v++] = Gia_ManHashAnd(gia, lit(x[0]), lit(x[1]));
    }
    std::vector<int> outs{lit(m.target)};
    for (int n : m.next) {
      outs.push_back(lit(n));
    }
    return outs;
  };
  const auto oa = blast(a);
  const auto ob = blast(b);
  bool       same = true;
  for (size_t i = 0; i < oa.size() && same; ++i) {
    same = oa[i] == ob[i];  // structural hashing already merged it
  }
  int rv = 1;
  if (!same) {
    for (size_t i = 0; i < oa.size(); ++i) {
      Gia_ManAppendCo(gia, oa[i]);
      Gia_ManAppendCo(gia, ob[i]);
    }
    Gia_ManHashStop(gia);
    Cec_ParCec_t cec;
    Cec_ManCecSetDefaultParams(&cec);
    cec.fSilent  = 1;
    cec.nBTLimit = 1000;
    rv           = Cec_ManVerify(gia, &cec);
  }
  Gia_ManStop(gia);
  return rv != 1 ? 0 : same ? 1 : 2;
}

// Sequential equivalence of two models' `target` outputs, inputs matched by
// name: the structural tier above, then SAT sweeping + induction (ABC's
// `dsec`, Mishchenko et al.) for register renames and retimed logic.
// Returns 0 (not shown equal), 1 (structure), 2 (CEC), 3 (dsec).
int seq_equal(const Lit_model& a, const Lit_model& b, int timeout_s) {
  if (model_text(a) == model_text(b)) {
    return 1;
  }
  if (const int c = comb_equal(a, b); c != 0) {
    return c;
  }
  // Free initial values are inputs named after their register: if they do not
  // match, the start states are unrelated and the miter cannot be proven.
  auto inits = [](const Lit_model& m) {
    std::vector<std::string> v;
    for (const auto& n : m.pis) {
      if (n.starts_with("init:")) {
        v.push_back(n);
      }
    }
    return v;
  };
  if (inits(a) != inits(b)) {
    return 0;
  }
  Abc_Ntk_t* ntk = build_ntk({&a, &b}, [&](Abc_Aig_t* man, const std::vector<std::vector<Bit>>& val) {
    return Abc_AigXor(man, model_bit(val[0], a.target), model_bit(val[1], b.target));
  });
  if (ntk == nullptr) {
    return 0;
  }
  int rv = -1;
  if (const int cst = Abc_NtkMiterIsConstant(ntk); cst == 1) {
    rv = 1;
  } else if (cst == 0) {
    rv = 0;
  } else if (Abc_NtkLatchNum(ntk) == 0) {
    rv = 0;  // combinational and not constant: unequal for some input
  } else if (Aig_Man_t* aig = Abc_NtkToDar(ntk, 0, 1); aig != nullptr) {
    Fra_Sec_t sp;
    Fra_SecSetDefaultParams(&sp);
    sp.TimeLimit      = std::max(1, timeout_s);
    sp.fSilent        = 1;
    sp.fVerbose       = 0;
    sp.fUsePdr        = 0;  // SAT sweeping + induction only: PDR, interpolation and
    sp.fInterpolation = 0;  // BDD reachability would re-prove the property itself
    sp.fReachability  = 0;
    rv           = Fra_FraigSec(aig, &sp, nullptr);
    Aig_ManStop(aig);
  }
  Abc_NtkDelete(ntk);
  return rv == 1 ? 3 : 0;
}

constexpr int kKeepModels = 3;  // past models kept per property for reuse=equiv

// One stored answer: what a run concluded for this model.
struct Stored_answer {
  int         frame     = kSeqUnknown;
  int         frames    = 0;
  bool        unbounded = false;
  bool        exact     = false;
  std::string run;  // the lhd run that stored it
};

std::optional<Stored_answer> answer_parse(const std::string& s) {
  std::istringstream in{s};
  Stored_answer      a;
  int                u = 0, e = 0;
  if (!(in >> a.frame >> a.frames >> u >> e)) {
    return std::nullopt;
  }
  in >> a.run;
  a.unbounded = u != 0;
  a.exact     = e != 0;
  return a;
}

// Input trace of a counterexample: per frame, the inputs that are 1.
using Trace = std::vector<std::unordered_set<std::string>>;

Trace trace_parse(const std::string& s) {
  Trace              t;
  std::istringstream in{s};
  std::string        line;
  while (std::getline(in, line)) {
    std::istringstream ls{line};
    std::string        tok;
    t.emplace_back();
    while (ls >> tok) {
      t.back().insert(tok);
    }
  }
  return t;
}

// Simulates the model on `tr`; returns the first frame where target is 1, if
// the assumes still hold at the last of `frames` frames (cvc5's rule).
int replay(const Lit_model& m, const Trace& tr, int frames) {
  std::vector<uint8_t> v(static_cast<size_t>(m.nvars()), 0);
  std::vector<uint8_t> st(m.regs.size(), 0);
  std::vector<std::string> pn;
  for (const auto& p : m.pis) {
    pn.push_back(esc_name(p));
  }
  auto bit = [&](int l) { return static_cast<uint8_t>(v[static_cast<size_t>(l >> 1)] ^ (l & 1)); };
  int  hit = -1;
  for (int f = 0; f < frames; ++f) {
    size_t x = 1;
    for (const auto& p : pn) {
      v[x++] = f < static_cast<int>(tr.size()) && tr[static_cast<size_t>(f)].contains(p) ? 1 : 0;
    }
    for (auto s : st) {
      v[x++] = s;
    }
    for (const auto& a : m.ands) {
      v[x++] = bit(a[0]) & bit(a[1]);
    }
    if (hit < 0 && bit(m.target) != 0) {
      hit = f;
    }
    if (f == frames - 1 && bit(m.ok) == 0) {
      return -1;
    }
    for (size_t i = 0; i < st.size(); ++i) {
      st[i] = bit(m.next[i]);
    }
  }
  return hit;
}

// bmc3 on the model alone: true when target cannot be 1 in `frames` frames.
bool model_bmc_clean(const Lit_model& m, int frames, int timeout_s) {
  if (frames <= 0) {
    return true;
  }
  Abc_Ntk_t* ntk = model_ntk(m);
  if (ntk == nullptr) {
    return false;
  }
  Saig_ParBmc_t pars;
  Saig_ParBmcSetDefaultParams(&pars);
  pars.nFramesMax = frames;
  pars.nTimeOut   = timeout_s;
  pars.fSilent    = 1;
  const int rv    = Abc_NtkDarBmc3(ntk, &pars, 0);
  Abc_NtkDelete(ntk);
  return rv == 1 || (rv == -1 && pars.iFrame + 1 >= frames);
}

// Debug (LHD_REPLAY=<file> from LHD_WITNESS_DUMP): simulate a cvc5 trace on
// this exact model and report, per cycle, assumes / target and the first
// registers whose value differs from the one cvc5 chose.
void replay_debug(const Lit_model& m, const char* file, const std::string& name, int frames) {
  std::istringstream in{read_file(file)};
  std::string        line, want = esc_name(name);
  bool               mine = false;
  int                at   = -1;
  std::map<std::pair<int, std::string>, char> pi_bit, reg_bit;  // (cycle, escaped bit name) -> '0'/'1'
  while (std::getline(in, line)) {
    std::istringstream ls{line};
    std::string        tag, nm, val;
    int                c = 0;
    ls >> tag;
    if (tag == "P") {
      ls >> nm >> c;
      if (mine) {
        break;
      }
      mine = nm == want;
      at   = c;
      continue;
    }
    if (!mine || !(ls >> c >> nm >> val)) {
      continue;
    }
    for (size_t k = 0; k < val.size(); ++k) {
      const std::string bit = std::to_string(val.size() - 1 - k);
      if (tag == "I") {
        pi_bit[{c, "ab_i_" + nm + "_b" + bit}] = val[k];
      } else {
        reg_bit[{c, "ab_s_" + nm + "_b" + bit}] = val[k];
        if (c == 0) {
          pi_bit[{0, "init:ab_s_" + nm + "_b" + bit}] = val[k];
        }
      }
    }
  }
  if (at < 0) {
    return;
  }
  std::fprintf(stderr, "replay %s: cvc5 refutes at %d\n", name.c_str(), at);
  std::vector<std::string> pn, rn;
  for (const auto& p : m.pis) {
    pn.push_back(esc_name(p));
  }
  for (const auto& r : m.regs) {
    rn.push_back(esc_name(r));
  }
  std::vector<uint8_t> v(static_cast<size_t>(m.nvars()), 0), st(m.regs.size(), 0);
  auto bit = [&](int l) { return static_cast<uint8_t>(v[static_cast<size_t>(l >> 1)] ^ (l & 1)); };
  int  shown = 0;
  for (int f = 0; f <= at && f < frames; ++f) {
    size_t x = 1;
    for (const auto& p : pn) {
      auto it = pi_bit.find({f, p});
      v[x++]  = it != pi_bit.end() && it->second == '1' ? 1 : 0;
    }
    std::string diff;
    int         nd = 0;
    for (size_t i = 0; i < st.size(); ++i) {
      v[x++] = st[i];
      auto it = reg_bit.find({f, rn[i]});
      if (f > 0 && it != reg_bit.end() && (it->second == '1') != (st[i] != 0)) {
        if (++nd <= 8) {
          diff += " " + rn[i] + (st[i] ? "=1(cvc5 0)" : "=0(cvc5 1)");
        }
      }
    }
    for (const auto& a : m.ands) {
      v[x++] = bit(a[0]) & bit(a[1]);
    }
    std::fprintf(stderr, "replay f%d ok=%d target=%d reg-diffs=%d%s\n", f, bit(m.ok), bit(m.target), nd, diff.c_str());
    if (nd > 0 && ++shown >= 3) {
      break;
    }
    for (size_t i = 0; i < st.size(); ++i) {
      st[i] = bit(m.next[i]);
    }
  }
}

// One query: build the sequential AIG and run bmc3.
Seq_answer seq_bmc_one(const Seq_model& m, size_t pix, int timeout_s) {
  Seq_answer ans;
  Abc_Ntk_t* ntk = Abc_NtkAlloc(ABC_NTK_STRASH, ABC_FUNC_AIG, 1);
  ntk->pName     = Extra_UtilStrsav(const_cast<char*>("lhd_bmc"));
  auto*     man  = static_cast<Abc_Aig_t*>(ntk->pManFunc);
  const Bit one  = Abc_AigConst1(ntk);
  const Bit zero = Abc_ObjNot(one);
  std::vector<std::pair<Abc_Obj_t*, Bit>> next;  // latch input, its next value
  std::unordered_map<Abc_Obj_t*, std::string> reg_name;  // latch output -> stable name
  auto latch = [&](std::string name) {
    Abc_Obj_t* l  = Abc_NtkCreateLatch(ntk);
    Abc_Obj_t* bi = Abc_NtkCreateBi(ntk);
    Abc_Obj_t* bo = Abc_NtkCreateBo(ntk);
    Abc_ObjAddFanin(l, bi);
    Abc_ObjAddFanin(bo, l);
    Abc_LatchSetInit0(l);
    reg_name[bo] = std::move(name);
    return std::pair<Abc_Obj_t*, Bit>{bi, bo};
  };
  auto pi = [&](const std::string& nm) {
    Abc_Obj_t* p = Abc_NtkCreatePi(ntk);
    Abc_ObjAssignName(p, const_cast<char*>(nm.c_str()), nullptr);
    return p;
  };
  auto fail = [&](std::string_view) {  // the reason is for debugging only
    Abc_NtkDelete(ntk);
    return ans;
  };
  // A saturating counter and "counter < k" / "counter == k" over it.
  auto counter = [&](int limit, const std::string& nm) {
    int kb = 1;
    while ((1 << kb) <= limit) {
      ++kb;
    }
    std::vector<Bit>        cnt;
    std::vector<Abc_Obj_t*> cnt_bi;
    for (int i = 0; i < kb; ++i) {
      auto [bi, bo] = latch(nm + std::to_string(i));
      cnt_bi.push_back(bi);
      cnt.push_back(bo);
    }
    return std::pair{cnt, cnt_bi};
  };
  auto less = [&](const std::vector<Bit>& cnt, int k) {
    Bit lt = zero;  // unsigned, LSB to MSB
    for (size_t i = 0; i < cnt.size(); ++i) {
      const Bit h = ((k >> i) & 1) ? one : zero;
      lt          = Abc_AigMux(man, Abc_AigXor(man, cnt[i], h), Abc_AigAnd(man, Abc_ObjNot(cnt[i]), h), lt);
    }
    return lt;
  };
  auto equal = [&](const std::vector<Bit>& cnt, int k) {
    Bit e = one;
    for (size_t i = 0; i < cnt.size(); ++i) {
      e = Abc_AigAnd(man, e, ((k >> i) & 1) ? cnt[i] : Abc_ObjNot(cnt[i]));
    }
    return e;
  };
  auto step = [&](const std::vector<Bit>& cnt, const std::vector<Abc_Obj_t*>& cnt_bi, Bit inc) {
    Bit carry = inc;
    for (size_t i = 0; i < cnt.size(); ++i) {
      next.emplace_back(cnt_bi[i], Abc_AigXor(man, cnt[i], carry));
      carry = Abc_AigAnd(man, cnt[i], carry);
    }
  };

  // `started` is 0 only in cycle 0, so registers read their init values there.
  auto [s_bi, started] = latch("_started");
  next.emplace_back(s_bi, one);

  // Reset-hold counter: in_reset while it is below reset_hold, then it stops.
  Bit in_reset = zero;
  if (m.reset_hold > 0) {
    auto [cnt, cnt_bi] = counter(m.reset_hold, "_rc");
    in_reset           = less(cnt, m.reset_hold);
    step(cnt, cnt_bi, in_reset);
  }
  const Seq_prop& p = m.props[pix];
  // Cycle counter for modes 1 and 2 (stops at the last frame).
  Bit is_last = one, at_now = zero;
  if (p.mode != 0) {
    auto [fc, fc_bi] = counter(m.frames, "_fc");
    is_last          = equal(fc, m.frames - 1);
    at_now           = p.at >= 0 ? equal(fc, p.at) : zero;
    step(fc, fc_bi, Abc_ObjNot(is_last));
  }

  Blaster b(ntk, nullptr);
  for (const auto& pin : m.pins) {
    auto r = const_bits(ntk, pin.in_reset);
    auto a = const_bits(ntk, pin.after_reset);
    if (r.empty() || r.size() != a.size()) {
      return fail("reset pin not constant");
    }
    for (size_t i = 0; i < r.size(); ++i) {
      r[i] = Abc_AigMux(man, in_reset, r[i], a[i]);
    }
    b.preset(pin.sym, std::move(r));
  }
  std::vector<std::vector<Bit>>        cur(m.regs.size());
  std::vector<std::vector<Abc_Obj_t*>> cur_bi(m.regs.size());
  for (size_t ri = 0; ri < m.regs.size(); ++ri) {
    const auto&    r    = m.regs[ri];
    const uint32_t w    = r.cur.getSort().getBitVectorSize();
    auto           init = const_bits(ntk, r.init);
    const std::string rn   = r.cur.hasSymbol() ? r.cur.getSymbol() : "r" + std::to_string(ri);
    for (uint32_t i = 0; i < w; ++i) {
      auto [bi, bo] = latch(rn + "_b" + std::to_string(i));
      const Bit iv  = i < init.size() ? init[i] : pi("init:" + rn + "_b" + std::to_string(i));
      cur[ri].push_back(Abc_AigMux(man, started, bo, iv));
      cur_bi[ri].push_back(bi);
    }
    b.preset(r.cur, cur[ri]);
  }
  // Memories: one latch per entry bit, free initial contents (as in cvc5).
  std::vector<std::vector<std::vector<Abc_Obj_t*>>> mem_bi(m.mems.size());
  for (size_t mi = 0; mi < m.mems.size(); ++mi) {
    const auto&       mm = m.mems[mi];
    const auto        so = mm.cur.getSort();
    const uint32_t    aw = so.getArrayIndexSort().getBitVectorSize();
    const uint32_t    dw = so.getArrayElementSort().getBitVectorSize();
    const std::string nm = mm.cur.hasSymbol() ? mm.cur.getSymbol() : "mem" + std::to_string(mi);
    Blaster::Entries  ent(size_t{1} << aw);
    mem_bi[mi].resize(ent.size());
    for (size_t k = 0; k < ent.size(); ++k) {
      for (uint32_t i = 0; i < dw; ++i) {
        const std::string bn = nm + "[" + std::to_string(k) + "]_b" + std::to_string(i);
        auto [bi, bo]        = latch(bn);
        ent[k].push_back(Abc_AigMux(man, started, bo, pi("init:" + bn)));
        mem_bi[mi][k].push_back(bi);
      }
    }
    b.preset_array(mm.cur, std::move(ent));
  }
  for (const auto& [sym, val] : m.binds) {  // asynchronous reads: dout := select(...)
    auto v = b.run_bv(val);
    if (b.bad() || v.size() != sym.getSort().getBitVectorSize()) {
      return fail(b.bad() ? b.why() : "bind width");
    }
    b.preset(sym, std::move(v));
  }
  for (size_t mi = 0; mi < m.mems.size(); ++mi) {
    auto e = b.run_arr(m.mems[mi].next);
    if (b.bad() || e.size() != mem_bi[mi].size()) {
      return fail(b.bad() ? b.why() : "memory next");
    }
    for (size_t k = 0; k < e.size(); ++k) {
      for (size_t i = 0; i < e[k].size() && i < mem_bi[mi][k].size(); ++i) {
        next.emplace_back(mem_bi[mi][k][i], e[k][i]);
      }
    }
  }
  for (size_t ri = 0; ri < m.regs.size(); ++ri) {
    const auto& r  = m.regs[ri];
    auto        nb = b.run_bv(r.next);
    if (b.bad() || nb.size() != cur[ri].size()) {
      return fail(b.bad() ? b.why() : "next-state width");
    }
    for (size_t i = 0; i < nb.size(); ++i) {
      const Bit v = r.hold_in_reset ? Abc_AigMux(man, in_reset, cur[ri][i], nb[i]) : nb[i];
      next.emplace_back(cur_bi[ri][i], v);
    }
  }

  // Environment: `viol` latches once an assume fails; later cycles do not count.
  Bit e_now = one;
  for (int ei : p.envs) {
    e_now = Abc_AigAnd(man, e_now, b.run(m.envs[static_cast<size_t>(ei)]));
    if (b.bad()) {
      return fail(b.why());
    }
  }
  auto [v_bi, viol] = latch("_viol");
  next.emplace_back(v_bi, Abc_AigOr(man, viol, Abc_ObjNot(e_now)));
  const Bit ok     = Abc_AigAnd(man, Abc_ObjNot(viol), e_now);
  const Bit window = p.always ? one : Abc_ObjNot(in_reset);
  Bit       target = zero;
  if (p.mode == 1) {
    target = Abc_AigAnd(man, ok, is_last);
  } else {
    const Bit h = b.run(p.holds);
    if (b.bad() || h == nullptr) {
      return fail(b.bad() ? b.why() : "property not boolean");
    }
    const Bit failing = Abc_AigAnd(man, window, Abc_ObjNot(h));
    if (p.mode == 0) {
      target = Abc_AigAnd(man, ok, failing);
    } else {
      auto [f_bi, failed] = latch("_failed");  // failed exactly at cycle `at`, envs still holding
      const Bit hit       = Abc_AigAnd(man, Abc_AigAnd(man, at_now, failing), ok);
      next.emplace_back(f_bi, Abc_AigOr(man, failed, hit));
      target = Abc_AigAnd(man, Abc_AigAnd(man, ok, is_last), Abc_AigOr(man, failed, hit));
    }
  }
  Abc_Obj_t* po = Abc_NtkCreatePo(ntk);
  Abc_ObjAddFanin(po, target);
  Abc_ObjAssignName(po, const_cast<char*>("target"), nullptr);
  for (auto& [bi, v] : next) {
    Abc_ObjAddFanin(bi, v);
  }
  // Witness inputs, before CI reordering changes nothing about object identity.
  std::vector<const std::vector<Bit>*> wbits;
  for (const auto& w : m.watch) {
    wbits.push_back(b.sym_bits(w));
  }
  ans.exact = !b.abstracted() && !m.inexact;
  // Memories abstracted make the network inexact, but a property whose cone
  // reads only real inputs and free initial values still gets real traces.
  if (!ans.exact) {
    std::unordered_set<std::string> real;
    for (const auto& w : m.watch) {
      if (w.hasSymbol()) {
        real.insert(w.getSymbol());
      }
    }
    if (auto cm = extract_model(ntk, target, ok, reg_name, false)) {
      ans.exact = std::all_of(cm->pis.begin(), cm->pis.end(), [&](const std::string& n) {
        const auto u = n.rfind("_b");
        return n.starts_with("init:") || (u != std::string::npos && real.contains(n.substr(0, u)));
      });
      if (!ans.exact && std::getenv("LHD_REUSE_DEBUG") != nullptr) {
        for (const auto& n : cm->pis) {
          const auto u = n.rfind("_b");
          if (!n.starts_with("init:") && (u == std::string::npos || !real.contains(n.substr(0, u)))) {
            std::fprintf(stderr, "inexact[%zu] mode%d: %s\n", pix, p.mode, esc_name(n).c_str());
          }
        }
      }
    }
  }
  Abc_NtkOrderCisCos(ntk);  // ToDar wants primary inputs before latch outputs
  Abc_NtkAddDummyBoxNames(ntk);
  if (Abc_NtkCheck(ntk) == 0) {
    return fail("Abc_NtkCheck");
  }

  // Cross-version reuse and PDR (mode 0), on the property's own cone.
  std::optional<Lit_model> lm;
  const bool               dbg = std::getenv("LHD_REUSE_DEBUG") != nullptr;
  const char* replay_file = std::getenv("LHD_REPLAY");
  if (p.mode == 0 && (!p.store.empty() || p.pdr || replay_file != nullptr)) {
    lm = extract_model(ntk, target, ok, reg_name, ans.exact);
    if (dbg) {
      std::fprintf(stderr, "reuse[%zu]: model %s pis=%zu regs=%zu ands=%zu store=%s\n", pix, lm ? "ok" : "none",
                   lm ? lm->pis.size() : 0, lm ? lm->regs.size() : 0, lm ? lm->ands.size() : 0, p.store.c_str());
    }
  }
  if (lm && replay_file != nullptr && !p.name.empty()) {
    replay_debug(*lm, replay_file, p.name, m.frames);
  }
  const std::string& dir = p.store;
  std::vector<Cube>  seed;  // cubes of the old invariant that are still inductive
  auto finish = [&](Seq_answer a) {  // keep what this run concluded for the next version
    if (lm && !dir.empty() && a.frame != kSeqUnknown) {
      const std::string txt = model_text(*lm);
      if (read_file(dir + "/model.txt") != txt) {  // a new model: shift the older ones down
        for (int k = kKeepModels - 1; k > 0; --k) {
          const std::string from = k == 1 ? "" : std::to_string(k - 1);
          std::rename((dir + "/model" + from + ".txt").c_str(), (dir + "/model" + std::to_string(k) + ".txt").c_str());
          std::rename((dir + "/answer" + from + ".txt").c_str(), (dir + "/answer" + std::to_string(k) + ".txt").c_str());
        }
      }
      write_file(dir + "/model.txt", txt);
      write_file(dir + "/answer.txt", std::format("{} {} {} {} {}\n", a.frame, m.frames, a.unbounded ? 1 : 0, a.exact ? 1 : 0,
                                                  m.run_id.empty() ? "-" : m.run_id));
    }
    Abc_NtkDelete(ntk);
    return a;
  };
  const auto last_ans = lm && !dir.empty() ? answer_parse(read_file(dir + "/answer.txt")) : std::nullopt;
  const bool same_run = last_ans && !m.run_id.empty() && last_ans->run == m.run_id;  // a sibling strategy, not a version
  if (lm && !dir.empty() && !same_run) {
    // 1. Counterexample replay: the old trace still fails (first failure).
    if ((p.reuse & kReuseCex) && lm->exact) {
      const Trace tr = trace_parse(read_file(dir + "/cex.txt"));
      const int   f  = tr.empty() ? -1 : replay(*lm, tr, m.frames);
      const int   w0 = p.always ? 0 : m.reset_hold;  // nothing can fail earlier
      if (f >= 0 && (f <= w0 || model_bmc_clean(*lm, f, timeout_s))) {
        Seq_answer a;
        a.frame = f;
        a.exact = true;
        a.how   = "cex";
        for (int c = 0; c <= f; ++c) {
          std::vector<std::string> row;
          for (const auto& w : m.watch) {
            const uint32_t    wd = w.getSort().getBitVectorSize();
            const std::string nm = w.hasSymbol() ? w.getSymbol() : std::string{};
            std::string       v(wd, '0');
            for (uint32_t bit = 0; bit < wd && !nm.empty(); ++bit) {
              if (tr[static_cast<size_t>(c)].contains(esc_name(nm + "_b" + std::to_string(bit)))) {
                v[wd - 1 - bit] = '1';
              }
            }
            row.push_back(std::move(v));
          }
          a.values.push_back(std::move(row));
        }
        return finish(a);
      }
    }
    // 2. Equivalence: an old model (the last 3 kept) is sequentially equal, so
    // its answer holds. The cheap tiers try all; SAT sweeping only the newest.
    std::optional<Stored_answer> old_ans;
    int                          tier = 0;  // which check proved it: 1 structure, 2 CEC, 3 dsec
    for (int k = 0; k < kKeepModels && !old_ans && (p.reuse & kReuseEquiv); ++k) {
      const std::string sfx = k == 0 ? "" : std::to_string(k);
      Lit_model         old;
      auto              oa = answer_parse(read_file(dir + "/answer" + sfx + ".txt"));
      if (oa && (m.run_id.empty() || oa->run != m.run_id) && model_parse(read_file(dir + "/model" + sfx + ".txt"), old)
          && (tier = k == 0 ? seq_equal(old, *lm, m.sec_s) : model_text(old) == model_text(*lm) ? 1 : comb_equal(old, *lm)) != 0) {
        old_ans = oa;
      }
    }
    if (old_ans) {
      Seq_answer a;
      a.exact = lm->exact;
      a.how   = tier == 1 ? "equiv:struct" : tier == 2 ? "equiv:cec" : "equiv:dsec";
      if (old_ans->unbounded) {
        a.frame     = kSeqClean;
        a.unbounded = true;
      } else if (old_ans->frame >= 0 && old_ans->frame < m.frames) {
        a.frame = old_ans->frame;
      } else if (old_ans->frames >= m.frames && (old_ans->frame == kSeqClean || old_ans->frame >= m.frames)) {
        a.frame = kSeqClean;
      }
      if (a.frame != kSeqUnknown) {
        return finish(a);
      }
    }
    // 3. Invariant: the old PDR invariant, Houdini-filtered, still blocks it.
    if (p.reuse & kReuseInv) {
      const std::string it = read_file(dir + "/inv.txt");
      seed                 = inv_parse(it, *lm);
      if (!it.empty() && houdini(*lm, seed)) {
        write_file(dir + "/inv.txt", inv_text(seed, *lm));
        Seq_answer a;
        a.frame     = kSeqClean;
        a.unbounded = true;
        a.exact     = ans.exact;
        a.how       = "inv";
        return finish(a);
      }
    }
  }

  Saig_ParBmc_t pars;
  Saig_ParBmcSetDefaultParams(&pars);
  pars.nFramesMax = m.frames;
  pars.nTimeOut   = timeout_s;
  pars.fSilent    = 1;
  const int rv    = Abc_NtkDarBmc3(ntk, &pars, 0);
  if (rv == 0 && ntk->pSeqModel != nullptr) {
    const Abc_Cex_t* cex = ntk->pSeqModel;
    ans.frame            = p.mode == 2 ? p.at : cex->iFrame;
    if (p.mode == 2) {
      // Inputs the failure does not depend on read as 0 (as in a cvc5 model).
      Aig_Man_t* aig  = Abc_NtkToDar(ntk, 0, 1);
      Abc_Cex_t* care = aig != nullptr ? Bmc_CexCareMinimize(aig, Saig_ManPiNum(aig), const_cast<Abc_Cex_t*>(cex), 4, 0, 0) : nullptr;
      auto       is_1 = [&](int idx) {
        return Abc_InfoHasBit(const_cast<unsigned*>(cex->pData), idx) && (care == nullptr || Abc_InfoHasBit(care->pData, idx));
      };
      std::unordered_map<Abc_Obj_t*, int>  pi_ix;
      Abc_Obj_t*                           obj;
      int                                  k;
      Abc_NtkForEachPi(ntk, obj, k) { pi_ix[obj] = k; }
      for (int f = 0; f <= p.at; ++f) {
        std::vector<std::string> row;
        for (size_t wi = 0; wi < m.watch.size(); ++wi) {
          const uint32_t w = m.watch[wi].getSort().getBitVectorSize();
          std::string    v(w, '0');
          for (uint32_t bit = 0; wbits[wi] != nullptr && bit < w && bit < wbits[wi]->size(); ++bit) {
            auto it = pi_ix.find(Abc_ObjRegular((*wbits[wi])[bit]));
            if (it != pi_ix.end() && is_1(cex->nRegs + f * cex->nPis + it->second)) {
              v[w - 1 - bit] = Abc_ObjIsComplement((*wbits[wi])[bit]) ? '0' : '1';
            } else if (it != pi_ix.end()) {
              v[w - 1 - bit] = Abc_ObjIsComplement((*wbits[wi])[bit]) ? '1' : '0';
            }
          }
          row.push_back(std::move(v));
        }
        ans.values.push_back(std::move(row));
      }
      if (!p.store.empty() && (p.reuse & kReuseCex)) {  // the trace, for replay on the next version
        std::string tr;
        Abc_Obj_t*  tr_pi;
        int         k2;
        for (int f = 0; f <= cex->iFrame; ++f) {
          Abc_NtkForEachPi(ntk, tr_pi, k2) {
            if (is_1(cex->nRegs + f * cex->nPis + k2)) {
              tr += esc_name(Abc_ObjName(tr_pi)) + " ";
            }
          }
          tr += "\n";
        }
        write_file(p.store + "/cex.txt", tr);
      }
      if (care != nullptr) {
        Abc_CexFree(care);
      }
      if (aig != nullptr) {
        Aig_ManStop(aig);
      }
    }
  } else if (rv == 1 || (rv == -1 && pars.iFrame + 1 >= m.frames)) {
    ans.frame = kSeqClean;
  }
  // 4. PDR: an all-cycles proof, seeded with the surviving old invariant.
  if (dbg) {
    std::fprintf(stderr, "reuse[%zu]: bmc3 frame=%d\n", pix, ans.frame);
  }
  if (lm && p.pdr && ans.frame == kSeqClean) {
    std::vector<Cube> inv = seed;
    if (pdr_prove(*lm, inv, m.pdr_s)) {
      ans.unbounded = true;
      ans.how       = seed.empty() ? "pdr" : "pdr+inv";
      if (!dir.empty()) {
        write_file(dir + "/inv.txt", inv_text(inv, *lm));
      }
    }
  }
  return finish(ans);
}

std::string pack(const Seq_answer& a) {
  std::string s = std::to_string(a.frame) + " " + (a.exact ? "1" : "0") + " " + std::to_string(a.values.size()) + " "
                  + (a.unbounded ? "1" : "0") + " " + (a.how.empty() ? std::string{"-"} : a.how) + "\n";
  for (const auto& row : a.values) {
    for (const auto& v : row) {
      s += v + " ";
    }
    s += "\n";
  }
  return s;
}

bool unpack(std::string_view s, size_t nwatch, Seq_answer& a) {
  std::istringstream in{std::string{s}};
  int                ex = 0, ub = 0;
  size_t             nrow = 0;
  if (!(in >> a.frame >> ex >> nrow >> ub >> a.how)) {
    return false;
  }
  a.exact     = ex != 0;
  a.unbounded = ub != 0;
  if (a.how == "-") {
    a.how.clear();
  }
  a.values.assign(nrow, std::vector<std::string>(nwatch));
  for (auto& row : a.values) {
    for (auto& v : row) {
      if (!(in >> v)) {
        return false;
      }
    }
  }
  return true;
}
}  // namespace

std::vector<Seq_answer> abc_seq_bmc(const Seq_model& m, int64_t deadline_ms) {
  std::vector<Seq_answer> out(m.props.size());
  if (m.props.empty() || m.frames <= 0) {
    return out;
  }
  ensure_abc_started();
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
  if (pid == 0) {  // child: ABC has no wall clock of its own
    close(fds[0]);
    if (int devnull = open("/dev/null", O_WRONLY); devnull >= 0) {
      dup2(devnull, STDOUT_FILENO);
      close(devnull);
    }
    ensure_abc_started();
    const int tsec = deadline_ms > 0 ? static_cast<int>(std::max<int64_t>(1, deadline_ms / 1000)) : 0;
    for (size_t i = 0; i < m.props.size(); ++i) {
      const std::string body = pack(seq_bmc_one(m, i, tsec));
      unsigned char     hdr[8];
      put_u32(hdr, static_cast<uint32_t>(i));
      put_u32(hdr + 4, static_cast<uint32_t>(body.size()));
      if (write(fds[1], hdr, sizeof hdr) != static_cast<ssize_t>(sizeof hdr)
          || write(fds[1], body.data(), body.size()) != static_cast<ssize_t>(body.size())) {
        break;
      }
    }
    close(fds[1]);
    _exit(0);
  }
  close(fds[1]);
  const auto  t0 = std::chrono::steady_clock::now();
  std::string buf;
  bool        expired = false;
  for (;;) {
    while (buf.size() >= 8) {  // complete records
      const auto idx = get_u32(reinterpret_cast<const unsigned char*>(buf.data()));
      const auto len = get_u32(reinterpret_cast<const unsigned char*>(buf.data()) + 4);
      if (buf.size() < 8 + static_cast<size_t>(len)) {
        break;
      }
      Seq_answer a;
      if (idx < out.size() && unpack(std::string_view{buf}.substr(8, len), m.watch.size(), a)) {
        out[idx] = std::move(a);
      }
      buf.erase(0, 8 + static_cast<size_t>(len));
    }
    int wait_ms = -1;
    if (deadline_ms > 0) {
      const auto left = deadline_ms
                        - std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
      if (left <= 0) {
        expired = true;
        break;
      }
      wait_ms = static_cast<int>(left);
    }
    pollfd    pf{fds[0], POLLIN, 0};
    const int pr = poll(&pf, 1, wait_ms);
    if (pr == 0) {
      continue;
    }
    if (pr < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    char          chunk[4096];
    const ssize_t n = read(fds[0], chunk, sizeof chunk);
    if (n <= 0) {
      break;
    }
    buf.append(chunk, static_cast<size_t>(n));
  }
  close(fds[0]);
  if (expired) {
    kill(pid, SIGKILL);
  }
  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
  }
  return out;
}

}  // namespace livehd::lec
