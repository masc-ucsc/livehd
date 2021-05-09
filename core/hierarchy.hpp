//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgraph_base_core.hpp"
#include "mmap_tree.hpp"

class Node;
class Lgraph;

class Hierarchy_tree : public mmap_lib::tree<Hierarchy_data> {
protected:
  Lgraph *top;

  void regenerate_step(Lgraph *lg, const Hierarchy_index &parent);

public:
  Hierarchy_tree(Lgraph *top);

  void regenerate();  // Triggered when the hierarchy may have changed

  // Lg_type_id get_lgid(const Hierarchy_index &hidx) const { return get_data(hidx).lgid; }
  Node get_instance_up_node(const Hierarchy_index &hidx) const;

  Lgraph *ref_lgraph(const Hierarchy_index &hidx) const;

  Hierarchy_index go_down(const Node &node) const;

  Hierarchy_index go_up(const Node &node) const;

  static constexpr mmap_lib::Tree_index root_index() { return mmap_lib::Tree_index::root(); }
  bool                                  is_root(const Node &node) const;

  void dump() const;
};
