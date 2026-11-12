// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <limits>

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
    unsigned          drive = 0;  // index in plan_buffer_choices' library
  };
  std::vector<Branch> buffers;  // postorder: child buffers precede their parent
  std::vector<Load>   root;
};
// Constructive bottom-up Steiner repair. Coordinates and distance bounds are DBU,
// capacitances are fF. Actual cell/wire timing is checked after legalization.
Buffer_tree plan_buffers(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                         double buffer_input_ff, double max_cap_ff, unsigned max_fanout, double max_distance_dbu,
                         double wire_cap_ff_dbu);
struct Buffer_drive {
  double   input_ff = 0, max_cap_ff = 0, max_distance_dbu = 0;
  unsigned max_fanout   = 32;
  double   intrinsic_ps = 0, resistance_ohm = 0, area = 0;
};
struct Buffer_limits {
  size_t cells = std::numeric_limits<size_t>::max();
  double area  = std::numeric_limits<double>::infinity();
};
// Generate spatial tree alternatives and select drives bottom-up using sink
// required times and RC delay. The estimate only proposes edits; STA validates
// the actual legalized result. Required times use ps; geometry uses DBU.
//
// `effort` in (0, 1] is how much of the alternatives to try: a net that matters little for timing is given a few
// topologies spread over the buffer library, never fewer than `min_topologies`. Topologies that would be built from the
// same limits are built once, whatever the effort.
Buffer_tree plan_buffer_choices(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                                const std::vector<double>& required_ps, const std::vector<Buffer_drive>& drives,
                                const Buffer_drive& driver, double wire_r_ohm_dbu, double wire_c_ff_dbu,
                                const Buffer_limits& limits = {}, double effort = 1.0, size_t min_topologies = 4);
}  // namespace livehd::placement
