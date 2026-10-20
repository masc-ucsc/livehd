//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// Hand-written rules, milestone G: window reads through the persistent lane layout
// (runtime/analyses.hpp) and the n-ary Concat clean-ups. The fixed-shape window rules
// (Get_mask of Get_mask / SHL / SRA / Set_mask, Set_mask and Concat shapes, comparisons by range) are
// in mask.rules.
//
// Get_mask(x, lo, hi) reads bits [lo, hi) of x as a non-negative number. Concat lanes are (value, width)
// pairs on pins 2i / 2i+1, most significant lane first.

#include <algorithm>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"

#include "runtime/analyses.hpp"
#include "runtime/match.hpp"

using namespace livehd::aegraph;
using namespace livehd::aegraph::rt;

AEG_RULE_GROUP(mask);

namespace {

// The operands of a three-operand Get_mask root: value, lo, hi (banks 0, 2, 3).
[[nodiscard]] bool get_mask_window(Ctx& ctx, const Root& root, Class_id& x, int64_t& lo, int64_t& hi) {
  if (root.ops.size() != 3 || root.ops[0].bank != 0 || root.ops[1].bank != 2 || root.ops[2].bank != 3) {
    return false;
  }
  const auto l = ctx.small_int(root.ops[1].cls), h = ctx.small_int(root.ops[2].cls);
  if (!l || !h || *l < 0 || *h <= *l) {
    return false;
  }
  x  = root.ops[0].cls;
  lo = *l;
  hi = *h;
  return true;
}

Class_id make_get_mask(Ctx& ctx, Class_id x, int64_t lo, int64_t hi) {
  return ctx.make(Ntype_op::Get_mask, {Operand{0, x}, Operand{2, ctx.konst(lo)}, Operand{3, ctx.konst(hi)}});
}

struct Lane_in {
  Class_id value;
  int64_t  width;
};

[[nodiscard]] bool concat_lanes(Ctx& ctx, const Root& root, std::vector<Lane_in>& lanes) {
  if (root.ops.size() % 2 != 0 || root.ops.empty()) {
    return false;
  }
  for (size_t i = 0; i < root.ops.size(); i += 2) {
    if (root.ops[i].bank != i || root.ops[i + 1].bank != i + 1) {
      return false;
    }
    const auto w = ctx.small_int(root.ops[i + 1].cls);
    if (!w || *w <= 0 || *w > kMaxLaneBit) {
      return false;
    }
    lanes.push_back({root.ops[i].cls, *w});
  }
  return true;
}

// (source, lo, hi) when the lane value is a Get_mask(source, lo, hi) member with a constant window.
struct Slice {
  Class_id source = kNoClass;
  int64_t  lo     = 0;
  int64_t  hi     = 0;
};
[[nodiscard]] std::optional<Slice> slice_of(Ctx& ctx, Class_id value) {
  std::optional<Slice> found;
  ctx.each_member(value, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    if (n == nullptr || n->op != Ntype_op::Get_mask) {
      return false;
    }
    const auto ops = ctx.operands_of(*n);
    if (ops.size() != 3 || ops[0].bank != 0 || ops[1].bank != 2 || ops[2].bank != 3) {
      return false;
    }
    const auto l = ctx.small_int(ops[1].cls), h = ctx.small_int(ops[2].cls);
    if (!l || !h || *l < 0 || *h <= *l) {
      return false;
    }
    found = Slice{ops[0].cls, *l, *h};
    return true;
  });
  return found;
}

}  // namespace

// Get_mask(x, lo, hi) where x's lane layout (a Concat, a Set_mask chain, a SHL, a packed Or/Sum of disjoint
// shifted fields) holds the whole window in one lane: read the lane's source instead, or zero for an empty
// lane. The persistent layout is built once per class, so a window read of a deep packed word is a lookup.
AEG_NATIVE_RULE(getmask_lane, simplify, 40, Ntype_op::Get_mask) {
  Class_id x = kNoClass;
  int64_t  lo = 0, hi = 0;
  if (!get_mask_window(ctx, root, x, lo, hi)) {
    return;
  }
  const auto  layout = layout_of(ctx.eg(), x);
  const Lane* lane   = find_lane(*layout, lo, hi);
  if (lane == nullptr) {
    return;
  }
  if (lane->zero) {
    out.subsume(ctx.konst(0));
    return;
  }
  if (lane->src == x) {
    return;  // the identity lane of a class without structure
  }
  const int64_t s_lo = lane->off + (lo - lane->lo);
  const int64_t s_hi = s_lo + (hi - lo);
  if (s_lo == 0) {
    const auto f = facts_of(ctx.eg(), lane->src);
    if (f.nonneg() && f.hi < kInf && f.hi <= ((Wide{1} << std::min<int64_t>(s_hi, 99)) - 1)) {
      out.subsume(lane->src);  // the lane is exactly the window and already fits it
      return;
    }
    if (const auto bits = unsigned_bits(ctx.eg(), lane->src); bits >= 0 && bits <= s_hi) {
      out.subsume(lane->src);  // a value wider than the interval facts reach (a wide input) that still fits the window
      return;
    }
  }
  out.subsume(make_get_mask(ctx, lane->src, s_lo, s_hi));
}

