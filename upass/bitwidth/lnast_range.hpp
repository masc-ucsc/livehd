//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

// ── Lnast_range — signed integer value-range lattice ─────────────────────────
//
// (Goal 1n N3) There are NO `+inf`/`-inf` flags: a range is either a concrete
// `[min, max]` or it is `unbounded` (no information). This is the "absent when
// unbounded" model — there is no half-bounded `(-inf, hi]` / `[lo, +inf)` state,
// because under the clean type model every declared envelope supplies BOTH
// bounds (`prim_type_int(max,min)`), so bare `max`-only / `min`-only constraints
// no longer arise.
//
// Bounds are int64_t today; any arithmetic that would exceed int64_t collapses
// to `unbounded` (the conservative top of the lattice). The remaining 1n step
// is the mechanical swap of these bounds to exact arbitrary-precision `Dlop`
// (so >64-bit envelopes need no collapse); it is deferred until `lnast_to_lgraph`
// exists to consume the published ranges — see todo/ 1n N3.
//
// Invariant: when `unbounded` is false, `min <= max`.
// Boolean / comparison results use the signed-1-bit lattice point {-1, 0}.
struct Lnast_range {
  int64_t min{0};
  int64_t max{0};
  bool    unbounded{true};

  // ── Constructors / named factories ────────────────────────────────────────

  static constexpr Lnast_range make_unbounded() noexcept {
    Lnast_range r;
    r.unbounded = true;
    return r;
  }

  static constexpr Lnast_range constant(int64_t v) noexcept {
    Lnast_range r;
    r.min       = v;
    r.max       = v;
    r.unbounded = false;
    return r;
  }

  // Signed 1-bit: range [-1, 0].  Used for boolean and comparison results.
  static constexpr Lnast_range boolean() noexcept { return bounded(-1, 0); }

  // ── Predicates ────────────────────────────────────────────────────────────

  bool is_constant() const noexcept { return !unbounded && min == max; }
  bool is_unbounded() const noexcept { return unbounded; }

  // True when this range covers a strictly smaller set of values than `other`.
  // An unbounded side is never narrower than anything (so we return false).
  bool is_narrower_than(const Lnast_range& other) const noexcept {
    if (unbounded) {
      return false;
    }
    if (other.unbounded) {
      return true;
    }
    return min > other.min || max < other.max;
  }

  // True iff every value representable by `other` is also representable by
  // *this — i.e. set containment *this ⊇ other. This is the type-envelope fit
  // predicate (task 1b): `envelope.contains(value_range)`. Sign-agnostic
  // (compares the signed bounds directly), so a non-negative 9-bit value like
  // 255 is contained by u8's [0,255] even though its signed width is 9. An
  // unbounded value range is never contained by a bounded envelope.
  bool contains(const Lnast_range& other) const noexcept {
    if (unbounded) {
      return true;  // (-inf,+inf) ⊇ anything
    }
    if (other.unbounded) {
      return false;  // bounded never contains unbounded
    }
    return min <= other.min && max >= other.max;
  }

  // True iff all representable values are ≥ 0.
  bool is_always_positive() const noexcept { return !unbounded && min >= 0; }
  // True iff all representable values are < 0.
  bool is_always_negative() const noexcept { return !unbounded && max < 0; }
  // True when the range spans negative values (i.e. two's complement signed).
  bool is_signed() const noexcept { return unbounded || min < 0; }

  // ── Signed bit-width ──────────────────────────────────────────────────────
  // Returns the minimum number of bits needed to represent [min, max] in two's
  // complement. Returns 64 (a conservative upper bound) for unbounded ranges.
  int64_t get_sbits() const noexcept {
    if (unbounded) {
      return 64;
    }
    return std::max(sbits_for(min), sbits_for(max));
  }

  // ── Lattice operations ────────────────────────────────────────────────────

  // join: widest range covering both — used for if/else mux join.
  Lnast_range join(const Lnast_range& b) const noexcept {
    if (unbounded || b.unbounded) {
      return make_unbounded();
    }
    return bounded(std::min(min, b.min), std::max(max, b.max));
  }

  // meet: intersection — used for explicit constraints.
  // If the intersection is empty, returns unbounded (conservative fallback).
  Lnast_range meet(const Lnast_range& b) const noexcept {
    if (unbounded) {
      return b;
    }
    if (b.unbounded) {
      return *this;
    }
    int64_t lo = std::max(min, b.min);
    int64_t hi = std::min(max, b.max);
    if (lo > hi) {
      return make_unbounded();  // empty intersection → conservative
    }
    return bounded(lo, hi);
  }

  // ── Bitwise (soundness: a naive range-UNION would neither
  //    bound `a&b` from below nor `a|b`/`a^b` from above — `0x55|0xaa=0xff`
  //    escaped [0x55,0xaa]) ─────────────────────────────────────────────────

