#include "hier_tree.hpp"

Hier_tree::Hier_tree(Graph_info&& json_info, unsigned int num_components) : ginfo(std::move(json_info)) {
  std::cout << "  creating hierarchy...";
  I(num_components >= 1);

  root = discover_hierarchy(ginfo, 0, num_components);
  std::cout << "done." << std::endl;
}

void Hier_tree::dump_node(const phier& node) const {
  fmt::print("{}node: {:<30} ", (node->parent == nullptr) ? "root " : "", node->name);

  if (node->is_leaf()) {
    fmt::print("area: {:.2f}, containing set {}.\n", node->area, node->graph_subset);
  } else {
    fmt::print("area: {:.2f}, children: {} and {}.\n", find_area(node), node->children[0]->name, node->children[1]->name);
    dump_node(node->children[0]);
    dump_node(node->children[1]);
  }
}

// add up the total area of all the leaves in the subtree
double Hier_tree::find_area(phier node) const {
  if (node->is_leaf()) {
    return node->area;
  }

  return find_area(node->children[0]) + find_area(node->children[1]);
}

int Hier_tree::find_tree_size(phier node) const {
  if (node->is_leaf()) {
    return 1;
  }

  return find_tree_size(node->children[0]) + find_tree_size(node->children[1]) + 1;
}

void Hier_tree::dump() const {
  fmt::print("\nprinting uncollapsed hierarchy ({} nodes):\n", find_tree_size(root));
  dump_node(root);
  std::cout << std::endl;

  for (size_t i = 0; i < collapsed_hiers.size(); i++) {
    fmt::print("printing collapsed hierarchy {} ({} nodes):\n", i, find_tree_size(collapsed_hiers[i]));
    dump_node(collapsed_hiers[i]);
    std::cout << std::endl;
  }
}