// Get_mask(y, 0, n) = y when 0 <= y < 2^n, with two exceptions that keep the classic shapes the later passes read.
// The mask of a LEAF (an input or a register output) is kept: it is where the width fact of the value is written in
// the graph, and the region passes that follow (mux sharing, muxctx) read booleans structurally, so dropping it
// makes them spell a Ror/EQ to get one back (wire_cond_match grew from 5 to 8 nodes). The mask of a Sum is kept
// too: a mask between two additions is what stops the chain from merging into one wide Sum, and the low-lane
// narrowing (which runs before bitwidth deletes the redundant masks, as it does in classic) works on each partial
// sum (loop_ref_arg: +26% pred_aig when the chain was flattened first).
AEG_NATIVE_RULE(getmask_fits, simplify, 30, Ntype_op::Get_mask) {
  Class_id y = kNoClass;
  int64_t  lo = 0, hi = 0;
  if (!get_mask_window(ctx, root, y, lo, hi) || lo != 0) {
    return;
  }
  const auto f = facts_of(ctx.eg(), y);
  if (!(f.nonneg() && f.hi < kInf && f.hi <= ((Wide{1} << std::min<int64_t>(hi, 99)) - 1))) {
    return;
  }
  if (ctx.eg().cls(y).kind == Egraph::Kind::leaf) {
    return;
  }
  bool arith = false;
  ctx.each_member(y, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    arith          = arith || (n != nullptr && n->op == Ntype_op::Sum);
    return arith;
  });
  if (arith) {
    return;
  }
  out.subsume(y);
}

// A window of an Or / Xor / Sum / And does not read the terms that cannot reach it:
//   * a term whose low `hi` bits are zero (a constant left shift by >= hi, a multiple of 2^hi) adds nothing to the bits
//     below `hi` of an Or, Xor or Sum (a Sum carries only upwards), and zeroes the whole window of an And;
//   * a non-negative term below 2^lo has no bits in [lo, hi): it adds nothing to an Or or Xor window and zeroes an And.
// The bit-gather ladder `((m[63] << 63) | .. | (m[1] << 1) | m[0])[0]` reads one bit of 64 shifted rows, and classic's
// demanded-bits narrowing leaves only the row that owns the bit. Sole readers, or a window that keeps at most half of
// the terms, replace the read (simplify); otherwise the narrower Or is an alternative the extraction prices.
AEG_NATIVE_RULE(getmask_drop_terms, simplify, 36, Ntype_op::Get_mask) {
  Class_id x = kNoClass;
  int64_t  lo = 0, hi = 0;
  if (!get_mask_window(ctx, root, x, lo, hi) || hi > kMaxLaneBit) {
    return;
  }
  const Enode* bank = nullptr;
  ctx.each_member(x, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    if (n != nullptr && (n->op == Ntype_op::Or || n->op == Ntype_op::Xor || n->op == Ntype_op::Sum || n->op == Ntype_op::And)) {
      bank = n;
      return true;
    }
    return false;
  });
  if (bank == nullptr) {
    return;
  }
  const Ntype_op op = bank->op;
  const auto     all = ctx.operands_of(*bank);
  if (all.size() < 2 || all.size() > 4096) {
    return;
  }
  const std::vector<Operand> terms(all.begin(), all.end());  // make() below invalidates the span
  ctx.grant_fuel(terms.size() * 6);
  const auto low_zero = [&](Class_id t) {
    bool zero = false;
    ctx.each_member(t, [&](Class_id member) {
      const Enode* n = ctx.node_of(member);
      if (n == nullptr || n->op != Ntype_op::SHL) {
        return false;
      }
      const auto ops = ctx.operands_of(*n);
      if (ops.size() != 2 || ops[0].bank != 0 || ops[1].bank != 1) {
        return false;
      }
      const auto k = ctx.small_int(ops[1].cls);
      zero         = k && *k >= hi;
      return zero;
    });
    if (zero) {
      return true;
    }
    if (hi <= 64) {
      const auto f    = facts_of(ctx.eg(), t);
      const auto want = hi == 64 ? ~uint64_t{0} : (uint64_t{1} << hi) - 1;
      return (f.kz & want) == want;
    }
    return false;
  };
  const auto below_window = [&](Class_id t) {
    if (lo == 0) {
      return false;
    }
    const auto f = facts_of(ctx.eg(), t);
    if (f.nonneg() && f.hi < kInf && f.hi <= ((Wide{1} << std::min<int64_t>(lo, 99)) - 1)) {
      return true;
    }
    const int64_t bits = unsigned_bits(ctx.eg(), t);
    return bits >= 0 && bits <= lo;
  };
  std::vector<Operand> rest;
  bool                 dropped = false;
  for (const auto& t : terms) {
    const bool dead = low_zero(t.cls) || (op != Ntype_op::Sum && below_window(t.cls));
    if (!dead) {
      rest.push_back(t);
      continue;
    }
    if (op == Ntype_op::And) {
      out.subsume(ctx.konst(0));  // a term with no bit in the window clears it
      return;
    }
    dropped = true;
  }
  if (!dropped || op == Ntype_op::And) {
    return;
  }
  const size_t   kept     = rest.size();
  const Class_id narrowed = rest.empty() ? ctx.konst(0) : ctx.make_bank(op, std::move(rest));
  const Class_id window   = make_get_mask(ctx, narrowed, lo, hi);
  if (ctx.single_use(x) || kept <= 1 || 2 * kept <= terms.size()) {
    out.subsume(window);
  } else {
    out.add(window);
  }
}

