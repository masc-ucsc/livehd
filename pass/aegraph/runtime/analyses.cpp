//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "runtime/analyses.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace livehd::aegraph::rt {

namespace {

constexpr int kDepth = 8;

// ---- directed interval arithmetic on Wide ----
//
// A lower bound lives in [-kInf, kInf - 1] and an upper bound in [-kInf + 1, kInf]; -kInf as a lower bound
// and kInf as an upper bound mean "unbounded". Every helper rounds in the direction of its bound, so a result
// that does not fit is the weaker bound, never a wrong one.

constexpr Wide dn(Wide v) { return v <= -kInf ? -kInf : (v >= kInf ? kInf - 1 : v); }
constexpr Wide up(Wide v) { return v >= kInf ? kInf : (v <= -kInf ? -kInf + 1 : v); }

constexpr bool unb_lo(Wide v) { return v <= -kInf; }
constexpr bool unb_hi(Wide v) { return v >= kInf; }

constexpr Wide lo_add(Wide a, Wide b) { return (unb_lo(a) || unb_lo(b)) ? -kInf : dn(a + b); }
constexpr Wide hi_add(Wide a, Wide b) { return (unb_hi(a) || unb_hi(b)) ? kInf : up(a + b); }
// lower bound of (x - y) from x's lower and y's upper bound; upper bound from x's upper and y's lower
constexpr Wide lo_sub(Wide xlo, Wide yhi) { return (unb_lo(xlo) || unb_hi(yhi)) ? -kInf : dn(xlo - yhi); }
constexpr Wide hi_sub(Wide xhi, Wide ylo) { return (unb_hi(xhi) || unb_lo(ylo)) ? kInf : up(xhi - ylo); }

// Bits needed by a non-negative finite value (0 for 0).
int bitlen(Wide v);

// v * 2^k with directed saturation.
Wide shl_dn(Wide v, int64_t k) {
  if (v == 0) {
    return 0;
  }
  const Wide m = v < 0 ? -v : v;
  if (k >= 100 || bitlen(m) + k >= 100) {
    return v < 0 ? -kInf : kInf - 1;
  }
  return v * (Wide{1} << k);
}
Wide shl_up(Wide v, int64_t k) {
  if (v == 0) {
    return 0;
  }
  const Wide m = v < 0 ? -v : v;
  if (k >= 100 || bitlen(m) + k >= 100) {
    return v < 0 ? -kInf + 1 : kInf;
  }
  return v * (Wide{1} << k);
}

// Product interval of two intervals; unbounded or very large operands give the unbounded one.
void mult_range(Wide alo, Wide ahi, Wide blo, Wide bhi, Wide& lo, Wide& hi) {
  const Wide limit = Wide{1} << 62;
  if (alo <= -limit || ahi >= limit || blo <= -limit || bhi >= limit) {
    lo = -kInf;
    hi = kInf;
    return;
  }
  const Wide c[4] = {alo * blo, alo * bhi, ahi * blo, ahi * bhi};
  lo              = *std::ranges::min_element(c);
  hi              = *std::ranges::max_element(c);
  if (lo <= -kInf || hi >= kInf) {  // the +/-kInf convention means "unbounded"; never claim a finite bound beyond it
    lo = -kInf;
    hi = kInf;
  }
}

int bitlen(Wide v) {
  int n = 0;
  while (v > 0) {
    v >>= 1;
    ++n;
  }
  return n;
}

constexpr Wide mask_w(int bits) { return bits >= 100 ? kInf : (Wide{1} << bits) - 1; }

constexpr uint64_t low_mask(int bits) { return bits >= 64 ? ~uint64_t{0} : (bits <= 0 ? 0 : (uint64_t{1} << bits) - 1); }

int trailing_known_zero(uint64_t kz) { return std::countr_one(kz); }

Facts top() { return {}; }

Facts of_const(int64_t v) {
  Facts f;
  f.lo = f.hi = v;
  f.kz        = ~static_cast<uint64_t>(v);
  f.ko        = static_cast<uint64_t>(v);
  return f;
}

// Tighten a fact from its own components.
void refine(Facts& f) {
  if (f.lo > f.hi) {
    f = top();  // an empty class cannot occur; never let a contradiction through
    return;
  }
  if (f.lo >= 0 && f.hi < kInf) {
    const int nb = bitlen(f.hi);
    if (nb < 64) {
      f.kz |= ~low_mask(nb);
      // the value is at most the OR of the bits that can be 1, and at least the bits known to be 1
      const Wide possible = static_cast<Wide>(~f.kz);
      if (possible < f.hi) {
        f.hi = possible;
      }
      if (static_cast<Wide>(f.ko) > f.lo) {
        f.lo = static_cast<Wide>(f.ko);
      }
      if (f.lo > f.hi) {
        f = top();
      }
    }
  }
}

Facts join(const Facts& a, const Facts& b) {
  Facts f;
  f.lo = std::min(a.lo, b.lo);
  f.hi = std::max(a.hi, b.hi);
  f.kz = a.kz & b.kz;
  f.ko = a.ko & b.ko;
  return f;
}

// Both facts hold for a class that equals two things.
Facts meet(const Facts& a, const Facts& b) {
  Facts f;
  f.lo = std::max(a.lo, b.lo);
  f.hi = std::min(a.hi, b.hi);
  f.kz = a.kz | b.kz;
  f.ko = a.ko | b.ko;
  refine(f);
  return f;
}

[[nodiscard]] const Operand* by_bank(std::span<const Operand> ops, uint32_t bank) {
  for (const auto& o : ops) {
    if (o.bank == bank) {
      return &o;
    }
  }
  return nullptr;
}

[[nodiscard]] std::optional<int64_t> const_i64(const Egraph& eg, Class_id c) {
  if (eg.cls(c).kind != Egraph::Kind::konst) {
    return std::nullopt;
  }
  const auto* v = eg.value_of(c);
  if (v == nullptr || !v->is_integer() || !v->is_just_i64() || v->has_unknowns()) {
    return std::nullopt;
  }
  return v->to_just_i64();
}

Facts facts_rec(const Egraph& eg, Class_id c, int depth);

Facts node_facts(const Egraph& eg, const Enode& n, int depth) {
  const auto ops = eg.operands(n);
  const auto get = [&](Class_id c) { return facts_rec(eg, c, depth - 1); };
  Facts      f;
  switch (n.op) {
    case Ntype_op::Sum: {
      f.lo = f.hi = 0;
      int tz      = 64;
      for (const auto& o : ops) {
        const auto g = get(o.cls);
        if (o.bank == 0) {
          f.lo = lo_add(f.lo, g.lo);
          f.hi = hi_add(f.hi, g.hi);
        } else {
          f.lo = lo_sub(f.lo, g.hi);
          f.hi = hi_sub(f.hi, g.lo);
        }
        tz = std::min(tz, trailing_known_zero(g.kz));
      }
      f.kz = low_mask(tz);
      break;
    }
    case Ntype_op::Mult: {
      f.lo = f.hi = 1;
      int tz      = 0;
      for (const auto& o : ops) {
        const auto g = get(o.cls);
        Wide       lo = 0, hi = 0;
        mult_range(f.lo, f.hi, g.lo, g.hi, lo, hi);
        f.lo = lo;
        f.hi = hi;
        tz   = std::min(64, tz + trailing_known_zero(g.kz));
      }
      f.kz = low_mask(tz);
      break;
    }
    case Ntype_op::And: {
      f.kz = 0;
      f.ko = ~uint64_t{0};
      bool any_nonneg = false;
      Wide cap        = kInf;
      for (const auto& o : ops) {
        const auto g = get(o.cls);
        f.kz |= g.kz;
        f.ko &= g.ko;
        if (g.nonneg()) {
          any_nonneg = true;
          cap        = std::min(cap, g.hi);
        }
      }
      if (any_nonneg) {
        f.lo = 0;
        f.hi = cap;
      }
      break;
    }
    case Ntype_op::Or: {
      f.kz = ~uint64_t{0};
      f.ko = 0;
      bool all_nonneg = true;
      Wide lo = 0, hi = 0;
      for (const auto& o : ops) {
        const auto g = get(o.cls);
        f.kz &= g.kz;
        f.ko |= g.ko;
        all_nonneg = all_nonneg && g.nonneg() && g.hi < kInf;
        lo         = std::max(lo, g.lo);
        hi         = std::max(hi, g.hi);
      }
      if (all_nonneg) {
        f.lo = lo;
        f.hi = mask_w(bitlen(hi));
      }
      break;
    }
    case Ntype_op::Xor: {
      uint64_t known = ~uint64_t{0}, value = 0;
      bool     all_nonneg = true;
      Wide     hi         = 0;
      for (const auto& o : ops) {
        const auto g = get(o.cls);
        known &= g.kz | g.ko;
        value ^= g.ko;
        all_nonneg = all_nonneg && g.nonneg() && g.hi < kInf;
        hi         = std::max(hi, g.hi);
      }
      f.kz = known & ~value;
      f.ko = known & value;
      if (all_nonneg) {
        f.lo = 0;
        f.hi = mask_w(bitlen(hi));
      }
      break;
    }
    case Ntype_op::Not: {
      if (ops.size() != 1) {
        break;
      }
      const auto g = get(ops[0].cls);
      f.lo = unb_hi(g.hi) ? -kInf : dn(-g.hi - 1);
      f.hi = unb_lo(g.lo) ? kInf : up(-g.lo - 1);
      f.kz = g.ko;
      f.ko = g.kz;
      break;
    }
    case Ntype_op::EQ:
    case Ntype_op::LT:
    case Ntype_op::GT:
    case Ntype_op::Ror:
    case Ntype_op::Rxor: f.lo = 0; f.hi = 1; break;
    case Ntype_op::Popcount: {
      f.lo = 0;
      if (const auto* amount = by_bank(ops, 1); amount != nullptr) {
        if (const auto w = const_i64(eg, amount->cls); w && *w >= 0) {
          f.hi = *w;
        }
      }
      break;
    }
    case Ntype_op::Get_mask: {
      const auto *a = by_bank(ops, 0), *lo = by_bank(ops, 2), *hi = by_bank(ops, 3);
      if (ops.size() != 3 || a == nullptr || lo == nullptr || hi == nullptr) {
        break;
      }
      const auto l = const_i64(eg, lo->cls), h = const_i64(eg, hi->cls);
      if (!l || !h || *l < 0 || *h <= *l || *h > (int64_t{1} << 30)) {
        break;
      }
      const int  w = static_cast<int>(std::min<int64_t>(*h - *l, 200));
      const auto g = get(a->cls);
      f.lo         = 0;
      f.hi         = mask_w(w);
      if (g.nonneg() && g.hi < kInf && *l < 100) {
        const Wide top_bits = g.hi >> *l;
        if (top_bits <= f.hi) {
          f.lo = g.lo >> *l;  // the mask drops nothing
          f.hi = top_bits;
        }
      }
      f.kz = (*l < 64 ? g.kz >> *l : 0) | ~low_mask(w);
      f.ko = (*l < 64 ? g.ko >> *l : 0) & low_mask(w);
      break;
    }
    case Ntype_op::Set_mask: {
      const auto *a = by_bank(ops, 0), *lo = by_bank(ops, 2), *hi = by_bank(ops, 3), *v = by_bank(ops, 4);
      if (ops.size() != 4 || a == nullptr || lo == nullptr || hi == nullptr || v == nullptr) {
        break;
      }
      const auto l = const_i64(eg, lo->cls), h = const_i64(eg, hi->cls);
      if (!l || !h || *l < 0 || *h <= *l || *h > 4096) {
        break;
      }
      const auto ga = get(a->cls), gv = get(v->cls);
      const uint64_t win = *h <= 64 ? (low_mask(static_cast<int>(*h)) & ~low_mask(static_cast<int>(*l))) : (*l < 64 ? ~low_mask(static_cast<int>(*l)) : 0);
      f.kz = (ga.kz & ~win) | (*l < 64 ? (gv.kz << *l) & win : 0);
      f.ko = (ga.ko & ~win) | (*l < 64 ? (gv.ko << *l) & win : 0);
      if (ga.nonneg() && ga.hi < kInf) {
        f.lo = 0;
        f.hi = mask_w(std::max<int>(bitlen(ga.hi), static_cast<int>(*h)));
      }
      break;
    }
    case Ntype_op::SHL: {
      const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        break;
      }
      const auto ga = get(a->cls);
      const auto k  = const_i64(eg, b->cls);
      if (k && *k >= 0 && *k <= 4096) {
        f.lo             = unb_lo(ga.lo) ? -kInf : shl_dn(ga.lo, *k);
        f.hi             = unb_hi(ga.hi) ? kInf : shl_up(ga.hi, *k);
        f.kz             = *k < 64 ? (ga.kz << *k) | low_mask(static_cast<int>(*k)) : ~uint64_t{0};
        f.ko             = *k < 64 ? ga.ko << *k : 0;
      } else if (ga.nonneg()) {
        f.lo = ga.lo;  // a shift left never shrinks a non-negative value
        f.kz = low_mask(trailing_known_zero(ga.kz));
      }
      break;
    }
    case Ntype_op::SRA: {
      const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        break;
      }
      const auto ga = get(a->cls);
      const auto k  = const_i64(eg, b->cls);
      if (k && *k >= 0 && *k <= 4096) {
        const int s = static_cast<int>(std::min<int64_t>(*k, 126));
        f.lo        = unb_lo(ga.lo) ? -kInf : ga.lo >> s;
        f.hi        = unb_hi(ga.hi) ? kInf : ga.hi >> s;
        f.kz        = *k < 64 ? ga.kz >> *k : 0;
        f.ko        = *k < 64 ? ga.ko >> *k : 0;
      } else if (ga.nonneg()) {
        f.lo = 0;
        f.hi = ga.hi;
      }
      break;
    }
    case Ntype_op::Sext: {
      const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        break;
      }
      const auto bits = const_i64(eg, b->cls);
      if (!bits || *bits < 1 || *bits > 98) {
        break;
      }
      const auto ga = get(a->cls);
      f.lo          = -(Wide{1} << (*bits - 1));
      f.hi          = (Wide{1} << (*bits - 1)) - 1;
      const int low = static_cast<int>(std::min<int64_t>(*bits - 1, 64));
      f.kz          = ga.kz & low_mask(low);
      f.ko          = ga.ko & low_mask(low);
      if (ga.lo >= f.lo && ga.hi <= f.hi) {
        f = meet(f, ga);  // a already fits: Sext keeps it
      }
      break;
    }
    case Ntype_op::Concat: {
      if (ops.size() % 2 != 0 || ops.empty()) {
        break;
      }
      int64_t total = 0;
      for (size_t i = 0; i < ops.size(); i += 2) {
        if (ops[i].bank != i || ops[i + 1].bank != i + 1) {
          return top();
        }
        const auto w = const_i64(eg, ops[i + 1].cls);
        if (!w || *w <= 0 || *w > 4096) {
          return top();
        }
        total += *w;
      }
      f.lo = 0;
      f.hi = mask_w(static_cast<int>(std::min<int64_t>(total, 200)));
      int64_t off = total;
      for (size_t i = 0; i < ops.size(); i += 2) {
        const int64_t w = *const_i64(eg, ops[i + 1].cls);
        off -= w;
        if (off >= 64) {
          continue;
        }
        const auto     g      = get(ops[i].cls);
        const uint64_t window = low_mask(static_cast<int>(std::min<int64_t>(w, 64 - off)));
        f.kz |= (g.kz & window) << off;
        f.ko |= (g.ko & window) << off;
      }
      // A lane value is masked to its window, so a bit is known only when the lane's own bit is known.
      break;
    }
    case Ntype_op::Mux: {
      const auto* sel = by_bank(ops, 0);
      if (sel == nullptr || ops.size() < 2) {
        break;
      }
      const size_t arms = ops.size() - 1;
      for (size_t i = 1; i < ops.size(); ++i) {
        if (ops[i].bank != i) {
          return top();  // a disconnected arm: the Q1 '?' selection could pick it
        }
      }
      if (arms > 2) {
        const auto gs = get(sel->cls);
        if (gs.lo < 0 || gs.hi > static_cast<Wide>(arms - 1)) {
          return top();  // an out-of-range selection is '?' (Q1): no fact about the result
        }
      }
      f = get(ops[1].cls);
      for (size_t i = 2; i < ops.size(); ++i) {
        f = join(f, get(ops[i].cls));
      }
      break;
    }
    case Ntype_op::Div: {
      const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        break;
      }
      const auto c = const_i64(eg, b->cls);
      if (!c || *c <= 0) {
        break;
      }
      const auto ga = get(a->cls);
      f.lo          = unb_lo(ga.lo) ? -kInf : ga.lo / *c;
      f.hi          = unb_hi(ga.hi) ? kInf : ga.hi / *c;
      break;
    }
    case Ntype_op::Rem: {
      const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        break;
      }
      const auto c = const_i64(eg, b->cls);
      if (!c || *c == 0 || *c == std::numeric_limits<int64_t>::min()) {
        break;
      }
      const Wide m  = (*c < 0 ? -*c : *c) - 1;
      const auto ga = get(a->cls);
      f.lo          = ga.lo >= 0 ? 0 : -m;
      f.hi          = ga.hi < 0 ? 0 : m;
      if (ga.nonneg()) {
        f.hi = std::min(f.hi, ga.hi);  // the remainder never exceeds a non-negative dividend
      }
      break;
    }
    default: break;
  }
  refine(f);
  return f;
}

