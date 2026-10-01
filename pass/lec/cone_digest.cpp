// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
// Stable cone identity and verdict spelling do not require the ABC accelerator.
#include <cstdio>
#include <utility>

#include "cone_abc.hpp"
#include "hash_util.hpp"

namespace livehd::lec {
// ---- canonical cone digest --------------------------------------------------
namespace {

using livehd::hash_util::combine64;
using livehd::hash_util::mix64;

uint64_t hstr(uint64_t h, std::string_view s) { return mix64(livehd::hash_util::fnv1a64(s, h)); }

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
    case cvc5::Kind::CONST_BOOLEAN  : p += t.getBooleanValue() ? "|b:1" : "|b:0"; break;
    default                         : break;
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
    uint64_t a = hstr(livehd::hash_util::kFnv1a64_offset, pay);
    uint64_t b = hstr(0x9ae16a3b2f90404fULL, pay);
    for (size_t i = 0; i < entry.first.getNumChildren(); ++i) {
      const auto it = memo.find(entry.first[i]);
      if (it == memo.end()) {
        return {};  // cannot happen (post-order), but never hash a partial DAG
      }
      a = combine64(a, it->second.first);
      b = combine64(b, it->second.second ^ 0x5851f42d4c957f2dULL);
    }
    memo.emplace(entry.first, std::make_pair(a, b));
  }
  const auto it = memo.find(t);
  if (it == memo.end()) {
    return {};
  }
  char buf[33];
  std::snprintf(buf,
                sizeof buf,
                "%016llx%016llx",
                static_cast<unsigned long long>(it->second.first),
                static_cast<unsigned long long>(it->second.second));
  return std::string{buf, 32};
}

std::string_view cone_verdict_name(Cone_verdict v) {
  switch (v) {
    case Cone_verdict::Proven     : return "PROVEN";
    case Cone_verdict::Refuted    : return "DIFF";
    case Cone_verdict::Unsupported: return "unsupported";
    default                       : return "unknown";
  }
}

}  // namespace livehd::lec
