#include "hier_tree.hpp"

phier Hier_tree::collapse(phier node, double threshold_area) {
  if (find_area(node) >= threshold_area) {
    if (!node->is_leaf()) {
      auto n1 = collapse(node->children[0], threshold_area);
      auto n2 = collapse(node->children[1], threshold_area);

      return make_hier_tree(n1, n2);
    }

    return make_hier_node(node->graph_subset);
  }

  // create a new subtree from an existing subtree
  std::function<phier(phier)> copy_subtree = [&, this](phier n) -> phier {
    if (n->is_leaf()) {
      return make_hier_node(n->graph_subset);
    }

    // I believe the HiReg paper states that child nodes can have areas less than the thresold,
    // as long as the total area between the child nodes is greater than the threshold
    auto n1 = copy_subtree(n->children[0]);
    auto n2 = copy_subtree(n->children[1]);

    return make_hier_tree(n1, n2);
  };

  // make all nodes belong to the same set
  // this lambda assumes that set_number currently contains a unique set
  std::function<void(phier)> collapse_subtree = [&, this](phier n) {
    if (n->is_leaf()) {
      ginfo.sets.push_back(ginfo.al.vert_set());
      int new_set = ginfo.sets.size() - 1;

      for (auto v : ginfo.sets[n->graph_subset]) {
        ginfo.sets[new_set].insert(v);
      }
    } else {
      collapse_subtree(n->children[0]);
      collapse_subtree(n->children[1]);
    }
  };

  auto new_subtree = copy_subtree(node);
  collapse_subtree(new_subtree);

  new_subtree->area         = find_area(new_subtree);
  new_subtree->graph_subset = ginfo.sets.size() - 1;

  // delete child nodes once everything is moved over
  new_subtree->children[0] = nullptr;
  new_subtree->children[1] = nullptr;

  return new_subtree;
}

void Hier_tree::collapse(double threshold_area) {
  std::cout << "  collapsing hierarchy...";
  I(threshold_area >= 0.0);

  if (threshold_area > 0.0) {
    auto new_tree = collapse(root, threshold_area);
    collapsed_hiers.push_back(new_tree);
  }
  std::cout << "done." << std::endl;
}