// A low window of a Sum reads its operands modulo 2^hi, so an operand that is itself a low window Get_mask(y, 0, k) of a
// Sum y (k >= hi) can be y: the sums merge into one wider Sum under the outer window,
//   Get_mask(Sum(Get_mask(Sum(a, b), 0, 16), 1-), 0, 16) = Get_mask(Sum(a, b, 1-), 0, 16)
// (modular arithmetic: the truncation of the inner sum is undone by the outer one). Classic folds the same chain
// (`(base + size - 1)` of an address range). Only when every operand of y already fits `hi` bits, so the merged Sum
// is no wider than the outer window: a wide y would trade the inner mask for a wider adder.
AEG_NATIVE_RULE(getmask_sum_chain, simplify, 37, Ntype_op::Get_mask) {
  Class_id x = kNoClass;
  int64_t  lo = 0, hi = 0;
  if (!get_mask_window(ctx, root, x, lo, hi) || lo != 0 || hi > 4096) {
    return;
  }
  const Enode* sum = nullptr;
  ctx.each_member(x, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    if (n != nullptr && n->op == Ntype_op::Sum) {
      sum = n;
      return true;
    }
    return false;
  });
  if (sum == nullptr) {
    return;
  }
  const std::vector<Operand> terms(ctx.operands_of(*sum).begin(), ctx.operands_of(*sum).end());
  if (terms.size() < 2 || terms.size() > 64) {
    return;
  }
  ctx.grant_fuel(terms.size() * 8);
  std::vector<Operand> merged;
  bool                 peeled = false;
  for (const auto& t : terms) {
    const Enode* inner = nullptr;
    Class_id     y     = kNoClass;
    if (const auto s = slice_of(ctx, t.cls); s && s->lo == 0 && s->hi >= hi) {
      y = s->source;
      ctx.each_member(y, [&](Class_id member) {
        const Enode* n = ctx.node_of(member);
        if (n != nullptr && n->op == Ntype_op::Sum) {
          inner = n;
          return true;
        }
        return false;
      });
    }
    if (inner == nullptr) {
      merged.push_back(t);
      continue;
    }
    const std::vector<Operand> ys(ctx.operands_of(*inner).begin(), ctx.operands_of(*inner).end());
    bool                       narrow = ys.size() <= 16;
    for (const auto& o : ys) {
      const int64_t bits = unsigned_bits(ctx.eg(), o.cls);
      narrow             = narrow && bits >= 0 && bits <= hi;
    }
    if (!narrow) {
      merged.push_back(t);
      continue;
    }
    for (const auto& o : ys) {
      // a subtracted Sum operand under a subtracted slot adds back: flip the bank when the slot is subtracted
      merged.push_back(Operand{t.bank == 0 ? o.bank : (o.bank == 0 ? uint32_t{1} : uint32_t{0}), o.cls});
    }
    peeled = true;
  }
  if (!peeled) {
    return;
  }
  out.subsume(make_get_mask(ctx, ctx.make_bank(Ntype_op::Sum, std::move(merged)), 0, hi));
}

