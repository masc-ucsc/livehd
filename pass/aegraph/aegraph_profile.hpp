// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// LHD_AEGRAPH_PROFILE=1: per-graph, per-phase wall times of pass.aegraph on
// stderr, in the SAME record shape as LHD_CPROP_PROFILE (pass/cprop/cprop_profile.hpp)
// with the record tag `aegraph-profile`:
//
//   aegraph-profile graph=<def> phase=<phase> ms=<ms> calls=<n>
//
// Phases (inclusive): prepass (SCC detection and the classic false-cycle cutter),
// build (hash-consing, the constant analysis and the rewrite rules), rules (the
// rewrite rules alone: NESTED in build, one call per outermost rewrite of a new
// e-node), extract (the union cost pass), elaborate (diff back into the graph,
// dead-node sweep) and post (the classic region passes that follow the engine).

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace livehd::aegraph_profile {
enum Phase { prepass, build, rules, extract, elaborate, post, count };
inline constexpr std::array<const char*, count> names{"prepass", "build", "rules", "extract", "elaborate", "post"};
struct Profile {
  std::array<double, count> ms{};
  std::array<size_t, count> calls{};
};
inline thread_local Profile* active = nullptr;

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
  bool             enabled = std::getenv("LHD_AEGRAPH_PROFILE") != nullptr;

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
    for (size_t i = 0; i < count; ++i) {
      std::fprintf(stderr,
                   "aegraph-profile graph=%.*s phase=%s ms=%.6f calls=%zu\n",
                   static_cast<int>(name.size()),
                   name.data(),
                   names[i],
                   profile.ms[i],
                   profile.calls[i]);
    }
  }
  Invocation(const Invocation&)            = delete;
  Invocation& operator=(const Invocation&) = delete;
};
}  // namespace livehd::aegraph_profile
