//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

// ── Lnast_range — simple signed min/max range lattice ────────────────────────
//
// A range with explicit ±∞ flags instead of overflow sentinels.  The two
// invariants are:
//   1. When both neg_inf and pos_inf are false: min <= max.
//   2. When neg_inf is true the `min` field is ignored.
//      When pos_inf is true the `max` field is ignored.
//
// All arithmetic methods return unbounded() conservatively on int64_t overflow.
// Boolean / comparison results use the signed-1-bit lattice point {-1, 0}.
struct Lnast_range {
  int64_t min{0};
  int64_t max{0};
  bool    neg_inf{true};  // min = -∞
  bool    pos_inf{true};  // max = +∞

  // ── Constructors / named factories ────────────────────────────────────────

  static constexpr Lnast_range unbounded() noexcept {
    Lnast_range r;
    r.neg_inf = true;
    r.pos_inf = true;
    return r;
  }

  static constexpr Lnast_range constant(int64_t v) noexcept {
    Lnast_range r;
    r.min     = v;
    r.max     = v;
    r.neg_inf = false;
    r.pos_inf = false;
    return r;
  }

  // Signed 1-bit: range [-1, 0].  Used for boolean and comparison results.
  static constexpr Lnast_range boolean() noexcept {
    Lnast_range r;
    r.min     = -1;
    r.max     = 0;
    r.neg_inf = false;
    r.pos_inf = false;
    return r;
  }

  // ── Predicates ────────────────────────────────────────────────────────────

  bool is_constant() const noexcept { return !neg_inf && !pos_inf && min == max; }
  bool is_unbounded() const noexcept { return neg_inf || pos_inf; }

  // True when this range covers a strictly smaller set of values than `other`.
  // An unbounded side is never narrower than anything (so we return false).
  bool is_narrower_than(const Lnast_range& other) const noexcept {
    if (is_unbounded()) return false;
    if (other.is_unbounded()) return true;
    return min > other.min || max < other.max;
  }

  // True iff all representable values are ≥ 0.
  bool is_always_positive() const noexcept { return !neg_inf && min >= 0; }
  // True iff all representable values are < 0.
  bool is_always_negative() const noexcept { return !pos_inf && max < 0; }
  // True when the range spans negative values (i.e. two's complement signed).
  bool is_signed() const noexcept { return neg_inf || min < 0; }

  // ── Signed bit-width ──────────────────────────────────────────────────────
  // Returns the minimum number of bits needed to represent [min, max] in two's
  // complement. Returns 64 (a conservative upper bound) for unbounded ranges.
  int64_t get_sbits() const noexcept {
    if (neg_inf || pos_inf) return 64;
    return std::max(sbits_for(min), sbits_for(max));
  }

  // ── Lattice operations ────────────────────────────────────────────────────

  // join: widest range covering both — used for if/else mux join.
  Lnast_range join(const Lnast_range& b) const noexcept {
    if (neg_inf || pos_inf || b.neg_inf || b.pos_inf) {
      // If either side is totally unbounded return unbounded.
      Lnast_range r;
      r.neg_inf = neg_inf   || b.neg_inf;
      r.pos_inf = pos_inf   || b.pos_inf;
      if (!r.neg_inf) r.min = std::min(min, b.min);
      if (!r.pos_inf) r.max = std::max(max, b.max);
      return r;
    }
    Lnast_range r;
    r.min     = std::min(min, b.min);
    r.max     = std::max(max, b.max);
    r.neg_inf = false;
    r.pos_inf = false;
    return r;
  }

  // meet: intersection — used for explicit constraints.
  // If the intersection is empty, returns unbounded (conservative fallback).
  Lnast_range meet(const Lnast_range& b) const noexcept {
    if (neg_inf || pos_inf) return b;  // [−∞,+∞] ∩ b = b
    if (b.neg_inf || b.pos_inf) return *this;
    int64_t lo = std::max(min, b.min);
    int64_t hi = std::min(max, b.max);
    if (lo > hi) return unbounded();  // empty intersection → conservative
    Lnast_range r;
    r.min     = lo;
    r.max     = hi;
    r.neg_inf = false;
    r.pos_inf = false;
    return r;
  }

  // ── Arithmetic ────────────────────────────────────────────────────────────

  Lnast_range add(const Lnast_range& b) const noexcept {
    if (is_unbounded() || b.is_unbounded()) return unbounded();
    int64_t lo, hi;
    if (add_overflow(min, b.min, lo) || add_overflow(max, b.max, hi)) return unbounded();
    return bounded(lo, hi);
  }

