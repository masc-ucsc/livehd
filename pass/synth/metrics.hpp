// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <string>

#include "unate.hpp"

namespace livehd::synth {
// Counts structural identities and endpoint occurrences, never physical area.
std::string source_metrics_json(const Logic_network& source);
std::string network_metrics_json(const Unate_network& network);
}  // namespace livehd::synth
