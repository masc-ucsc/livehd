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
  gated_clock,        // ICG-gated clock, or the event signal is not its clock root
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
};

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
  }
  return "invalid";
}

// The codes a converted (domino) state carries; every other final code retains CMOS.
constexpr bool converts(Reason reason) { return reason == Reason::domino_area || reason == Reason::domino_all; }

}  // namespace livehd::domplace
