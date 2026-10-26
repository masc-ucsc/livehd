// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "gp_timing.hpp"

namespace livehd::placement::timeclosure {
// Save the actual rejected RC/pin frame and complete pooled timing states used
// by compare_timing(). No graph edit, extra timing evaluation or state commit.
// prefix's parent must exist. The three files are diagnostics, not final output.
void write_gp_timing_rejection(const std::string& prefix, const Gp_timing_rejection&, const physical::Parasitics&,
                               const std::map<std::string, std::pair<double, double>>& locations, int iteration,
                               double density_overflow);
}  // namespace livehd::placement::timeclosure
