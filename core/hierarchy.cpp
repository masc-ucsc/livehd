//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "absl/strings/substitute.h"

#include "hierarchy.hpp"
#include "lgraph.hpp"

Hierarchy_tree::Hierarchy_tree(LGraph *top)
  :mmap_lib::tree<Hierarchy_data>(top->get_path(), absl::StrCat(top->get_name(), "_htree")) {
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

void Hierarchy_tree::regenerate() {}

