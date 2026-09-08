// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

namespace livehd::abc {

// Existing ABC netlist-import allowance, also used by ware scoring.
inline constexpr uint64_t kAbcImportNodeBytes = 1024;

struct Parallel_stats {
  unsigned limit        = 1;
  unsigned peak_workers = 0;
  unsigned peak_abc     = 0;
  uint64_t memory_limit = 0;
  uint64_t memory_waits = 0;
};

// Zero selects the host's available CPUs; an explicit value is an upper bound.
unsigned synthesis_thread_limit(unsigned requested, unsigned available);

// Actual footprint includes ALL threads and shared graph/library state once.
// Reserve the still-unrealized part of active jobs' projections as well, so
// launching several workers before they allocate cannot overcommit the host.
bool admit_abc_worker(uint64_t limit, uint64_t actual, uint64_t baseline, uint64_t active_projection, uint64_t next_projection);

// Bootstrap a new private ABC session using the existing predicted AIG size.
// This is a scheduling estimate, never a replacement for measured admission.
uint64_t projected_abc_memory(uint64_t aig_nodes, uint64_t liberty_bytes);

}  // namespace livehd::abc
