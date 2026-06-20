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
// The n<=1 edge matters: Dlop::get_mask_value(0) and get_neg_mask_value(0) both
// return 1 (defensive branches), which makes the naive signed reconstruction of
// a 1-bit int(-1,0) come out as (max=1, min=1) instead of (max=0, min=-1). These
// helpers special-case it. `bits == 0` means "unbounded / no derivation" and the
// caller should treat the returned 0 as a sentinel, not a real bound.

inline Dlop unsigned_max_from_bits(uint32_t bits) {
  if (bits == 0) {
    return *Dlop::create_integer(0);
  }
  return *Dlop::get_mask_value(bits);  // 2^bits - 1
}

inline Dlop unsigned_min_from_bits(uint32_t /*bits*/) { return *Dlop::create_integer(0); }

inline Dlop signed_max_from_bits(uint32_t bits) {
  if (bits <= 1) {
    return *Dlop::create_integer(0);  // 1-bit signed max is 0 (range is {-1,0})
  }
  return *Dlop::get_mask_value(bits - 1);  // 2^(bits-1) - 1
}

inline Dlop signed_min_from_bits(uint32_t bits) {
  if (bits == 0) {
    return *Dlop::create_integer(0);  // sentinel: unbounded
  }
  if (bits == 1) {
    return *Dlop::create_integer(-1);  // 1-bit signed min is -1
  }
  if (bits == 2) {
    return *Dlop::create_integer(-2);  // 2-bit signed min is -2: get_neg_mask_value(1)
                                       // hits the same wart as get_neg_mask_value(0) and returns +1
  }
  return *Dlop::get_neg_mask_value(bits - 1);  // -2^(bits-1)
}

// Convenience: pick the right max/min pair by signedness.
inline Dlop max_from_bits(uint32_t bits, bool is_signed) {
  return is_signed ? signed_max_from_bits(bits) : unsigned_max_from_bits(bits);
}
inline Dlop min_from_bits(uint32_t bits, bool is_signed) {
  return is_signed ? signed_min_from_bits(bits) : unsigned_min_from_bits(bits);
}

// Built-in scalar typecast classification. Recognizes the callable names
// prp2lnast emits for a type-constructor cast — `int`, `unsigned`/`uint`,
// `string`, and the sized `uN`/`sN`/`iN` forms. Shared verbatim by the comptime
// fold (constprop) and the runtime hardware lowering (runner) so the two never
// disagree on what counts as a cast. nullopt for any other name (a user
// function, a `__cellop`, an enum type, `bool`/`boolean`, …).
enum class Typecast_kind : uint8_t { to_int, to_uint, to_string, to_sized };

struct Typecast_info {
  Typecast_kind kind{Typecast_kind::to_int};
  bool          sized_signed = false;  // to_sized only: sN/iN (true) vs uN (false)
  int           sized_bits   = 0;      // to_sized only: the N in uN/sN/iN
};

inline std::optional<Typecast_info> classify_typecast(std::string_view fname) {
  if (fname == "int") {
    return Typecast_info{Typecast_kind::to_int};
  }
  if (fname == "uint" || fname == "unsigned") {
    return Typecast_info{Typecast_kind::to_uint};
  }
  if (fname == "string") {
    return Typecast_info{Typecast_kind::to_string};
  }
  // u<num> / s<num> / i<num>: a one-letter sign tag followed by decimal digits.
  if (fname.size() >= 2 && (fname[0] == 'u' || fname[0] == 's' || fname[0] == 'i')) {
    for (size_t i = 1; i < fname.size(); ++i) {
      if (fname[i] < '0' || fname[i] > '9') {
        return std::nullopt;
      }
    }
    Typecast_info ti{Typecast_kind::to_sized};
    ti.sized_signed = (fname[0] == 's' || fname[0] == 'i');
    ti.sized_bits   = std::stoi(std::string(fname.substr(1)));
    return ti;
  }
  return std::nullopt;
}

}  // namespace upass
