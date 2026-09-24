// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <string>

namespace livehd::usyn {
// Sampled process footprint. A missing reading stays explicit: zero never
// proves zero usage.
struct Resource_observation {
  uint64_t samples = 0, unavailable = 0;
  uint64_t peak_bytes = 0;
  uint64_t sample(uint64_t footprint);
};

// Cooperative admission at bounded-work checkpoints. Samples include the
// original region's baseline mapping, not just the alternative optimizer.
// Zero limits disable their gate; zero footprint means an unavailable sample.
struct Resource_budget {
  double      time_limit_ms       = 0;
  uint64_t    growth_limit_bytes  = 0;
  uint64_t    process_limit_bytes = 0;
  uint64_t    entry_bytes         = 0;
  double      elapsed_ms          = 0;
  uint64_t    peak_bytes          = 0;
  uint64_t    samples             = 0;
  std::string reason;
  bool        admit(double elapsed, uint64_t footprint);
};
}  // namespace livehd::usyn
