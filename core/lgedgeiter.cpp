//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cassert>

#include "lgraph.hpp"

#include "lgedge.hpp"
#include "lgedgeiter.hpp"

Edge_raw_iterator::CPod_iterator Edge_raw_iterator::CPod_iterator::operator++() {
  CPod_iterator i(ptr, e, inputs);
  I(Node_Internal::get(e).is_node_state());
  I(Node_Internal::get(ptr).is_node_state());

  const auto &  node = Node_Internal::get(ptr);

  if ((inputs && !ptr->is_last_input()) || (!inputs && !ptr->is_last_output())) {
    ptr += ptr->next_node_inc();
    I(&node == &Node_Internal::get(ptr));
    return i;
  }

  if (node.is_last_state()) {
    ptr = e;
    return i;
  }

  const Edge_raw *ptr2 = ptr;
  while (true) {
    const auto &         root_page = Node_Internal_Page::get(ptr2);
    const Node_Internal *root      = (const Node_Internal *)&root_page;

    Index_ID idx   = Node_Internal::get(ptr2).get_next();
    Index_ID delta = idx - root_page.get_idx();

    I(node.get_master_root_nid() == root[delta].get_master_root_nid());

    I(root[delta].is_node_state());

    if (inputs) {
      ptr2 = root[delta].get_input_begin();
      if (root[delta].has_local_inputs()) {
        ptr = ptr2;
        break;
      }
    } else {
      ptr2 = root[delta].get_output_begin();
      if (root[delta].has_local_outputs()) {
        ptr = ptr2;
        break;
      }
    }

    if (root[delta].is_last_state()) {
      ptr = e;
      // if ((root[delta].has_local_outputs() && !inputs)
      //   ||(root[delta].has_local_inputs() && inputs))
      I(&node == &Node_Internal::get(ptr));
      break;  // No more in this iterator
    }
  }

  return i;
}

Fast_edge_iterator::Fast_iter &Fast_edge_iterator::Fast_iter::operator++() {
  I(nid!=0);

  nid = current_g->fast_next(nid);

  if (visit_sub && nid.is_invalid()) {
    auto next_hidx = top_g->ref_htree()->get_depth_preorder_next(hidx);
    if (!next_hidx.is_invalid()) {
      hidx = next_hidx;
      current_g = top_g->ref_htree()->ref_lgraph(hidx);
      nid = current_g->fast_first();
    }else{
      nid = 0;
      current_g = top_g;
      hidx = Hierarchy_tree::root_index(); // Root, last
    }
  }

  return *this;
};

Fast_edge_iterator::Fast_iter Fast_edge_iterator::begin() const {
  auto nid = top_g->fast_first();

  return Fast_edge_iterator::Fast_iter(top_g, top_g, Hierarchy_tree::root_index(), nid, visit_sub);
}

void Fwd_edge_iterator::Fwd_iter::fwd_first(LGraph *lg) {
}

void Fwd_edge_iterator::Fwd_iter::fwd_next() {
}


void Bwd_edge_iterator::Bwd_iter::bwd_first(LGraph *lg) {
}

void Bwd_edge_iterator::Bwd_iter::bwd_next() {
}

