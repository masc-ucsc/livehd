//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cassert>

#include "lgraph.hpp"

#include "lgedge.hpp"
#include "lgedgeiter.hpp"

Fast_edge_iterator::CFast_edge_iterator Fast_edge_iterator::CFast_edge_iterator::operator++() {
  CFast_edge_iterator i(nid, g);

  nid = g->fast_next(nid);

  return i;
};

Edge_raw_iterator::CPod_iterator Edge_raw_iterator::CPod_iterator::operator++() {
  CPod_iterator i(ptr, e, inputs);
  I(Node_Internal::get(e).is_node_state());
  I(Node_Internal::get(ptr).is_node_state());

  const auto &  node = Node_Internal::get(ptr);

  if ((inputs && !ptr->is_last_input()) || (!inputs && !ptr->is_last_output())) {
    ptr += ptr->next_node_inc();
    assert(&node == &Node_Internal::get(ptr));
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

    assert(node.get_master_root_nid() == root[delta].get_master_root_nid());

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
      assert(&node == &Node_Internal::get(ptr));
      break;  // No more in this iterator
    }
  }

  return i;
}

bool Edge_raw_iterator_base::check_frontier() {
  bool pushed = false;
  for (auto &it : *frontier) {
    if (it.second > 0) {
      auto node = Node(g,0,Node::Compact(it.first));
      // FIXME: What if it is a sub-module just pure combinational or with flops? How to distinguish???
      if (node.get_type().is_pipelined()) {
        pending->push_back(it.first);
        it.second = -1;  // Mark as pipelined, but keep not to visit twice
        pushed    = true;
      }
    }
  }
  if (!pushed) {
    return false;
  }
  return true;
}

void Forward_edge_iterator::CForward_edge_iterator::add_node(const Index_ID nid) {
  assert(g->get_node_int(nid).is_master_root());

  for (const auto &c : g->out_edges_raw(nid)) {
    I(g->get_node_int(c.get_idx()).is_root());
    Index_ID    master_root_nid = g->get_node_int(c.get_idx()).get_nid();
    I(g->get_node_int(master_root_nid).is_master_root());

    Frontier_type::iterator fit = frontier->find(master_root_nid);

    if (fit == frontier->end()) {
      int32_t ninputs = g->get_node_int(master_root_nid).get_node_num_inputs() - 1;
      assert(ninputs >= 0);
      if (ninputs == 0) {  // Done already
        pending->push_back(master_root_nid);
      } else {
        (*frontier)[master_root_nid] = ninputs;
      }
    } else {
      int ninputs = (fit->second) - 1;
      if (ninputs == 0) {  // Done
        pending->push_back(master_root_nid);
        frontier->erase(fit);
      } else {
        fit->second = ninputs;
      }
    }
  }
}

Forward_edge_iterator::CForward_edge_iterator Forward_edge_iterator::begin() {
  for (auto it = g->input_array.begin(); it != g->input_array.end(); ++it) {
    pending.push_back(it.get_field().nid);
  }

  for (auto it = g->output_array.begin(); it != g->output_array.end(); ++it) {
    if (!g->get_node_int(it.get_field().nid).has_node_inputs())
      pending.push_back(it.get_field().nid);
  }

  // for forward iteration we want to start from constants as well
  const LGraph *lgr     = const_cast<LGraph *>(g);
  const auto &constants = lgr->get_const_node_ids();
  Index_ID    cid       = constants.get_first();
  while (cid) {
    assert(cid);
    pending.push_back(cid);
    cid = constants.get_next(cid);
  }

  Index_ID b = 0;
  if (!pending.empty()) {
    b = pending.back();
    pending.pop_back();
  }

  CForward_edge_iterator it(b, g, &frontier, &pending);

  return it;
}

void Backward_edge_iterator::CBackward_edge_iterator::find_dce_nodes() {
  Pending_type       discovered;
  std::set<Index_ID> dc_visited;
  std::set<Index_ID> floating;
  // floating.set_empty_key(0);     // 0 is not allowed as key
  // floating.set_deleted_key(0); // 128 is not allowed as key (4KB aligned)

  for (const auto &_idx : *frontier) {
    Index_ID nid = _idx.first;
    floating.insert(nid);
    discovered.push_back(nid);
    while (discovered.size() > 0) {
      Index_ID current = discovered.back();
      discovered.pop_back();
      dc_visited.insert(current);
      for (const auto &c : g->out_edges_raw(current)) {
        floating.erase(current);

        I(g->get_node_int(c.get_idx()).is_root());
        Index_ID    nid = g->get_node_int(c.get_idx()).get_nid();
        I(g->get_node_int(nid).is_master_root());

        if (dc_visited.find(nid) == dc_visited.end() && global_visited.find(nid) == global_visited.end()) {
          discovered.push_back(nid);
          floating.insert(nid);
        }
      }
    }
  }

  if (floating.size() > 0) {
    Pass::warn(fmt::format("graph {} is not DCE free, consider running the DCE pass\n", g->get_name()));
    for (const auto &nid : floating) {
      pending->push_back(nid);
    }
  }else{
    // FIXME: CHeck if the number of visited nodes matches n_nodes
    // Otherwise, insert disconnected nodes that may exist in the graph
  }
}

void Backward_edge_iterator::CBackward_edge_iterator::add_node(const Index_ID nid) {
  assert(g->get_node_int(nid).is_master_root());

  for (const auto &c : g->inp_edges_raw(nid)) {
    I(g->get_node_int(c.get_idx()).is_root());
    Index_ID    master_root_nid = g->get_node_int(c.get_idx()).get_nid();
    I(g->get_node_int(master_root_nid).is_master_root());

    Frontier_type::iterator fit = frontier->find(master_root_nid);

    if (fit == frontier->end()) {
      int32_t noutputs = g->get_node_int(master_root_nid).get_node_num_outputs() - 1;
      assert(noutputs >= 0);
      if (noutputs == 0) {  // Done already
        pending->push_back(master_root_nid);
      } else {
        (*frontier)[master_root_nid] = noutputs;
      }
    } else {
      int noutputs = (fit->second) - 1;
      if (noutputs == 0) {  // Done
        pending->push_back(master_root_nid);
        frontier->erase(fit);
      } else {
        fit->second = noutputs;
      }
    }
  }
}

Backward_edge_iterator::CBackward_edge_iterator Backward_edge_iterator::begin() {
  // FIXME: This may need to be moved to nid==0. If any input not visited, then add it (but only
  // if full input/output)

  // FIXME: pending has WAY too much redundant entries

  for (auto it = g->input_array.begin(); it != g->input_array.end(); ++it) {  // inputs without connection to preserve them
    if (!g->get_node_int(it.get_field().nid).has_node_outputs()) {
      I(g->get_node_int(it.get_field().nid).is_master_root());
      pending.push_back(it.get_field().nid);
    }
  }
  for (auto it = g->output_array.begin(); it != g->output_array.end(); ++it) {
    if (!g->get_node_int(it.get_field().nid).has_node_outputs()) {  // do not add outputs with connections
      I(g->get_node_int(it.get_field().nid).is_master_root());
      pending.push_back(it.get_field().nid);
    }
  }

  Index_ID b = 0;
  if (!pending.empty()) {
    b = pending.back();
    pending.pop_back();
  }

  CBackward_edge_iterator it(b, g, &frontier, &pending);

  return it;
}
