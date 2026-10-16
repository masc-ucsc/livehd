// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "place.hpp"
namespace livehd::placement {
struct Buffer_tree {
  struct Load {
    bool     buffer = false;
    unsigned index  = 0;
  };
  struct Branch {
    Point             position;
    std::vector<Load> loads;
  };
  std::vector<Branch> buffers;  // postorder: child buffers precede their parent
  std::vector<Load>   root;
};
// Constructive bottom-up Steiner repair. Coordinates and distance bounds are DBU,
// capacitances are fF. Actual cell/wire timing is checked after legalization.
Buffer_tree plan_buffers(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                         double buffer_input_ff, double max_cap_ff, unsigned max_fanout, double max_distance_dbu,
                         double wire_cap_ff_dbu);
}  // namespace livehd::placement
