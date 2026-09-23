// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "tmap.hpp"

namespace livehd::synth {
// Read the currently active ABC frame's NLDM library and boundary table.
// All ABC-owned pointers stay inside these helpers.
bool                abc_has_timing();
Network_environment abc_boundary_environment(void* network);
struct Timing_qor {
  bool   valid    = false;
  double area     = -1;
  double delay_ps = -1;
};
Timing_qor abc_timing_qor(void* network);
// Budget: meeting it wins, then minimum area; if neither meets, minimum
// delay wins. Without a budget require non-increasing area AND delay.
//
// `delay_tolerance` is a RELATIVE tie band on delay (0.01 = 1%): two delays
// within it are TIED and area decides. The delays compared here come from this
// flow's own timer, while the design is judged by an independent sign-off timer
// (lhdtrack's OpenSTA), and below about 1% the two do not even agree on which
// of two netlists is faster -- measured on bedrock arbiters, a candidate the
// flow rated 1% faster at +2.7% area was SLOWER under OpenSTA. Strict delay
// comparison made area unreachable whenever a budget is missed (two netlists
// never tie bit-for-bit), so the smaller netlist lost to a noise-level delay
// difference. Meeting vs missing the budget stays strict.
bool       timing_better(const Timing_qor& candidate, const Timing_qor& incumbent, double budget_ps, bool prefer_tie = false,
                         double delay_tolerance = 0);
// Characterize the mapped function under the installed boundary table.
bool       abc_fragment_timing(void* network, Mapped_fragment& fragment);
}  // namespace livehd::synth
