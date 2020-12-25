#include <utility>  // for std::pair

#include "pass_fplan.hpp"

// TODO: these globals are temporary - I'll move them back into pass_fplan when I figure out the
// class hierarchy more

int pattern_counter = 0;

absl::flat_hash_set<Node>      depth_graph;
absl::flat_hash_set<LGraph*>   found_lgs;
absl::flat_hash_map<Node, int> unique_color;  // might add to lgraph itself later?

Hierarchy_tree* t;
int depth;

// returns the most frequent pattern color
int Pass_fplan::find_most_frequent_pattern(const unsigned int beam_width) {
  int best_pattern_color = 1;

  bool more_patterns;

  do {
    more_patterns = false;
    for (int i = 1; i <= pattern_counter; i++) {
      for (auto n : depth_graph) {
        if (unique_color[n] == i) {
          for (XEdge e : n.inp_edges()) {
            
            // TODO: n.inp_edges() segfaults.

            auto d = e.driver.get_node();
            if (unique_color[d] == 0 && depth_graph.contains(d)) {
              unique_color[d] = i;
            }
          }
        }
      }
    }

    for (auto hidx : t->depth_preorder()) {
      if (hidx.level >= depth) {
        Node tn(root_lg, hidx, Hardcoded_input_nid);
        fmt::print("name: {}, level: {}, pos: {}, color: {}\n",
                   tn.debug_name(),
                   tn.get_hidx().level,
                   tn.get_hidx().pos,
                   unique_color[tn]);
      }
    }
    fmt::print("\n");

  } while (more_patterns);

  return best_pattern_color;
}

void Pass_fplan::discover_reg(unsigned int beam_width) {
  t = root_lg->ref_htree();

  depth = 0;
  for (auto hidx : t->depth_preorder()) {
    if (hidx.level > depth) {
      depth = hidx.level;
    }
  }

  while (depth >= 0) {
    // NOTE: pattern value of 0 means "no pattern assigned".

    for (auto hidx : t->depth_preorder()) {
      if (hidx.level >= depth) {
        Node tn(root_lg, hidx, Hardcoded_input_nid);
        if (!found_lgs.contains(tn.get_lg())) {
          unique_color[tn] = ++pattern_counter;
          found_lgs.emplace(tn.get_lg());
        }
        depth_graph.emplace(tn);
      }
    }

    find_most_frequent_pattern(beam_width);

    depth--;
  }
}