// A window that spans several lanes of a Concat only this Get_mask reads is the Concat of the covered pieces:
//   Get_mask(Concat(.., v_i, w_i, ..), lo, hi) = Concat(Get_mask(v_i, a_i, b_i), b_i - a_i, ...)
// over the lanes the window touches (classic indexed_get_mask re-packs a private Concat the same way). The
// pieces are narrower than the lanes, so what feeds a lane is demanded at the window's width only. A Concat
// other readers keep alive is left alone: it exists either way and the re-pack would cost its lanes twice.
AEG_NATIVE_RULE(getmask_concat_straddle, simplify, 35, Ntype_op::Get_mask) {
  Class_id x = kNoClass;
  int64_t  lo = 0, hi = 0;
  if (!get_mask_window(ctx, root, x, lo, hi) || !ctx.single_use(x)) {
    return;
  }
  const Enode* concat = nullptr;
  ctx.each_member(x, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    if (n != nullptr && n->op == Ntype_op::Concat) {
      concat = n;
      return true;
    }
    return false;
  });
  if (concat == nullptr) {
    return;
  }
  const auto ops = ctx.operands_of(*concat);
  if (ops.size() % 2 != 0 || ops.size() < 4 || ops.size() > 128) {
    return;
  }
  std::vector<Lane_in> lanes;
  int64_t              total = 0;
  for (size_t i = 0; i < ops.size(); i += 2) {
    if (ops[i].bank != i || ops[i + 1].bank != i + 1) {
      return;
    }
    const auto w = ctx.small_int(ops[i + 1].cls);
    if (!w || *w <= 0 || *w > kMaxLaneBit) {
      return;
    }
    lanes.push_back({ops[i].cls, *w});
    total += *w;
  }
  std::vector<Operand> pieces;
  int64_t              off     = total;  // the lane's lowest bit within the Concat
  int64_t              touched = 0;
  for (const auto& lane : lanes) {
    off -= lane.width;
    const int64_t a = std::max(lo, off), b = std::min(hi, off + lane.width);
    if (b <= a) {
      continue;
    }
    ++touched;
    const bool whole = a == off && b == off + lane.width;
    const auto k = static_cast<uint32_t>(2 * (touched - 1));
    pieces.push_back(Operand{k, whole ? lane.value : make_get_mask(ctx, lane.value, a - off, b - off)});
    pieces.push_back(Operand{k + 1, ctx.konst(b - a)});
  }
  // One lane: getmask_lane's. The window must end inside the Concat (above it the value is zero padding, a
  // Concat of fewer bits than the window would read as narrower than declared).
  if (touched < 2 || hi > total) {
    return;
  }
  out.subsume(ctx.make(Ntype_op::Concat, std::span<const Operand>(pieces.data(), pieces.size())));
}

// The per-lane form of classic vectorize_bit_reductions for its simplest family: an And (Or) with a run of at
// least four operands that are single bits of ONE word at adjacent positions,
//   And(x[a], x[a+1], .., x[a+n-1], rest)  =  And(EQ(x[a +: n], 2^n - 1), rest)
//   Or (x[a], x[a+1], .., x[a+n-1], rest)  =  Or (Ror(x[a +: n]), rest)
// (each operand is a 0/1 value, so the AND is "every bit of the window set" and the OR is "any bit set"). An alt:
// the extraction keeps whichever is cheaper, and the classic region pass still vectorizes the shapes that need a
// whole expression over the bits (Or(EQ(request[j], 0), priority[i][j])).
AEG_NATIVE_RULE(bit_run_reduction, alt, 20, Ntype_op::And, Ntype_op::Or) {
  constexpr size_t kMinTerms = 4;
  if (root.ops.size() < kMinTerms || root.ops.size() > 256) {
    return;
  }
  struct Bit {
    size_t   index;  // operand index
    int64_t  pos;
  };
  absl::flat_hash_map<Class_id, std::vector<Bit>> by_source;
  std::vector<Class_id>                           order;  // first-seen: deterministic
  for (size_t i = 0; i < root.ops.size(); ++i) {
    if (root.ops[i].bank != 0) {
      return;
    }
    const auto s = slice_of(ctx, root.ops[i].cls);
    if (!s || s->lo < 0 || s->hi != s->lo + 1) {
      continue;
    }
    auto [it, fresh] = by_source.try_emplace(s->source);
    if (fresh) {
      order.push_back(s->source);
    }
    it->second.push_back({i, s->lo});
  }
  for (const Class_id source : order) {
    auto& bits = by_source[source];
    if (bits.size() < kMinTerms) {
      continue;
    }
    std::ranges::sort(bits, {}, &Bit::pos);
    // The first run of strictly consecutive positions that is long enough (a repeated position ends a run).
    size_t start = 0;
    while (start < bits.size()) {
      size_t end = start + 1;
      while (end < bits.size() && bits[end].pos == bits[end - 1].pos + 1) {
        ++end;
      }
      if (end - start >= kMinTerms && end - start <= 62) {
        const int64_t        lo = bits[start].pos, n = static_cast<int64_t>(end - start);
        std::vector<bool>    taken(root.ops.size(), false);
        for (size_t k = start; k < end; ++k) {
          taken[bits[k].index] = true;
        }
        const Class_id window = make_get_mask(ctx, source, lo, lo + n);
        const Class_id test   = root.op == Ntype_op::And
                                    ? ctx.make(Ntype_op::EQ, {Operand{0, window}, Operand{0, ctx.konst((int64_t{1} << n) - 1)}})
                                    : ctx.make(Ntype_op::Ror, {Operand{0, window}});
        std::vector<Operand> rest{Operand{0, test}};
        for (size_t i = 0; i < root.ops.size(); ++i) {
          if (!taken[i]) {
            rest.push_back(Operand{0, root.ops[i].cls});
          }
        }
        out.add(ctx.make_bank(root.op, std::move(rest)));
        return;
      }
      start = end;
    }
  }
}

