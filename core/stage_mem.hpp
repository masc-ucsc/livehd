// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "debug_env.hpp"

namespace livehd::debug {
namespace phase_mem {
inline bool on() { return env<"LHD_PHASE_MEM">() != nullptr; }

inline uint64_t status_kb(const char* key) {
  uint64_t value = 0;
  if (std::FILE* f = std::fopen("/proc/self/status", "r")) {
    char         line[256];
    const size_t n = std::strlen(key);
    while (std::fgets(line, sizeof line, f) != nullptr) {
      if (std::strncmp(line, key, n) == 0) {
        value = std::strtoull(line + n, nullptr, 10);
        break;
      }
    }
    std::fclose(f);
  }
  return value;
}

// Stage markers run on the pipeline thread. Save its enclosing
// phase peaks before resetting the process-wide Linux watermark.
inline std::vector<uint64_t*>& open_peaks() {
  static thread_local std::vector<uint64_t*> peaks;
  return peaks;
}

inline uint64_t sample_peak() {
  const auto high = status_kb("VmHWM:");
  for (auto* peak : open_peaks()) {
    *peak = std::max(*peak, high);
  }
  return high;
}

inline void reset_high_water() {
  if (std::FILE* f = std::fopen("/proc/self/clear_refs", "w")) {
    std::fputs("5", f);
    std::fclose(f);
  }
}

inline void trim(bool default_on) {
#if defined(__GLIBC__)
  const char* option = env<"LHD_PHASE_TRIM">();
  if (option ? option[0] != '0' : default_on) {
    malloc_trim(0);
  }
#else
  (void)default_on;
#endif
}

struct Allocator_mb {
  unsigned long long live = 0;
  unsigned long long free = 0;
};

inline Allocator_mb allocator_mb() {
#if defined(__GLIBC__)
  const auto info = mallinfo2();
  return {(info.uordblks + info.hblkhd) >> 20, info.fordblks >> 20};
#else
  return {};
#endif
}
}  // namespace phase_mem

// LHD_PHASE_MEM (debug): one stderr line with the resident memory at this point of a pass, the
// highest it reached since the previous call (the kernel's high-water mark is reset at every call;
// Linux only), and the allocator's own view -- bytes handed out and not returned (live) and bytes it
// holds free. LHD_PHASE_TRIM (experiment) first hands freed pages back to the system.
inline void stage_memory(const char* pass, const char* stage) {
  phase_mem::trim(/*default_on=*/false);
  if (!phase_mem::on()) {
    return;
  }
  const auto high = phase_mem::sample_peak();
  const auto mem  = phase_mem::allocator_mb();
  std::fprintf(stderr,
               "lhd-stage-mem %s %s rss_mb=%llu peak_mb=%llu live_mb=%llu free_mb=%llu\n",
               pass,
               stage,
               static_cast<unsigned long long>(phase_mem::status_kb("VmRSS:") / 1024),
               static_cast<unsigned long long>(high / 1024),
               mem.live,
               mem.free);
  phase_mem::reset_high_water();
}

}  // namespace livehd::debug
