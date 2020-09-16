#include <functional>

#include "hier_tree.hpp"

Hier_tree::phier Hier_tree::make_hier_tree(phier t1, phier t2) {
  auto pnode  = std::make_shared<Hier_node>(ginfo);
  pnode->name = "node_" + std::to_string(unique_node_counter);

  pnode->children[0] = t1;
  t1->parent         = pnode;

  pnode->children[1] = t2;
  t2->parent         = pnode;

  unique_node_counter++;

  return pnode;
}

Hier_tree::phier Hier_tree::make_hier_node(const set_t s) {
  I(s.size() > 0);

  phier pnode = std::make_shared<Hier_node>(ginfo);
  pnode->name = "leaf_node_" + std::to_string(++unique_node_counter);

  for (auto v : s) {
    pnode->area += ginfo.areas(v);
  }

  for (auto v : s) {
    pnode->graph_set.insert(v);
  }

  return pnode;
}

// create a new subtree from an existing subtree
Hier_tree::phier Hier_tree::copy_subtree(phier rnode) {
  if (rnode->is_leaf()) {
    return make_hier_node(rnode->graph_set);
  }

  I(rnode->children[0] != nullptr);
  I(rnode->children[1] != nullptr);

  // I believe the HiReg paper states that child nodes can have layouts less than the thresold,
  // as long as the total area between the child nodes is greater than the threshold
  auto n1 = copy_subtree(rnode->children[0]);
  auto n2 = copy_subtree(rnode->children[1]);

  return make_hier_tree(n1, n2);
}

Hier_tree::phier Hier_tree::collapse(phier node, Graph_info<g_type>& gi, double threshold_area) {
  if (find_area(node) >= threshold_area) {
    if (node->is_leaf()) {
      return make_hier_node(node->graph_set);
    }

    auto n1 = collapse(node->children[0], gi, threshold_area);
    auto n2 = collapse(node->children[1], gi, threshold_area);

    I(node->children[0] != nullptr);
    I(node->children[1] != nullptr);

    return make_hier_tree(n1, n2);
  }

  auto collapse_set = gi.al.vert_set();

  std::function<void(phier)> get_subtree_nodes = [&](phier rnode) {
    if (rnode->is_leaf()) {
      for (auto v : rnode->graph_set) {
        collapse_set.insert(v);
      }
    } else {
      I(rnode->children[0] != nullptr);
      I(rnode->children[1] != nullptr);

      get_subtree_nodes(rnode->children[0]);
      get_subtree_nodes(rnode->children[1]);
    }
  };

  get_subtree_nodes(node);

  auto collapsed_v = gi.collapse_to_vertex(collapse_set);

  node->area = gi.areas(collapsed_v);
  node->graph_set.insert(collapsed_v);

  // delete child nodes once everything is moved over
  // TODO: should this be a recursive resetting?
  node->children[0].reset();
  node->children[1].reset();

  return node;
}

void Hier_tree::collapse(const size_t hier_index, const double threshold_area) {
  I(threshold_area >= 0.0);
  I(hier_index < hiers.size());
  I(hier_index != 0);

  if (threshold_area > 0.0) {
    hiers[hier_index] = copy_subtree(hiers[0]);
    hiers[hier_index] = collapse(hiers[hier_index], collapsed_gis[hier_index], threshold_area);
  }
}