// Or of shifted windows of ONE word whose positions are contiguous in both the source and the destination:
//   Or(x[a +: n] << s, x[a+n +: m] << (s+n), rest) = Or(x[a +: n+m] << s, rest)
// (a bare window is shifted by zero). Every shifted window is `x[lo, hi) << s`, so its bit lo+i sits at s+i; two
// windows with the same delta lo-s whose destination ranges touch tile one wider window exactly. This is the
// bit-gather ladder `(g[3]<<3) | (g[2]<<2) | (g[1]<<1) | g[0]` that classic canonicalizes (pack canonicalization plus
// vectorize_bit_reductions) BEFORE a surrounding Or flattens it away; the engine's `flatten` merges nested Ors
// first, so without this rule a strided gather stays one shift and one Get_mask per bit. An alt: extraction keeps
// whichever form the profile prices lower.
AEG_NATIVE_RULE(or_shifted_windows, alt, 21, Ntype_op::Or) {
  constexpr size_t kMaxTerms = size_t{1} << 14;
  if (root.ops.size() < 2 || root.ops.size() > kMaxTerms) {
    return;
  }
  // One scan of every operand: the member fuel of a rule run is sized for a few operands, so an n-ary root pays for
  // its own arity (a 1392-bit gather would otherwise stop being read after its first 40 terms).
  ctx.grant_fuel(root.ops.size() * 12);
  struct Term {
    size_t   index;
    Class_id source;
    int64_t  lo, hi, shift;
    [[nodiscard]] int64_t delta() const { return lo - shift; }
  };
  std::vector<Term> terms;
  for (size_t i = 0; i < root.ops.size(); ++i) {
    if (root.ops[i].bank != 0) {
      return;
    }
    const Class_id cls = root.ops[i].cls;
    bool           got = false;
    ctx.each_member(cls, [&](Class_id member) {
      const Enode* n = ctx.node_of(member);
      if (n == nullptr || n->op != Ntype_op::SHL) {
        return false;
      }
      const auto ops = ctx.operands_of(*n);
      if (ops.size() != 2 || ops[0].bank != 0 || ops[1].bank != 1) {
        return false;
      }
      const auto amount = ctx.small_int(ops[1].cls);
      const auto slice  = slice_of(ctx, ops[0].cls);
      if (!amount || *amount < 0 || *amount > kMaxLaneBit || !slice) {
        return false;
      }
      terms.push_back({i, slice->source, slice->lo, slice->hi, *amount});
      got = true;
      return true;
    });
    if (!got) {
      if (const auto slice = slice_of(ctx, cls)) {
        terms.push_back({i, slice->source, slice->lo, slice->hi, 0});
      }
    }
  }
  if (terms.size() < 2) {
    return;
  }
  std::ranges::sort(terms, [](const Term& a, const Term& b) {
    return std::tuple{a.source, a.delta(), a.shift, a.index} < std::tuple{b.source, b.delta(), b.shift, b.index};
  });
  std::vector<bool>    taken(root.ops.size(), false);
  std::vector<Operand> merged;  // the rebuilt terms
  size_t               start = 0;
  while (start < terms.size()) {
    size_t  end  = start + 1;
    int64_t next = terms[start].shift + (terms[start].hi - terms[start].lo);
    while (end < terms.size() && terms[end].source == terms[start].source && terms[end].delta() == terms[start].delta()
           && terms[end].shift == next) {
      next += terms[end].hi - terms[end].lo;
      ++end;
    }
    if (end - start >= 2) {
      const Term&    first  = terms[start];
      const int64_t  lo     = first.lo;
      const int64_t  hi     = lo + (next - first.shift);
      const Class_id window = make_get_mask(ctx, first.source, lo, hi);
      for (size_t k = start; k < end; ++k) {
        taken[terms[k].index] = true;
      }
      merged.push_back(Operand{0, first.shift == 0 ? window : ctx.make(Ntype_op::SHL, {Operand{0, window}, Operand{1, ctx.konst(first.shift)}})});
    }
    start = end;
  }
  if (merged.empty()) {
    return;
  }
  std::vector<Operand> rest = std::move(merged);
  for (size_t i = 0; i < root.ops.size(); ++i) {
    if (!taken[i]) {
      rest.push_back(Operand{0, root.ops[i].cls});
    }
  }
  out.add(ctx.make_bank(Ntype_op::Or, std::move(rest)));
}