Facts facts_rec(const Egraph& eg, Class_id c, int depth) {
  if (depth <= 0) {
    return top();
  }
  auto& cache = eg.analysis_cache().facts;
  if (const auto it = cache.find(c); it != cache.end() && it->second.depth >= depth) {
    return it->second.facts;
  }
  Facts       f;
  const auto& cl = eg.cls(c);
  switch (cl.kind) {
    case Egraph::Kind::konst: {
      const auto v = const_i64(eg, c);
      if (v) {
        f = of_const(*v);
      } else if (const auto* d = eg.value_of(c); d != nullptr && d->is_integer() && !d->has_unknowns()) {
        if (!d->is_negative()) {
          f.lo = 0;
        } else {
          f.hi = -1;
        }
      }
      break;
    }
    case Egraph::Kind::node: f = node_facts(eg, eg.enode(cl.first), depth); break;
    case Egraph::Kind::uni: f = meet(facts_rec(eg, cl.alt_a, depth - 1), facts_rec(eg, cl.alt_b, depth - 1)); break;
    case Egraph::Kind::leaf:
      if (const auto it = eg.analysis_cache().seeds.find(c); it != eg.analysis_cache().seeds.end()) {
        f = it->second;
      }
      break;
    default: break;  // a '?' literal: no bound
  }
  // Remember it with the budget it had: a deeper question later recomputes (a class is recomputed at most
  // kDepth times), a shallower one reuses it.
  cache.insert_or_assign(c, Cached_facts{f, static_cast<int8_t>(depth)});
  return f;
}

}  // namespace

