// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <thread>
#ifdef __linux__
#include <sched.h>
#endif

namespace livehd::sim {
// The CPUs this process may use (affinity), capped under `bazel test`. Pure
// machine/test configuration: it does NOT move with the current load, so a
// decision that ends up in a build stamp (e.g. whether a precompiled header is
// used) stays the same from one run to the next.
inline unsigned available_cpus() {
  unsigned cpus = std::max(1u, std::thread::hardware_concurrency());
#ifdef __linux__
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
    cpus = std::max(1, CPU_COUNT(&affinity));
  }
#endif
  if (std::getenv("TEST_SRCDIR") != nullptr) {
    cpus = std::min(cpus, 2u);
  }
  return cpus;
}

// How many compiles to run NOW: available_cpus() minus the current load. An
// overloaded host still gets one worker, so compilation always makes progress.
// Use it only for the thread cap, never for anything that is stamped.
inline unsigned available_compile_jobs() {
  unsigned cpus = available_cpus();
  double load = 0;
  if (getloadavg(&load, 1) == 1) {
    cpus = load >= cpus ? 1 : std::max(1u, cpus - static_cast<unsigned>(std::ceil(load)));
  }
  return cpus;
}
}  // namespace livehd::sim