// Or of fields at DISJOINT constant windows, each a value of known width optionally shifted left by a constant:
//   Or(a << 8, b << 4, c)  =  Concat(a, w_a, b, 4, c, 4)       (w_a, 4 and 4 the widths; gaps are zero lanes)
// Classic's canonicalize_or_pack (the bit-gather ladder `(m[63] << 63) | .. | m[0]` of a packed row). An alt: the
// profile prices a constant shift and a Concat as wiring (synth) or as one instruction per lane (sim), and an Or of n
// shifted fields as n - 1 gates, so the original stays when a field is shared and the Concat would not be cheaper.
// Every field needs a proven width (unsigned_bits) and the windows must not overlap, or the rule declines.
AEG_NATIVE_RULE(or_pack_to_concat, alt, 19, Ntype_op::Or) {
  constexpr size_t kMinFields = 3;
  constexpr size_t kMaxFields = 1024;
  if (root.ops.size() < kMinFields || root.ops.size() > kMaxFields) {
    return;
  }
  ctx.grant_fuel(root.ops.size() * 8);
  struct Field {
    Class_id value;
    int64_t  lo, width;
  };
  std::vector<Field> fields;
  for (const auto& o : root.ops) {
    if (o.bank != 0) {
      return;
    }
    Class_id value = o.cls;
    int64_t  shift = 0;
    ctx.each_member(o.cls, [&](Class_id member) {
      const Enode* n = ctx.node_of(member);
      if (n == nullptr || n->op != Ntype_op::SHL) {
        return false;
      }
      const auto ops = ctx.operands_of(*n);
      if (ops.size() != 2 || ops[0].bank != 0 || ops[1].bank != 1) {
        return false;
      }
      const auto k = ctx.small_int(ops[1].cls);
      if (!k || *k < 0 || *k > kMaxLaneBit) {
        return false;
      }
      value = ops[0].cls;
      shift = *k;
      return true;
    });
    const int64_t w = unsigned_bits(ctx.eg(), value);
    if (w < 0 || shift + w > kMaxLaneBit) {
      return;  // a field without a proven width: no window
    }
    if (w == 0) {
      continue;  // provably zero: contributes nothing
    }
    fields.push_back({value, shift, w});
  }
  if (fields.size() < kMinFields) {
    return;
  }
  std::ranges::sort(fields, [](const Field& a, const Field& b) { return a.lo < b.lo; });
  std::vector<Operand> ops;  // built least significant first, then reversed by pairs
  std::vector<std::pair<Class_id, int64_t>> lanes;
  int64_t pos = 0;
  for (const auto& f : fields) {
    if (f.lo < pos) {
      return;  // overlapping windows: the bits merge, which a Concat cannot say
    }
    if (f.lo > pos) {
      lanes.emplace_back(ctx.konst(0), f.lo - pos);
    }
    lanes.emplace_back(f.value, f.width);
    pos = f.lo + f.width;
  }
  for (size_t i = lanes.size(); i-- > 0;) {  // most significant first
    const auto k = static_cast<uint32_t>(ops.size());
    ops.push_back(Operand{k, lanes[i].first});
    ops.push_back(Operand{k + 1, ctx.konst(lanes[i].second)});
  }
  out.add(ctx.make(Ntype_op::Concat, std::span<const Operand>(ops.data(), ops.size())));
}

