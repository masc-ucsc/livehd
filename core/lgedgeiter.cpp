//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cassert>

#include "lgraph.hpp"

#include "lgedge.hpp"
#include "lgedgeiter.hpp"

CFast_edge_iterator CFast_edge_iterator::operator++() {
  CFast_edge_iterator i(top_g, hid, nid, visit_sub);

  nid = top_g->fast_next(hid, nid);
  if (visit_sub) {
    if (top_g->is_sub(hid, nid)) {
      h_stack.emplace_back(hid, nid);
      hid = top_g->get_sub_hierarchy_id(hid, nid);
      nid = top_g->fast_next(0);
    }
  }else if (nid ==0) {
    if (!h_stack.empty()) {
      I(hid != h_stack.back().hid);
      I(h_stack.back().hid);
      hid = h_stack.back().hid;
      nid = h_stack.back().nid;
      h_stack.pop_back();
    }
  }

  return i;
};

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

bool Edge_raw_iterator_base::update_frontier() {
  bool pushed = false;
  for (auto &it : *frontier) {
    if (it.second > 0) {
      auto node = Node(top_g,Node::Compact(it.first));
      // FIXME: What if it is a sub-module just pure combinational or with flops? How to distinguish???
      if (node.get_type().is_pipelined()) {
        pending->set(it.first);
        it.second = -1;  // Mark as pipelined, but keep not to visit twice
        pushed    = true;
      }
    }
  }
  if (!pushed) {
    if (*hardcoded_nid) {
      nid = *hardcoded_nid;
      *hardcoded_nid = 0;
      return true;
    }

    return false;
  }
  return true;
}

void CForward_edge_iterator::set_current_node_as_visited() {
  I(top_g->get_node_int(nid).is_master_root());

  Node node(top_g,hid,nid);
  for (const auto &e : node.out_edges()) {
    const Node::Compact key = e.sink.get_node().get_compact();

    Frontier_type::iterator fit = frontier->find(key);

    if (fit == frontier->end()) {
      auto ninputs = node.get_num_inputs()-1; // -1 for self
      I(ninputs >= 0);
      if (ninputs == 0) {  // Done already
        pending->set(key);
      } else {
        (*frontier)[key] = ninputs;
      }
    } else {
      auto ninputs = (fit->second) - 1;
      if (ninputs == 0) {  // Done
        pending->set(key);
        frontier->erase(fit);
      } else {
        fit->second = ninputs;
      }
    }
  }
}

CForward_edge_iterator Forward_edge_iterator::begin() {
  pending.set(Node::Compact(0,Node::Hardcoded_input_nid));

  hardcoded_nid = Node::Hardcoded_output_nid;

  pending.insert(top_g->get_const_node_ids().begin(),top_g->get_const_node_ids().end());

  // Add any sub node that has no inputs but has outputs (not hit with forward)
  for(auto c_sub:top_g->get_sub_ids()) {
    I(c_sub.hid==0);
    Node n_sub(top_g,c_sub);
    if (n_sub.has_outputs() && !n_sub.has_inputs()) {
      pending.set(c_sub);
    }
  }

  I(!pending.empty());
  auto it = pending.begin();
  auto it_nid = it->nid;
  auto it_hid = it->hid;
  pending.erase(it);

  CForward_edge_iterator it2(top_g, it_hid, it_nid, &frontier, &pending, &hardcoded_nid);

  return it2;
}

void CBackward_edge_iterator::find_dce_nodes() {
  Node_set  discovered;
  Node_set  dc_visited;
  Node_set  floating;

  for (auto it : *frontier) {
    auto current = it.first;
    floating.set(current);

    do{
      dc_visited.set(current);
      floating.erase(current);

      Node node(top_g,current);
      for (const auto &e : node.out_edges()) {

        const Node::Compact key = e.sink.get_node().get_compact();

        if (!dc_visited.contains(key) && !back_iter_global_visited.contains(key)) {
          discovered.set(key);
          floating.set(key);
        }
      }
      if (discovered.empty())
        break;
      auto it = discovered.begin();
      current = *it;
      discovered.erase(it);
    }while(true);
  }

  if (!floating.empty()) {
    Pass::warn(fmt::format("graph {} is not DCE free, consider running the DCE pass\n", top_g->get_name()));
    pending->insert(floating.begin(),floating.end());
  }else{
    // FIXME: CHeck if the number of visited nodes matches n_nodes
    // Otherwise, insert disconnected nodes that may exist in the graph
  }
}

void CBackward_edge_iterator::set_current_node_as_visited() {

  I(top_g->get_node_int(nid).is_master_root());

  Node node(top_g,hid,nid);
  for (const auto &e : node.inp_edges()) {
    const Node::Compact key = e.driver.get_node().get_compact();

    Frontier_type::iterator fit = frontier->find(key);

    if (fit == frontier->end()) {
      auto noutputs = node.get_num_outputs()-1; // -1 for self
      I(noutputs >= 0);
      if (noutputs == 0) {  // Done already
        pending->set(key);
      } else {
        (*frontier)[key] = noutputs;
      }
    } else {
      auto noutputs = (fit->second) - 1;
      I(noutputs >= 0);
      if (noutputs == 0) {  // Done
        pending->set(key);
        frontier->erase(fit);
      } else {
        fit->second = noutputs;
      }
    }
  }

}

CBackward_edge_iterator Backward_edge_iterator::begin() {

  pending.set(Node::Compact(0,Node::Hardcoded_output_nid));
  hardcoded_nid = Node::Hardcoded_input_nid;

  // Add any sub node that has no outputs but has inputs (not hit with backward)
  for(auto c_sub:top_g->get_sub_ids()) {
    I(c_sub.hid==0);
    Node n_sub(top_g,c_sub);
    if (!n_sub.has_outputs() && n_sub.has_inputs()) {
      pending.set(c_sub);
    }
  }

  I(!pending.empty());
  auto it = pending.begin();
  auto it_nid = it->nid;
  auto it_hid = it->hid;
  pending.erase(it);

  CBackward_edge_iterator it2(top_g, it_hid, it_nid, &frontier, &pending, &hardcoded_nid);

  return it2;
}
