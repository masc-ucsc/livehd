//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

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

// Built-in scalar typecast classification. Recognizes the callable names
// prp2lnast emits for a type-constructor cast — `signed`/`unsigned`/`uint`,
// `string`, and the sized `uN`/`sN` forms. Shared verbatim by the comptime
// fold (constprop) and the runtime hardware lowering (runner) so the two never
// disagree on what counts as a cast. nullopt for any other name (a user
// function, a `__cellop`, an enum type, `bool`/`boolean`, …).
//
// `signed`/`unsigned` (no width) are REINTERPRETS (Verilog $signed/$unsigned):
// they keep the input's bits and width and only flip the sign tag, so they
// require a FULLY-TYPED input (a known width). The sized `uN`/`sN` forms are
// CHECKED value-casts (overflow is an error, use `wrap`/`sat` to drop bits).
// There is no unbounded `int` cast anymore.
enum class Typecast_kind : uint8_t { to_signed, to_uint, to_string, to_sized, to_bool };

struct Typecast_info {
  Typecast_kind kind{Typecast_kind::to_uint};
  bool          sized_signed = false;  // to_sized only: sN (true) vs uN (false)
  int           sized_bits   = 0;      // to_sized only: the N in uN/sN
};

inline std::optional<Typecast_info> classify_typecast(std::string_view fname) {
  if (fname == "signed") {
    return Typecast_info{Typecast_kind::to_signed};
  }
  if (fname == "unsigned") {
    return Typecast_info{Typecast_kind::to_uint};
  }
  if (fname == "string") {
    return Typecast_info{Typecast_kind::to_string};
  }
  if (fname == "bool" || fname == "boolean") {
    return Typecast_info{Typecast_kind::to_bool};
  }
  // u<num> / s<num>: a one-letter sign tag followed by decimal digits.
  if (fname.size() >= 2 && (fname[0] == 'u' || fname[0] == 's')) {
    for (size_t i = 1; i < fname.size(); ++i) {
      if (fname[i] < '0' || fname[i] > '9') {
        return std::nullopt;
      }
    }
    Typecast_info ti{Typecast_kind::to_sized};
    ti.sized_signed = (fname[0] == 's');
    ti.sized_bits   = std::stoi(std::string(fname.substr(1)));
    return ti;
  }
  return std::nullopt;
}

}  // namespace upass