// One lane of a pack being rebuilt: value, bit offset and width.
struct Pack_lane {
  Class_id value;
  int64_t  lo;
  int64_t  width;
};

// The lanes (least significant first) a Set_mask base can be read as, or false: constant zero (no lanes), a Concat,
// or a value that fits the low `low` bits (one lane).
[[nodiscard]] bool pack_lanes_of(Ctx& ctx, Class_id base, int64_t low, std::vector<Pack_lane>& lanes) {
  if (ctx.is_int(base, 0)) {
    return true;
  }
  const Enode* concat = nullptr;
  ctx.each_member(base, [&](Class_id member) {
    const Enode* n = ctx.node_of(member);
    if (n != nullptr && n->op == Ntype_op::Concat) {
      concat = n;
      return true;
    }
    return false;
  });
  if (concat != nullptr) {
    const auto ops = ctx.operands_of(*concat);
    if (ops.size() % 2 != 0 || ops.empty() || ops.size() > 128) {
      return false;
    }
    int64_t total = 0;
    for (size_t i = 0; i < ops.size(); i += 2) {
      const auto w = ctx.small_int(ops[i + 1].cls);
      if (ops[i].bank != i || ops[i + 1].bank != i + 1 || !w || *w <= 0 || *w > kMaxLaneBit) {
        return false;
      }
      total += *w;
    }
    int64_t off = total;
    for (size_t i = 0; i < ops.size(); i += 2) {
      const int64_t w = *ctx.small_int(ops[i + 1].cls);
      off -= w;
      lanes.insert(lanes.begin(), Pack_lane{ops[i].cls, off, w});
    }
    return true;
  }
  const auto f = facts_of(ctx.eg(), base);
  if (low > 0 && f.nonneg() && f.hi < kInf && f.hi <= ((Wide{1} << std::min<int64_t>(low, 99)) - 1)) {
    lanes.push_back(Pack_lane{base, 0, low});
    return true;
  }
  return false;
}

// Set_mask(base, lo, hi, v) over a pack (zero, a Concat, a value that fits below the window) whose lanes the
// window covers whole, or extends, is the pack with that lane replaced: a chain of constant, disjoint lane writes
// (a packed array or struct built field by field) becomes one Concat, as classic's pack canonicalization makes it.
// A write that cuts a lane in two, or a base that is anything else, keeps the Set_mask.
AEG_NATIVE_RULE(setmask_to_concat, simplify, 20, Ntype_op::Set_mask) {
  if (root.ops.size() != 4 || root.ops[0].bank != 0 || root.ops[1].bank != 2 || root.ops[2].bank != 3 || root.ops[3].bank != 4) {
    return;
  }
  const auto l = ctx.small_int(root.ops[1].cls), h = ctx.small_int(root.ops[2].cls);
  if (!l || !h || *l < 0 || *h <= *l || *h > kMaxLaneBit) {
    return;
  }
  std::vector<Pack_lane> base;
  if (!pack_lanes_of(ctx, root.ops[0].cls, *l, base)) {
    return;
  }
  std::vector<Pack_lane> lanes;  // least significant first
  bool                   placed   = false;
  int64_t                kept_top = 0;  // the end of the kept lanes below the window
  for (const auto& lane : base) {
    const int64_t end = lane.lo + lane.width;
    if (end <= *l) {
      lanes.push_back(lane);
      kept_top = end;
    } else if (lane.lo >= *h) {
      if (!placed) {
        lanes.push_back(Pack_lane{root.ops[3].cls, *l, *h - *l});
        placed = true;
      }
      lanes.push_back(lane);
    } else if (lane.lo >= *l && end <= *h) {
      continue;  // covered whole: replaced
    } else {
      return;  // the window cuts this lane
    }
  }
  if (!placed) {
    if (*l > kept_top) {  // the window starts above the pack: zero padding in between
      lanes.push_back(Pack_lane{ctx.konst(0), kept_top, *l - kept_top});
    }
    lanes.push_back(Pack_lane{root.ops[3].cls, *l, *h - *l});
  }
  // The window's own lane keeps only the low bits of v when v is wider.
  std::vector<Operand> ops;
  for (size_t i = lanes.size(); i-- > 0;) {  // most significant first
    Class_id value = lanes[i].value;
    if (lanes[i].value == root.ops[3].cls && lanes[i].lo == *l && lanes[i].width == *h - *l) {
      const auto f = facts_of(ctx.eg(), value);
      if (!(f.nonneg() && f.hi < kInf && f.hi <= ((Wide{1} << std::min<int64_t>(lanes[i].width, 99)) - 1))) {
        value = make_get_mask(ctx, value, 0, lanes[i].width);
      }
    }
    const auto k = static_cast<uint32_t>(ops.size());
    ops.push_back(Operand{k, value});
    ops.push_back(Operand{k + 1, ctx.konst(lanes[i].width)});
  }
  out.subsume(ctx.make(Ntype_op::Concat, std::span<const Operand>(ops.data(), ops.size())));
}

