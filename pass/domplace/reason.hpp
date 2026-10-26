// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>
#include <string_view>

namespace livehd::domplace {

// Why a state bit is, or is not, a provisional domino stage. The spellings are
// a report and sidecar contract: append new codes, never respell an old one.
// A retained state keeps its original CMOS+DFF implementation; nothing erases
// a control to make a state eligible.
enum class Reason : uint8_t {
  candidate,          // eligible; the physical choice is still open
  domino_area,        // converted: estimated domino area below the CMOS+DFF estimate
  domino_all,         // converted by select=all
  async_reset,        // asynchronous clear/preset
  initial_value,      // power-on initial value
  negative_edge,      // negedge capture
  unknown_edge,       // capture edge not established
  gated_clock,        // the clock root is a recognized clock gate's output
  derived_clock,      // the clock control is not a direct region input
  multibit_clock,     // clock wider than one bit
  constant_clock,     // constant or absent clock
  clock_domain,       // clock outside the selected domain
  memory,             // memory scope or role (barrier)
  opaque,             // opaque scope, opaque or hierarchy role (barrier)
  transparent_latch,  // level-sensitive source latch
  icg,                // clock-gate latch
  identity_endpoint,  // one-literal endpoint, retained unless identity conversion is requested
  constant_endpoint,  // no support after cofactoring
  polarity_illegal,   // no positive-output formula within stack/branches/inputs
  area,               // CMOS+DFF estimate not larger than the domino estimate
  cmos_estimate,      // the CMOS cone could not be priced, so no area claim is made
  selection_budget,   // physical selection work exhausted before this state
  definition_clock,   // the clock is a port of a preserved definition, not traced to the design clock root
  polarity_search,    // the positive-output search stopped at its own limits: no illegality proof
  // U21 clock gates (synth::fold_gated_clocks): a folded register converts on
  // its root clock with the gate's enable as a data input; these retain it.
  enable_stack,        // the gate's hold (Q under its enable) did not fit the domino formula's stack/input caps
  gated_chain,         // a gate on an already gated clock
  gated_unsafe,        // an AND/OR clock without a phase-safe enable storage (glitch safety not provable)
  gated_mixed,         // the gate's enable storage is sampled by another clock
  inverted_clock,      // the clock (or the gate's clock operand) is inverted
  divided_clock,       // a register output or a divided/inverted clock cell drives the clock
  gated_enable_state,  // the gate's enable cone holds state or hierarchy (or exceeds the clone limit)
  gated_fall_sample,   // the gate samples its enable at the falling edge (pass.usyn.domino_fall_gates folds it)
};

inline constexpr Reason kLastReason = Reason::gated_fall_sample;  // keep at the last enumerator

constexpr std::string_view reason_code(Reason reason) {
  switch (reason) {
    case Reason::candidate        : return "candidate";
    case Reason::domino_area      : return "domino-area";
    case Reason::domino_all       : return "domino-all";
    case Reason::async_reset      : return "async-reset";
    case Reason::initial_value    : return "initial-value";
    case Reason::negative_edge    : return "negative-edge";
    case Reason::unknown_edge     : return "unknown-edge";
    case Reason::gated_clock      : return "gated-clock";
    case Reason::derived_clock    : return "derived-clock";
    case Reason::multibit_clock   : return "multibit-clock";
    case Reason::constant_clock   : return "constant-clock";
    case Reason::clock_domain     : return "clock-domain";
    case Reason::memory           : return "memory";
    case Reason::opaque           : return "opaque";
    case Reason::transparent_latch: return "transparent-latch";
    case Reason::icg              : return "icg";
    case Reason::identity_endpoint: return "identity-endpoint";
    case Reason::constant_endpoint: return "constant-endpoint";
    case Reason::polarity_illegal : return "polarity-illegal";
    case Reason::area             : return "area";
    case Reason::cmos_estimate    : return "cmos-estimate";
    case Reason::selection_budget : return "selection-budget";
    case Reason::definition_clock : return "definition-clock";
    case Reason::polarity_search  : return "polarity-search";
    case Reason::enable_stack      : return "enable-stack";
    case Reason::gated_chain       : return "gated-chain";
    case Reason::gated_unsafe      : return "gated-unsafe";
    case Reason::gated_mixed       : return "gated-mixed";
    case Reason::inverted_clock    : return "inverted-clock";
    case Reason::divided_clock     : return "divided-clock";
    case Reason::gated_enable_state: return "gated-enable-state";
    case Reason::gated_fall_sample : return "gated-fall-sample";
  }
  return "invalid";
}

// The reason a spelling names (the inverse of reason_code), or `fallback`.
constexpr Reason reason_from_code(std::string_view code, Reason fallback) {
  for (uint8_t r = 0; r <= static_cast<uint8_t>(kLastReason); ++r) {
    if (reason_code(static_cast<Reason>(r)) == code) {
      return static_cast<Reason>(r);
    }
  }
  return fallback;
}

// The codes a converted (domino) state carries; every other final code retains CMOS.
constexpr bool converts(Reason reason) { return reason == Reason::domino_area || reason == Reason::domino_all; }

}  // namespace livehd::domplace
