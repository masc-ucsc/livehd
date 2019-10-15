//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "mmap_tree.hpp"

#include "node.hpp"

class Hierarchy_tree : public mmap_lib::tree<Hierarchy_data> {
protected:
  LGraph *top;

  void regenerate_step(LGraph *lg, const Hierarchy_index &parent);
public:
  Hierarchy_tree(LGraph *top);

  void regenerate(); // Triggered when the hierarchy may have changed

  //Lg_type_id get_lgid(const Hierarchy_index &hidx) const { return get_data(hidx).lgid; }
  Node get_instance_up_node(const Hierarchy_index &hidx) const;

  LGraph *ref_lgraph(const Hierarchy_index &hidx) const;

  Hierarchy_index go_down(const Node &node) const;

  Hierarchy_index go_up(const Node &node) const { return get_parent(node.get_hidx()); }
  bool is_root(const Node &node) const { return node.get_hidx().is_root(); }

};

