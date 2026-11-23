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

#include "debug_env.hpp"
#include "phase_profile.hpp"

namespace livehd::aegraph_profile {
enum Phase { prepass, build, rules, extract, elaborate, post, count };
struct Spec {
  static constexpr std::array<const char*, count> names{"prepass", "build", "rules", "extract", "elaborate", "post"};
  static constexpr auto                           label = "aegraph";
  static bool                                     enabled() { return debug::env<"LHD_AEGRAPH_PROFILE">() != nullptr; }
};
using Timer      = Phase_profile<Phase, Spec>::Timer;
using Invocation = Phase_profile<Phase, Spec>::Invocation;
}  // namespace livehd::aegraph_profile
