//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgraph_base_core.hpp"
#include "mmap_tree.hpp"

class Node;
class Lgraph;

// hidx:
//
// ":" root is_hierarchical
// "" is_invalid (root, not hierarhical) -> is_root
// ":lgid.nid"  top(nid)->lgid
// ":lgid1.nid1:lgid2.nid2"  top(nid1)->lgid1(nid2)->lgid2
//
class Hierarchy {
protected:
  Lgraph *top;

  static Hierarchy_index go_down(Hierarchy_index, Lg_type_id lgid, Index_id nid);
  static std::tuple<Hierarchy_index, Lgraph *> go_next_down(Hierarchy_index hidx, Lgraph *lg);
  std::tuple<Hierarchy_index, Lgraph *, Index_id> get_instance_up(const Hierarchy_index hidx) const;

public:
  Hierarchy(Lgraph *top);

  Lgraph *ref_lgraph(const Hierarchy_index hidx) const;

  Node get_instance_up_node(const Hierarchy_index hidx) const;

  static Hierarchy_index go_up(const Hierarchy_index hidx);
  static Hierarchy_index go_down(const Node &node);
  static Hierarchy_index go_up(const Node &node);

  std::tuple<Hierarchy_index, Lgraph *> get_next(const Hierarchy_index hidx) const;

  static constexpr Hierarchy_index hierarchical_root() { return ":"; }
  static constexpr Hierarchy_index non_hierarchical() { return ""; }

  static bool  is_root(const Node &node);
  static constexpr bool  is_root(const Hierarchy_index hidx) { return hidx.empty() || hidx == ":"; }
  static constexpr bool  is_invalid(const Hierarchy_index hidx) { return hidx.empty(); }

  void dump() const;
};
