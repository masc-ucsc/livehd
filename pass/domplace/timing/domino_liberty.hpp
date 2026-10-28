// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <vector>

#include "domino_timing_model.hpp"

namespace livehd::domplace::timing {
// The provisional domino cells as one Liberty library, for engines that read
// Liberty only (OpenTimer, so placement resizing, buffer repair and timing-driven
// placement can run on a domino netlist). The cells are exactly the ones the
// native stage timer characterizes (domino_timing_cells): one sequential cell
// per bundle variant with its rails as setup-checked inputs, CLK, and Q
// carrying the register's clock-to-Q arcs.
//
// A Liberty setup check captures at the next clock edge, a domino stage at a
// fraction of the period (Capture_budget): the rail setup is therefore written
// as `setup + uncertainty + (1 - fraction) * period_ps`, which leaves the same
// slack in ps for a single-clock design. The stage timer stays the authority
// for the schedule's other checks (precharge, evaluate-to-storage).
//
// Throws std::runtime_error naming the variant when a cell is unsupported
// (its model has an unmeasured coefficient or a missing register arc) or the
// period is not positive: a Liberty cell must never carry a silent zero.
std::string domino_liberty(const Domino_timing_view& view, const liberty::timing::Library& base, double period_ps);
}  // namespace livehd::domplace::timing
