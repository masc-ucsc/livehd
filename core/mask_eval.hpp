// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <stdexcept>

#include "hlop/dlop.hpp"

namespace livehd {

// Constant evaluation shares the graph's contiguous-window/-1 contract.
// Empty selections also occur transiently while folding parametric LNAST.
inline Dlop eval_get_mask(const Dlop& value, const Dlop& mask) {
  if (mask.has_unknowns()) {
    return *Dlop::unknown(std::max(1, mask.get_signed_bits()));
  }
  if (mask.is_known_zero()) {
    return *Dlop::create_integer(0);
  }
  if (mask.is_just_i64() && mask.to_just_i64() == -1) {
    return *value.get_mask_op();
  }
  const auto [lo, hi] = mask.get_mask_range();
  if (mask.is_negative() || lo < 0 || hi <= lo) {
    throw std::invalid_argument("bit selection requires a contiguous window or -1");
  }
  return *value.get_mask_op_opt(lo, hi);
}

inline Dlop eval_set_mask(const Dlop& base, const Dlop& mask, const Dlop& value) {
  if (!base.is_numeric() || !value.is_numeric() || !mask.is_numeric() || mask.has_unknowns()) {
    return *Dlop::nil();
  }
  if (mask.is_known_zero()) {
    return base;
  }
  if (mask.is_just_i64() && mask.to_just_i64() == -1) {
    return value;
  }
  const auto [lo, hi] = mask.get_mask_range();
  if (mask.is_negative() || lo < 0 || hi <= lo) {
    throw std::invalid_argument("bit assignment requires a contiguous window or -1");
  }
  return *base.set_mask_op_opt(lo, hi, value);
}

}  // namespace livehd