void seed_leaf(const Egraph& eg, Class_id cls, int bits, bool is_unsigned) {
  if (bits > 98 && is_unsigned) {
    eg.analysis_cache().wide_seeds[cls] = bits;  // no interval fits, but the width is still a fact
    return;
  }
  if (bits <= 0 || bits > 98) {
    return;
  }
  Facts f;
  if (is_unsigned) {
    f.lo = 0;
    f.hi = mask_w(bits);
  } else {
    f.lo = -(Wide{1} << (bits - 1));
    f.hi = (Wide{1} << (bits - 1)) - 1;
  }
  refine(f);
  eg.analysis_cache().seeds[cls] = f;
}

namespace {

int64_t unsigned_bits_rec(const Egraph& eg, Class_id c, int depth) {
  int64_t best = -1;
  const auto keep = [&](int64_t n) {
    if (n >= 0 && (best < 0 || n < best)) {
      best = n;
    }
  };
  const auto facts = facts_rec(eg, c, 2);
  if (facts.nonneg() && facts.hi < kInf) {
    keep(bitlen(facts.hi));
  }
  const auto& cache = eg.analysis_cache().wide_seeds;
  if (const auto it = cache.find(c); it != cache.end()) {
    keep(it->second);
  }
  if (depth <= 0) {
    return best;
  }
  eg.for_each_member(c, 8, [&](Class_id member) {
    const Enode* n = eg.enode_of(member);
    if (n == nullptr) {
      return false;
    }
    const auto ops = eg.operands(*n);
    switch (n->op) {
      case Ntype_op::Get_mask: {
        const auto *lo = by_bank(ops, 2), *hi = by_bank(ops, 3);
        if (ops.size() != 3 || lo == nullptr || hi == nullptr) {
          break;
        }
        const auto l = const_i64(eg, lo->cls), h = const_i64(eg, hi->cls);
        if (l && h && *l >= 0 && *h > *l && *h - *l <= kMaxLaneBit) {
          keep(*h - *l);
        }
        break;
      }
      case Ntype_op::Concat: {
        if (ops.size() % 2 != 0 || ops.empty()) {
          break;
        }
        int64_t total = 0;
        bool    ok    = true;
        for (size_t i = 0; i < ops.size() && ok; i += 2) {
          const auto w = ops[i].bank == i && ops[i + 1].bank == i + 1 ? const_i64(eg, ops[i + 1].cls) : std::nullopt;
          ok           = w && *w > 0 && *w <= kMaxLaneBit;
          total += ok ? *w : 0;
        }
        if (ok && total <= kMaxLaneBit) {
          keep(total);
        }
        break;
      }
      case Ntype_op::Or:
      case Ntype_op::Xor: {
        int64_t widest = 0;
        for (const auto& o : ops) {
          const auto b = o.bank == 0 ? unsigned_bits_rec(eg, o.cls, depth - 1) : int64_t{-1};
          if (b < 0) {
            return false;
          }
          widest = std::max(widest, b);
        }
        keep(widest);
        break;
      }
      case Ntype_op::And: {
        for (const auto& o : ops) {
          if (o.bank == 0) {
            keep(unsigned_bits_rec(eg, o.cls, depth - 1));
          }
        }
        break;
      }
      case Ntype_op::SHL: {
        const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
        if (ops.size() != 2 || a == nullptr || b == nullptr) {
          break;
        }
        const auto k = const_i64(eg, b->cls);
        const auto w = k && *k >= 0 && *k <= kMaxLaneBit ? unsigned_bits_rec(eg, a->cls, depth - 1) : int64_t{-1};
        if (w >= 0) {
          keep(w + *k);
        }
        break;
      }
      case Ntype_op::SRA: {
        const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
        if (ops.size() != 2 || a == nullptr || b == nullptr) {
          break;
        }
        const auto k = const_i64(eg, b->cls);
        const auto w = k && *k >= 0 ? unsigned_bits_rec(eg, a->cls, depth - 1) : int64_t{-1};
        if (w >= 0) {
          keep(std::max<int64_t>(w - *k, 0));
        }
        break;
      }
      default: break;
    }
    return false;
  });
  return best;
}

}  // namespace