// SRA(x, k) of a non-negative value below 2^n is the window Get_mask(x, k, n). It is only rewritten when the
// lane layout of x holds that window in one lane of another class (or zero): the packed read an array element
// or a struct field makes (`m[3]` of a Set_mask / Concat word is a shift by the lane offset). A plain right
// shift of an unstructured value stays a shift, which costs one operation less than shift-and-mask.
AEG_NATIVE_RULE(sra_lane, simplify, 40, Ntype_op::SRA) {
  if (root.ops.size() != 2 || root.ops[0].bank != 0 || root.ops[1].bank != 1) {
    return;
  }
  const auto k = ctx.small_int(root.ops[1].cls);
  if (!k || *k <= 0 || *k > kMaxLaneBit) {
    return;
  }
  const Class_id x = root.ops[0].cls;
  const auto     f = facts_of(ctx.eg(), x);
  int64_t        n = 0;
  if (f.nonneg() && f.hi < kInf) {
    while (n < 120 && (Wide{1} << n) <= f.hi) {
      ++n;
    }
  } else {
    n = unsigned_bits(ctx.eg(), x);  // a value wider than the interval facts reach still has a width
    if (n < 0) {
      return;
    }
  }
  if (n <= *k) {
    out.subsume(ctx.konst(0));
    return;
  }
  const auto  layout = layout_of(ctx.eg(), x);
  const Lane* lane   = find_lane(*layout, *k, n);
  if (lane == nullptr || (!lane->zero && lane->src == x)) {
    return;
  }
  out.subsume(make_get_mask(ctx, x, *k, n));
}

// Concat clean-ups on the lane list: a leading constant-zero lane adds nothing (a Concat is non-negative and
// the zero lane only pads); two adjacent constant lanes are one constant lane; two adjacent windows of the
// same source, reassembled in order, are one wider window. The partial-constant Concat shapes.
AEG_NATIVE_RULE(concat_lanes, simplify, 30, Ntype_op::Concat) {
  std::vector<Lane_in> lanes;
  if (!concat_lanes(ctx, root, lanes)) {
    return;
  }
  bool changed = false;
  while (lanes.size() > 1 && ctx.is_int(lanes.front().value, 0)) {
    lanes.erase(lanes.begin());
    changed = true;
  }
  for (size_t i = 0; i + 1 < lanes.size();) {
    Lane_in&       hi_lane = lanes[i];
    const Lane_in& lo_lane = lanes[i + 1];
    const auto     ch = ctx.small_int(hi_lane.value), cl = ctx.small_int(lo_lane.value);
    if (ch && cl && hi_lane.width + lo_lane.width <= 62) {
      const int64_t mh = *ch & ((int64_t{1} << hi_lane.width) - 1), ml = *cl & ((int64_t{1} << lo_lane.width) - 1);
      hi_lane.value = ctx.konst((mh << lo_lane.width) | ml);
      hi_lane.width += lo_lane.width;
      lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(i) + 1);
      changed = true;
      continue;
    }
    const auto sh = slice_of(ctx, hi_lane.value), sl = slice_of(ctx, lo_lane.value);
    if (sh && sl && sh->source == sl->source && sh->lo == sl->hi && sh->hi - sh->lo == hi_lane.width
        && sl->hi - sl->lo == lo_lane.width) {
      hi_lane.value = make_get_mask(ctx, sh->source, sl->lo, sh->hi);
      hi_lane.width += lo_lane.width;
      lanes.erase(lanes.begin() + static_cast<std::ptrdiff_t>(i) + 1);
      changed = true;
      continue;
    }
    ++i;
  }
  if (!changed) {
    return;
  }
  std::vector<Operand> ops;
  for (size_t i = 0; i < lanes.size(); ++i) {
    ops.push_back(Operand{static_cast<uint32_t>(2 * i), lanes[i].value});
    ops.push_back(Operand{static_cast<uint32_t>(2 * i + 1), ctx.konst(lanes[i].width)});
  }
  out.subsume(ctx.make(Ntype_op::Concat, std::span<const Operand>(ops.data(), ops.size())));
}
