// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "abc_parallel.hpp"

#include <algorithm>
#include <limits>

namespace livehd::abc {
namespace {
uint64_t add(uint64_t a, uint64_t b) {
  return b > std::numeric_limits<uint64_t>::max() - a ? std::numeric_limits<uint64_t>::max() : a + b;
}
uint64_t mul(uint64_t a, uint64_t b) {
  return a && b > std::numeric_limits<uint64_t>::max() / a ? std::numeric_limits<uint64_t>::max() : a * b;
}
}  // namespace

unsigned synthesis_thread_limit(unsigned requested, unsigned available) {
  available = std::max(1U, available);
  return requested ? std::min(requested, available) : available;
}

bool admit_abc_worker(uint64_t limit, uint64_t actual, uint64_t baseline, uint64_t active_projection, uint64_t next_projection) {
  if (!limit || !actual) {
    return false;  // Unknown physical memory never enables parallel admission.
  }
  const uint64_t committed = std::max(actual, add(baseline, active_projection));
  return committed < limit && next_projection < limit - committed;
}

uint64_t projected_abc_memory(uint64_t aig_nodes, uint64_t liberty_bytes) {
  // ABC keeps several network forms and parsed timing tables. Fixed session
  // startup dominates tiny colors; wide colors use the graph's saturating AIG
  // predictor. These conservative initial allowances reserve memory before workers
  // allocate; the scheduler also samples aggregate physical footprint.
  return add(add(uint64_t{256} << 20, mul(liberty_bytes, 8)), mul(aig_nodes, 4 * kAbcImportNodeBytes));
}

}  // namespace livehd::abc
