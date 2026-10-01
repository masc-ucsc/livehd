//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "hlop/dlop.hpp"

namespace upass {

// Canonical reconstruction of an integer's value range from its magnitude bit
// width. `bits` is the read-only `.[bits]` attribute — the inverse of
//   bits = max(get_bits(max), get_bits(min))
// computed in decl_facts/ssa. Several passes used to open-code this with raw
// Dlop::get_mask_value(bits-1) calls; route them all through here so the rule
// lives in one place.
//
// `bits == 0` means "unbounded / no derivation" for the SIGNED pair, and the
// caller should treat the returned 0 as a sentinel, not a real bound.

// The one integer-type width ceiling. Every spelling that can name a width --
// a `:uN` annotation, an explicit `f<uN>` bind, a generic DECLARATION DEFAULT --
// must reject above this BEFORE materializing a bound: max_from_bits(N) builds a
// 2^N-1 Dlop and stringifying it into an LNAST const is what actually exhausts
// memory (a bare `<T=u2000000000>` peaked at 40 GB RSS before this existed).
inline constexpr int64_t kMaxIntTypeWidth = 1 << 20;

inline Dlop unsigned_max_from_bits(uint32_t bits) {
  return *Dlop::get_mask_value(static_cast<int>(bits));  // 2^bits - 1 (0 bits -> 0)
}

inline Dlop unsigned_min_from_bits(uint32_t /*bits*/) { return *Dlop::create_integer(0); }

inline Dlop signed_max_from_bits(uint32_t bits) {
  // 2^(bits-1) - 1, so a 1-bit signed maxes at 0 (its range is {-1, 0}).
  return *Dlop::get_mask_value(bits == 0 ? 0 : static_cast<int>(bits) - 1);
}

inline Dlop signed_min_from_bits(uint32_t bits) {
  if (bits == 0) {
    return *Dlop::create_integer(0);  // sentinel: unbounded
  }
  return *Dlop::get_neg_mask_value(static_cast<int>(bits) - 1);  // -2^(bits-1)
}

// Convenience: pick the right max/min pair by signedness.
inline Dlop max_from_bits(uint32_t bits, bool is_signed) {
  return is_signed ? signed_max_from_bits(bits) : unsigned_max_from_bits(bits);
}
inline Dlop min_from_bits(uint32_t bits, bool is_signed) {
  return is_signed ? signed_min_from_bits(bits) : unsigned_min_from_bits(bits);
}

// `.[bits]` of a declared integer range [min, max]: the magnitude width when
// min is non-negative, else the wider two's-complement width of the bounds.
inline int64_t range_bits(const Dlop& max, const Dlop& min) {
  return min.is_negative() ? std::max<int64_t>(max.get_signed_bits(), min.get_signed_bits())
                           : static_cast<int64_t>(max.get_payload_bits());
}

// `.[bits]` of an UNTYPED comptime integer (user ruling 2026-09-27 (6)): as wide
// as its value, the minimal width that holds it (13 -> 4, 0 -> 1, -4 -> 3).
inline int64_t value_bits(const Dlop& v) {
  return v.is_negative() ? static_cast<int64_t>(v.get_signed_bits()) : std::max<int64_t>(1, v.get_payload_bits());
}

// The one bool<->integer conversion advice (06-functions.md "Boolean ports"),
// the tail of every hint that rejects mixing the two (typecheck operators and
// stores, the front end's initializer check). Never `Signed(b)`: it
// reinterprets the bit, so `Signed(true) == -1`.
inline constexpr std::string_view kBoolIntCastHint
    = "`U1(b)` turns a Bool into a bit (true == 1); `x != 0` or `Bool(x)` turns an integer into a Bool";

// Built-in scalar typecast classification. Recognizes the callable names
// prp2lnast emits for a type-constructor cast (docs 07-typesystem: the type
// name is also the cast) -- `Signed`/`Unsigned`, `String`, `Bool`, and the
// sized `U<N>`/`S<N>` forms. Shared verbatim by the comptime fold (constprop)
// and the runtime hardware lowering (runner) so the two never disagree on what
// counts as a cast. nullopt for any other name (a user function, a
// `__cellop`, an enum type, `Clock`/`Reset`, …). The old lowercase spellings
// (`u8`, `boolean`, `unsigned`) are banned words the front end rejects, and a
// lowercase callee is never a cast here.
//
// `Signed`/`Unsigned` (no width) are REINTERPRETS (Verilog $signed/$unsigned):
// they keep the input's bits and width and only flip the sign tag, so they
// require a FULLY-TYPED input (a known width). The sized `U<N>`/`S<N>` forms
// are CHECKED value-casts (overflow is an error, use `wrap`/`sat` to drop bits).
// There is no unbounded `int` cast anymore.
enum class Typecast_kind : uint8_t { to_signed, to_uint, to_string, to_sized, to_bool };

struct Typecast_info {
  Typecast_kind kind{Typecast_kind::to_uint};
  bool          sized_signed = false;  // to_sized only: S<N> (true) vs U<N> (false)
  int           sized_bits   = 0;      // to_sized only: the N in U<N>/S<N>
};

inline std::optional<Typecast_info> classify_typecast(std::string_view fname) {
  if (fname == "Signed") {
    return Typecast_info{Typecast_kind::to_signed};
  }
  if (fname == "Unsigned") {
    return Typecast_info{Typecast_kind::to_uint};
  }
  if (fname == "String") {
    return Typecast_info{Typecast_kind::to_string};
  }
  if (fname == "Bool") {
    return Typecast_info{Typecast_kind::to_bool};
  }
  // U<num> / S<num>: a one-letter sign tag followed by decimal digits.
  if (fname.size() >= 2 && (fname[0] == 'U' || fname[0] == 'S')) {
    for (size_t i = 1; i < fname.size(); ++i) {
      if (fname[i] < '0' || fname[i] > '9') {
        return std::nullopt;
      }
    }
    if (fname.size() > 10) {
      return std::nullopt;  // an absurd width (the front end caps widths anyway)
    }
    Typecast_info ti{Typecast_kind::to_sized};
    ti.sized_signed = (fname[0] == 'S');
    ti.sized_bits   = std::stoi(std::string(fname.substr(1)));
    return ti;
  }
  return std::nullopt;
}

// The built-in `std.clog2(x)` (docs 13-stdlib "Built-in `std` namespace"): the
// qualified callee name the front end lowers it to, and its value, Verilog
// `$clog2` (the smallest n with (1 << n) >= x, so std.clog2(1) == 0). nullopt
// unless x is a known integer >= 1. Shared by the front end's type-bound fold,
// constprop and the template port-bound fold so they cannot disagree.
inline constexpr std::string_view std_clog2_callee = "std.clog2";

inline std::optional<Dlop> std_clog2(const Dlop& x) {
  if (x.is_invalid() || !x.is_integer() || x.has_unknowns() || x.is_negative() || x.is_known_zero()) {
    return std::nullopt;
  }
  return *Dlop::create_integer(x.sub_op(*Dlop::create_integer(1))->get_payload_bits());
}

}  // namespace upass
