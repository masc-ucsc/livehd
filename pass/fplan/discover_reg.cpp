#include "hier_tree.hpp"

#include <map>
#include <set>

set_t Hier_tree::find_most_freq_pattern(set_t graph, const size_t bwidth) const {
  std::map<Lg_type_id, set_t> ul;

  // remove verts with duplicate labels
  for (auto v : graph) {
    if (ul.find(ginfo.labels[v]) == ul.end()) {
      ul.emplace(ginfo.labels[v], ginfo.al.vert_set());
      ul.at(ginfo.labels[v]).insert(v);
    }
  }

  set_t best = ul.begin()->second;

  while (ul.size() > 0) {
    std::set<vertex_t> new_ul;
    for (auto v : graph) {
      // etc
    }

    break;
  }
  
  return ginfo.al.vert_set();
}

void Hier_tree::compress_hier(std::vector<set_t>& pl) {
  pl.clear();
}

void Hier_tree::discover_regularity(size_t hier_index) {
  std::cout << "  discovering regularity...";

  auto hier = hiers[hier_index];
  I(hier != nullptr);

  std::vector<set_t> pattern_list;
  int                curr_min_depth = find_tree_depth(hier);

  std::function<void(phier, unsigned int, unsigned int, set_t&)> get_level_children
      = [this, &get_level_children](phier node, unsigned int level, unsigned int minlevel, set_t& level_set) {
          if (node->is_leaf()) {
            if (level >= minlevel) {
              for (auto v : ginfo.sets[node->graph_subset]) {
                level_set.insert(v);
              }
            }
            return;
          }

          get_level_children(node->children[0], level + 1, minlevel, level_set);
          get_level_children(node->children[1], level + 1, minlevel, level_set);
        };

  while (curr_min_depth >= 0) {
    set_t level_children = ginfo.al.vert_set();
    get_level_children(hier, 0, curr_min_depth, level_children);

    do {
      set_t most_freq_pattern = find_most_freq_pattern(level_children, 100);
      pattern_list.push_back(most_freq_pattern);
      compress_hier(pattern_list);
    } while (false); // no repeating patterns in graph

#ifdef FPLAN_DBG_VERBOSE
    fmt::print("\ndepth: {}, nodes:\n", curr_min_depth);
    unsigned int total = 0;
    for (auto v : level_children) {
      fmt::print("  node: {:<15}\n", ginfo.debug_names[v]);
      total++;
    }
    fmt::print("  total: {}\n", total);
#endif

    curr_min_depth--;
  }

  std::cout << "done." << std::endl;
}
