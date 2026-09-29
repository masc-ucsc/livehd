// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "satopt_stages.hpp"

namespace livehd::satopt {
// Exact, bounded resynthesis of one-bit mux selects and register/memory
// enables. Derives a truth table over at most three existing one-bit values,
// proves it, and commits only a strict reduction in cells. Never uses ODCs or
// assumed one-hotness: even a failing unique-if check keeps its controls.
void simplify_controls(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Stage_report& report, Meter& meter);
}  // namespace livehd::satopt
