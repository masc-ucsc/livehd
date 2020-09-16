#include <functional>

#include "hier_tree.hpp"

Hier_tree::phier Hier_tree::make_hier_tree(phier t1, phier t2) {
  auto pnode  = std::make_shared<Hier_node>();
  pnode->name = "node_" + std::to_string(node_number);

  pnode->children[0] = t1;
  t1->parent         = pnode;

  pnode->children[1] = t2;
  t2->parent         = pnode;

  node_number++;

  return pnode;
}

Hier_tree::phier Hier_tree::make_hier_node(const vertex_t v) {
  I(v != ginfo.al.null_vert());

  phier pnode = std::make_shared<Hier_node>();
  pnode->name = "leaf_node_" + std::to_string(node_number);

  pnode->area = ginfo.areas(v);

  pnode->graph_vert = v;

  node_number++;

  return pnode;
}

// create a new subtree from an existing subtree
Hier_tree::phier Hier_tree::copy_subtree(phier rnode) {
  if (rnode->is_leaf()) {
    return make_hier_node(rnode->graph_vert);
  }

  I(rnode->children[0] != nullptr);
  I(rnode->children[1] != nullptr);

  // I believe the HiReg paper states that child nodes can have layouts less than the thresold,
  // as long as the total area between the child nodes is greater than the threshold
  auto n1 = copy_subtree(rnode->children[0]);
  auto n2 = copy_subtree(rnode->children[1]);

  return make_hier_tree(n1, n2);
}

Hier_tree::phier Hier_tree::collapse(phier node, double threshold_area) {
  if (find_area(node) >= threshold_area) {
    if (!node->is_leaf()) {
      auto n1 = collapse(node->children[0], threshold_area);
      auto n2 = collapse(node->children[1], threshold_area);

      I(node->children[0] != nullptr);
      I(node->children[1] != nullptr);

      return make_hier_tree(n1, n2);
    }

    return make_hier_node(node->graph_vert);
  }

  // TODO: finish writing this!
  I(false);

  auto edges = ginfo.al.edge_set();
  auto verts = ginfo.al.vert_set();

  auto collapsed_v = ginfo.al.insert_vert();

  // right now, assume that every graph_set vector has one element in it.

  // make all nodes belong to the same set
  // this lambda assumes that set_number currently contains a unique set
  std::function<void(phier)> collapse_subtree = [&](phier rnode) {
    if (rnode->is_leaf()) {
      verts.insert(rnode->graph_vert);
    } else {
      I(rnode->children[0] != nullptr);
      I(rnode->children[1] != nullptr);

      collapse_subtree(rnode->children[0]);
      collapse_subtree(rnode->children[1]);
    }
  };

  /*
  // ginfo.sets.push_back(ginfo.al.vert_set());
  // size_t set_loc = ginfo.sets.size() - 1;

  auto verts = ginfo.al.vert_set();
  auto edges = ginfo.al.edge_set();

  // right now, assume that every graph_set vector has one element in it.

  // make all nodes belong to the same set
  // this lambda assumes that set_number currently contains a unique set
  std::function<void(phier)> get_node_info = [&, this](phier rnode) {
    if (rnode->is_leaf()) {
      for (auto n : ginfo.sets[rnode->graph_subset]) {
        verts.insert(n);
        for (auto e : ginfo.out_edges(n)) {
          edges.insert(e);
        }
      }
    } else {
      I(rnode->children[0] != nullptr);
      I(rnode->children[1] != nullptr);

      get_node_info(rnode->children[0]);
      get_node_info(rnode->children[1]);
    }
  };

  auto new_subtree = copy_subtree(node);
  get_node_info(new_subtree);
  */

  auto new_subtree = copy_subtree(node);
  collapse_subtree(new_subtree);

  new_subtree->area       = find_area(new_subtree);
  new_subtree->graph_vert = collapsed_v;

  // delete child nodes once everything is moved over
  new_subtree->children[0] = nullptr;
  new_subtree->children[1] = nullptr;

  return new_subtree;
}

void Hier_tree::collapse(const size_t hier_index, const double threshold_area) {
  I(threshold_area >= 0.0);
  I(hier_index < hiers.size());
  I(hier_index != 0);

  if (threshold_area > 0.0) {
    hiers[hier_index] = collapse(hiers[0], threshold_area);
  }
}