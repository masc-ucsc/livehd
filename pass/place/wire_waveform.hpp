// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass/liberty/timing_model.hpp"

namespace livehd::placement::late {
struct Wire_waveform {
  double delay_ps = 0, slew_ps = 0;
};
// First-order RC approximation to a wire tree, using its Elmore time constant.
// Convolve a linear driver ramp with exp(-t/tau)/tau, then measure crossings at
// the receiver's voltage thresholds. Driver arrival is measured at its output
// threshold; slew uses each library's own thresholds and slew derate. This is a
// wire waveform approximation, not an effective-capacitance gate model.
Wire_waveform ramp_wire(double driver_slew_ps, double elmore_ps, const liberty::timing::Thresholds& driver,
                        const liberty::timing::Thresholds& receiver, unsigned transition);
}  // namespace livehd::placement::late
