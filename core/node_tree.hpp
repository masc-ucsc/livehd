//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lgraph_base_core.hpp"
#include "mmap_tree.hpp"
#include "node.hpp"

using Tree_index = mmap_lib::Tree_index;

class Node_tree : public mmap_lib::tree<Node> {
private:
  constexpr static bool debug_verbose = false;
protected:
  LGraph *root;

public:
  Node_tree(LGraph *root);

  // return root LGraph used to generate the node tree
  LGraph* get_root_lg() const { return root; }

  void dump() const;
};