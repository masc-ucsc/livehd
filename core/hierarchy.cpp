//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "hierarchy.hpp"

#include "absl/strings/substitute.h"
#include "annotate.hpp"
#include "lgraph.hpp"

Hierarchy_tree::Hierarchy_tree(LGraph *_top)
    : mmap_lib::tree<Hierarchy_data>(_top->get_path(), absl::StrCat(_top->get_name(), "_htree")), top(_top) {}

LGraph *Hierarchy_tree::ref_lgraph(const Hierarchy_index &hidx) const {
  // NOTE: if this becomes a bottleneck, we can memorize the LGraph *
  const auto &data = get_data(hidx);

  I(data.lgid);

  auto *lg = LGraph::open(get_path(), data.lgid);
  I(lg);
  return lg;
}

Node Hierarchy_tree::get_instance_up_node(const Hierarchy_index &hidx) const {
  const auto &data = get_data(hidx);

  auto up_hidx = get_parent(hidx);

  I(data.lgid);

  LGraph *lg;
  if (up_hidx.is_root()) {
    lg = top;
  } else {
    const auto &up_data = get_data(up_hidx);
    lg                  = LGraph::open(get_path(), up_data.lgid);
    I(top != lg);
  }
  I(lg);

  return Node(top, lg, up_hidx, data.up_nid);
}

void Hierarchy_tree::regenerate_step(LGraph *lg, const Hierarchy_index &parent) {
  auto *tree_pos = Ann_node_tree_pos::ref(lg);

  int conta = 0;
  for (auto it : *tree_pos) {
    auto node = it.first.get_node(lg);
    I(node.is_type_sub());
    if (!node.is_type_sub_present()) continue;

    tree_pos->set(it.first, conta);

    auto child_lgid = node.get_type_sub();

    Hierarchy_data data(child_lgid, node.get_nid());
    auto           child = add_child(parent, data);

    auto *child_lg = lg->get_library().try_find_lgraph(child_lgid);
    if (child_lg) {
      regenerate_step(child_lg, child);
    }

    conta++;
  }
}

void Hierarchy_tree::regenerate() {
  Hierarchy_data data(top->get_lgid(), 0);
  set_root(data);

  regenerate_step(top, Hierarchy_tree::root_index());

#if 0
  for(const auto &index:depth_preorder()) {
    std::string indent(index.level, ' ');
    const auto &index_data = get_data(index);
    fmt::print("{} l:{} p:{} lgid:{} nid:{}\n", indent, index.level, index.pos, index_data.lgid, index_data.up_nid);
  }
#endif
}

Hierarchy_index Hierarchy_tree::go_down(const Node &node) const {
  auto *tree_pos = Ann_node_tree_pos::ref(node.get_class_lgraph());
  I(tree_pos);
  I(tree_pos->has(node.get_compact_class()));
  auto pos = tree_pos->get(node.get_compact_class());

  I(!is_leaf(node.get_hidx()));
  Hierarchy_index child(node.get_hidx().level + 1, pos);
  return child;
}