  // Smallest all-ones mask covering v (v >= 0): 0→0, 5→7, 0xaa→0xff.
  static constexpr int64_t ones_cover(int64_t v) noexcept {
    uint64_t u = static_cast<uint64_t>(v);
    u |= u >> 1;
    u |= u >> 2;
    u |= u >> 4;
    u |= u >> 8;
    u |= u >> 16;
    u |= u >> 32;
    return static_cast<int64_t>(u);
  }

  // a & b: for non-negatives, 0 <= a&b <= min(max_a, max_b). Any possible
  // negative operand → unbounded (sign-extension makes the bound unsafe).
  Lnast_range band(const Lnast_range& b) const noexcept {
    if (!unbounded && !b.unbounded && is_constant() && b.is_constant()) {
      return constant(min & b.min);  // single points fold exactly (any sign)
    }
    if (unbounded || b.unbounded || min < 0 || b.min < 0) {
      return make_unbounded();
    }
    return bounded(0, std::min(max, b.max));
  }

  // a | b: for non-negatives, max(min_a, min_b) <= a|b <= ones-cover of the
  // wider operand's max (OR never sets a bit above either operand's top bit).
  Lnast_range bor(const Lnast_range& b) const noexcept {
    if (!unbounded && !b.unbounded && is_constant() && b.is_constant()) {
      return constant(min | b.min);  // single points fold exactly (any sign)
    }
    if (unbounded || b.unbounded || min < 0 || b.min < 0) {
      return make_unbounded();
    }
    return bounded(std::max(min, b.min), ones_cover(std::max(max, b.max)));
  }

  // a ^ b: for non-negatives, 0 <= a^b <= ones-cover of the wider max.
  Lnast_range bxor(const Lnast_range& b) const noexcept {
    if (!unbounded && !b.unbounded && is_constant() && b.is_constant()) {
      return constant(min ^ b.min);  // single points fold exactly (any sign)
    }
    if (unbounded || b.unbounded || min < 0 || b.min < 0) {
      return make_unbounded();
    }
    return bounded(0, ones_cover(std::max(max, b.max)));
  }

  // ~x = -x - 1, exactly: range flips to [-max-1, -min-1].
  Lnast_range bnot() const noexcept {
    if (unbounded) {
      return make_unbounded();
    }
    return bounded(-max - 1, -min - 1);
  }

  // ── Arithmetic ────────────────────────────────────────────────────────────

  Lnast_range add(const Lnast_range& b) const noexcept {
    if (unbounded || b.unbounded) {
      return make_unbounded();
    }
    int64_t lo, hi;
    if (add_overflow(min, b.min, lo) || add_overflow(max, b.max, hi)) {
      return make_unbounded();
    }
    return bounded(lo, hi);
  }

  Lnast_range sub(const Lnast_range& b) const noexcept {
    if (unbounded || b.unbounded) {
      return make_unbounded();
    }
    // [a.min - b.max, a.max - b.min]
    int64_t lo, hi;
    if (sub_overflow(min, b.max, lo) || sub_overflow(max, b.min, hi)) {
      return make_unbounded();
    }
    return bounded(lo, hi);
  }

  Lnast_range mul(const Lnast_range& b) const noexcept {
    if (unbounded || b.unbounded) {
      return make_unbounded();
    }
    using i128          = __int128;
    i128           c0   = (i128)min * b.min;
    i128           c1   = (i128)min * b.max;
    i128           c2   = (i128)max * b.min;
    i128           c3   = (i128)max * b.max;
    i128           lo   = std::min({c0, c1, c2, c3});
    i128           hi   = std::max({c0, c1, c2, c3});
    constexpr i128 kMin = (i128)std::numeric_limits<int64_t>::min();
    constexpr i128 kMax = (i128)std::numeric_limits<int64_t>::max();
    if (lo < kMin || hi > kMax) {
      return make_unbounded();
    }
    return bounded((int64_t)lo, (int64_t)hi);
  }

  Lnast_range neg() const noexcept {
    if (unbounded) {
      return make_unbounded();
    }
    if (min == std::numeric_limits<int64_t>::min()) {
      return make_unbounded();  // INT64_MIN negation overflow
    }
    return bounded(-max, -min);
  }

  // Integer division (Goal 1n N5). For any integer divisor d with |d| >= 1,
  // |a / d| <= |a|, so the quotient fits the dividend's magnitude. (d == 0 is
  // undefined; we don't model it.) Non-negative dividend with strictly-positive
  // divisor stays in [0, a.max].
  Lnast_range div(const Lnast_range& b) const noexcept {
    if (unbounded) {
      return make_unbounded();
    }
    int64_t m;
    if (!magnitude(m)) {
      return make_unbounded();  // INT64_MIN — can't take |.|
    }
    if (min >= 0 && !b.unbounded && b.min >= 1) {
      return bounded(0, max);
    }
    return bounded(-m, m);
  }