  Lnast_range sub(const Lnast_range& b) const noexcept {
    if (is_unbounded() || b.is_unbounded()) return unbounded();
    // [a.min - b.max, a.max - b.min]
    int64_t lo, hi;
    if (sub_overflow(min, b.max, lo) || sub_overflow(max, b.min, hi)) return unbounded();
    return bounded(lo, hi);
  }

  Lnast_range mul(const Lnast_range& b) const noexcept {
    if (is_unbounded() || b.is_unbounded()) return unbounded();
    // All four corner products.
    using i128 = __int128;
    i128 c0 = (i128)min * b.min;
    i128 c1 = (i128)min * b.max;
    i128 c2 = (i128)max * b.min;
    i128 c3 = (i128)max * b.max;
    i128 lo = std::min({c0, c1, c2, c3});
    i128 hi = std::max({c0, c1, c2, c3});
    constexpr i128 kMin = (i128)std::numeric_limits<int64_t>::min();
    constexpr i128 kMax = (i128)std::numeric_limits<int64_t>::max();
    if (lo < kMin || hi > kMax) return unbounded();
    return bounded((int64_t)lo, (int64_t)hi);
  }

  Lnast_range neg() const noexcept {
    if (is_unbounded()) return unbounded();
    // Check INT64_MIN negation overflow.
    if (min == std::numeric_limits<int64_t>::min()) return unbounded();
    return bounded(-max, -min);
  }

  // Logical shift left: result = [min << b_lo, max << b_hi].
  // Conservative: returns unbounded when b is not bounded-positive.
  Lnast_range shl(const Lnast_range& amt) const noexcept {
    if (is_unbounded()) return unbounded();
    if (amt.is_unbounded() || amt.min < 0) return unbounded();
    if (amt.min > 62 || amt.max > 62) return unbounded();
    using i128 = __int128;
    i128 lo = (i128)min << amt.min;
    i128 hi = (i128)max << amt.max;
    constexpr i128 kMin = (i128)std::numeric_limits<int64_t>::min();
    constexpr i128 kMax = (i128)std::numeric_limits<int64_t>::max();
    if (lo < kMin || hi > kMax) return unbounded();
    return bounded((int64_t)std::min(lo, hi), (int64_t)std::max(lo, hi));
  }

  // Arithmetic shift right: conservative when b is unknown.
  Lnast_range sra(const Lnast_range& amt) const noexcept {
    if (is_unbounded()) return unbounded();
    if (amt.is_unbounded() || amt.min < 0) return unbounded();
    int64_t shift = amt.min;  // shift by minimum narrows range the least
    if (shift > 63) shift = 63;
    int64_t lo = min >> shift;
    int64_t hi = max >> shift;
    return bounded(std::min(lo, hi), std::max(lo, hi));
  }

private:
  // ── Helpers ───────────────────────────────────────────────────────────────

  static constexpr Lnast_range bounded(int64_t lo, int64_t hi) noexcept {
    Lnast_range r;
    r.min     = lo;
    r.max     = hi;
    r.neg_inf = false;
    r.pos_inf = false;
    return r;
  }

  // Minimum signed bits to represent the value v in two's complement.
  static constexpr int64_t sbits_for(int64_t v) noexcept {
    if (v >= 0) {
      // Need floor(log2(v))+1 data bits + 1 sign bit = bit_width(v) + 1.
      return static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(v))) + 1;
    }
    // v < 0: smallest N where -2^(N-1) <= v, i.e. 2^(N-1) >= -v.
    // N-1 = ceil(log2(-v)) = bit_width(-v - 1).
    return static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(-v - 1))) + 1;
  }

  // Returns true and leaves `result` undefined on overflow.
  static bool add_overflow(int64_t a, int64_t b, int64_t& result) noexcept {
    using i128 = __int128;
    i128 r = (i128)a + b;
    if (r < std::numeric_limits<int64_t>::min() || r > std::numeric_limits<int64_t>::max()) return true;
    result = (int64_t)r;
    return false;
  }

  static bool sub_overflow(int64_t a, int64_t b, int64_t& result) noexcept {
    using i128 = __int128;
    i128 r = (i128)a - b;
    if (r < std::numeric_limits<int64_t>::min() || r > std::numeric_limits<int64_t>::max()) return true;
    result = (int64_t)r;
    return false;
  }
};
