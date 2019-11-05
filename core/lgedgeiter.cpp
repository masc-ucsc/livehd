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

  if (nid.is_invalid()) {
    if (visit_sub) {
      auto next_hidx = top_g->ref_htree()->get_depth_preorder_next(hidx);
      if (!next_hidx.is_invalid()) {
        hidx = next_hidx;
        current_g = top_g->ref_htree()->ref_lgraph(hidx);
        nid = current_g->fast_first();
      }else{
        current_g = top_g;
        I(nid == 0);
        hidx.invalidate();
      }
    }else{
      I(nid == 0);
      hidx.invalidate();
    }
  }

  return *this;
};

Fast_edge_iterator::Fast_iter Fast_edge_iterator::begin() const {
  auto nid = top_g->fast_first();

  return Fast_edge_iterator::Fast_iter(top_g, top_g, Hierarchy_tree::root_index(), nid, visit_sub);
}

Flow_base_iterator::Flow_base_iterator(bool _visit_sub)
  :global_it(Fast_edge_iterator::Fast_iter(_visit_sub))
  ,global_it_end(Fast_edge_iterator::Fast_iter(_visit_sub))
  ,visit_sub(_visit_sub) {
}

Flow_base_iterator::Flow_base_iterator(LGraph *lg, bool _visit_sub)
  :global_it(lg->fast(_visit_sub).begin())
  ,global_it_end(Fast_edge_iterator::Fast_iter(_visit_sub))
  ,visit_sub(_visit_sub) {
}

void Fwd_edge_iterator::Fwd_iter::topo_add_chain_down(const Node_pin &dst_pin) {

  I(dst_pin.get_node().is_type_sub_present());

  auto down_pin = dst_pin.get_down_pin();
  I(down_pin.is_sink()); // fwd

  //fmt::print("topo       down node:{} down_pin:{}\n", down_pin.get_node().debug_name(), down_pin.debug_name());

  for (auto &edge2 : down_pin.inp_edges()) {  // fwd
    I(edge2.sink.get_pid() == down_pin.get_pid());
    topo_add_chain_fwd(edge2.driver);
  }
}

void Fwd_edge_iterator::Fwd_iter::topo_add_chain_fwd(const Node_pin &dst_pin) {

  const auto  dst_node = dst_pin.get_node();
  if (visited.count(dst_node.get_compact()))
    return;

  if (visit_sub) {
    if (dst_node.is_type_sub_present()) { // DOWN??
      topo_add_chain_down(dst_pin);
      return;
    }else if (dst_node.is_graph_input()) { // fwd: UP??
      if (!dst_node.is_root()) { // fwd: UP??
        auto up_pin = dst_pin.get_up_pin();
        if (up_pin.is_invalid())
          return; // Pin is not connected

        I(up_pin.is_sink()); // fwd

        for (auto &edge2 : up_pin.inp_edges()) {  // fwd
          I(edge2.sink.get_pid() == up_pin.get_pid());
          topo_add_chain_fwd(edge2.driver);
        }
      }
      return;
    }
  }

  pending_stack.push_back(dst_node);
}

void Fwd_edge_iterator::Fwd_iter::fwd_get_from_pending() {

  do {

    while(!pending_stack.empty()) {
      auto node = pending_stack.back();

#if 1
      if (visited.count(node.get_compact())) {
        pending_stack.pop_back();
        continue;
      }
#endif
//      if (node.debug_name() == "node_372_xor_lg_test_2")
//        fmt::print("HERE\n");
//

      //fmt::print("trying {}\n",node.debug_name());

      bool any_propagated=false;
      if (visit_sub && node.is_type_sub_present()) {
        for (auto &pin : node.out_connected_pins()) { // fwd
          topo_add_chain_down(pin);
          any_propagated=true;
        }
      }

      bool can_be_visited=false;
      if (likely(!any_propagated && !node.is_graph_io() && (!visit_sub || !node.is_type_sub_present()))) {
        can_be_visited = true;
        for (auto &edge2 : node.inp_edges()) { // fwd
          //fmt::print("adding {} from {}\n",edge2.driver.get_node().debug_name(), node.debug_name());
          topo_add_chain_fwd(edge2.driver);
        }
      }

      if (pending_stack.back() != node)
        continue;
      visited.insert(node.get_compact());
      pending_stack.pop_back();

      if (can_be_visited) {
        current_node.update(node);
        return;
      }
    }

    if (global_it == global_it_end) {
      current_node.invalidate();
      return;
    }

    I(!(*global_it).is_graph_io()); // NOTE: should we propagate IO for going up?
    if (!visited.count((*global_it).get_compact())) {
      pending_stack.push_back(*global_it);
      for (auto &edge2 : (*global_it).inp_edges()) { // fwd
        //fmt::print("chain  {} from {}\n",edge2.driver.get_node().debug_name(), (*global_it).debug_name());
        topo_add_chain_fwd(edge2.driver);
      }
    }

    ++global_it;

  }while(true);

}

void Fwd_edge_iterator::Fwd_iter::fwd_first(LGraph *lg) {
  I(current_node.is_invalid());

  fwd_get_from_pending();

  I(!current_node.is_invalid());
}

void Fwd_edge_iterator::Fwd_iter::fwd_next() {

  I(!current_node.is_invalid());

  fwd_get_from_pending();
}

void Bwd_edge_iterator::Bwd_iter::bwd_first(LGraph *lg) {
  I(pending_stack.empty());
  I(visited.empty());
}

void Bwd_edge_iterator::Bwd_iter::bwd_next() {
}

