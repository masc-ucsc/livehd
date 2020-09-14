#include "hier_tree.hpp"

#include <functional>
#include <random>

#include "fmt/core.h"

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
  std::function<unsigned int(phier, unsigned int)> find_depth = [&find_depth](phier rnode, unsigned int depth) -> unsigned int {
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

void Hier_tree::dump_hier() const {
  for (size_t i = 0; i < hiers.size(); i++) {
    fmt::print("printing hierarchy {} ({} nodes):\n", i, find_tree_size(hiers[i]));
    dump_node(hiers[i]);
    fmt::print("\n");
  }
}

void Hier_tree::dump_patterns() const {
  for (size_t i = 0; i < hier_patterns.size(); i++) {
    fmt::print("printing pattern {} ({} nodes):\n", i, hier_patterns[i].count());
    for (auto v : hier_patterns[i].verts) {
      fmt::print("    label: {}, count: {}\n", v.first, v.second);
    }
  }
}

void Hier_tree::make_leaf_dims() {
  static std::default_random_engine     gen;
  static std::uniform_real_distribution dist(max_aspect_ratio, 1.0 - max_aspect_ratio);

  for (auto v : ginfo.al.verts()) {
    double width_factor = dist(gen);

    double width  = ginfo.areas(v) * width_factor;
    double height = ginfo.areas(v) * (1.0 - width_factor);

    leaf_dims[ginfo.labels(v)] = {width, height};
  }
}
