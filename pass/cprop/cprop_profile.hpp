// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace livehd::cprop_profile {
enum Phase { order, range, footprint, dead, state, scalar, pack, fresh, cse, sharing, vectorize, count };
inline constexpr std::array<const char*, count> names{
    "order", "range", "footprint", "dead", "state", "scalar", "pack", "fresh", "cse", "sharing", "vectorize"};
struct Profile {
  std::array<double, count> ms{};
  std::array<size_t, count> calls{};
};
inline thread_local Profile* active = nullptr;
// Inclusive times: nested queries and traversal construction are reported
// separately; do not add the columns to obtain the invocation's elapsed time.
class Timer {
  Phase                                 phase;
  Profile*                              profile = active;
  std::chrono::steady_clock::time_point start;

public:
  explicit Timer(Phase p) : phase(p) {
    if (profile) {
      start = std::chrono::steady_clock::now();
    }
  }
  ~Timer() {
    if (profile) {
      profile->ms[phase] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
      ++profile->calls[phase];
    }
  }
};
class Invocation {
  Profile          profile;
  Profile*         previous = active;
  std::string_view name;
  bool             enabled = std::getenv("LHD_CPROP_PROFILE") != nullptr;

public:
  explicit Invocation(std::string_view n) : name(n) {
    if (enabled) {
      active = &profile;
    }
  }
  ~Invocation() {
    active = previous;
    if (!enabled) {
      return;
    }
    // One output record per phase so parallel graph workers cannot interleave
    // fragments of a record. Graph names identify the invocation body.
    for (size_t i = 0; i < count; ++i) {
      std::fprintf(stderr,
                   "cprop-profile graph=%.*s phase=%s ms=%.6f calls=%zu\n",
                   static_cast<int>(name.size()),
                   name.data(),
                   names[i],
                   profile.ms[i],
                   profile.calls[i]);
    }
  }
};
}  // namespace livehd::cprop_profile
