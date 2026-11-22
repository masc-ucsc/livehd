// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace livehd::liberty::timing {

// Linear extrapolation uses the nearest pair at each end of an axis. A
// scalar table has an empty axis and uses index zero for that dimension.
template <class T>
T nldm_interpolate(std::span<const T> x, std::span<const T> y, std::span<const T> values, T x_value, T y_value) {
  struct Interval {
    size_t lo = 0, hi = 0;
    T      fraction = 0;
  };
  const auto interval = [](std::span<const T> axis, T value) -> Interval {
    if (axis.size() < 2) {
      return {};
    }
    value            = std::isfinite(value) ? std::max(value, T{0}) : T{0};
    const auto upper = static_cast<size_t>(std::upper_bound(axis.begin(), axis.end(), value) - axis.begin());
    const auto hi    = std::clamp<size_t>(upper, 1, axis.size() - 1);
    const auto lo    = hi - 1;
    return {lo, hi, (value - axis[lo]) / (axis[hi] - axis[lo])};
  };
  const auto a    = interval(x, x_value);
  const auto b    = interval(y, y_value);
  const auto ny   = std::max<size_t>(1, y.size());
  const auto low  = std::lerp(values[a.lo * ny + b.lo], values[a.lo * ny + b.hi], b.fraction);
  const auto high = std::lerp(values[a.hi * ny + b.lo], values[a.hi * ny + b.hi], b.fraction);
  return std::lerp(low, high, a.fraction);
}

}  // namespace livehd::liberty::timing
