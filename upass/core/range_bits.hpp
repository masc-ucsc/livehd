//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

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
  return *Dlop::get_neg_mask_value(bits - 1);  // -2^(bits-1)
}

// Convenience: pick the right max/min pair by signedness.
inline Dlop max_from_bits(uint32_t bits, bool is_signed) {
  return is_signed ? signed_max_from_bits(bits) : unsigned_max_from_bits(bits);
}
inline Dlop min_from_bits(uint32_t bits, bool is_signed) {
  return is_signed ? signed_min_from_bits(bits) : unsigned_min_from_bits(bits);
}

}  // namespace upass
