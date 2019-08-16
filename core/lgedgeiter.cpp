//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <cassert>

#include "lgraph.hpp"

#include "lgedge.hpp"
#include "lgedgeiter.hpp"

CFast_edge_iterator CFast_edge_iterator::operator++() {
  CFast_edge_iterator i(top_g, current_g, hidx, nid, visit_sub);

  nid = current_g->fast_next(nid);

  if (nid==0) {
    if (!h_stack.empty()) {
      I(visit_sub);
      I(hidx != h_stack.back().hidx);
      hidx      = h_stack.back().get_hidx();
      nid       = h_stack.back().get_nid();
      current_g = h_stack.back().get_class_lgraph();
      h_stack.pop_back();
    }
  }else if (visit_sub && current_g->is_sub(nid)) {
    const auto &sub = current_g->get_type_sub_node(nid);
    if (!sub.is_black_box()) {
      h_stack.emplace_back(Node(top_g, current_g, hidx, nid));

      hidx = current_g->hierarchy_go_down(hidx, nid);

      current_g = LGraph::open(top_g->get_path(), sub.get_name());

      nid = current_g->fast_next(0);
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

  I(pending->empty());
  bool pushed = false;
  for (auto &it : *frontier) {
    I(it.second >= 0);

#if 1
    // Faster
    Node node = current_node;
    node.update(it.first);
#else
    // Slower
    Node node(current_node.get_top_lgraph(), it.first);
#endif

    if (*hardcoded_nid == Node::Hardcoded_output_nid && node.has_outputs()) {
      continue;
    }else if (*hardcoded_nid == Node::Hardcoded_input_nid && node.has_inputs()) {
      continue;
    }else if (!node.get_type().is_pipelined()) {
      continue;
    }

    //fmt::print("Adding node {}\n", node.debug_name());

    pending->insert(it.first);
    pushed    = true;
    frontier->erase(it.first);
    break;
  }
  if (!pushed) {
    if (!pending->empty())
      return true;

    if (*hardcoded_nid) {
      // NOTE: Not very fast API, but it is called once per iterator
      Node node(current_node);
      node.update(*hardcoded_nid);
      I(node.get_class_lgraph() == node.get_top_lgraph()); // End of iteration
      pending->insert(node.get_compact());
      *hardcoded_nid = 0;
      return true;
    }

    return false;
  }
  return true;
}

void CForward_edge_iterator::set_current_node_as_visited() {

  global_visited->insert(current_node.get_compact());

  if (visit_sub && current_node.is_type_sub()) {
    const auto &sub = current_node.get_type_sub_node();
    if (!sub.is_black_box()) {
      LGraph *lg      = current_node.get_class_lgraph();
      LGraph *down_lg = LGraph::open(lg->get_path(), sub.get_name());

      insert_forward_graph_start_points(down_lg, current_node.hierarchy_go_down());
    }
  }

  for (const auto &e : current_node.out_edges()) {
    const auto sink_node = e.sink.get_node();
    if (sink_node.get_nid() == Node::Hardcoded_output_nid)
      continue;

    const Node::Compact key = sink_node.get_compact();
    if (global_visited->find(key) != global_visited->end())
      continue;

    Frontier_type::iterator fit = frontier->find(key);

    if (fit == frontier->end()) {
      auto ninputs = sink_node.get_num_inputs()-1; // -1 for self
      I(ninputs >= 0);
      if (ninputs == 0) {  // Done already
        pending->insert(key);
      } else {
        (*frontier)[key] = ninputs;
      }
    } else {
      auto ninputs = (fit->second) - 1;
      if (ninputs == 0) {  // Done
        pending->insert(key);
        frontier->erase(fit);
      } else {
        fit->second = ninputs;
      }
    }
  }
}

void CForward_edge_iterator::insert_forward_graph_start_points(LGraph *lg, Hierarchy_index down_hidx) {

  Node::Compact compact(down_hidx, Node::Hardcoded_input_nid);

  pending->insert(compact);

  for(const auto it:lg->get_const_value_map()) {
    pending->insert(Node::Compact(down_hidx, it.second.nid));
  }
  for(const auto it:lg->get_const_sview_map()) {
    pending->insert(Node::Compact(down_hidx, it.second.nid));
  }

  // Add any sub node that has no inputs but has outputs (not hit with forward)
  for(auto it:lg->get_sub_nodes_map()) {
    Node n_sub(lg, it.first);
    if (n_sub.has_outputs() && !n_sub.has_inputs()) {
      pending->insert(n_sub.get_compact());
    }
  }

}

void CBackward_edge_iterator::insert_backward_graph_start_points(LGraph *lg, Hierarchy_index down_hidx) {

  pending->insert(Node::Compact(down_hidx, Node::Hardcoded_output_nid));

  // Add any sub node that has no outputs but has inputs (not hit with backward)
  for(auto it:lg->get_sub_nodes_map()) {
    Node n_sub(lg, it.first);
    if (!n_sub.has_outputs() && n_sub.has_inputs()) {
      pending->insert(n_sub.get_compact());
    }
  }

}

CForward_edge_iterator::CForward_edge_iterator(
    LGraph *lg
    ,bool _visit_sub
    ,Frontier_type *_frontier
    ,Node_set_type *_pending
    ,Index_ID *_hardcoded_nid
    ,Node_set_type *_global_visited)
  : Edge_raw_iterator_base(
      _visit_sub
      ,_frontier
      ,_pending
      ,_hardcoded_nid
      ,_global_visited) {

  I(pending->empty());

  insert_forward_graph_start_points(lg, lg->hierarchy_root());

  I(!pending->empty());
  auto it = pending->begin();

  current_node.update(lg, *it);

  pending->erase(it);
}

CBackward_edge_iterator::CBackward_edge_iterator(
    LGraph *lg
    ,bool _visit_sub
    ,Frontier_type *_frontier
    ,Node_set_type *_pending
    ,Index_ID *_hardcoded_nid
    ,Node_set_type *_global_visited)
  : Edge_raw_iterator_base(
      _visit_sub
      ,_frontier
      ,_pending
      ,_hardcoded_nid
      ,_global_visited) {

  I(pending->empty());

  insert_backward_graph_start_points(lg, lg->hierarchy_root());

  I(!pending->empty());
  auto it = pending->begin();

  current_node.update(lg, *it);

  pending->erase(it);
}

CFast_edge_iterator Fast_edge_iterator::begin() const {
  if (top_g->empty())
    return end();

  return CFast_edge_iterator(top_g, top_g, it_hidx, top_g->fast_next(0), visit_sub);
}

void CBackward_edge_iterator::set_current_node_as_visited() {

  global_visited->insert(current_node.get_compact());

  if (visit_sub && current_node.is_type_sub()) {
    const auto &sub = current_node.get_type_sub_node();
    if (!sub.is_black_box()) {
      LGraph *lg      = current_node.get_class_lgraph();
      LGraph *down_lg = LGraph::open(lg->get_path(), sub.get_name());

      insert_backward_graph_start_points(down_lg, current_node.hierarchy_go_down());
    }
  }

  for (const auto &e : current_node.inp_edges()) {
    const auto driver_node = e.driver.get_node();
    if (driver_node.get_nid() == Node::Hardcoded_input_nid)
      continue;

    const Node::Compact key = driver_node.get_compact();
    if (global_visited->find(key) != global_visited->end())
      continue;

    Frontier_type::iterator fit = frontier->find(key);

    if (fit == frontier->end()) {
      auto noutputs = driver_node.get_num_outputs()-1; // -1 for self
      I(noutputs >= 0);
      if (noutputs == 0) {  // Done already
        pending->insert(key);
      } else {
        (*frontier)[key] = noutputs;
      }
    } else {
      auto noutputs = (fit->second) - 1;
      I(noutputs >= 0);
      if (noutputs == 0) {  // Done
        pending->insert(key);
        frontier->erase(fit);
      } else {
        fit->second = noutputs;
      }
    }
  }

}

