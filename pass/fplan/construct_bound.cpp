#include "hier_tree.hpp"

void Hier_tree::construct_bounds(size_t pat_index, unsigned int optimal_thresh) {
  // if # blocks < optimal_thresh, do an exhaustive branch and bound.
  // if > optimal_thresh, use simulated annealing or slicing trees.

  // go over hierarchy tree:
  // post order walk (left, right, node)

  // at the end, the bounding trees for all hierarchy trees are combined together into a bounding curve.
  // this represents the outline of the entire design.

}