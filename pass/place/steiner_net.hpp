// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace livehd::placement {
// The placement RC tree of one net as numbers: the capacitance of every node (the input pins first, in order, then the
// junctions of the tree; half of each wire's capacitance on each end) and the resistor edges. The same model as
// steiner_rc, which is this plus labels. A tree of n nodes has n-1 edges.
struct Steiner_net {
  struct Edge {
    unsigned from = 0, to = 0;
    double   resistance_ohm                = 0;
    bool     operator==(const Edge&) const = default;
  };
  std::vector<double> node_cap_ff;
  std::vector<Edge>   edges;
  double              wire_length_um                       = 0;
  bool                operator==(const Steiner_net&) const = default;
};
// `pins` are positions in DBU, `driver_index` indexes them, `per_um` is {horizontal R, horizontal C, vertical R,
// vertical C} per micron (ohm, fF). Throws on a bad driver or coordinates out of the FLUTE range. `approximate` is
// set when the work-bounded fallback built the tree.
Steiner_net steiner_net(const std::vector<std::pair<double, double>>& pins, size_t driver_index,
                        const std::array<double, 4>& per_um, double dbu, bool* approximate = nullptr);
}  // namespace livehd::placement
