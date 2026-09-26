// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "resource_budget.hpp"

#include <algorithm>

namespace livehd::usyn {
uint64_t Resource_observation::sample(uint64_t footprint) {
  ++samples;
  unavailable += footprint == 0;
  peak_bytes   = std::max(peak_bytes, footprint);
  return footprint;
}

bool Resource_budget::admit(double elapsed, uint64_t footprint) {
  elapsed_ms = std::max(elapsed_ms, elapsed);
  peak_bytes = std::max(peak_bytes, footprint);
  ++samples;
  if (!reason.empty()) {
    return false;
  }
  if (time_limit_ms > 0 && elapsed_ms >= time_limit_ms) {
    reason      = "region wall-time budget exhausted";
    out_of_time = true;
  } else if (process_limit_bytes && footprint > process_limit_bytes) {
    reason = "process physical-memory budget exhausted";
  } else if (growth_limit_bytes && entry_bytes && footprint > entry_bytes && footprint - entry_bytes > growth_limit_bytes) {
    reason = "region memory-growth budget exhausted";
  }
  return reason.empty();
}
}  // namespace livehd::usyn