  // Integer remainder (Goal 1n N5). |a % d| < |d| and |a % d| <= |a|; the sign
  // follows the dividend (truncated semantics). Falls back to the dividend's
  // magnitude when the divisor is unknown.
  Lnast_range mod(const Lnast_range& b) const noexcept {
    if (unbounded) {
      return make_unbounded();
    }
    int64_t ma;
    if (!magnitude(ma)) {
      return make_unbounded();
    }
    int64_t bound = ma;
    if (!b.unbounded) {
      int64_t db;
      if (!b.magnitude(db) || db == 0) {
        return make_unbounded();
      }
      bound = std::min(ma, db - 1);
    }
    if (min >= 0) {
      return bounded(0, bound);
    }
    if (max < 0) {
      return bounded(-bound, 0);
    }
    return bounded(-bound, bound);
  }

  // Sign-extend to a signed value whose sign bit sits at position `sign_bit`
  // (0-indexed). The result lies in [-2^sign_bit, 2^sign_bit - 1] (Goal 1n N5).
  // sign_bit < 0 or too wide → unbounded.
  static Lnast_range sext_to(int64_t sign_bit) noexcept {
    if (sign_bit < 0 || sign_bit >= 63) {
      return make_unbounded();
    }
    const int64_t lo = -(int64_t{1} << sign_bit);
    const int64_t hi = (int64_t{1} << sign_bit) - 1;
    return bounded(lo, hi);
  }

  // Logical shift left: result = [min << b_lo, max << b_hi].
  // Conservative: returns unbounded when b is not bounded-positive.
  Lnast_range shl(const Lnast_range& amt) const noexcept {
    if (unbounded || amt.unbounded || amt.min < 0) {
      return make_unbounded();
    }
    if (amt.min > 62 || amt.max > 62) {
      return make_unbounded();
    }
    using i128          = __int128;
    i128           lo   = (i128)min << amt.min;
    i128           hi   = (i128)max << amt.max;
    constexpr i128 kMin = (i128)std::numeric_limits<int64_t>::min();
    constexpr i128 kMax = (i128)std::numeric_limits<int64_t>::max();
    if (lo < kMin || hi > kMax) {
      return make_unbounded();
    }
    return bounded((int64_t)std::min(lo, hi), (int64_t)std::max(lo, hi));
  }

  // Arithmetic shift right: conservative when b is unknown.
  Lnast_range sra(const Lnast_range& amt) const noexcept {
    if (unbounded || amt.unbounded || amt.min < 0) {
      return make_unbounded();
    }
    int64_t shift = amt.min;  // shift by minimum narrows range the least
    if (shift > 63) {
      shift = 63;
    }
    int64_t lo = min >> shift;
    int64_t hi = max >> shift;
    return bounded(std::min(lo, hi), std::max(lo, hi));
  }

private:
  // ── Helpers ───────────────────────────────────────────────────────────────

  static constexpr Lnast_range bounded(int64_t lo, int64_t hi) noexcept {
    Lnast_range r;
    r.min       = lo;
    r.max       = hi;
    r.unbounded = false;
    return r;
  }

  // Largest |endpoint| of a bounded range, into `out`. Returns false if either
  // endpoint is INT64_MIN (whose magnitude overflows int64_t).
  bool magnitude(int64_t& out) const noexcept {
    constexpr int64_t kMin = std::numeric_limits<int64_t>::min();
    if (min == kMin || max == kMin) {
      return false;
    }
    out = std::max(min < 0 ? -min : min, max < 0 ? -max : max);
    return true;
  }

  // Minimum signed bits to represent the value v in two's complement.
  static constexpr int64_t sbits_for(int64_t v) noexcept {
    if (v >= 0) {
      return static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(v))) + 1;
    }
    return static_cast<int64_t>(std::bit_width(static_cast<uint64_t>(-v - 1))) + 1;
  }

  static bool add_overflow(int64_t a, int64_t b, int64_t& result) noexcept {
    using i128 = __int128;
    i128 r     = (i128)a + b;
    if (r < std::numeric_limits<int64_t>::min() || r > std::numeric_limits<int64_t>::max()) {
      return true;
    }
    result = (int64_t)r;
    return false;
  }

  static bool sub_overflow(int64_t a, int64_t b, int64_t& result) noexcept {
    using i128 = __int128;
    i128 r     = (i128)a - b;
    if (r < std::numeric_limits<int64_t>::min() || r > std::numeric_limits<int64_t>::max()) {
      return true;
    }
    result = (int64_t)r;
    return false;
  }
};
