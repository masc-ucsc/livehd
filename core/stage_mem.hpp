// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "debug_env.hpp"

namespace livehd::debug {

// LHD_PHASE_MEM (debug): one stderr line with the resident memory at this point of a pass, the
// highest it reached since the previous call (the kernel's high-water mark is reset at every call;
// Linux only), and the allocator's own view -- bytes handed out and not returned (live) and bytes it
// holds free. LHD_PHASE_TRIM (experiment) first hands freed pages back to the system.
inline void stage_memory(const char* pass, const char* stage) {
#if defined(__GLIBC__)
  if (env<"LHD_PHASE_TRIM">() != nullptr) {
    malloc_trim(0);
  }
#endif
  if (env<"LHD_PHASE_MEM">() == nullptr) {
    return;
  }
  const auto status_kb = [](const char* key) {
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
  };
  unsigned long long live_mb = 0, free_mb = 0;
#if defined(__GLIBC__)
  const auto info = mallinfo2();
  live_mb         = (info.uordblks + info.hblkhd) >> 20;
  free_mb         = info.fordblks >> 20;
#endif
  std::fprintf(stderr, "lhd-stage-mem %s %s rss_mb=%llu peak_mb=%llu live_mb=%llu free_mb=%llu\n", pass, stage,
               static_cast<unsigned long long>(status_kb("VmRSS:") / 1024),
               static_cast<unsigned long long>(status_kb("VmHWM:") / 1024), live_mb, free_mb);
  if (std::FILE* f = std::fopen("/proc/self/clear_refs", "w")) {
    std::fputs("5", f);
    std::fclose(f);
  }
}

}  // namespace livehd::debug
