// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cmath>
#include <stdexcept>

namespace livehd::placement {
// Input validation shared by placement and timing code: throw
// std::invalid_argument(message) unless the condition holds.
inline void require(bool ok, const char* message) {
  if (!ok) {
    throw std::invalid_argument(message);
  }
}
inline void require_nonnegative(double value, const char* message) { require(std::isfinite(value) && value >= 0, message); }
}  // namespace livehd::placement
