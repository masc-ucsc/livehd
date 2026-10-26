// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cmath>
#include <cstdint>

#include "pass/place/require.hpp"

// Validation helpers shared by the buffering implementation files; require()
// and require_nonnegative() come from //pass/place:require.
namespace livehd::placement::buffering::detail {
// Integer DBU magnitudes up to 2^53 convert to double without rounding.
inline constexpr int64_t exact_dbu_limit = int64_t{1} << 53;
template <class P>
bool finite(const P& p) {
  return std::isfinite(p.x) && std::isfinite(p.y);
}
}  // namespace livehd::placement::buffering::detail