int64_t unsigned_bits(const Egraph& eg, Class_id cls) { return unsigned_bits_rec(eg, cls, 3); }

Facts facts_of(const Egraph& eg, Class_id cls) {
  return facts_rec(eg, cls, kDepth);
}

// ---------------------------------------------------------------------------
// (base, polarity)
// ---------------------------------------------------------------------------

Polarity polarity_of(const Egraph& eg, Class_id cls) {
  Polarity p;
  p.base = cls;
  const auto f = facts_of(eg, cls);
  if (!(f.lo >= 0 && f.hi <= 1)) {
    return p;
  }
  p.valid = true;
  // EQ(o, 0) of a 0/1 operand `o` is the complement of o's polarity.
  eg.for_each_member(cls, 8, [&](Class_id member) {
    const Enode* n = eg.enode_of(member);
    if (n == nullptr || n->op != Ntype_op::EQ) {
      return false;
    }
    const auto ops = eg.operands(*n);
    if (ops.size() != 2) {
      return false;
    }
    for (int side = 0; side < 2; ++side) {
      const auto zero  = const_i64(eg, ops[side].cls);
      const auto other = ops[1 - side].cls;
      if (!zero || *zero != 0) {
        continue;
      }
      const auto fo = facts_of(eg, other);
      if (!(fo.lo >= 0 && fo.hi <= 1)) {
        continue;
      }
      const auto inner = polarity_of(eg, other);
      if (inner.base == cls) {
        continue;
      }
      p.base     = inner.base;
      p.inverted = !inner.inverted;
      return true;
    }
    return false;
  });
  return p;
}

