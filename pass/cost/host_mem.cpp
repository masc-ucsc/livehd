// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "host_mem.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>

#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>

#include <cstdio>
#endif

#include <sys/resource.h>

namespace livehd::cost {

#if !defined(__APPLE__)
// A cgroup memory limit, if this process is confined by one. _SC_PHYS_PAGES
// reports the HOST's RAM, so inside a 4 GiB container on a 256 GiB machine the
// budget would compute to ~204 GiB and the guard would never fire before the
// cgroup OOM killer -- i.e. exactly the CI case this exists for. 0 = no limit.
static uint64_t cgroup_limit_bytes() {
  // cgroup v2 first, then v1. "max" means unlimited.
  for (const char* path : {"/sys/fs/cgroup/memory.max", "/sys/fs/cgroup/memory/memory.limit_in_bytes"}) {
    std::FILE* f = std::fopen(path, "r");
    if (f == nullptr) {
      continue;
    }
    unsigned long long v   = 0;
    const int          got = std::fscanf(f, "%llu", &v);
    std::fclose(f);
    // A v1 "unlimited" is a sentinel near UINT64_MAX rather than a word, so
    // treat an implausibly large reading as no limit.
    if (got == 1 && v != 0 && v < (uint64_t{1} << 62)) {
      return static_cast<uint64_t>(v);
    }
  }
  return 0;
}
#endif

uint64_t physical_ram_bytes() {
#if defined(__APPLE__)
  uint64_t bytes = 0;
  size_t   len   = sizeof(bytes);
  if (::sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) != 0) {
    return 0;
  }
  return bytes;
#else
  const long pages     = ::sysconf(_SC_PHYS_PAGES);
  const long page_size = ::sysconf(_SC_PAGE_SIZE);
  if (pages <= 0 || page_size <= 0) {
    return 0;
  }
  const uint64_t host = static_cast<uint64_t>(pages) * static_cast<uint64_t>(page_size);
  // Whatever actually kills us first is the real "physical" memory here.
  const uint64_t cg = cgroup_limit_bytes();
  return cg != 0 && cg < host ? cg : host;
#endif
}

uint64_t process_rss_bytes() {
#if defined(__APPLE__)
  // getrusage's ru_maxrss is the PEAK, so it cannot answer "how much am I using
  // now" -- it never goes down after a network is freed. mach task_info does.
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t      count = MACH_TASK_BASIC_INFO_COUNT;
  if (::task_info(::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)
      != KERN_SUCCESS) {
    return 0;
  }
  return info.resident_size;
#else
  // statm field 2 is resident pages.
  std::FILE* f = std::fopen("/proc/self/statm", "r");
  if (f == nullptr) {
    return 0;
  }
  unsigned long total = 0, resident = 0;
  const int     got = std::fscanf(f, "%lu %lu", &total, &resident);
  std::fclose(f);
  if (got != 2) {
    return 0;
  }
  const long page_size = ::sysconf(_SC_PAGE_SIZE);
  return page_size <= 0 ? 0 : static_cast<uint64_t>(resident) * static_cast<uint64_t>(page_size);
#endif
}

uint64_t process_footprint_bytes() {
#if defined(__APPLE__)
  // phys_footprint is what jetsam kills on, and it counts compressed/paged pages
  // that resident_size drops -- so it does not undercount under pressure.
  task_vm_info_data_t    info{};
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (::task_info(::mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0;
  }
  return info.phys_footprint;
#else
  // No separate footprint on Linux; the OOM killer scores on RSS.
  return process_rss_bytes();
#endif
}

#if defined(__APPLE__)
// Current virtual_size (total mapped VA, mostly PROT_NONE reservation on arm64).
// This is the FLOOR that RLIMIT_AS cannot be set below on Darwin.
static uint64_t process_virtual_size() {
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t      count = MACH_TASK_BASIC_INFO_COUNT;
  if (::task_info(::mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
    return 0;
  }
  return info.virtual_size;
}
#endif

uint64_t arm_address_space_limit(uint64_t budget) {
  if (budget == 0) {
    return 0;  // nothing to enforce
  }

#if defined(__APPLE__)
  // RLIMIT_AS's floor on Darwin is the current virtual_size, so an absolute small
  // limit EINVALs. Set (virtual_size + budget): real allocations grow VA ~1:1, so
  // this caps real memory at ~budget. Read virtual_size NOW -- never hardcode the
  // ~415 GiB arm64 baseline.
  const uint64_t vsz = process_virtual_size();
  if (vsz == 0) {
    return 0;
  }
  // Guard the addition against overflow (vsz is already ~415 GiB).
  if (budget > UINT64_MAX - vsz) {
    return 0;
  }
  const uint64_t limit = vsz + budget;
#else
  const uint64_t limit = budget;  // Linux: absolute VA cap, baseline VA is a few MB
#endif

  struct rlimit rl{};
  if (::getrlimit(RLIMIT_AS, &rl) != 0) {
    return 0;
  }
  // Do not raise a lower hard limit we cannot exceed, and do not fight an already
  // tighter cap (a container / a caller's ulimit).
  if (rl.rlim_max != RLIM_INFINITY && limit > static_cast<uint64_t>(rl.rlim_max)) {
    return 0;
  }
  if (rl.rlim_cur != RLIM_INFINITY && static_cast<uint64_t>(rl.rlim_cur) <= limit) {
    return 0;  // an existing cap is already at least as strict
  }
  rl.rlim_cur = static_cast<rlim_t>(limit);
  if (::setrlimit(RLIMIT_AS, &rl) != 0) {
    return 0;
  }
  return limit;
}

uint64_t configured_budget_bytes() {
  int budget_mb = 0;
  if (const char* env = std::getenv("LIVEHD_MEMORY_BUDGET_MB"); env != nullptr && *env != '\0') {
    int         v   = 0;
    const char* end = env;
    while (*end != '\0') {
      ++end;
    }
    auto [p, ec] = std::from_chars(env, end, v);
    if (ec == std::errc{} && p == end && v >= 0) {
      budget_mb = v;  // 0 is a valid "use the default budget" value
    }
  }
  return budget_bytes(budget_mb);
}

uint64_t install_memory_backstop() { return arm_address_space_limit(configured_budget_bytes()); }

uint64_t arm_child_share(int nsiblings) {
  if (nsiblings <= 1) {
    return 0;  // a lone child already inherits the parent's whole-budget cap
  }
  const uint64_t budget = configured_budget_bytes();
  if (budget == 0) {
    return 0;  // unenforceable -- do not gate
  }
  return arm_address_space_limit(budget / static_cast<uint64_t>(nsiblings));
}

uint64_t reserve_bytes() {
  constexpr uint64_t kFloor = uint64_t{2} << 30;  // 2 GiB
  const uint64_t     phys   = physical_ram_bytes();
  if (phys == 0) {
    return kFloor;
  }
  // max(2 GiB, 20% of physical) -- but never so much that nothing is left to
  // work with. Without the half-of-physical cap, the 2 GiB floor swallows a
  // <=2 GiB host entirely, budget_bytes returns 0, and the guard silently turns
  // itself OFF on the machine most likely to run out of memory.
  return std::min(std::max(kFloor, phys / 5), phys / 2);
}

uint64_t budget_bytes(int budget_mb) {
  if (budget_mb > 0) {
    return static_cast<uint64_t>(budget_mb) << 20;
  }
  const uint64_t phys = physical_ram_bytes();
  if (phys == 0) {
    return 0;  // unknown host: unenforceable
  }
  const uint64_t reserve = reserve_bytes();
  return reserve >= phys ? 0 : phys - reserve;
}

}  // namespace livehd::cost
