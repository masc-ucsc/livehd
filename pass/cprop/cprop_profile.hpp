// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "debug_env.hpp"
#include "phase_profile.hpp"

namespace livehd::cprop_profile {
enum Phase { order, range, footprint, dead, state, scalar, pack, fresh, cse, sharing, vectorize, count };
struct Spec {
  static constexpr std::array<const char*, count> names{
      "order", "range", "footprint", "dead", "state", "scalar", "pack", "fresh", "cse", "sharing", "vectorize"};
  static constexpr auto label = "cprop";
  static bool           enabled() { return debug::env<"LHD_CPROP_PROFILE">() != nullptr; }
};
using Timer      = Phase_profile<Phase, Spec>::Timer;
using Invocation = Phase_profile<Phase, Spec>::Invocation;
}  // namespace livehd::cprop_profile
