//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Wrap / saturate narrowing math for the bitwidth pass.
//
// Per `the LiveHD docs`, value-math that depends on bits / range
// information lives in the bitwidth pass. The wrap and saturate
// narrowings are pure Const transformations parameterised by a bit
// width and signedness:
//
//   wrap(v, n)      — two's-complement modulo 2^n
//   saturate(v, n)  — clamp to the [min, max] of an n-bit value
//
// `uPass_attributes` still records the wrap / saturate POLICY (i.e.
// which variables carry the `[wrap]` / `[saturate]` attribute), but
// delegates the actual value-math to these helpers via the public
// API below. Both passes can include this header — it is inline-only
// and has no link dependency, just <const.hpp>.

#include <cstdint>

#include "const.hpp"

namespace upass::bitwidth {

// Wrap an unsigned value to `n` bits using a 2^n − 1 mask.
// `n == 0` (unknown width) leaves `v` unchanged. Invalid / unknown-bits
// constants pass through.
inline Const wrap_to_unsigned(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  Const mask = *Dlop::get_mask_value(n);
  return *v.and_op(mask);
}

// Two's-complement wrap to `n` signed bits: take the low `n` bits, then
// sign-extend from bit `n-1`. `Const::adjust_bits` only masks to the low
// `n` bits (no sign extension), so e.g. `0xCAFE` wrapped to s4 would wrongly
// yield 14 instead of -2; mask explicitly and sext from the sign bit.
inline Const wrap_to_signed(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  Const masked = *v.and_op(*Dlop::get_mask_value(n));
  return *masked.sext_op(*Dlop::create_integer(static_cast<int>(n) - 1));
}

// Saturate an unsigned value to [0, 2^n − 1]. Negative inputs clamp to 0;
// inputs greater than the max clamp to the max.
inline Const saturate_unsigned(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  Const lo = *Dlop::create_integer(0);
  Const hi = *Dlop::get_mask_value(n);
  if (v.is_negative()) {
    return lo;
  }
  // v > hi — compare via subtraction sign.
  if (v.sub_op(hi)->is_positive() && !v.same_repr(hi)) {
    return hi;
  }
  return v;
}

// Saturate a signed value to [-2^(n-1), 2^(n-1) − 1].
inline Const saturate_signed(const Const& v, uint32_t n) {
  if (n == 0 || v.is_invalid() || v.is_string() || v.has_unknowns() || v.is_nil()) {
    return v;
  }
  Const hi = *Dlop::get_mask_value(n - 1);
  Const lo = *Dlop::get_neg_mask_value(n - 1);
  if (v.sub_op(hi)->is_positive() && !v.same_repr(hi)) {
    return hi;
  }
  if (lo.sub_op(v)->is_positive() && !v.same_repr(lo)) {
    return lo;
  }
  return v;
}

}  // namespace upass::bitwidth
