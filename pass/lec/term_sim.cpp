// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "term_sim.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <tuple>

namespace livehd::lec {

namespace {

using u64 = uint64_t;

inline uint32_t nwords(uint32_t w) { return w == 0 ? 1 : (w + 63) / 64; }
inline u64      top_mask(uint32_t w) {
  const uint32_t r = w & 63;
  return r == 0 ? ~u64{0} : ((u64{1} << r) - 1);
}
inline void clear_top(u64* d, uint32_t w) { d[nwords(w) - 1] &= top_mask(w); }
inline bool get_bit(const u64* a, uint32_t i) { return ((a[i >> 6] >> (i & 63)) & 1) != 0; }
inline void set_bit(u64* a, uint32_t i) { a[i >> 6] |= u64{1} << (i & 63); }
inline void zero(u64* d, uint32_t nw) { std::memset(d, 0, sizeof(u64) * nw); }
inline void copy(u64* d, const u64* s, uint32_t nw) { std::memcpy(d, s, sizeof(u64) * nw); }
inline bool is_zero(const u64* a, uint32_t nw) {
  for (uint32_t i = 0; i < nw; ++i) {
    if (a[i] != 0) {
      return false;
    }
  }
  return true;
}
inline bool equal(const u64* a, const u64* b, uint32_t nw) { return std::memcmp(a, b, sizeof(u64) * nw) == 0; }
inline bool ult(const u64* a, const u64* b, uint32_t nw) {
  for (uint32_t i = nw; i-- > 0;) {
    if (a[i] != b[i]) {
      return a[i] < b[i];
    }
  }
  return false;
}
inline bool slt(const u64* a, const u64* b, uint32_t w) {
  const bool sa = get_bit(a, w - 1);
  const bool sb = get_bit(b, w - 1);
  if (sa != sb) {
    return sa;
  }
  return ult(a, b, nwords(w));
}
// dst bits [doff, doff+n) |= src bits [soff, soff+n); the dst range is zero.
void or_bits(u64* dst, uint32_t doff, const u64* src, uint32_t soff, uint32_t n) {
  while (n > 0) {
    const uint32_t sb   = soff & 63;
    const uint32_t db   = doff & 63;
    const uint32_t take = std::min({n, 64 - sb, 64 - db});
    u64            v    = src[soff >> 6] >> sb;
    if (take < 64) {
      v &= (u64{1} << take) - 1;
    }
    dst[doff >> 6] |= v << db;
    soff           += take;
    doff           += take;
    n              -= take;
  }
}
void add(u64* d, const u64* a, const u64* b, uint32_t nw) {
  u64 carry = 0;
  for (uint32_t i = 0; i < nw; ++i) {
    const u64 s  = a[i] + b[i];
    const u64 c1 = s < a[i] ? 1 : 0;
    d[i]         = s + carry;
    carry        = c1 | (d[i] < s ? 1 : 0);
  }
}
void sub(u64* d, const u64* a, const u64* b, uint32_t nw) {
  u64 borrow = 0;
  for (uint32_t i = 0; i < nw; ++i) {
    const u64 x  = a[i] - b[i];
    const u64 b1 = a[i] < b[i] ? 1 : 0;
    d[i]         = x - borrow;
    borrow       = b1 | (x < borrow ? 1 : 0);
  }
}
void neg(u64* d, const u64* a, uint32_t nw) {
  u64 carry = 1;
  for (uint32_t i = 0; i < nw; ++i) {
    const u64 x = ~a[i];
    d[i]        = x + carry;
    carry       = (carry != 0 && d[i] == 0) ? 1 : 0;
  }
}
// d = a * b mod 2^(64*nw); d must not alias a or b.
void mul(u64* d, const u64* a, const u64* b, uint32_t nw) {
  zero(d, nw);
  for (uint32_t i = 0; i < nw; ++i) {
    if (a[i] == 0) {
      continue;
    }
    u64 carry = 0;
    for (uint32_t j = 0; i + j < nw; ++j) {
      const unsigned __int128 t = static_cast<unsigned __int128>(a[i]) * b[j] + d[i + j] + carry;
      d[i + j]                  = static_cast<u64>(t);
      carry                     = static_cast<u64>(t >> 64);
    }
  }
}
// SMT-LIB bvudiv/bvurem: x / 0 = all ones, x % 0 = x. q/r may be null.
void udivrem(u64* q, u64* r, const u64* a, const u64* b, uint32_t w) {
  const uint32_t nw = nwords(w);
  if (is_zero(b, nw)) {
    if (q != nullptr) {
      for (uint32_t i = 0; i < nw; ++i) {
        q[i] = ~u64{0};
      }
      clear_top(q, w);
    }
    if (r != nullptr) {
      copy(r, a, nw);
    }
    return;
  }
  if (nw == 1) {
    if (q != nullptr) {
      q[0] = a[0] / b[0];
    }
    if (r != nullptr) {
      r[0] = a[0] % b[0];
    }
    return;
  }
  if (nw == 2) {
    const auto A = (static_cast<unsigned __int128>(a[1]) << 64) | a[0];
    const auto B = (static_cast<unsigned __int128>(b[1]) << 64) | b[0];
    if (q != nullptr) {
      const auto Q = A / B;
      q[0]         = static_cast<u64>(Q);
      q[1]         = static_cast<u64>(Q >> 64);
    }
    if (r != nullptr) {
      const auto R = A % B;
      r[0]         = static_cast<u64>(R);
      r[1]         = static_cast<u64>(R >> 64);
    }
    return;
  }
  // Restoring long division, one bit at a time (wide operands are rare).
  std::vector<u64> rem(nw + 1, 0), quo(nw, 0), bb(nw + 1, 0), tmp(nw + 1, 0);
  copy(bb.data(), b, nw);
  for (uint32_t i = w; i-- > 0;) {
    for (uint32_t k = nw + 1; k-- > 0;) {
      rem[k] = (rem[k] << 1) | (k > 0 ? rem[k - 1] >> 63 : 0);
    }
    rem[0] |= get_bit(a, i) ? 1 : 0;
    if (!ult(rem.data(), bb.data(), nw + 1)) {
      sub(tmp.data(), rem.data(), bb.data(), nw + 1);
      rem.swap(tmp);
      set_bit(quo.data(), i);
    }
  }
  if (q != nullptr) {
    copy(q, quo.data(), nw);
  }
  if (r != nullptr) {
    copy(r, rem.data(), nw);
  }
}
// Shift amount: min(value(b), w).
uint32_t shift_amount(const u64* b, uint32_t w) {
  const uint32_t nw = nwords(w);
  for (uint32_t i = 1; i < nw; ++i) {
    if (b[i] != 0) {
      return w;
    }
  }
  return b[0] >= w ? w : static_cast<uint32_t>(b[0]);
}
void shl(u64* d, const u64* a, uint32_t s, uint32_t w) {
  const uint32_t nw = nwords(w);
  if (s >= w) {
    zero(d, nw);
    return;
  }
  const uint32_t ws = s >> 6;
  const uint32_t bs = s & 63;
  for (uint32_t i = nw; i-- > 0;) {
    u64 v = 0;
    if (i >= ws) {
      v = a[i - ws] << bs;
      if (bs != 0 && i >= ws + 1) {
        v |= a[i - ws - 1] >> (64 - bs);
      }
    }
    d[i] = v;
  }
  clear_top(d, w);
}
void lshr(u64* d, const u64* a, uint32_t s, uint32_t w) {
  const uint32_t nw = nwords(w);
  if (s >= w) {
    zero(d, nw);
    return;
  }
  const uint32_t ws = s >> 6;
  const uint32_t bs = s & 63;
  for (uint32_t i = 0; i < nw; ++i) {
    u64 v = 0;
    if (i + ws < nw) {
      v = a[i + ws] >> bs;
      if (bs != 0 && i + ws + 1 < nw) {
        v |= a[i + ws + 1] << (64 - bs);
      }
    }
    d[i] = v;
  }
}
void ashr(u64* d, const u64* a, uint32_t s, uint32_t w) {
  const bool sign = get_bit(a, w - 1);
  lshr(d, a, s, w);
  if (sign) {
    for (uint32_t i = s >= w ? 0 : w - s; i < w; ++i) {
      set_bit(d, i);
    }
  }
}
u64 splitmix(u64 x) {
  x += 0x9e3779b97f4a7c15ULL;
  x  = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x  = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
void parse_hex(const std::string& s, u64* d, uint32_t nw) {
  zero(d, nw);
  uint32_t bit = 0;
  for (size_t i = s.size(); i-- > 0;) {
    const char c = s[i];
    u64        v = 0;
    if (c >= '0' && c <= '9') {
      v = static_cast<u64>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      v = static_cast<u64>(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
      v = static_cast<u64>(c - 'A' + 10);
    }
    if ((bit >> 6) < nw) {
      d[bit >> 6] |= v << (bit & 63);
    }
    bit += 4;
  }
}
std::string to_hex(const u64* a, uint32_t w) {
  static constexpr char digits[] = "0123456789abcdef";
  const uint32_t        n        = (w + 3) / 4;
  std::string           s(n, '0');
  for (uint32_t k = 0; k < n; ++k) {
    const uint32_t bit = 4 * k;
    u64            v   = a[bit >> 6] >> (bit & 63);
    if ((bit & 63) > 60 && (bit >> 6) + 1 < nwords(w)) {
      v |= a[(bit >> 6) + 1] << (64 - (bit & 63));
    }
    s[n - 1 - k] = digits[v & 0xF];
  }
  return s;
}

}  // namespace

int Term_sim::leaf_index(const cvc5::Term& t) const {
  const auto it = leaf_of_.find(t.getId());
  return it == leaf_of_.end() ? -1 : it->second;
}

uint64_t* Term_sim::leaf_words(size_t leaf) { return &words_[nodes_[leaves_[leaf].node].off]; }
uint32_t  Term_sim::leaf_nwords(size_t leaf) const { return nodes_[leaves_[leaf].node].nw; }
void      Term_sim::set_array_seed(size_t leaf, uint64_t seed) { leaf_seed_[leaf] = seed; }

void Term_sim::set_leaf_value(size_t leaf, const cvc5::Term& value) {
  const auto& n = nodes_[leaves_[leaf].node];
  if (n.sort == kBool) {
    words_[n.off] = value.getBooleanValue() ? 1 : 0;
  } else if (n.sort == kBv) {
    parse_hex(value.getBitVectorValue(16), &words_[n.off], n.nw);
  }
}

bool Term_sim::build(const std::vector<cvc5::Term>& assertions, const std::vector<cvc5::Term>& roots, std::string& why) {
  std::vector<cvc5::Term> constraints;
  for (const auto& a : assertions) {
    if (a.getKind() == cvc5::Kind::EQUAL && a.getNumChildren() == 2 && a[0].getKind() == cvc5::Kind::CONSTANT
        && !defs_.contains(a[0].getId())) {
      defs_.emplace(a[0].getId(), a[1]);
      continue;
    }
    constraints.push_back(a);
  }
  for (const auto& r : roots) {
    if (!add_node(r, why)) {
      return false;
    }
    root_nodes_.push_back(node_of_.at(r.getId()));
  }
  for (const auto& c : constraints) {
    if (!c.getSort().isBoolean()) {
      why = "non-Boolean assertion";
      return false;
    }
    if (!add_node(c, why)) {
      return false;
    }
    constraint_nodes_.push_back(node_of_.at(c.getId()));
  }
  arr_ref_.assign(arr_slots_, 0);
  leaf_seed_.assign(leaves_.size(), 0);
  built_.assign(nodes_.size(), cvc5::Term{});
  built_gen_.assign(nodes_.size(), 0);
  return true;
}

bool Term_sim::add_node(const cvc5::Term& root, std::string& why) {
  constexpr uint32_t kBusy = UINT32_MAX;
  if (node_of_.contains(root.getId())) {
    return true;
  }
  // Iterative post-order: a node is created once all its operands exist. A
  // defined symbol's single operand is its definition.
  struct Frame {
    cvc5::Term t;
    bool       expanded;
  };
  std::vector<Frame> stk{
      {root, false}
  };
  auto operands = [&](const cvc5::Term& t) {
    std::vector<cvc5::Term> ops;
    if (t.getKind() == cvc5::Kind::CONSTANT) {
      if (auto it = defs_.find(t.getId()); it != defs_.end()) {
        ops.push_back(it->second);
      }
      return ops;
    }
    if (t.getKind() == cvc5::Kind::CONST_ARRAY) {
      ops.push_back(t.getConstArrayBase());
      return ops;
    }
    ops.reserve(t.getNumChildren());
    for (size_t i = 0; i < t.getNumChildren(); ++i) {
      ops.push_back(t[i]);
    }
    return ops;
  };
  while (!stk.empty()) {
    auto& fr = stk.back();
    if (!fr.expanded) {
      if (node_of_.contains(fr.t.getId())) {
        stk.pop_back();
        continue;
      }
      fr.expanded = true;
      node_of_.emplace(fr.t.getId(), kBusy);
      const auto ops = operands(fr.t);
      for (size_t i = ops.size(); i-- > 0;) {
        auto it = node_of_.find(ops[i].getId());
        if (it == node_of_.end()) {
          stk.push_back({ops[i], false});
        } else if (it->second == kBusy) {
          why = "cyclic definition";
          return false;
        }
      }
      continue;
    }
    const cvc5::Term t = fr.t;
    stk.pop_back();
    const auto ops  = operands(t);
    const auto sort = t.getSort();
    Node       n;
    if (sort.isBoolean()) {
      n.sort  = kBool;
      n.width = 1;
    } else if (sort.isBitVector()) {
      n.sort  = kBv;
      n.width = sort.getBitVectorSize();
    } else if (sort.isArray() && sort.getArrayIndexSort().isBitVector() && sort.getArrayElementSort().isBitVector()) {
      n.sort      = kArray;
      n.width     = sort.getArrayElementSort().getBitVectorSize();
      n.p0        = sort.getArrayIndexSort().getBitVectorSize();
      has_arrays_ = true;
    } else {
      why = "unsupported sort " + sort.toString();
      return false;
    }
    n.c0 = static_cast<uint32_t>(kids_.size());
    n.nc = static_cast<uint32_t>(ops.size());
    for (const auto& o : ops) {
      const auto it = node_of_.find(o.getId());
      if (it == node_of_.end() || it->second == kBusy) {
        why = "cyclic definition";
        return false;
      }
      kids_.push_back(it->second);
    }
    auto child_sort = [&](uint32_t i) { return nodes_[kids_[n.c0 + i]].sort; };
    auto all_sort   = [&](uint8_t s) {
      for (uint32_t i = 0; i < n.nc; ++i) {
        if (child_sort(i) != s) {
          return false;
        }
      }
      return true;
    };
    using K      = cvc5::Kind;
    const K kind = t.getKind();
    bool    ok   = true;
    switch (kind) {
      case K::CONSTANT:
        if (ops.empty()) {
          n.op   = Op::Leaf;
          n.leaf = static_cast<int32_t>(leaves_.size());
        } else {
          n.op = Op::Alias;
          ok   = nodes_[kids_[n.c0]].sort == n.sort && nodes_[kids_[n.c0]].width == n.width;
        }
        break;
      case K::CONST_BOOLEAN:
      case K::CONST_BITVECTOR: n.op = Op::Const; break;
      case K::CONST_ARRAY    : n.op = Op::ConstArray; break;
      case K::NOT:
        n.op = Op::Not;
        ok   = n.nc == 1 && all_sort(kBool);
        break;
      case K::AND:
        n.op = Op::And;
        ok   = all_sort(kBool);
        break;
      case K::OR:
        n.op = Op::Or;
        ok   = all_sort(kBool);
        break;
      case K::XOR:
        n.op = Op::Xor;
        ok   = all_sort(kBool);
        break;
      case K::IMPLIES:
        n.op = Op::Implies;
        ok   = n.nc == 2 && all_sort(kBool);
        break;
      case K::EQUAL:
        n.op = Op::Eq;
        ok   = n.nc >= 2 && (all_sort(kBool) || all_sort(kBv));
        break;
      case K::DISTINCT:
        n.op = Op::Distinct;
        ok   = n.nc >= 2 && (all_sort(kBool) || all_sort(kBv));
        break;
      case K::ITE:
        n.op = Op::Ite;
        ok   = n.nc == 3 && child_sort(0) == kBool;
        break;
      case K::BITVECTOR_NOT   : n.op = Op::BvNot; break;
      case K::BITVECTOR_AND   : n.op = Op::BvAnd; break;
      case K::BITVECTOR_OR    : n.op = Op::BvOr; break;
      case K::BITVECTOR_XOR   : n.op = Op::BvXor; break;
      case K::BITVECTOR_NAND  : n.op = Op::BvNand; break;
      case K::BITVECTOR_NOR   : n.op = Op::BvNor; break;
      case K::BITVECTOR_XNOR  : n.op = Op::BvXnor; break;
      case K::BITVECTOR_COMP  : n.op = Op::BvComp; break;
      case K::BITVECTOR_NEG   : n.op = Op::BvNeg; break;
      case K::BITVECTOR_ADD   : n.op = Op::BvAdd; break;
      case K::BITVECTOR_SUB   : n.op = Op::BvSub; break;
      case K::BITVECTOR_MULT  : n.op = Op::BvMul; break;
      case K::BITVECTOR_UDIV  : n.op = Op::BvUdiv; break;
      case K::BITVECTOR_UREM  : n.op = Op::BvUrem; break;
      case K::BITVECTOR_SDIV  : n.op = Op::BvSdiv; break;
      case K::BITVECTOR_SREM  : n.op = Op::BvSrem; break;
      case K::BITVECTOR_SMOD  : n.op = Op::BvSmod; break;
      case K::BITVECTOR_SHL   : n.op = Op::BvShl; break;
      case K::BITVECTOR_LSHR  : n.op = Op::BvLshr; break;
      case K::BITVECTOR_ASHR  : n.op = Op::BvAshr; break;
      case K::BITVECTOR_ULT   : n.op = Op::BvUlt; break;
      case K::BITVECTOR_ULE   : n.op = Op::BvUle; break;
      case K::BITVECTOR_UGT   : n.op = Op::BvUgt; break;
      case K::BITVECTOR_UGE   : n.op = Op::BvUge; break;
      case K::BITVECTOR_SLT   : n.op = Op::BvSlt; break;
      case K::BITVECTOR_SLE   : n.op = Op::BvSle; break;
      case K::BITVECTOR_SGT   : n.op = Op::BvSgt; break;
      case K::BITVECTOR_SGE   : n.op = Op::BvSge; break;
      case K::BITVECTOR_ULTBV : n.op = Op::BvUltbv; break;
      case K::BITVECTOR_SLTBV : n.op = Op::BvSltbv; break;
      case K::BITVECTOR_REDOR : n.op = Op::BvRedor; break;
      case K::BITVECTOR_REDAND: n.op = Op::BvRedand; break;
      case K::BITVECTOR_EXTRACT:
        n.op = Op::Extract;
        n.p0 = t.getOp()[0].getUInt32Value();
        n.p1 = t.getOp()[1].getUInt32Value();
        break;
      case K::BITVECTOR_CONCAT: n.op = Op::Concat; break;
      case K::BITVECTOR_ZERO_EXTEND:
        n.op = Op::Zext;
        n.p0 = t.getOp()[0].getUInt32Value();
        break;
      case K::BITVECTOR_SIGN_EXTEND:
        n.op = Op::Sext;
        n.p0 = t.getOp()[0].getUInt32Value();
        break;
      case K::BITVECTOR_REPEAT:
        n.op = Op::Repeat;
        n.p0 = t.getOp()[0].getUInt32Value();
        break;
      case K::BITVECTOR_ROTATE_LEFT:
        n.op = Op::Rotl;
        n.p0 = t.getOp()[0].getUInt32Value();
        break;
      case K::BITVECTOR_ROTATE_RIGHT:
        n.op = Op::Rotr;
        n.p0 = t.getOp()[0].getUInt32Value();
        break;
      case K::SELECT:
        n.op = Op::Select;
        ok   = n.nc == 2 && child_sort(0) == kArray && child_sort(1) == kBv;
        break;
      case K::STORE:
        n.op = Op::Store;
        ok   = n.nc == 3 && child_sort(0) == kArray && child_sort(1) == kBv && child_sort(2) == kBv;
        break;
      default: why = "unsupported term kind " + std::to_string(kind); return false;
    }
    if (ok && n.op != Op::Leaf && n.op != Op::Alias && n.op != Op::Const && n.op != Op::ConstArray && n.nc == 0) {
      ok = false;
    }
    if (ok && n.op >= Op::BvNot && n.op <= Op::Rotr) {
      ok = all_sort(kBv);
    }
    if (!ok) {
      why = "unsupported operand shape for " + std::to_string(kind);
      return false;
    }
    if (n.sort == kArray) {
      n.off = arr_slots_++;
    } else {
      n.nw  = nwords(n.width);
      n.off = static_cast<uint32_t>(words_.size());
      words_.resize(words_.size() + n.nw, 0);
      if (n.op == Op::Const) {
        if (n.sort == kBool) {
          words_[n.off] = t.getBooleanValue() ? 1 : 0;
        } else {
          parse_hex(t.getBitVectorValue(16), &words_[n.off], n.nw);
        }
      }
    }
    const auto id = static_cast<uint32_t>(nodes_.size());
    if (n.op == Op::Leaf) {
      Leaf l;
      l.term     = t;
      l.node     = id;
      l.width    = n.width;
      l.is_bool  = n.sort == kBool;
      l.is_array = n.sort == kArray;
      if (l.is_array) {
        l.index_width = n.p0;
      }
      leaf_of_.emplace(t.getId(), static_cast<int>(leaves_.size()));
      leaves_.push_back(std::move(l));
    }
    nodes_.push_back(n);
    orig_.push_back(t);
    node_of_[t.getId()] = id;
  }
  return true;
}

void Term_sim::select(const Node& n, uint32_t arr, const uint64_t* idx, uint64_t* out) {
  const uint32_t inw = nwords(n.p0);  // the array's index width rides p0 of the array node; see eval_node
  const uint32_t enw = nwords(n.width);
  uint32_t       r   = arr;
  while (true) {
    const Arr& a = arrs_[r];
    if (a.kind == 2) {
      if (equal(&words_[a.idx], idx, inw)) {
        copy(out, &words_[a.val], enw);
        return;
      }
      r = a.parent;
      continue;
    }
    if (a.kind == 1) {
      copy(out, &words_[a.val], enw);
      return;
    }
    // A free array: deterministic pseudo-random content per (seed, index).
    u64 h = leaf_seed_[a.leaf] ^ 0x5851f42d4c957f2dULL;
    for (uint32_t i = 0; i < inw; ++i) {
      h = splitmix(h ^ idx[i]);
    }
    for (uint32_t i = 0; i < enw; ++i) {
      out[i] = splitmix(h + i);
    }
    clear_top(out, n.width);
    if (record_) {
      reads_.push_back(Read{a.leaf, std::vector<u64>(idx, idx + inw), std::vector<u64>(out, out + enw)});
    }
    return;
  }
}

void Term_sim::eval_node(uint32_t id) {
  const Node& n   = nodes_[id];
  const auto  kid = [&](uint32_t i) -> const Node& { return nodes_[kids_[n.c0 + i]]; };
  const auto  val = [&](uint32_t i) -> const u64* { return &words_[nodes_[kids_[n.c0 + i]].off]; };
  u64*        d   = n.sort == kArray ? nullptr : &words_[n.off];
  switch (n.op) {
    case Op::Leaf:
      if (n.sort == kArray) {
        arr_ref_[n.off] = static_cast<uint32_t>(arrs_.size());
        arrs_.push_back(Arr{0, 0, static_cast<uint32_t>(n.leaf), 0, 0});
      } else {
        clear_top(d, n.width);
      }
      return;
    case Op::Const: return;
    case Op::ConstArray:
      arr_ref_[n.off] = static_cast<uint32_t>(arrs_.size());
      arrs_.push_back(Arr{1, 0, 0, 0, kid(0).off});
      return;
    case Op::Alias:
      if (n.sort == kArray) {
        arr_ref_[n.off] = arr_ref_[kid(0).off];
      } else {
        copy(d, val(0), n.nw);
      }
      return;
    case Op::Ite: {
      const bool c = val(0)[0] != 0;
      if (n.sort == kArray) {
        arr_ref_[n.off] = arr_ref_[kid(c ? 1 : 2).off];
      } else {
        copy(d, val(c ? 1 : 2), n.nw);
      }
      return;
    }
    case Op::Not: d[0] = val(0)[0] == 0 ? 1 : 0; return;
    case Op::And: {
      u64 r = 1;
      for (uint32_t i = 0; i < n.nc && r != 0; ++i) {
        r = val(i)[0];
      }
      d[0] = r != 0 ? 1 : 0;
      return;
    }
    case Op::Or: {
      u64 r = 0;
      for (uint32_t i = 0; i < n.nc && r == 0; ++i) {
        r = val(i)[0];
      }
      d[0] = r != 0 ? 1 : 0;
      return;
    }
    case Op::Xor: {
      u64 r = 0;
      for (uint32_t i = 0; i < n.nc; ++i) {
        r ^= val(i)[0] != 0 ? 1 : 0;
      }
      d[0] = r;
      return;
    }
    case Op::Implies: d[0] = (val(0)[0] == 0 || val(1)[0] != 0) ? 1 : 0; return;
    case Op::Eq     : {
      const uint32_t nw = kid(0).nw;
      bool           r  = true;
      for (uint32_t i = 1; i < n.nc && r; ++i) {
        r = equal(val(0), val(i), nw);
      }
      d[0] = r ? 1 : 0;
      return;
    }
    case Op::Distinct: {
      const uint32_t nw = kid(0).nw;
      bool           r  = true;
      for (uint32_t i = 0; i < n.nc && r; ++i) {
        for (uint32_t j = i + 1; j < n.nc && r; ++j) {
          r = !equal(val(i), val(j), nw);
        }
      }
      d[0] = r ? 1 : 0;
      return;
    }
    case Op::BvNot:
      for (uint32_t i = 0; i < n.nw; ++i) {
        d[i] = ~val(0)[i];
      }
      clear_top(d, n.width);
      return;
    case Op::BvAnd:
    case Op::BvOr:
    case Op::BvXor: {
      copy(d, val(0), n.nw);
      for (uint32_t k = 1; k < n.nc; ++k) {
        const u64* b = val(k);
        for (uint32_t i = 0; i < n.nw; ++i) {
          d[i] = n.op == Op::BvAnd ? (d[i] & b[i]) : n.op == Op::BvOr ? (d[i] | b[i]) : (d[i] ^ b[i]);
        }
      }
      return;
    }
    case Op::BvNand:
    case Op::BvNor:
    case Op::BvXnor: {
      const u64* a = val(0);
      const u64* b = val(1);
      for (uint32_t i = 0; i < n.nw; ++i) {
        d[i] = n.op == Op::BvNand ? ~(a[i] & b[i]) : n.op == Op::BvNor ? ~(a[i] | b[i]) : ~(a[i] ^ b[i]);
      }
      clear_top(d, n.width);
      return;
    }
    case Op::BvComp: d[0] = equal(val(0), val(1), kid(0).nw) ? 1 : 0; return;
    case Op::BvNeg:
      neg(d, val(0), n.nw);
      clear_top(d, n.width);
      return;
    case Op::BvAdd:
      if (n.nw == 1) {
        u64 s = 0;
        for (uint32_t k = 0; k < n.nc; ++k) {
          s += val(k)[0];
        }
        d[0] = s & top_mask(n.width);
        return;
      }
      copy(d, val(0), n.nw);
      for (uint32_t k = 1; k < n.nc; ++k) {
        add(d, d, val(k), n.nw);
      }
      clear_top(d, n.width);
      return;
    case Op::BvSub:
      if (n.nw == 1) {
        u64 s = val(0)[0];
        for (uint32_t k = 1; k < n.nc; ++k) {
          s -= val(k)[0];
        }
        d[0] = s & top_mask(n.width);
        return;
      }
      copy(d, val(0), n.nw);
      for (uint32_t k = 1; k < n.nc; ++k) {
        sub(d, d, val(k), n.nw);
      }
      clear_top(d, n.width);
      return;
    case Op::BvMul:
      if (n.nw == 1) {
        u64 s = val(0)[0];
        for (uint32_t k = 1; k < n.nc; ++k) {
          s *= val(k)[0];
        }
        d[0] = s & top_mask(n.width);
        return;
      }
      copy(d, val(0), n.nw);
      scratch_.resize(n.nw);
      for (uint32_t k = 1; k < n.nc; ++k) {
        mul(scratch_.data(), d, val(k), n.nw);
        copy(d, scratch_.data(), n.nw);
      }
      clear_top(d, n.width);
      return;
    case Op::BvUdiv: udivrem(d, nullptr, val(0), val(1), n.width); return;
    case Op::BvUrem: udivrem(nullptr, d, val(0), val(1), n.width); return;
    case Op::BvSdiv:
    case Op::BvSrem:
    case Op::BvSmod: {
      const uint32_t   w  = n.width;
      const uint32_t   nw = n.nw;
      const u64*       s  = val(0);
      const u64*       t  = val(1);
      const bool       ns = get_bit(s, w - 1);
      const bool       nt = get_bit(t, w - 1);
      std::vector<u64> as(nw), at(nw), r(nw);
      if (ns) {
        neg(as.data(), s, nw);
        clear_top(as.data(), w);
      } else {
        copy(as.data(), s, nw);
      }
      if (nt) {
        neg(at.data(), t, nw);
        clear_top(at.data(), w);
      } else {
        copy(at.data(), t, nw);
      }
      if (n.op == Op::BvSdiv) {
        udivrem(r.data(), nullptr, as.data(), at.data(), w);
        if (ns != nt) {
          neg(d, r.data(), nw);
          clear_top(d, w);
        } else {
          copy(d, r.data(), nw);
        }
        return;
      }
      udivrem(nullptr, r.data(), as.data(), at.data(), w);
      if (n.op == Op::BvSrem) {
        if (ns) {
          neg(d, r.data(), nw);
          clear_top(d, w);
        } else {
          copy(d, r.data(), nw);
        }
        return;
      }
      // bvsmod: the sign follows the divisor.
      if (is_zero(r.data(), nw) || (!ns && !nt)) {
        copy(d, r.data(), nw);
      } else if (ns && !nt) {
        sub(d, t, r.data(), nw);  // -u + t
      } else if (!ns && nt) {
        add(d, r.data(), t, nw);  // u + t
      } else {
        neg(d, r.data(), nw);  // -u
      }
      clear_top(d, w);
      return;
    }
    case Op::BvShl:
    case Op::BvLshr:
    case Op::BvAshr: {
      const uint32_t s = shift_amount(val(1), n.width);
      if (n.nw == 1) {
        const u64 a = val(0)[0];
        if (n.op == Op::BvShl) {
          d[0] = s >= n.width ? 0 : (a << s) & top_mask(n.width);
        } else if (n.op == Op::BvLshr) {
          d[0] = s >= n.width ? 0 : a >> s;
        } else {
          ashr(d, val(0), s, n.width);
        }
        return;
      }
      if (n.op == Op::BvShl) {
        shl(d, val(0), s, n.width);
      } else if (n.op == Op::BvLshr) {
        lshr(d, val(0), s, n.width);
      } else {
        ashr(d, val(0), s, n.width);
      }
      return;
    }
    case Op::BvUlt   : d[0] = ult(val(0), val(1), kid(0).nw) ? 1 : 0; return;
    case Op::BvUle   : d[0] = !ult(val(1), val(0), kid(0).nw) ? 1 : 0; return;
    case Op::BvUgt   : d[0] = ult(val(1), val(0), kid(0).nw) ? 1 : 0; return;
    case Op::BvUge   : d[0] = !ult(val(0), val(1), kid(0).nw) ? 1 : 0; return;
    case Op::BvSlt   : d[0] = slt(val(0), val(1), kid(0).width) ? 1 : 0; return;
    case Op::BvSle   : d[0] = !slt(val(1), val(0), kid(0).width) ? 1 : 0; return;
    case Op::BvSgt   : d[0] = slt(val(1), val(0), kid(0).width) ? 1 : 0; return;
    case Op::BvSge   : d[0] = !slt(val(0), val(1), kid(0).width) ? 1 : 0; return;
    case Op::BvUltbv : d[0] = ult(val(0), val(1), kid(0).nw) ? 1 : 0; return;
    case Op::BvSltbv : d[0] = slt(val(0), val(1), kid(0).width) ? 1 : 0; return;
    case Op::BvRedor : d[0] = is_zero(val(0), kid(0).nw) ? 0 : 1; return;
    case Op::BvRedand: {
      const Node& a  = kid(0);
      bool        on = true;
      for (uint32_t i = 0; i + 1 < a.nw && on; ++i) {
        on = val(0)[i] == ~u64{0};
      }
      d[0] = on && val(0)[a.nw - 1] == top_mask(a.width) ? 1 : 0;
      return;
    }
    case Op::Extract: {
      const uint32_t len = n.p0 - n.p1 + 1;
      if (n.nw == 1 && kid(0).nw == 1) {
        d[0] = (val(0)[0] >> n.p1) & top_mask(len);
        return;
      }
      zero(d, n.nw);
      or_bits(d, 0, val(0), n.p1, len);
      return;
    }
    case Op::Concat: {
      if (n.nw == 1) {
        u64 r = 0;
        for (uint32_t k = 0; k < n.nc; ++k) {
          const Node& c = kid(k);
          r             = c.width >= 64 ? val(k)[0] : (r << c.width) | val(k)[0];
        }
        d[0] = r & top_mask(n.width);
        return;
      }
      zero(d, n.nw);
      uint32_t off = 0;
      for (uint32_t k = n.nc; k-- > 0;) {
        or_bits(d, off, val(k), 0, kid(k).width);
        off += kid(k).width;
      }
      return;
    }
    case Op::Zext:
      zero(d, n.nw);
      copy(d, val(0), kid(0).nw);
      return;
    case Op::Sext: {
      const Node& a = kid(0);
      zero(d, n.nw);
      copy(d, val(0), a.nw);
      if (get_bit(val(0), a.width - 1)) {
        for (uint32_t i = a.width; i < n.width; ++i) {
          set_bit(d, i);
        }
      }
      return;
    }
    case Op::Repeat: {
      const Node& a = kid(0);
      zero(d, n.nw);
      for (uint32_t k = 0; k < n.p0; ++k) {
        or_bits(d, k * a.width, val(0), 0, a.width);
      }
      return;
    }
    case Op::Rotl:
    case Op::Rotr: {
      const uint32_t w = n.width;
      uint32_t       r = n.p0 % w;
      if (n.op == Op::Rotr) {
        r = (w - r) % w;
      }
      zero(d, n.nw);
      or_bits(d, r, val(0), 0, w - r);  // low w-r bits move up by r
      or_bits(d, 0, val(0), w - r, r);  // the top r bits wrap to the bottom
      return;
    }
    case Op::Select: {
      const Node& a = kid(0);
      Node        sel{};
      sel.p0    = a.p0;
      sel.width = n.width;
      select(sel, arr_ref_[a.off], val(1), d);
      return;
    }
    case Op::Store: {
      arr_ref_[n.off] = static_cast<uint32_t>(arrs_.size());
      arrs_.push_back(Arr{2, arr_ref_[kid(0).off], 0, kid(1).off, kid(2).off});
      return;
    }
  }
}

bool Term_sim::eval(bool record_reads) {
  record_ = record_reads;
  reads_.clear();
  arrs_.clear();
  for (uint32_t i = 0; i < nodes_.size(); ++i) {
    eval_node(i);
  }
  for (const auto c : constraint_nodes_) {
    if (words_[nodes_[c].off] == 0) {
      return false;
    }
  }
  return true;
}

bool Term_sim::root(size_t r) const { return words_[nodes_[root_nodes_[r]].off] != 0; }

cvc5::Term Term_sim::root_value(size_t r) const { return const_of(root_nodes_[r]); }

cvc5::Term Term_sim::const_of(uint32_t id) const {
  const Node& n = nodes_[id];
  if (n.sort == kBool) {
    return tm_.mkBoolean(words_[n.off] != 0);
  }
  if (n.width <= 64) {
    return tm_.mkBitVector(n.width, words_[n.off]);
  }
  return tm_.mkBitVector(n.width, to_hex(&words_[n.off], n.width), 16);
}

cvc5::Term Term_sim::leaf_value(size_t leaf) const { return const_of(leaves_[leaf].node); }

Term_sim::Substitution Term_sim::substitution() const {
  Substitution s;
  for (const auto& l : leaves_) {
    if (!l.is_array) {
      s.from.push_back(l.term);
      s.to.push_back(const_of(l.node));
    }
  }
  for (uint32_t id = 0; id < nodes_.size(); ++id) {
    if (nodes_[id].op == Op::Alias && nodes_[id].sort != kArray) {
      s.from.push_back(orig_[id]);
      s.to.push_back(const_of(id));
    }
  }
  // Tape order puts a memory's definition after the memories it reads, so
  // each inlined definition only refers to free memories.
  for (uint32_t id = 0; id < nodes_.size(); ++id) {
    if (nodes_[id].op == Op::Alias && nodes_[id].sort == kArray) {
      auto inl = orig_[kids_[nodes_[id].c0]].substitute(s.from, s.to);
      s.from.push_back(orig_[id]);
      s.to.push_back(inl);
    }
  }
  for (const auto& r : reads_) {
    const auto& l   = leaves_[r.leaf];
    auto        bvc = [&](const std::vector<u64>& v, uint32_t w) {
      return w <= 64 ? tm_.mkBitVector(w, v[0]) : tm_.mkBitVector(w, to_hex(v.data(), w), 16);
    };
    s.read_from.push_back(tm_.mkTerm(cvc5::Kind::SELECT, {l.term, bvc(r.idx, l.index_width)}));
    s.read_to.push_back(bvc(r.val, l.width));
  }
  return s;
}

std::vector<cvc5::Term> Term_sim::pins(bool with_reads) const {
  std::vector<cvc5::Term> out;
  out.reserve(leaves_.size() + reads_.size());
  for (const auto& l : leaves_) {
    if (!l.is_array) {
      out.push_back(tm_.mkTerm(cvc5::Kind::EQUAL, {l.term, const_of(l.node)}));
    }
  }
  // The defined symbols too (every cycle's state symbol): implied by the
  // leaves, but stating them turns each unrolled definition into a constant
  // check, so the solver confirms the trace by substitution instead of
  // bit-blasting the unrolling (dino: 13.6 s -> well under a second).
  for (uint32_t id = 0; id < nodes_.size(); ++id) {
    if (nodes_[id].op == Op::Alias && nodes_[id].sort != kArray) {
      out.push_back(tm_.mkTerm(cvc5::Kind::EQUAL, {orig_[id], const_of(id)}));
    }
  }
  for (const auto& r : with_reads ? reads_ : std::vector<Read>{}) {
    const auto& l   = leaves_[r.leaf];
    auto        bvc = [&](const std::vector<u64>& v, uint32_t w) {
      return w <= 64 ? tm_.mkBitVector(w, v[0]) : tm_.mkBitVector(w, to_hex(v.data(), w), 16);
    };
    out.push_back(
        tm_.mkTerm(cvc5::Kind::EQUAL, {tm_.mkTerm(cvc5::Kind::SELECT, {l.term, bvc(r.idx, l.index_width)}), bvc(r.val, l.width)}));
  }
  return out;
}

std::vector<Term_sim::Field> Term_sim::fields(const std::vector<int>& leaf_port, size_t max_values) {
  // A constant is any node no leaf reaches (`zext(#b011011)` is as constant
  // as `#b0011011`); one evaluation gives every such node its value.
  std::vector<uint8_t> konst(nodes_.size(), 0);
  for (uint32_t id = 0; id < nodes_.size(); ++id) {
    const Node& n = nodes_[id];
    uint8_t     k = n.op == Op::Const ? 1 : 0;
    if (n.op != Op::Leaf && n.op != Op::Const && n.sort == kBv && n.nc > 0) {
      k = 1;
      for (uint32_t i = 0; i < n.nc && k != 0; ++i) {
        k = konst[kids_[n.c0 + i]];
      }
    }
    konst[id] = k;
  }
  eval();
  // prov(node, bit): the input (port, bit)s the node's bit copies -- a SET,
  // because a mux may route either of two fields (a dual-issue unit steering
  // the low or the high instruction slot into one decoder). Memoized; an
  // in-progress entry reads as empty, which also ends a combinational cycle.
  using Cands                                    = std::vector<int64_t>;
  constexpr size_t                     kMaxCands = 4;
  constexpr int                        kMaxDepth = 512;
  absl::flat_hash_map<uint64_t, Cands> memo;
  auto                                 pack = [](int64_t port, uint64_t bit) { return (port << 32) | static_cast<int64_t>(bit); };
  std::function<Cands(uint32_t, uint32_t, int)> prov = [&](uint32_t id, uint32_t bit, int depth) -> Cands {
    const uint64_t key = (static_cast<uint64_t>(id) << 24) | (bit & 0xFFFFFF);
    if (auto it = memo.find(key); it != memo.end()) {
      return it->second;
    }
    if (depth > kMaxDepth || bit >= (1u << 24)) {
      return {};
    }
    memo.emplace(key, Cands{});
    const Node& n   = nodes_[id];
    const auto  kid = [&](uint32_t i) { return kids_[n.c0 + i]; };
    Cands       r;
    switch (n.op) {
      case Op::Leaf:
        if (n.sort == kBv && static_cast<size_t>(n.leaf) < leaf_port.size() && leaf_port[static_cast<size_t>(n.leaf)] >= 0) {
          r.push_back(pack(leaf_port[static_cast<size_t>(n.leaf)], bit));
        }
        break;
      case Op::Alias  : r = prov(kid(0), bit, depth + 1); break;
      case Op::Extract: r = prov(kid(0), n.p1 + bit, depth + 1); break;
      case Op::Concat : {
        uint32_t off = 0;
        for (uint32_t k = n.nc; k-- > 0;) {
          const uint32_t w = nodes_[kid(k)].width;
          if (bit < off + w) {
            r = prov(kid(k), bit - off, depth + 1);
            break;
          }
          off += w;
        }
        break;
      }
      case Op::Zext:
        if (bit < nodes_[kid(0)].width) {
          r = prov(kid(0), bit, depth + 1);
        }
        break;
      case Op::Sext: r = prov(kid(0), std::min(bit, nodes_[kid(0)].width - 1), depth + 1); break;
      case Op::Ite : {
        r = prov(kid(1), bit, depth + 1);
        for (const auto c : prov(kid(2), bit, depth + 1)) {
          if (std::find(r.begin(), r.end(), c) == r.end()) {
            r.push_back(c);
          }
        }
        if (r.size() > kMaxCands) {
          r.clear();  // too many routes: not a decoded field
        }
        break;
      }
      case Op::BvShl:
      case Op::BvLshr:
      case Op::BvAshr: {
        const Node& amt = nodes_[kid(1)];
        if (konst[kid(1)] == 0) {
          break;
        }
        const uint32_t k = shift_amount(&words_[amt.off], n.width);
        if (n.op == Op::BvShl) {
          if (bit >= k) {
            r = prov(kid(0), bit - k, depth + 1);
          }
        } else if (bit + k < n.width) {
          r = prov(kid(0), bit + k, depth + 1);
        } else if (n.op == Op::BvAshr) {
          r = prov(kid(0), n.width - 1, depth + 1);
        }
        break;
      }
      case Op::BvAnd:
      case Op::BvOr : {
        int  data = -1;
        bool keep = true;
        for (uint32_t k = 0; k < n.nc && keep; ++k) {
          const Node& c = nodes_[kid(k)];
          if (konst[kid(k)] != 0) {
            keep = get_bit(&words_[c.off], bit) == (n.op == Op::BvAnd);
          } else if (data < 0) {
            data = static_cast<int>(k);
          } else {
            keep = false;
          }
        }
        if (keep && data >= 0) {
          r = prov(kid(static_cast<uint32_t>(data)), bit, depth + 1);
        }
        break;
      }
      default: break;
    }
    memo[key] = r;
    return r;
  };
  std::map<std::tuple<int, uint32_t, uint32_t>, std::set<uint64_t>> found;
  constexpr size_t kMaxMemo = 4'000'000;  // a dictionary is a heuristic: bound its cost on huge unrollings
  for (uint32_t id = 0; id < nodes_.size() && memo.size() < kMaxMemo; ++id) {
    const Node& n = nodes_[id];
    if ((n.op != Op::Eq && n.op != Op::Distinct && n.op != Op::BvComp) || n.nc != 2) {
      continue;
    }
    const Node& a  = nodes_[kids_[n.c0]];
    const bool  ka = konst[kids_[n.c0]] != 0;
    const bool  kb = konst[kids_[n.c0 + 1]] != 0;
    if (a.sort != kBv || ka == kb) {
      continue;
    }
    const Node&    c = ka ? a : nodes_[kids_[n.c0 + 1]];
    const uint32_t x = ka ? kids_[n.c0 + 1] : kids_[n.c0];
    if (c.width > 64) {
      continue;
    }
    const uint64_t v = words_[c.off];
    for (const auto p0 : prov(x, 0, 0)) {
      // The low bits must copy one contiguous field; bits above it may only
      // be padding (a zero extension the constant also has zero), as in
      // `zext(instr[6:0]) == 8'h6f`.
      uint32_t m = 1;
      while (m < c.width) {
        const auto cands = prov(x, m, 0);
        if (std::find(cands.begin(), cands.end(), p0 + m) == cands.end()) {
          break;
        }
        ++m;
      }
      if (m < 64 && (v >> m) != 0) {
        continue;
      }
      auto& vals = found[{static_cast<int>(p0 >> 32), static_cast<uint32_t>(p0 & 0xFFFFFFFF), m}];
      if (vals.size() < max_values) {
        vals.insert(v);
      }
    }
  }
  std::vector<Field> out;
  for (auto& [k, vals] : found) {
    out.push_back(Field{std::get<0>(k), std::get<1>(k), std::get<2>(k), std::vector<uint64_t>(vals.begin(), vals.end())});
  }
  return out;
}

Term_sim::Reduced Term_sim::reduce(const std::vector<bool>& symbolic_leaf, const std::vector<size_t>& root_ids) {
  Reduced              out;
  const size_t         N = nodes_.size();
  std::vector<uint8_t> sym(N, 0);
  const auto           kid  = [&](uint32_t id, uint32_t i) { return kids_[nodes_[id].c0 + i]; };
  const auto           bval = [&](uint32_t id) { return words_[nodes_[id].off] != 0; };
  for (uint32_t id = 0; id < N; ++id) {
    const Node& n = nodes_[id];
    uint8_t     s = 0;
    switch (n.op) {
      case Op::Leaf      : s = symbolic_leaf[static_cast<size_t>(n.leaf)] ? 1 : 0; break;
      case Op::Const     :
      case Op::ConstArray: s = 0; break;
      case Op::Ite       : s = sym[kid(id, 0)] ? 1 : sym[kid(id, bval(kid(id, 0)) ? 1 : 2)]; break;
      case Op::And       :
      case Op::Or        : {
        const bool absorb = n.op == Op::Or;  // a concrete child with this value decides the node
        bool       any    = false;
        bool       fixed  = false;
        for (uint32_t i = 0; i < n.nc; ++i) {
          const auto c = kid(id, i);
          if (sym[c]) {
            any = true;
          } else if (bval(c) == absorb) {
            fixed = true;
          }
        }
        s = fixed ? 0 : (any ? 1 : 0);
        break;
      }
      default:
        for (uint32_t i = 0; i < n.nc && s == 0; ++i) {
          s = sym[kid(id, i)];
        }
        break;
    }
    sym[id] = s;
  }

  ++gen_;
  if (gen_ == 0) {
    std::fill(built_gen_.begin(), built_gen_.end(), 0);
    gen_ = 1;
  }
  auto ready = [&](uint32_t id) { return built_gen_[id] == gen_; };
  // The operands a node's reduced term is built from.
  auto needs = [&](uint32_t id, std::vector<uint32_t>& v) {
    v.clear();
    const Node& n = nodes_[id];
    if ((!sym[id] && n.sort != kArray) || n.op == Op::Leaf || n.op == Op::Const || n.op == Op::ConstArray) {
      return;
    }
    if (n.op == Op::Ite && !sym[kid(id, 0)]) {
      v.push_back(kid(id, bval(kid(id, 0)) ? 1 : 2));
      return;
    }
    for (uint32_t i = 0; i < n.nc; ++i) {
      const auto c = kid(id, i);
      if ((n.op == Op::And || n.op == Op::Or) && !sym[c]) {
        continue;  // a concrete identity operand (true for AND, false for OR)
      }
      v.push_back(c);
    }
  };
  std::vector<uint32_t>                  ops;
  std::vector<std::pair<uint32_t, bool>> stk;
  std::vector<uint32_t>                  pinned_arrays;  // concrete free memories a symbolic read reaches
  auto                                   build_term = [&](uint32_t root) {
    stk.clear();
    stk.emplace_back(root, false);
    while (!stk.empty()) {
      auto [id, expanded] = stk.back();
      if (ready(id)) {
        stk.pop_back();
        continue;
      }
      if (!expanded) {
        stk.back().second = true;
        needs(id, ops);
        for (const auto c : ops) {
          if (!ready(c)) {
            stk.emplace_back(c, false);
          }
        }
        continue;
      }
      stk.pop_back();
      const Node& n = nodes_[id];
      cvc5::Term  t;
      if (!sym[id] && n.sort != kArray) {
        t = const_of(id);
      } else if (n.op == Op::Leaf || n.op == Op::ConstArray) {
        t = orig_[id];
      } else if (n.op == Op::Alias) {
        // Keep the unrolling's shape: the symbol stays, its definition becomes one EQUAL.
        out.assertions.push_back(tm_.mkTerm(cvc5::Kind::EQUAL, {orig_[id], built_[kid(id, 0)]}));
        t = orig_[id];
      } else {
        needs(id, ops);
        if (n.op == Op::Ite && !sym[kid(id, 0)]) {
          t = built_[ops[0]];
        } else if ((n.op == Op::And || n.op == Op::Or) && ops.size() == 1) {
          t = built_[ops[0]];
        } else {
          std::vector<cvc5::Term> kids;
          kids.reserve(ops.size());
          for (const auto c : ops) {
            kids.push_back(built_[c]);
          }
          const auto& o = orig_[id];
          t             = o.hasOp() ? tm_.mkTerm(o.getOp(), kids) : tm_.mkTerm(o.getKind(), kids);
        }
        ++out.terms;
      }
      if (n.sort == kArray) {
        out.has_arrays = true;
        if (n.op == Op::Leaf && !sym[id]) {
          pinned_arrays.push_back(static_cast<uint32_t>(n.leaf));
        }
      }
      built_[id]     = t;
      built_gen_[id] = gen_;
    }
    return built_[root];
  };
  for (const auto c : constraint_nodes_) {
    if (!sym[c]) {
      if (!bval(c)) {
        out.vacuous = true;
      }
      continue;
    }
    out.constrained = true;
    out.assertions.push_back(build_term(c));
  }
  for (const auto r : root_ids) {
    out.roots.push_back(build_term(root_nodes_[r]));
  }
  // A concrete (non-symbolic) free memory a symbolic-index read still reaches
  // stays a symbol in the reduced formula; pin it to the content the folded
  // concrete-index reads used (the reads recorded by eval(true)), so a model
  // agrees with every folded value. Its other entries stay free: a superset of
  // the simulated content, so an UNSAT still covers it.
  if (!pinned_arrays.empty()) {
    std::sort(pinned_arrays.begin(), pinned_arrays.end());
    pinned_arrays.erase(std::unique(pinned_arrays.begin(), pinned_arrays.end()), pinned_arrays.end());
    for (const auto& r : reads_) {
      if (!std::binary_search(pinned_arrays.begin(), pinned_arrays.end(), r.leaf)) {
        continue;
      }
      const auto& l   = leaves_[r.leaf];
      auto        bvc = [&](const std::vector<u64>& v, uint32_t w) {
        return w <= 64 ? tm_.mkBitVector(w, v[0]) : tm_.mkBitVector(w, to_hex(v.data(), w), 16);
      };
      out.assertions.push_back(
          tm_.mkTerm(cvc5::Kind::EQUAL,
                     {tm_.mkTerm(cvc5::Kind::SELECT, {l.term, bvc(r.idx, l.index_width)}), bvc(r.val, l.width)}));
    }
  }
  return out;
}

}  // namespace livehd::lec
