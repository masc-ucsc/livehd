#include <functional>

#include "hier_tree.hpp"

Hier_tree::phier Hier_tree::collapse(phier node, double threshold_area) {
  if (find_area(node) >= threshold_area) {
    if (!node->is_leaf()) {
      auto n1 = collapse(node->children[0], threshold_area);
      auto n2 = collapse(node->children[1], threshold_area);

      I(node->children[0] != nullptr);
      I(node->children[1] != nullptr);

      return make_hier_tree(n1, n2);
    }

    return make_hier_node(node->graph_subset);
  }

  // create a new subtree from an existing subtree
  std::function<phier(phier)> copy_subtree = [&, this](phier rnode) -> phier {
    if (rnode->is_leaf()) {
      return make_hier_node(rnode->graph_subset);
    }

    I(rnode->children[0] != nullptr);
    I(rnode->children[1] != nullptr);

    // I believe the HiReg paper states that child nodes can have areas less than the thresold,
    // as long as the total area between the child nodes is greater than the threshold
    auto n1 = copy_subtree(rnode->children[0]);
    auto n2 = copy_subtree(rnode->children[1]);

    return make_hier_tree(n1, n2);
  };

  // make all nodes belong to the same set
  // this lambda assumes that set_number currently contains a unique set
  std::function<void(phier)> collapse_subtree = [&, this](phier rnode) {
    if (rnode->is_leaf()) {
      ginfo.sets.push_back(ginfo.al.vert_set());
      int new_set = ginfo.sets.size() - 1;

      for (auto v : ginfo.sets[rnode->graph_subset]) {
        ginfo.sets[new_set].insert(v);
      }
    } else {
      I(rnode->children[0] != nullptr);
      I(rnode->children[1] != nullptr);

      collapse_subtree(rnode->children[0]);
      collapse_subtree(rnode->children[1]);
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
  I(threshold_area >= 0.0);

  if (threshold_area > 0.0) {
    hiers.push_back(collapse(hiers[0], threshold_area));
  }
}