// ---------------------------------------------------------------------------
// Lane layout
// ---------------------------------------------------------------------------

const Lane* find_lane(const Layout& layout, int64_t lo, int64_t hi) {
  for (const auto& lane : layout) {
    if (lane.lo <= lo && hi <= lane.hi) {
      return &lane;
    }
  }
  return nullptr;
}

namespace {

constexpr size_t kMaxLayoutLanes = 256;

Layout identity_layout(Class_id c) { return {Lane{0, kLaneInf, c, 0, false}}; }

// Replace the window [lo, hi) of `base` by `lane`.
Layout overlay(const Layout& base, const Lane& lane) {
  Layout out;
  for (const auto& b : base) {
    if (b.hi <= lane.lo || b.lo >= lane.hi) {
      out.push_back(b);
      continue;
    }
    if (b.lo < lane.lo) {
      Lane left = b;
      left.hi   = lane.lo;
      out.push_back(left);
    }
    if (b.hi > lane.hi) {
      Lane right = b;
      right.lo   = lane.hi;
      right.off  = b.off + (lane.hi - b.lo);
      out.push_back(right);
    }
  }
  out.push_back(lane);
  std::ranges::sort(out, {}, &Lane::lo);
  return out;
}

// SHL(H, k) member of `c`, if any: returns (H, k).
std::optional<std::pair<Class_id, int64_t>> shl_of(const Egraph& eg, Class_id c) {
  std::optional<std::pair<Class_id, int64_t>> found;
  eg.for_each_member(c, 8, [&](Class_id member) {
    const Enode* n = eg.enode_of(member);
    if (n == nullptr || n->op != Ntype_op::SHL) {
      return false;
    }
    const auto ops = eg.operands(*n);
    const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
    if (ops.size() != 2 || a == nullptr || b == nullptr) {
      return false;
    }
    const auto k = const_i64(eg, b->cls);
    if (!k || *k <= 0 || *k > kMaxLaneBit) {
      return false;
    }
    found = std::pair{a->cls, *k};
    return true;
  });
  return found;
}

std::optional<Layout> layout_rec(const Egraph& eg, Class_id c, int depth);

// Or / Sum of disjoint shifted fields: field_i occupies [k_i, k_{i+1}) and fits it; the topmost field is open.
std::optional<Layout> packed_layout(const Egraph& eg, std::span<const Operand> ops) {
  struct Field {
    Class_id cls;
    int64_t  shift;
  };
  std::vector<Field> fields;
  bool               has_base = false;
  for (const auto& o : ops) {
    if (o.bank != 0) {
      return std::nullopt;
    }
    if (const auto zero = const_i64(eg, o.cls); zero && *zero == 0) {
      continue;
    }
    if (const auto s = shl_of(eg, o.cls); s) {
      fields.push_back({s->first, s->second});
    } else {
      if (has_base) {
        return std::nullopt;
      }
      has_base = true;
      fields.push_back({o.cls, 0});
    }
  }
  if (fields.size() < 2) {
    return std::nullopt;
  }
  std::ranges::sort(fields, {}, &Field::shift);
  Layout out;
  if (fields.front().shift > 0) {
    out.push_back(Lane{0, fields.front().shift, kNoClass, 0, true});
  }
  for (size_t i = 0; i < fields.size(); ++i) {
    const int64_t lo = fields[i].shift;
    const int64_t hi = i + 1 < fields.size() ? fields[i + 1].shift : kLaneInf;
    if (hi <= lo) {
      return std::nullopt;  // two fields at one shift
    }
    if (i + 1 < fields.size()) {
      const auto g = facts_of(eg, fields[i].cls);
      bool fits = false;
      if (g.nonneg() && g.hi < kInf) {
        fits = g.hi <= mask_w(static_cast<int>(std::min<int64_t>(hi - lo, 100)));
      } else {
        const auto bits = unsigned_bits(eg, fields[i].cls);  // wider than the interval facts reach
        fits            = bits >= 0 && bits <= hi - lo;
      }
      if (!fits) {
        return std::nullopt;  // the field could spill into the next one
      }
    }
    out.push_back(Lane{lo, hi, fields[i].cls, 0, false});
  }
  return out;
}

std::optional<Layout> node_layout(const Egraph& eg, const Enode& n) {
  const auto ops = eg.operands(n);
  switch (n.op) {
    case Ntype_op::Concat: {
      if (ops.size() % 2 != 0 || ops.empty()) {
        return std::nullopt;
      }
      int64_t total = 0;
      for (size_t i = 0; i < ops.size(); i += 2) {
        if (ops[i].bank != i || ops[i + 1].bank != i + 1) {
          return std::nullopt;
        }
        const auto w = const_i64(eg, ops[i + 1].cls);
        if (!w || *w <= 0 || *w > kMaxLaneBit) {
          return std::nullopt;
        }
        total += *w;
      }
      Layout  out;
      int64_t off = total;
      for (size_t i = 0; i < ops.size(); i += 2) {
        const int64_t w = *const_i64(eg, ops[i + 1].cls);
        off -= w;
        out.push_back(Lane{off, off + w, ops[i].cls, 0, false});
      }
      std::ranges::sort(out, {}, &Lane::lo);
      out.push_back(Lane{total, kLaneInf, kNoClass, 0, true});  // a Concat is non-negative
      return out;
    }
    case Ntype_op::Set_mask: {
      const auto *a = by_bank(ops, 0), *lo = by_bank(ops, 2), *hi = by_bank(ops, 3), *v = by_bank(ops, 4);
      if (ops.size() != 4 || a == nullptr || lo == nullptr || hi == nullptr || v == nullptr) {
        return std::nullopt;
      }
      const auto l = const_i64(eg, lo->cls), h = const_i64(eg, hi->cls);
      if (!l || !h || *l < 0 || *h <= *l || *h > kMaxLaneBit) {
        return std::nullopt;
      }
      // The base layout comes through the cache (layout_of warms a write chain bottom-up), so a long Set_mask chain
      // never recurses; a base with too many lanes is left as an opaque word.
      const auto base = layout_of(eg, a->cls);
      if (base->size() > kMaxLayoutLanes) {
        return std::nullopt;
      }
      return overlay(*base, Lane{*l, *h, v->cls, 0, false});
    }
    case Ntype_op::SRA: {
      // SRA(x, k): bit q of the result is bit q + k of x, so every lane of x moves down by k (sign included).
      const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        return std::nullopt;
      }
      const auto k = const_i64(eg, b->cls);
      if (!k || *k <= 0 || *k > kMaxLaneBit) {
        return std::nullopt;
      }
      const auto base = layout_of(eg, a->cls);
      Layout     out;
      for (const auto& lane : *base) {
        if (lane.hi != kLaneInf && lane.hi <= *k) {
          continue;
        }
        Lane moved = lane;
        moved.lo   = std::max<int64_t>(lane.lo - *k, 0);
        moved.hi   = lane.hi == kLaneInf ? kLaneInf : lane.hi - *k;
        moved.off  = lane.zero ? 0 : lane.off + (moved.lo + *k - lane.lo);
        out.push_back(moved);
      }
      return out;
    }
    case Ntype_op::SHL: {
      const auto *a = by_bank(ops, 0), *b = by_bank(ops, 1);
      if (ops.size() != 2 || a == nullptr || b == nullptr) {
        return std::nullopt;
      }
      const auto k = const_i64(eg, b->cls);
      if (!k || *k <= 0 || *k > kMaxLaneBit) {
        return std::nullopt;
      }
      return Layout{Lane{0, *k, kNoClass, 0, true}, Lane{*k, kLaneInf, a->cls, 0, false}};
    }
    case Ntype_op::Or:
    case Ntype_op::Sum: return packed_layout(eg, ops);
    default: break;
  }
  return std::nullopt;
}

