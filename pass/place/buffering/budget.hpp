// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <chrono>

// Elapsed-time bounds shared by the buffering searches.
namespace livehd::placement::buffering::detail {
using Clock = std::chrono::steady_clock;
inline double elapsed(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
}  // namespace livehd::placement::buffering::detail
