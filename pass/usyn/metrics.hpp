// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

#include "lnet.hpp"

namespace livehd::usyn {
// Counts the source network's endpoint occurrences (its combinational
// outputs) and reachable nodes, never physical area.
std::string source_metrics_json(const livehd::synth::Lnet& source);
}  // namespace livehd::usyn