std::optional<Layout> layout_rec(const Egraph& eg, Class_id c, int depth) {
  if (depth <= 0) {
    return std::nullopt;
  }
  const auto& cl = eg.cls(c);
  if (cl.kind == Egraph::Kind::node) {
    return node_layout(eg, eg.enode(cl.first));
  }
  if (cl.kind == Egraph::Kind::uni) {
    if (auto a = layout_rec(eg, cl.alt_a, depth - 1)) {
      return a;
    }
    return layout_rec(eg, cl.alt_b, depth - 1);
  }
  return std::nullopt;
}

}  // namespace

namespace {

// The class a Set_mask / SRA member of `c` reads as its base, or kNoClass: the chain a layout is built along.
Class_id layout_base(const Egraph& eg, Class_id c) {
  const auto& cl = eg.cls(c);
  if (cl.kind != Egraph::Kind::node) {
    return kNoClass;
  }
  const Enode& n = eg.enode(cl.first);
  if (n.op != Ntype_op::Set_mask && n.op != Ntype_op::SRA) {
    return kNoClass;
  }
  const auto ops = eg.operands(n);
  const auto* a  = by_bank(ops, 0);
  return a == nullptr ? kNoClass : a->cls;
}

}  // namespace

std::shared_ptr<const Layout> layout_of(const Egraph& eg, Class_id cls) {
  auto& cache = eg.analysis_cache().layouts;
  if (const auto it = cache.find(cls); it != cache.end()) {
    return it->second;
  }
  // Warm the base chain bottom-up first: a write chain of thousands of Set_masks is then a loop, not a recursion.
  std::vector<Class_id> chain;
  for (Class_id c = cls; c != kNoClass && !cache.contains(c) && chain.size() < (size_t{1} << 20); c = layout_base(eg, c)) {
    chain.push_back(c);
  }
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
    auto found = layout_rec(eg, *it, 4);
    cache.emplace(*it, std::make_shared<const Layout>(found ? std::move(*found) : identity_layout(*it)));
  }
  return cache.find(cls)->second;
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

