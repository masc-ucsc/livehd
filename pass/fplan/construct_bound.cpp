#include <random>
#include <unordered_map>

#include "hier_tree.hpp"

constexpr double max_aspect_ratio = 1.0 / 5.0;

std::default_random_engine     gen;
std::uniform_real_distribution dist(max_aspect_ratio, 1.0 - max_aspect_ratio);

std::unordered_map<Lg_type_id::type, double> label_area_map;

void Hier_tree::construct_bounds(const size_t pat_index, const size_t num_inst, const unsigned int optimal_thresh) {
  label_area_map.reserve(ginfo.al.order());
  for (auto v : ginfo.al.verts()) {
    auto label = ginfo.labels(v);
    I(label_area_map[label] == 0.0 || label_area_map[label] == ginfo.areas(v));
    label_area_map[label] = ginfo.areas(v);
  }

  // pat_list is a sequence of patterns that must be traversed linearly
  pattern_vec_t& pat_list = pattern_lists[pat_index];

  for (pattern_t pat : pat_list) {
    // go into blobb, copy over the code required to run it without calling exec() on it
    // might have to use a temp file for now, that's okay to get things running
    // once done, convert to bounding curve (?)
  }

  /*
    High level alg:
      create a bounding curve for a pattern
      add that bounding curve to a list of created bounding curves
      go to next pattern, loop
        if we recognize a pattern, use the already-created bounding curve instead
        (not sure how I would do this with blobb...)
  */
}