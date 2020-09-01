#include "hier_tree.hpp"

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

    fmt::print("depth: {}, nodes:\n", curr_min_depth);
    unsigned int total = 0;
    for (auto v : level_children) {
      fmt::print("  node: {:<30}\n", ginfo.debug_names[v]);
      total++;
    }
    fmt::print("  total: {}\n", total);

    curr_min_depth--;
  }

  std::cout << "done." << std::endl;
}
