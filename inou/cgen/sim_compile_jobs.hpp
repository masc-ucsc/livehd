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
// Snapshot the CPUs available to this process at setup. An overloaded host
// still gets one worker, so compilation always makes progress.
inline unsigned available_compile_jobs() {
  unsigned cpus = std::max(1u, std::thread::hardware_concurrency());
#ifdef __linux__
  cpu_set_t affinity;
  CPU_ZERO(&affinity);
  if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
    cpus = std::max(1, CPU_COUNT(&affinity));
  }
#endif
  double load = 0;
  if (getloadavg(&load, 1) == 1) {
    cpus = load >= cpus ? 1 : std::max(1u, cpus - static_cast<unsigned>(std::ceil(load)));
  }
  if (std::getenv("TEST_SRCDIR") != nullptr) {
    cpus = std::min(cpus, 2u);
  }
  return cpus;
}
}  // namespace livehd::sim
