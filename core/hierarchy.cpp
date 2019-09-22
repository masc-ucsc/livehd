//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "absl/strings/substitute.h"

#include "hierarchy.hpp"
#include "lgraph.hpp"
#include "annotate.hpp"

Hierarchy_tree::Hierarchy_tree(LGraph *_top)
  :mmap_lib::tree<Hierarchy_data>(_top->get_path(), absl::StrCat(_top->get_name(), "_htree"))
  ,top(_top) {
}

LGraph *Hierarchy_tree::ref_lgraph(const Hierarchy_index &hidx) const {

  const auto &data = get_data(hidx);

  I(data.lgid);

  auto *lg = LGraph::open(get_path(), data.lgid);
  I(lg);
  return lg;
}

Node Hierarchy_tree::get_instance_node(const Hierarchy_index &hidx) const {

  const auto &data = get_data(hidx);

  I(data.lgid);

  auto *lg = LGraph::open(get_path(), data.lgid);
  I(lg);
  I(top!=lg);

  return Node(top, lg, hidx, data.nid);
}

void Hierarchy_tree::regenerate_step(LGraph *lg, const Hierarchy_index &parent) {
  for (const auto it : lg->get_down_nodes_map()) {
    auto child_lgid = it.second;

    Hierarchy_data data(child_lgid, it.first.nid); // FIXME: inline once move operator is added to mmap_lib::tree
    auto child = add_child(parent, data);

    auto *child_lg = lg->get_library().try_find_lgraph(child_lgid);
    if (child_lg) {
      I(Ann_node_tree_pos::ref(lg)->get(Node::Compact_class(it.first.nid)) == child.pos); // down_nodes should be consistent with add_child

      regenerate_step(child_lg, child);
    }
  }
}

void Hierarchy_tree::regenerate() {
  Hierarchy_data data(top->get_lgid(), 0);
  set_root(data);

  regenerate_step(top, Hierarchy_tree::root_index());
}

Hierarchy_index Hierarchy_tree::go_down(const Node &node) const {
  // FIXME: Move to node.cpp
  auto *tree_pos = Ann_node_tree_pos::ref(node.get_class_lgraph());
  I(tree_pos);
  I(tree_pos->has(node.get_compact_class()));
  auto pos = tree_pos->get(node.get_compact_class());

  I(!is_leaf(node.get_hidx()));
  Hierarchy_index child(node.get_hidx().level, pos);
  return child;
}

