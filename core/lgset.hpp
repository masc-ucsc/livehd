//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "absl/container/flat_hash_set.h"

#include "iassert.hpp"
#include "dense.hpp"

#include "node.hpp"

using Node_set_base = absl::flat_hash_set<Node::Compact>;
class Node_set : public Node_set_base {
protected:
  friend class LGraph;
  friend class LGraph_Node_Type;
  friend class Node_pin;
  friend class Node;

  void set(Index_ID nid) {
    Node_set_base::insert(Node::Compact(nid,0));
  }

  void set(Index_ID nid, Hierarchy_id hid) {
    Node_set_base::insert(Node::Compact(nid,hid));
  }

public:
  void serialize(std::string_view file) {
    fmt::print("serialize {} not implemented\n",file);
  }

  void unserialize(std::string_view file) {
    // This can be lazy, and reload can happen afterwards
    fmt::print("unserialize {} not implemented\n",file);
  }

  bool has(const Node &node) const {
    return contains(node.get_compact());
  }

  void set(const Node &node) {
    Node_set_base::insert(node.get_compact());
  }

  void set(const Node::Compact &cnode) {
    Node_set_base::insert(cnode);
  }

  static Node get_node(LGraph *g, Node_set_base::const_iterator it) {
    return Node(g, *it);
  }
};

