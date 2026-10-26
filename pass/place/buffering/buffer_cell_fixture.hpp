// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <array>
#include <string>
#include <utility>

#include "pass/liberty/timing_model.hpp"

namespace livehd::placement::buffering::test_fixture {
// Combinational noninverting A->Y cell for in-memory test libraries. Both pins
// allow 1000 ps transitions; tables are indexed by output transition.
inline liberty::timing::Cell buffer_cell(std::string name, std::array<double, 2> input_ff, double max_cap_ff,
                                         std::array<liberty::timing::Table, 2> delay, std::array<liberty::timing::Table, 2> slew) {
  liberty::timing::Cell cell;
  cell.name              = std::move(name);
  auto& in               = cell.pins["A"];
  in.name                = "A";
  in.direction           = "input";
  in.capacitance_ff      = {input_ff[0], input_ff[1]};
  in.max_transition_ps   = 1000;
  auto& out              = cell.pins["Y"];
  out.name               = "Y";
  out.direction          = "output";
  out.function           = "A";
  out.max_capacitance_ff = max_cap_ff;
  out.max_transition_ps  = 1000;
  liberty::timing::Arc arc;
  arc.related_pin = "A";
  arc.type        = "combinational";
  arc.sense       = liberty::timing::Sense::positive;
  for (unsigned t = 0; t < 2; ++t) {
    arc.delay[t] = std::move(delay[t]);
    arc.slew[t]  = std::move(slew[t]);
  }
  out.arcs.push_back(std::move(arc));
  return cell;
}
}  // namespace livehd::placement::buffering::test_fixture