Analysis_registry& Analysis_registry::instance() {
  static Analysis_registry registry;
  return registry;
}

void Analysis_registry::add(const Analysis_info& info) {
  list_.push_back(info);
  std::ranges::sort(list_, [](const Analysis_info& a, const Analysis_info& b) { return std::string_view{a.name} < b.name; });
}

const Analysis_info* Analysis_registry::find(std::string_view name) const {
  for (const auto& a : list_) {
    if (name == a.name) {
      return &a;
    }
  }
  return nullptr;
}

namespace {
[[nodiscard]] bool window_ok(int64_t n) { return n >= 0 && n <= 98; }
}  // namespace

}  // namespace livehd::aegraph::rt

// ---------------------------------------------------------------------------
// The analyses a guard can name (rules/analyses.decl). AEG_ANALYSIS opens its own namespaces, so these sit at
// global scope.
// ---------------------------------------------------------------------------

using namespace livehd::aegraph;
using namespace livehd::aegraph::rt;

// 0 or 1: the structural test (comparisons, 1-bit windows, boolean And/Or/Xor/Mux) and the interval [0, 1].
AEG_ANALYSIS(bool01, 1, 0) { return ctx.bool01(cls[0]); }

// lo <= x <= hi for every value of the class.
AEG_ANALYSIS(range, 1, 2) {
  const auto f = facts_of(ctx.eg(), cls[0]);
  return f.lo >= k[0] && f.hi <= k[1];
}

AEG_ANALYSIS(nonneg, 1, 0) { return facts_of(ctx.eg(), cls[0]).nonneg(); }

// 0 <= x < 2^n.
AEG_ANALYSIS(fits, 1, 1) {
  if (!window_ok(k[0])) {
    return false;
  }
  const auto f = facts_of(ctx.eg(), cls[0]);
  return f.nonneg() && f.hi <= mask_w(static_cast<int>(k[0]));
}

// -2^(n-1) <= x < 2^(n-1).
AEG_ANALYSIS(sfits, 1, 1) {
  if (k[0] < 1 || !window_ok(k[0])) {
    return false;
  }
  const auto f = facts_of(ctx.eg(), cls[0]);
  return f.lo >= -(Wide{1} << (k[0] - 1)) && f.hi <= (Wide{1} << (k[0] - 1)) - 1;
}

