#include "hier_tree.hpp"

void Hier_tree::dump_node(const phier node) const {
  static int depth = -1;

  depth++;

  std::string prefix;
  if (node->parent == nullptr) {
    prefix.append("root, ");
  }
  if (node->is_leaf()) {
    prefix.append("leaf, ");
  }
  fmt::print("node: {:<30}{}depth: {}, ", node->name, prefix, depth);

  if (node->is_leaf()) {
    fmt::print("area: {:.2f}, containing set {}.\n", node->area, node->graph_subset);
  } else {
    fmt::print("area: {:.2f}, children: {} and {}.\n", find_area(node), node->children[0]->name, node->children[1]->name);
    dump_node(node->children[0]);
    dump_node(node->children[1]);
  }

  depth--;
}

// add up the total area of all the leaves in the subtree
double Hier_tree::find_area(phier node) const {
  if (node->is_leaf()) {
    return node->area;
  }

  I(node->children[0] != nullptr);
  I(node->children[1] != nullptr);

  return find_area(node->children[0]) + find_area(node->children[1]);
}

unsigned int Hier_tree::find_tree_size(phier node) const {
  if (node->is_leaf()) {
    return 1;
  }

  I(node->children[0] != nullptr);
  I(node->children[1] != nullptr);

  return find_tree_size(node->children[0]) + find_tree_size(node->children[1]) + 1;
}

unsigned int Hier_tree::find_tree_depth(phier node) const {
  std::function<unsigned int(phier, unsigned int)> find_depth
      = [&find_depth](phier rnode, unsigned int depth) -> unsigned int {
    if (rnode->is_leaf()) {
      return depth;
    }

    I(rnode->children[0] != nullptr);
    I(rnode->children[1] != nullptr);

    unsigned int dc0 = find_depth(rnode->children[0], depth + 1);
    unsigned int dc1 = find_depth(rnode->children[1], depth + 1);

    return std::max(dc0, dc1);
  };

  return find_depth(node, 0);
}

void Hier_tree::dump() const {
  for (size_t i = 0; i < hiers.size(); i++) {
    fmt::print("printing hierarchy {} ({} nodes):\n", i, find_tree_size(hiers[i]));
    dump_node(hiers[i]);
    std::cout << std::endl;
  }
}
