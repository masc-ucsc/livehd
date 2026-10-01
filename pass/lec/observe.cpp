// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "observe.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>

#include "absl/container/flat_hash_set.h"

namespace livehd::lec {

namespace {

using Bits = Ground_eval::Bits;

Bits add_bits(const Bits& a, const Bits& b) {
  Bits    r(a.size());
  uint8_t c = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    const uint8_t x = a[i], y = i < b.size() ? b[i] : 0;
    r[i] = x ^ y ^ c;
    c    = static_cast<uint8_t>((x & y) | (x & c) | (y & c));
  }
  return r;
}

Bits neg_bits(const Bits& a) {
  Bits inv(a.size());
  for (size_t i = 0; i < a.size(); ++i) {
    inv[i] = a[i] ^ 1U;
  }
  Bits one(a.size(), 0);
  if (!one.empty()) {
    one[0] = 1;
  }
  return add_bits(inv, one);
}

Bits mul_bits(const Bits& a, const Bits& b) {
  Bits r(a.size(), 0);
  for (size_t j = 0; j < b.size(); ++j) {
    if (b[j] == 0) {
      continue;
    }
    Bits sh(a.size(), 0);
    for (size_t i = 0; i + j < a.size(); ++i) {
      sh[i + j] = a[i];
    }
    r = add_bits(r, sh);
  }
  return r;
}

// Saturated numeric value (>= cap means "cap").
int amount_of(const Bits& v, int cap) {
  uint64_t n = 0;
  for (size_t i = v.size(); i-- > 0;) {
    if (n > static_cast<uint64_t>(cap)) {
      return cap;
    }
    n = (n << 1) | v[i];
  }
  return n > static_cast<uint64_t>(cap) ? cap : static_cast<int>(n);
}

// -1 / 0 / 1 unsigned (or two's-complement signed) comparison of equal widths.
int cmp_bits(const Bits& a, const Bits& b, bool is_signed) {
  if (a.size() != b.size() || a.empty()) {
    return 0;
  }
  const size_t top = a.size() - 1;
  if (is_signed && a[top] != b[top]) {
    return a[top] != 0 ? -1 : 1;
  }
  for (size_t i = a.size(); i-- > 0;) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

std::optional<Bits> eval_node(const cvc5::Term& t, const std::vector<const std::optional<Bits>*>& ch) {
  const auto kind = t.getKind();
  auto       get  = [&](size_t i) -> const Bits* { return i < ch.size() && ch[i]->has_value() ? &**ch[i] : nullptr; };
  auto       all  = [&]() {
    for (size_t i = 0; i < ch.size(); ++i) {
      if (get(i) == nullptr) {
        return false;
      }
    }
    return true;
  };
  const int w = t.getSort().isBitVector() ? static_cast<int>(t.getSort().getBitVectorSize()) : 1;
  switch (kind) {
    case cvc5::Kind::CONST_BITVECTOR: {
      std::string s = t.getBitVectorValue(2);
      Bits        r(s.size());
      for (size_t i = 0; i < s.size(); ++i) {
        r[s.size() - 1 - i] = s[i] == '1' ? 1 : 0;
      }
      return r;
    }
    case cvc5::Kind::CONST_BOOLEAN: return Bits{static_cast<uint8_t>(t.getBooleanValue() ? 1 : 0)};
    case cvc5::Kind::ITE          : {
      const Bits* c = get(0);
      if (c == nullptr) {
        return std::nullopt;
      }
      const Bits* v = get((*c)[0] != 0 ? 1 : 2);
      return v == nullptr ? std::nullopt : std::optional<Bits>(*v);
    }
    default: break;
  }
  if (!all() || ch.empty()) {
    return std::nullopt;
  }
  const Bits& a = *get(0);
  switch (kind) {
    case cvc5::Kind::NOT          :
    case cvc5::Kind::BITVECTOR_NOT: {
      Bits r(a.size());
      for (size_t i = 0; i < a.size(); ++i) {
        r[i] = a[i] ^ 1U;
      }
      return r;
    }
    case cvc5::Kind::AND           :
    case cvc5::Kind::OR            :
    case cvc5::Kind::XOR           :
    case cvc5::Kind::BITVECTOR_AND :
    case cvc5::Kind::BITVECTOR_OR  :
    case cvc5::Kind::BITVECTOR_XOR :
    case cvc5::Kind::BITVECTOR_NAND:
    case cvc5::Kind::BITVECTOR_NOR :
    case cvc5::Kind::BITVECTOR_XNOR: {
      Bits r = a;
      for (size_t k = 1; k < ch.size(); ++k) {
        const Bits& b = *get(k);
        if (b.size() != r.size()) {
          return std::nullopt;
        }
        for (size_t i = 0; i < r.size(); ++i) {
          switch (kind) {
            case cvc5::Kind::AND           :
            case cvc5::Kind::BITVECTOR_AND :
            case cvc5::Kind::BITVECTOR_NAND: r[i] &= b[i]; break;
            case cvc5::Kind::OR            :
            case cvc5::Kind::BITVECTOR_OR  :
            case cvc5::Kind::BITVECTOR_NOR : r[i] |= b[i]; break;
            default                        : r[i] ^= b[i]; break;
          }
        }
      }
      if (kind == cvc5::Kind::BITVECTOR_NAND || kind == cvc5::Kind::BITVECTOR_NOR || kind == cvc5::Kind::BITVECTOR_XNOR) {
        for (auto& x : r) {
          x ^= 1U;
        }
      }
      return r;
    }
    case cvc5::Kind::IMPLIES : return Bits{static_cast<uint8_t>((a[0] == 0 || (*get(1))[0] != 0) ? 1 : 0)};
    case cvc5::Kind::EQUAL   :
    case cvc5::Kind::DISTINCT: {
      bool eq = true;
      for (size_t k = 1; k < ch.size(); ++k) {
        eq = eq && *get(k) == a;
      }
      return Bits{static_cast<uint8_t>((kind == cvc5::Kind::EQUAL) == eq ? 1 : 0)};
    }
    case cvc5::Kind::BITVECTOR_ULT:
    case cvc5::Kind::BITVECTOR_ULE:
    case cvc5::Kind::BITVECTOR_UGT:
    case cvc5::Kind::BITVECTOR_UGE:
    case cvc5::Kind::BITVECTOR_SLT:
    case cvc5::Kind::BITVECTOR_SLE:
    case cvc5::Kind::BITVECTOR_SGT:
    case cvc5::Kind::BITVECTOR_SGE: {
      const bool sg = kind == cvc5::Kind::BITVECTOR_SLT || kind == cvc5::Kind::BITVECTOR_SLE || kind == cvc5::Kind::BITVECTOR_SGT
                      || kind == cvc5::Kind::BITVECTOR_SGE;
      const int  c  = cmp_bits(a, *get(1), sg);
      bool       r  = false;
      switch (kind) {
        case cvc5::Kind::BITVECTOR_ULT:
        case cvc5::Kind::BITVECTOR_SLT: r = c < 0; break;
        case cvc5::Kind::BITVECTOR_ULE:
        case cvc5::Kind::BITVECTOR_SLE: r = c <= 0; break;
        case cvc5::Kind::BITVECTOR_UGT:
        case cvc5::Kind::BITVECTOR_SGT: r = c > 0; break;
        default                       : r = c >= 0; break;
      }
      return Bits{static_cast<uint8_t>(r ? 1 : 0)};
    }
    case cvc5::Kind::BITVECTOR_CONCAT: {
      Bits r;
      for (size_t k = ch.size(); k-- > 0;) {  // the last operand is the low part
        const Bits& b = *get(k);
        r.insert(r.end(), b.begin(), b.end());
      }
      return r;
    }
    case cvc5::Kind::BITVECTOR_EXTRACT: {
      const auto hi = t.getOp()[0].getUInt32Value();
      const auto lo = t.getOp()[1].getUInt32Value();
      if (hi >= a.size() || lo > hi) {
        return std::nullopt;
      }
      return Bits(a.begin() + lo, a.begin() + hi + 1);
    }
    case cvc5::Kind::BITVECTOR_ZERO_EXTEND:
    case cvc5::Kind::BITVECTOR_SIGN_EXTEND: {
      Bits          r   = a;
      const uint8_t ext = kind == cvc5::Kind::BITVECTOR_SIGN_EXTEND && !a.empty() ? a.back() : 0;
      r.resize(static_cast<size_t>(w), ext);
      return r;
    }
    case cvc5::Kind::BITVECTOR_REPEAT: {
      Bits r;
      while (static_cast<int>(r.size()) < w) {
        r.insert(r.end(), a.begin(), a.end());
      }
      r.resize(static_cast<size_t>(w));
      return r;
    }
    case cvc5::Kind::BITVECTOR_SHL :
    case cvc5::Kind::BITVECTOR_LSHR:
    case cvc5::Kind::BITVECTOR_ASHR: {
      const int     n    = amount_of(*get(1), w);
      const uint8_t fill = kind == cvc5::Kind::BITVECTOR_ASHR && !a.empty() ? a.back() : 0;
      Bits          r(a.size(), kind == cvc5::Kind::BITVECTOR_SHL ? 0 : fill);
      for (int i = 0; i < w; ++i) {
        const int src = kind == cvc5::Kind::BITVECTOR_SHL ? i - n : i + n;
        if (src >= 0 && src < w) {
          r[static_cast<size_t>(i)] = a[static_cast<size_t>(src)];
        }
      }
      return r;
    }
    case cvc5::Kind::BITVECTOR_ADD: {
      Bits r = a;
      for (size_t k = 1; k < ch.size(); ++k) {
        r = add_bits(r, *get(k));
      }
      return r;
    }
    case cvc5::Kind::BITVECTOR_SUB : return add_bits(a, neg_bits(*get(1)));
    case cvc5::Kind::BITVECTOR_NEG : return neg_bits(a);
    case cvc5::Kind::BITVECTOR_MULT: {
      Bits r = a;
      for (size_t k = 1; k < ch.size(); ++k) {
        r = mul_bits(r, *get(k));
      }
      return r;
    }
    default: return std::nullopt;
  }
}

}  // namespace

bool Bit_set::any() const {
  return std::any_of(words_.begin(), words_.end(), [](uint64_t w) { return w != 0; });
}

int Bit_set::count() const {
  int n = 0;
  for (auto w : words_) {
    n += std::popcount(w);
  }
  return n;
}

int Bit_set::lowest() const {
  for (size_t i = 0; i < words_.size(); ++i) {
    if (words_[i] != 0) {
      return static_cast<int>(i * 64) + std::countr_zero(words_[i]);
    }
  }
  return -1;
}

int Bit_set::highest() const {
  for (size_t i = words_.size(); i-- > 0;) {
    if (words_[i] != 0) {
      return static_cast<int>(i * 64) + 63 - std::countl_zero(words_[i]);
    }
  }
  return -1;
}

Bit_set Bit_set::merge(const Bit_set& o) {
  Bit_set      fresh(width_);
  const size_t n = std::min(words_.size(), o.words_.size());
  for (size_t i = 0; i < n; ++i) {
    const uint64_t add  = o.words_[i] & ~words_[i];
    fresh.words_[i]     = add;
    words_[i]          |= add;
  }
  return fresh;
}

std::vector<int> Bit_set::bits() const {
  std::vector<int> out;
  for (size_t i = 0; i < words_.size(); ++i) {
    uint64_t w = words_[i];
    while (w != 0) {
      out.push_back(static_cast<int>(i * 64) + std::countr_zero(w));
      w &= w - 1;
    }
  }
  return out;
}

const std::optional<Ground_eval::Bits>& Ground_eval::eval(const cvc5::Term& t) {
  if (auto it = memo_.find(t); it != memo_.end()) {
    return it->second;
  }
  // Iterative post-order: an encoded next state can be a very deep chain.
  std::vector<std::pair<cvc5::Term, bool>> st;
  st.emplace_back(t, false);
  while (!st.empty()) {
    auto [cur, expanded] = st.back();
    if (memo_.contains(cur)) {
      st.pop_back();
      continue;
    }
    const auto kind = cur.getKind();
    if (kind == cvc5::Kind::CONSTANT || kind == cvc5::Kind::VARIABLE) {
      memo_.emplace(cur, std::nullopt);
      st.pop_back();
      continue;
    }
    if (!expanded) {
      st.back().second = true;
      for (size_t i = 0; i < cur.getNumChildren(); ++i) {
        if (!memo_.contains(cur[i])) {
          st.emplace_back(cur[i], false);
        }
      }
      continue;
    }
    st.pop_back();
    std::vector<const std::optional<Bits>*> ch;
    ch.reserve(cur.getNumChildren());
    for (size_t i = 0; i < cur.getNumChildren(); ++i) {
      ch.push_back(&memo_.at(cur[i]));
    }
    auto v = eval_node(cur, ch);
    memo_.emplace(cur, std::move(v));
  }
  return memo_.at(t);
}

int Bit_demand::width_of(const cvc5::Term& t) {
  const auto s = t.getSort();
  return s.isBitVector() ? static_cast<int>(s.getBitVectorSize()) : 1;
}

Bit_demand::Slot& Bit_demand::slot(const cvc5::Term& t) {
  auto [it, fresh] = slots_.try_emplace(t);
  if (fresh) {
    const int w        = width_of(t);
    it->second.seen    = Bit_set(w);
    it->second.pending = Bit_set(w);
  }
  return it->second;
}

void Bit_demand::push(const cvc5::Term& t, const Bit_set& d) {
  if (!d.any()) {
    return;
  }
  auto& s     = slot(t);
  auto  added = s.seen.merge(d);
  if (!added.any()) {
    return;
  }
  s.pending.merge(added);
  if (!s.queued) {
    s.queued = true;
    work_.push_back(t);
  }
}

void Bit_demand::demand(const cvc5::Term& t, const Bit_set& bits) {
  if (!t.isNull()) {
    push(t, bits);
  }
}

void Bit_demand::demand_all(const cvc5::Term& t) {
  if (t.isNull()) {
    return;
  }
  Bit_set b(width_of(t));
  b.set_all();
  push(t, b);
}

void Bit_demand::run(const Target_cb& cb) {
  while (!work_.empty()) {
    const cvc5::Term t = work_.back();
    work_.pop_back();
    auto& s   = slot(t);
    s.queued  = false;
    Bit_set d = s.pending;
    s.pending = Bit_set(d.width());
    if (!d.any()) {
      continue;
    }
    if (auto tg = targets_.find(t); tg != targets_.end()) {
      cb(tg->second, d);
      continue;  // a target is a cut: the demand stops here
    }
    if (ground_.eval(t)) {
      continue;  // a comptime value: no symbol below to observe
    }
    const size_t n    = t.getNumChildren();
    const int    w    = d.width();
    auto         all  = [&](const cvc5::Term& c) { demand_all(c); };
    auto         same = [&](const cvc5::Term& c, const Bit_set& b) {
      if (width_of(c) == b.width()) {
        push(c, b);
      } else {
        demand_all(c);  // a width surprise: stay conservative
      }
    };
    switch (t.getKind()) {
      case cvc5::Kind::CONSTANT         :
      case cvc5::Kind::VARIABLE         :
      case cvc5::Kind::CONST_BITVECTOR  :
      case cvc5::Kind::CONST_BOOLEAN    : break;
      case cvc5::Kind::BITVECTOR_EXTRACT: {
        const int lo = static_cast<int>(t.getOp()[1].getUInt32Value());
        Bit_set   c(width_of(t[0]));
        for (int i : d.bits()) {
          c.set(i + lo);
        }
        push(t[0], c);
        break;
      }
      case cvc5::Kind::BITVECTOR_CONCAT: {
        int base = 0;  // the LAST operand holds the low bits
        for (size_t k = n; k-- > 0;) {
          const int cw = width_of(t[k]);
          Bit_set   c(cw);
          for (int i : d.bits()) {
            if (i >= base && i < base + cw) {
              c.set(i - base);
            }
          }
          push(t[k], c);
          base += cw;
        }
        break;
      }
      case cvc5::Kind::BITVECTOR_NOT:
      case cvc5::Kind::BITVECTOR_XOR:
      case cvc5::Kind::BITVECTOR_XNOR:
        for (size_t k = 0; k < n; ++k) {
          same(t[k], d);
        }
        break;
      case cvc5::Kind::BITVECTOR_AND :
      case cvc5::Kind::BITVECTOR_NAND:
      case cvc5::Kind::BITVECTOR_OR  :
      case cvc5::Kind::BITVECTOR_NOR : {
        // A constant operand bit that DOMINATES (0 for and, 1 for or) fixes the
        // result bit: no other operand's bit there is needed.
        const bool    is_and = t.getKind() == cvc5::Kind::BITVECTOR_AND || t.getKind() == cvc5::Kind::BITVECTOR_NAND;
        const uint8_t dom    = is_and ? 0 : 1;
        Bit_set       live   = d;
        for (size_t k = 0; k < n; ++k) {
          const auto& cv = ground_.eval(t[k]);
          if (!cv || cv->size() != static_cast<size_t>(w)) {
            continue;
          }
          Bit_set keep(w);
          for (int i : live.bits()) {
            if ((*cv)[static_cast<size_t>(i)] != dom) {
              keep.set(i);
            }
          }
          live = keep;
        }
        for (size_t k = 0; k < n; ++k) {
          same(t[k], live);
        }
        break;
      }
      case cvc5::Kind::ITE:
        if (const auto& cv = ground_.eval(t[0]); cv) {
          same(t[(*cv)[0] != 0 ? 1 : 2], d);  // a comptime-decided arm: the other is dead
          break;
        }
        all(t[0]);
        if (width_of(t[1]) == w && t.getSort().isBitVector()) {
          push(t[1], d);
          push(t[2], d);
        } else {
          all(t[1]);
          all(t[2]);
        }
        break;
      case cvc5::Kind::BITVECTOR_ZERO_EXTEND:
      case cvc5::Kind::BITVECTOR_SIGN_EXTEND: {
        const int cw = width_of(t[0]);
        Bit_set   c(cw);
        bool      high = false;
        for (int i : d.bits()) {
          if (i < cw) {
            c.set(i);
          } else {
            high = true;
          }
        }
        if (high && t.getKind() == cvc5::Kind::BITVECTOR_SIGN_EXTEND) {
          c.set(cw - 1);
        }
        push(t[0], c);
        break;
      }
      case cvc5::Kind::BITVECTOR_REPEAT: {
        const int cw = width_of(t[0]);
        Bit_set   c(cw);
        for (int i : d.bits()) {
          c.set(i % cw);
        }
        push(t[0], c);
        break;
      }
      case cvc5::Kind::BITVECTOR_ROTATE_LEFT :
      case cvc5::Kind::BITVECTOR_ROTATE_RIGHT: {
        const int  r    = static_cast<int>(t.getOp()[0].getUInt32Value() % static_cast<uint32_t>(std::max(1, w)));
        const bool left = t.getKind() == cvc5::Kind::BITVECTOR_ROTATE_LEFT;
        Bit_set    c(w);
        for (int i : d.bits()) {
          c.set(left ? (i - r + w) % w : (i + r) % w);
        }
        same(t[0], c);
        break;
      }
      case cvc5::Kind::BITVECTOR_SHL :
      case cvc5::Kind::BITVECTOR_LSHR:
      case cvc5::Kind::BITVECTOR_ASHR: {
        int     amount = 0;
        Bit_set c(w);
        if (const auto& av = ground_.eval(t[1]); av) {
          amount = amount_of(*av, w);
          for (int i : d.bits()) {
            if (t.getKind() == cvc5::Kind::BITVECTOR_SHL) {
              c.set(i - amount);
            } else if (i + amount < w) {
              c.set(i + amount);
            } else if (t.getKind() == cvc5::Kind::BITVECTOR_ASHR) {
              c.set(w - 1);
            }
          }
        } else {
          all(t[1]);
          if (t.getKind() == cvc5::Kind::BITVECTOR_SHL) {
            c.set_range(0, d.highest());
          } else {
            c.set_range(d.lowest(), w - 1);
          }
        }
        same(t[0], c);
        break;
      }
      case cvc5::Kind::BITVECTOR_ADD :
      case cvc5::Kind::BITVECTOR_SUB :
      case cvc5::Kind::BITVECTOR_NEG :
      case cvc5::Kind::BITVECTOR_MULT: {
        Bit_set c(w);  // carries only move up: bit i needs operand bits 0..i
        c.set_range(0, d.highest());
        for (size_t k = 0; k < n; ++k) {
          same(t[k], c);
        }
        break;
      }
      default:
        for (size_t k = 0; k < n; ++k) {
          all(t[k]);
        }
        break;
    }
  }
}

absl::flat_hash_map<std::string, Obs_plan> plan_observability(const Encoded& re, const Encoded& ie,
                                                              const std::vector<Obs_candidate>& cands) {
  const Encoded* enc[2] = {&re, &ie};
  Bit_demand     eng[2];
  struct Cut {
    int     w[2]  = {0, 0};
    int     w_min = 0;
    Bit_set keep;           // positions < w_min tied + compared
    bool    whole = false;  // a demand above w_min: keep the full cut
  };
  std::vector<Cut>                 cuts(cands.size());
  absl::flat_hash_set<std::string> cand_next;
  for (size_t i = 0; i < cands.size(); ++i) {
    auto& c = cuts[i];
    c.w[0]  = cands[i].cur[0].width;
    c.w[1]  = cands[i].cur[1].width;
    c.w_min = std::min(c.w[0], c.w[1]);
    c.keep  = Bit_set(c.w_min);
    for (int s = 0; s < 2; ++s) {
      eng[s].add_target(cands[i].cur[s].term, static_cast<int>(i));
    }
    cand_next.insert(std::string("\x01nxt:") + cands[i].key);
  }

  // Roots: every obligation other than a candidate's next state.
  for (int s = 0; s < 2; ++s) {
    const Encoded& e = *enc[s];
    auto&          g = eng[s];
    for (const auto& [name, v] : e.outputs) {
      if (cand_next.contains(name)) {
        continue;
      }
      g.demand_all(v.term);
      g.demand_all(v.x_mask);
    }
    for (const auto& [k, t] : e.next_mem) {
      g.demand_all(t);
    }
    for (const auto& [k, t] : e.next_mem_x) {
      g.demand_all(t);
    }
    for (const auto& [k, v] : e.next_read) {
      g.demand_all(v.term);
      g.demand_all(v.x_mask);
    }
    for (const auto& [k, ports] : e.mem_wr) {
      for (const auto& p : ports) {
        g.demand_all(p.addr);
        g.demand_all(p.wmask);
        g.demand_all(p.din);
      }
    }
    for (const auto& [k, ports] : e.mem_rd) {
      for (const auto& p : ports) {
        g.demand_all(p.dout);
        g.demand_all(p.addr);
        g.demand_all(p.value);
      }
    }
    for (const auto& [k, mw] : e.mem_whole) {
      g.demand_all(mw.cond);
      g.demand_all(mw.bus);
      g.demand_all(mw.reset);
      g.demand_all(mw.init);
    }
    for (const auto& [l, r] : e.equalities) {
      g.demand_all(l);
      g.demand_all(r);
    }
  }

  // Keep `bits` (positions < w_min) of cut i: demand the next state there on
  // BOTH sides, since the kept bits are compared jointly.
  auto keep_bits = [&](size_t i, const Bit_set& bits) {
    auto& c = cuts[i];
    if (c.whole) {
      return;
    }
    Bit_set add = c.keep.merge(bits);
    if (!add.any()) {
      return;
    }
    for (int s = 0; s < 2; ++s) {
      const Val& nx = cands[i].next[s];
      Bit_set    nb(nx.width);
      for (int b : add.bits()) {
        nb.set(b);
      }
      eng[s].demand(nx.term, nb);
      eng[s].demand(nx.x_mask, nb);
    }
  };
  auto make_whole = [&](size_t i) {
    auto& c = cuts[i];
    if (c.whole) {
      return;
    }
    Bit_set all(c.w_min);
    all.set_all();
    keep_bits(i, all);
    c.whole = true;
    for (int s = 0; s < 2; ++s) {
      eng[s].demand_all(cands[i].next[s].term);
      eng[s].demand_all(cands[i].next[s].x_mask);
    }
  };

  const bool dbg = std::getenv("LEC_DUMP_OBS") != nullptr;
  if (dbg) {
    // Roots only: which cut bits each side's obligations read directly.
    for (int s = 0; s < 2; ++s) {
      Bit_demand           probe = eng[s];
      std::vector<Bit_set> seen;
      for (const auto& c : cands) {
        seen.emplace_back(c.cur[s].width);
      }
      probe.run([&](int id, const Bit_set& nb) { seen[static_cast<size_t>(id)].merge(nb); });
      for (size_t i = 0; i < cands.size(); ++i) {
        std::string list;
        int         shown = 0;
        for (int b : seen[i].bits()) {
          if (shown++ < 64) {
            list += std::to_string(b) + ",";
          }
        }
        std::fprintf(stderr,
                     "[LEC_OBS] side %d roots read %d/%d bit(s) of %s {%s}\n",
                     s,
                     seen[i].count(),
                     seen[i].width(),
                     cands[i].key.c_str(),
                     list.c_str());
      }
    }
  }
  bool progress = true;
  while (progress) {
    progress = false;
    for (int s = 0; s < 2; ++s) {
      eng[s].run([&](int id, const Bit_set& nb) {
        const auto i = static_cast<size_t>(id);
        if (nb.highest() >= cuts[i].w_min) {
          make_whole(i);
        } else {
          Bit_set b(cuts[i].w_min);
          for (int x : nb.bits()) {
            b.set(x);
          }
          keep_bits(i, b);
        }
        progress = true;
      });
    }
  }

  absl::flat_hash_map<std::string, Obs_plan> out;
  for (size_t i = 0; i < cands.size(); ++i) {
    const auto& c = cuts[i];
    if (c.whole) {
      continue;
    }
    // Nothing to free: the cut already equals its full width on both sides.
    if (c.keep.all() && c.w[0] == c.w[1]) {
      continue;
    }
    Obs_plan p;
    p.w_ref  = c.w[0];
    p.w_impl = c.w[1];
    p.keep   = c.keep.bits();
    out.emplace(cands[i].key, std::move(p));
  }
  return out;
}

Val free_unkept_bits(cvc5::TermManager& tm, const Val& v, int width, const std::vector<int>& keep, const std::string& tag) {
  std::vector<uint8_t> kept(static_cast<size_t>(width), 0);
  for (int b : keep) {
    if (b >= 0 && b < width && b < v.width) {
      kept[static_cast<size_t>(b)] = 1;
    }
  }
  // Runs from the LSB up; the concat wants MSB first.
  std::vector<cvc5::Term> parts;
  int                     lo = 0;
  while (lo < width) {
    const uint8_t k  = kept[static_cast<size_t>(lo)];
    int           hi = lo;
    while (hi + 1 < width && kept[static_cast<size_t>(hi + 1)] == k) {
      ++hi;
    }
    if (k != 0) {
      auto op = tm.mkOp(cvc5::Kind::BITVECTOR_EXTRACT, {static_cast<uint32_t>(hi), static_cast<uint32_t>(lo)});
      parts.push_back(tm.mkTerm(op, {v.term}));
    } else {
      parts.push_back(tm.mkConst(tm.mkBitVectorSort(static_cast<uint32_t>(hi - lo + 1)), tag + std::to_string(lo)));
    }
    lo = hi + 1;
  }
  std::reverse(parts.begin(), parts.end());
  cvc5::Term t = parts.front();
  if (parts.size() > 1) {
    t = tm.mkTerm(cvc5::Kind::BITVECTOR_CONCAT, parts);
  }
  return Val{t, width, v.is_signed};
}

cvc5::Term extract_kept(cvc5::TermManager& tm, const cvc5::Term& t, const std::vector<int>& keep) {
  std::vector<cvc5::Term> parts;  // LSB-first runs, reversed below
  size_t                  i = 0;
  while (i < keep.size()) {
    size_t j = i;
    while (j + 1 < keep.size() && keep[j + 1] == keep[j] + 1) {
      ++j;
    }
    auto op = tm.mkOp(cvc5::Kind::BITVECTOR_EXTRACT, {static_cast<uint32_t>(keep[j]), static_cast<uint32_t>(keep[i])});
    parts.push_back(tm.mkTerm(op, {t}));
    i = j + 1;
  }
  std::reverse(parts.begin(), parts.end());
  if (parts.size() == 1) {
    return parts.front();
  }
  return tm.mkTerm(cvc5::Kind::BITVECTOR_CONCAT, parts);
}

}  // namespace livehd::lec