// Every bit of the mask m (non-negative, below bit 63) is a known 0.
AEG_ANALYSIS(known_zero, 1, 1) {
  if (k[0] < 0) {
    return false;
  }
  const auto f = facts_of(ctx.eg(), cls[0]);
  return (f.kz & static_cast<uint64_t>(k[0])) == static_cast<uint64_t>(k[0]);
}

// Every bit of the mask m (non-negative) is a known 1.
AEG_ANALYSIS(known_one, 1, 1) {
  if (k[0] < 0) {
    return false;
  }
  const auto f = facts_of(ctx.eg(), cls[0]);
  return (f.ko & static_cast<uint64_t>(k[0])) == static_cast<uint64_t>(k[0]);
}

// The low n bits are structurally zero (a left shift, a multiple, a packed word whose low field is empty).
AEG_ANALYSIS(low_zero, 1, 1) {
  if (k[0] < 0 || k[0] > 64) {
    return false;
  }
  const auto f = facts_of(ctx.eg(), cls[0]);
  return (f.kz & low_mask(static_cast<int>(k[0]))) == low_mask(static_cast<int>(k[0]));
}

// x & y == 0: no bit position can be 1 in both.
AEG_ANALYSIS(exclusive, 2, 0) {
  const auto& eg = ctx.eg();
  const auto fx = facts_of(eg, cls[0]), fy = facts_of(eg, cls[1]);
  if (fx.nonneg() && fy.nonneg() && fx.hi < (Wide{1} << 64) && fy.hi < (Wide{1} << 64) && ((~fx.kz) & (~fy.kz)) == 0) {
    return true;
  }
  const auto px = polarity_of(eg, cls[0]), py = polarity_of(eg, cls[1]);
  if (px.valid && py.valid && px.base == py.base && px.inverted != py.inverted) {
    return true;  // b and its complement
  }
  // EQ(b, c1) and EQ(b, c2) with c1 != c2 are never both true.
  const auto eq_const = [&](Class_id c) {
    std::optional<std::pair<Class_id, int64_t>> out;
    eg.for_each_member(c, 8, [&](Class_id member) {
      const Enode* n = eg.enode_of(member);
      if (n == nullptr || n->op != Ntype_op::EQ) {
        return false;
      }
      const auto ops = eg.operands(*n);
      if (ops.size() != 2) {
        return false;
      }
      for (int side = 0; side < 2; ++side) {
        if (const auto v = const_i64(eg, ops[side].cls); v && !const_i64(eg, ops[1 - side].cls)) {
          out = std::pair{ops[1 - side].cls, *v};
          return true;
        }
      }
      return false;
    });
    return out;
  };
  const auto ex = eq_const(cls[0]), ey = eq_const(cls[1]);
  return ex && ey && ex->first == ey->first && ex->second != ey->second;
}

// Opposite booleans: y = 1 - x, one is the base and the other EQ(base, 0).
AEG_ANALYSIS(opposite, 2, 0) {
  const auto& eg = ctx.eg();
  const auto px = polarity_of(eg, cls[0]), py = polarity_of(eg, cls[1]);
  return px.valid && py.valid && px.base == py.base && px.inverted != py.inverted;
}

// The window [lo, hi) of x lies inside one lane of x's layout whose source is another class (or is zero).
AEG_ANALYSIS(has_lane, 1, 2) {
  if (k[0] < 0 || k[1] <= k[0]) {
    return false;
  }
  const auto  layout = layout_of(ctx.eg(), cls[0]);
  const auto* lane   = find_lane(*layout, k[0], k[1]);
  return lane != nullptr && (lane->zero || lane->src != cls[0]);
}

// Every x is below every y: the largest x is under the smallest y.
AEG_ANALYSIS(lt_always, 2, 0) {
  const auto fx = facts_of(ctx.eg(), cls[0]), fy = facts_of(ctx.eg(), cls[1]);
  return fx.hi < kInf && fy.lo > -kInf && fx.hi < fy.lo;
}

// Every x is at least every y.
AEG_ANALYSIS(ge_always, 2, 0) {
  const auto fx = facts_of(ctx.eg(), cls[0]), fy = facts_of(ctx.eg(), cls[1]);
  return fx.lo > -kInf && fy.hi < kInf && fx.lo >= fy.hi;
}

// x != y: disjoint intervals, or a bit that is a known 1 in one and a known 0 in the other.
AEG_ANALYSIS(distinct, 2, 0) {
  const auto fx = facts_of(ctx.eg(), cls[0]), fy = facts_of(ctx.eg(), cls[1]);
  return (fx.hi < kInf && fy.lo > -kInf && fx.hi < fy.lo) || (fy.hi < kInf && fx.lo > -kInf && fy.hi < fx.lo)
         || ((fx.ko & fy.kz) | (fx.kz & fy.ko)) != 0;
}

AEG_ANALYSIS(nonzero, 1, 0) {
  const auto f = facts_of(ctx.eg(), cls[0]);
  return f.lo > 0 || f.hi < 0 || f.ko != 0;
}

AEG_ANALYSIS(zero, 1, 0) {
  const auto f = facts_of(ctx.eg(), cls[0]);
  return f.lo == 0 && f.hi == 0;
}
