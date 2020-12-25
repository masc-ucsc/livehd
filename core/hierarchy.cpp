//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "hierarchy.hpp"

#include "annotate.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

Hierarchy_tree::Hierarchy_tree(LGraph *_top)
    : mmap_lib::tree<Hierarchy_data>(_top->get_path(), absl::StrCat(_top->get_name(), "_htree")), top(_top) {}

LGraph *Hierarchy_tree::ref_lgraph(const Hierarchy_index &hidx) const {
  I(!hidx.is_invalid()); // no hierarchical should not call this

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
  for (auto it : lg->get_down_nodes_map()) {

    auto child_lgid = lg->get_type_sub(it.first.get_nid());

    auto node = it.first.get_node(lg);
#ifndef NDEBUG
    I(node.is_type_sub());
    I(child_lgid == node.get_type_sub());
#endif

    auto *child_lg  = lg->get_library().try_find_lgraph(child_lgid); // faster
    if (child_lg==nullptr) {
      child_lg = node.ref_type_sub_lgraph();
      if (child_lg==nullptr)
        continue;
    }

    //Hierarchy_data data(child_lgid, node.get_nid());
    auto      child = add_child(parent, {child_lgid, node.get_nid()});

    I(child.pos == conta+get_first_child(parent).pos);
    tree_pos->set(it.first, conta);

    regenerate_step(child_lg, child);

    conta++;
  }
}

void Hierarchy_tree::regenerate() {
  Hierarchy_data data(top->get_lgid(), 0);
  set_root(data);

  regenerate_step(top, Hierarchy_tree::root_index());
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

Hierarchy_index Hierarchy_tree::go_up(const Node &node) const { return get_parent(node.get_hidx()); }
bool            Hierarchy_tree::is_root(const Node &node) const { return node.get_hidx().is_root(); }

/* LCOV_EXCL_START */
void Hierarchy_tree::dump() const {
  for (const auto &index : depth_preorder()) {
    std::string indent(index.level, ' ');
    const auto &index_data = get_data(index);
    fmt::print("{} level:{} pos:{} lgid:{} nid:{}\n", indent, index.level, index.pos, index_data.lgid, index_data.up_nid);
  }
}
/* LCOV_EXCL_STOP */
