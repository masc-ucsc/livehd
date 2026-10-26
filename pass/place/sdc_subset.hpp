// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

#include "late_timing.hpp"

namespace livehd::placement::late {
struct Sdc_units {
  double time_ps = 1, capacitance_ff = 1;
};
// Strict data reader for the saved-SDC late-mode subset: named positive clocks,
// same-waveform divide-by-one generated clocks, asynchronous groups, explicit
// IO delays, pin loads and driving cells. Exact selectors only; no Tcl execution,
// variable expansion or wildcard queries. Unknown commands/options throw.
Constraints read_saved_sdc(std::string_view text, std::string_view design, Sdc_units units);
}  // namespace livehd::placement::late
