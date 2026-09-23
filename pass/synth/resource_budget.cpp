// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "resource_budget.hpp"

#include <algorithm>
#include <limits>

namespace livehd::synth {
uint64_t Resource_observation::sample(uint64_t parent, std::optional<uint64_t> worker) {
  ++samples;
  parent_unavailable += parent == 0;
  const auto child    = worker.value_or(0);
  if (worker) {
    ++worker_samples;
    worker_unavailable += child == 0;
    worker_peak_bytes   = std::max(worker_peak_bytes, child);
  }
  const auto total = parent > std::numeric_limits<uint64_t>::max() - child ? std::numeric_limits<uint64_t>::max() : parent + child;
  peak_bytes       = std::max(peak_bytes, total);
  return total;
}

bool Resource_budget::admit(double elapsed, uint64_t footprint) {
  elapsed_ms = std::max(elapsed_ms, elapsed);
  peak_bytes = std::max(peak_bytes, footprint);
  ++samples;
  if (!reason.empty()) {
    return false;
  }
  if (time_limit_ms > 0 && elapsed_ms >= time_limit_ms) {
    reason = "region wall-time budget exhausted";
  } else if (process_limit_bytes && footprint > process_limit_bytes) {
    reason = "process physical-memory budget exhausted";
  } else if (growth_limit_bytes && entry_bytes && footprint > entry_bytes && footprint - entry_bytes > growth_limit_bytes) {
    reason = "region memory-growth budget exhausted";
  }
  return reason.empty();
}
}  // namespace livehd::synth
