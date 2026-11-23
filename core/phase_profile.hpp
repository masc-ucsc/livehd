// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <chrono>
#include <cstdio>
#include <string_view>

namespace livehd {

// Nested invocations restore the enclosing graph's counters. Each pass has
// its own phase names and debug switch; nested phase times are inclusive.
template <class Phase, class Spec>
struct Phase_profile {
  struct Profile {
    std::array<double, Spec::names.size()> ms{};
    std::array<size_t, Spec::names.size()> calls{};
  };
  inline static thread_local Profile* active = nullptr;

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
    Timer(const Timer&)            = delete;
    Timer& operator=(const Timer&) = delete;
  };

  class Invocation {
    Profile          profile;
    Profile*         previous = active;
    std::string_view name;
    bool             enabled = Spec::enabled();

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
      // fragments of a record.
      for (size_t i = 0; i < Spec::names.size(); ++i) {
        std::fprintf(stderr,
                     "%s-profile graph=%.*s phase=%s ms=%.6f calls=%zu\n",
                     Spec::label,
                     static_cast<int>(name.size()),
                     name.data(),
                     Spec::names[i],
                     profile.ms[i],
                     profile.calls[i]);
      }
    }
    Invocation(const Invocation&)            = delete;
    Invocation& operator=(const Invocation&) = delete;
  };
};
}  // namespace livehd
