#include "hier_tree.hpp"

Hier_tree::Hier_tree(Graph_info&& netlist, unsigned int num_components) : ginfo(std::move(netlist)) {
  std::cout << "  creating hierarchy...";
  I(num_components >= 1);

  // if the graph is not fully connected, ker-lin fails to work.
  for (const auto& v : ginfo.al.verts()) {
    for (const auto& ov : ginfo.al.verts()) {
      if (ginfo.find_edge(v, ov) == ginfo.al.null_edge()) {
        auto temp_e        = ginfo.al.insert_edge(v, ov);
        ginfo.weights[temp_e] = 0;
      }
    }
  }

  hiers.push_back(discover_hierarchy(ginfo, 0, num_components));

  // undo temp edge creation because it's really inconvienent elsewhere
  for (const auto& v : ginfo.al.verts()) {
    for (const auto& ov : ginfo.al.verts()) {
      auto e = ginfo.find_edge(v, ov);
      if (e != ginfo.al.null_edge() && ginfo.weights[e] == 0) {
        ginfo.al.erase_edge(e);
      }
    }
  }

  std::cout << "done." << std::endl;
}

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
