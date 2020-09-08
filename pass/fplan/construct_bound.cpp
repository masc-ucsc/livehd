#include "hier_tree.hpp"

void Hier_tree::construct_bounds(size_t pat_index, unsigned int optimal_thresh) {
  // if # blocks < optimal_thresh, do an exhaustive branch and bound.
  // if > optimal_thresh, use simulated annealing or slicing trees.

  // ideas:
  // iterate over the pattern_list of all patterns, slicing as we go
  // first write the branch and bound alg

  // at the end, the bounding trees for all hierarchy trees are combined together into a bounding curve.
  // this represents the outline of the entire design.

}