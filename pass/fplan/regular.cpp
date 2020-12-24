#include <utility>  // for std::pair

#include "pass_fplan.hpp"

void Pass_fplan::compute_depth(LGraph* lg, unsigned int depth) {
  cli.depth[lg] = depth;

  lg->each_sub_fast([&](Node& n, Lg_type_id lgid) -> bool {
    LGraph* sub_lg = LGraph::open(path, lgid);

    // check if a valid subgraph (collapsed node ok)
    if (sub_lg && !sub_lg->is_empty()) {
      compute_depth(sub_lg, depth + 1);
    }

    return true;
  });
}

using Lg_pattern = absl::flat_hash_map<LGraph*, unsigned int>;

// returns <# of instances of pattern, pattern>
std::pair<int, Lg_pattern> Pass_fplan::find_most_frequent_pattern(std::vector<Lg_pattern>& l, unsigned int beam_width) {

  for (auto pattern : l) {
    for (auto pair : pattern) {
      //std::cout << pair.first->get_name() << std::endl;
    }
    //std::cout << std::endl;
  }

  Lg_pattern best = l[0];

  while (l.size() > 0) {
    std::vector<Lg_pattern> l_new;
    l_new.reserve(l.size()); // l_new will be smaller than l, but this avoids a ton of memory allocations

    int pat_i = 0;
    for (auto pattern : l) {
      fmt::print("pattern {}:\n", pat_i++);
      for (auto pair : pattern) {
        fmt::print("  elem {}:\n", pair.first->get_name());

        // discover all connections to a given LGraph
        // (find the original module that is driving the connection)

      }
    }
  }

  

  return std::pair<int, Lg_pattern>(0, Lg_pattern());
}

void Pass_fplan::discover_reg(unsigned int beam_width) {

  compute_depth(root_lg, 0);

  int depth = 0;
  for (auto pair : cli.depth) {
    if (pair.second > depth) {
      depth = pair.second;
    }
  }

  while (depth >= 0) {
    std::vector<Lg_pattern> pattern_set;
    size_t pattern_i = 0;

    for (auto pair : cli.depth) {
      if (pair.second >= depth) {
        pattern_set.emplace_back();
        pattern_set[pattern_i++][pair.first] = 1;
      }
    }

    fmt::print("\ndepth {}\n", depth);

    auto p = find_most_frequent_pattern(pattern_set, beam_width);

    depth--;
  }
}