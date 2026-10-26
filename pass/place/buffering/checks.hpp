// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "pass/place/require.hpp"

// Validation and bound helpers shared by the buffering implementation files.
namespace livehd::placement::buffering::detail {
using Clock                              = std::chrono::steady_clock;
// Integer DBU magnitudes up to 2^53 convert to double without rounding.
inline constexpr int64_t exact_dbu_limit = int64_t{1} << 53;

using placement::require;
inline double elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
template <class P>
bool finite(const P& p) {
  return std::isfinite(p.x) && std::isfinite(p.y);
}
// Whether the X-then-Y path a -> (b.x, a.y) -> b avoids every CLOSED mask.
// Mask coordinates are divided by scale to reach the points' units.
template <class P, class R>
bool rectilinear_clear(P a, P b, const std::vector<R>& masks, double scale = 1) {
  const auto clear = [&](P from, P to) {
    for (const auto& r : masks) {
      if (std::max(from.x, to.x) >= r.xl / scale && std::min(from.x, to.x) <= r.xh / scale && std::max(from.y, to.y) >= r.yl / scale
          && std::min(from.y, to.y) <= r.yh / scale) {
        return false;
      }
    }
    return true;
  };
  const P bend{b.x, a.y};
  return clear(a, bend) && clear(bend, b);
}
}  // namespace livehd::placement::buffering::detail
