// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdexcept>

#include "hlop/dlop.hpp"

namespace livehd {
inline Dlop eval_get_mask(const Dlop& value, int lo, int hi) {
  if (lo < 0 || hi < lo) {
    throw std::invalid_argument("invalid half-open bit range");
  }
  return *value.get_mask_op_opt(lo, hi);
}
inline Dlop eval_get_mask(const Dlop& value, int bit) { return *value.get_mask_op(bit); }
inline Dlop eval_set_mask(const Dlop& base, const Dlop& value, int lo, int hi) {
  if (lo < 0 || hi < lo) {
    throw std::invalid_argument("invalid half-open bit range");
  }
  return *base.set_mask_op_opt(lo, hi, value);
}
inline Dlop eval_set_mask(const Dlop& base, const Dlop& value, int bit) { return *base.set_mask_op(value, bit); }
}  // namespace livehd
