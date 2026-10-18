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
// Generate spatial tree alternatives and select drives bottom-up using sink
// required times and RC delay. The estimate only proposes edits; STA validates
// the actual legalized result. Required times use ps; geometry uses DBU.
Buffer_tree plan_buffer_choices(Point root, const std::vector<Point>& loads, const std::vector<double>& capacitances,
                                const std::vector<double>& required_ps, const std::vector<Buffer_drive>& drives,
                                const Buffer_drive& driver, double wire_r_ohm_dbu, double wire_c_ff_dbu);
}  // namespace livehd::placement
