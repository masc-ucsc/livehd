// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "satopt_stages.hpp"

namespace livehd::satopt {
// Color-boundary constants (stage `boundary`). A value computed in one color
// (synthesis region) and read by a combinational cell of another crosses the
// cut as a free input of the reader's region: whatever the producer's logic
// makes constant is lost there, bit by bit. Simulation nominates the bits of
// every crossing value that no pattern changes, one masked query per value
// proves them over the definition's cone (whatever colors it spans), and each
// reading color then reads `Or(And(value, keep), constant)` through cells of
// its own color, so its mapper sees the constant bits. The producer and the
// readers inside its own color are untouched: no value changes its function.
//
// Inapplicable to an uncolored graph (the compile step, before pass.color).
void prove_boundary_bits(const std::vector<std::shared_ptr<hhds::Graph>>& graphs, Profile profile, Stage_report& report,
                         Meter& meter);
// The same proofs at the instance boundaries of a hierarchy (`definitions`: every definition of one private library,
// `top` the one whose inputs are free): the bits of a definition output that are constant are read as such by every
// instance's readers, and the bits of a definition input that EVERY instance drives with one constant are read as
// such inside the definition, zeros and ones alike, to a fixpoint (a pass-through chain learns them round by round).
// New cells take a neighbor's color (specialize::inherit_colors).
void prove_instance_bits(const std::vector<std::shared_ptr<hhds::Graph>>& definitions, std::string_view top, Profile profile,
                         Stage_report& report, Meter& meter);
// A node of `graphs` carries a color: the stage has something to look at.
[[nodiscard]] bool has_colors(const std::vector<std::shared_ptr<hhds::Graph>>& graphs);
}  // namespace livehd::satopt
