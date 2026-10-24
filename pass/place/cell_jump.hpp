// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <vector>

#include "dreamplace/native/placement.hpp"

namespace livehd::placement {
// Experimental continuation of the fixed-pin neighbor initialization. The
// caller supplies the distance-one seed; zero/one add no propagation. Each
// successive graph frontier is visited once, in either signal direction.
struct Cell_jump_options {
  unsigned depth = 0, settle = 100, interval = 10, fix_period = 2;
};
struct Cell_jump_sample {
  int      iteration = 0;
  unsigned depth = 0, roots = 0, candidates = 0, crossing_cells = 0;
  unsigned moved = 0, unavailable = 0, crossing_edges_before = 0, crossing_edges_after = 0;
  double   total_displacement = 0, maximum_displacement = 0;
};
class Cell_jump {
public:
  Cell_jump(Cell_jump_options options, std::vector<int> macros);
  std::vector<DreamPlace::native::MovablePosition> update(const DreamPlace::native::Design& design, int iteration);
  const std::vector<Cell_jump_sample>&             samples() const { return history; }

private:
  Cell_jump_options             options;
  std::vector<int>              macros, depth, frontier;
  std::vector<Cell_jump_sample> history;
  unsigned                      roots = 0, next_depth = 2;
};
// Open rectangle intersection: touching a macro edge/corner is not crossing.
bool cell_jump_crosses(double ax, double ay, double bx, double by, double xl, double yl, double xh, double yh);
}  // namespace livehd::placement
