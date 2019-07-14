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

bool Edge_raw_iterator_base::try_insert_pending(const Node &node, const Node::Compact &compact) {
  I(node.get_compact() == compact);

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

  if (global_visited->find(node.get_compact())!=global_visited->end())
    return false;

  if (visit_sub) {
    auto empty = node.is_type_sub_empty();
    if (!empty) {
      LGraph *sub_lg = node.get_type_sub_lgraph();
      insert_graph_start_points(sub_lg, node.hierarchy_go_down());

      fmt::print(" adddel:{} from:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());
      delayed.push_back(node.get_compact()); // Do sub hierarchy first
      return false;
    }
  }

  fmt::print(" 2pending:{} from:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());
  pending->insert(compact);
  return true;
}

bool Edge_raw_iterator_base::update_frontier() {

  I(pending->empty());

  while (!delayed.empty()) {
    I(visit_sub);
    if (global_visited->find(delayed.back()) != global_visited->end()) {
      delayed.pop_back();
      continue;
    }

    Node node(current_node.get_top_lgraph(), delayed.back());
    delayed.pop_back();

    fmt::print(" delayed:{} from:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());
    propagate_io(node);
    if (!pending->empty())
      return true;
  }

  std::vector<Node::Compact> delayed_frontier_erase;

  for (auto &it : *frontier) {
    I(it.second > 0);

    Node node(current_node.get_top_lgraph(), it.first);

#if 0
    if (*hardcoded_nid == Node::Hardcoded_output_nid && node.has_outputs()) { // Forward_iterator
      continue;
    }else if (*hardcoded_nid == Node::Hardcoded_input_nid && node.has_inputs()) { // Backward_iterator
      continue;
    }
#endif
    if (!node.get_type().is_pipelined()) { // Flops/latches/rams or subgraphs
      continue;
    }

    bool inserted =  try_insert_pending(node, it.first);
    if (inserted) {
      fmt::print("  frontier.erase:{} from:{}\n",node.debug_name(), node.get_class_lgraph()->get_name());
      delayed_frontier_erase.emplace_back(it.first);
    }
  }
  for (const auto &c_node : delayed_frontier_erase) {
    frontier->erase(c_node);
  }

  if (!pending->empty())
    return true;

  if (*hardcoded_nid) {
    // NOTE: Not very fast API, but it is called once per iterator
    auto *top_lg = current_node.get_top_lgraph();
    Node node(top_lg, top_lg, top_lg->hierarchy_root(), *hardcoded_nid);
    pending->insert(node.get_compact());
    *hardcoded_nid = 0;
    return true;
  }

  return false;
}

void CForward_edge_iterator::set_current_node_as_visited() {
  GI(visit_sub, !(current_node.is_type_sub() && !current_node.is_type_sub_empty()));

  propagate_io(current_node);
}

void CForward_edge_iterator::propagate_io(const Node &node) {

  fmt::print("  prop:{} from:{}\n", node.debug_name(), node.get_class_lgraph()->get_name());

  if (global_visited->find(node.get_compact())!=global_visited->end())
    return;
  global_visited->insert(node.get_compact());

  for (const auto &e : node.out_edges()) {
    const auto sink_node = e.sink.get_node();
    if (sink_node.get_nid() == Node::Hardcoded_output_nid)
      continue;

    const Node::Compact sink_node_compact = sink_node.get_compact();
    if (global_visited->find(sink_node_compact) != global_visited->end())
      continue;

    Frontier_type::iterator fit = frontier->find(sink_node_compact);

    if (fit == frontier->end()) {
      auto ninputs = sink_node.get_num_inputs()-1; // -1 for self
      fmt::print("    out_new:{} {}\n", sink_node.debug_name(), ninputs);
      I(ninputs >= 0);
      if (ninputs == 0) {  // Done already
        try_insert_pending(sink_node, sink_node_compact);
      } else {
        (*frontier)[sink_node_compact] = ninputs;
      }
    } else {
      auto ninputs = (fit->second) - 1;
      fmt::print("    out_old:{} {}\n", sink_node.debug_name(), ninputs);
      if (ninputs == 0) {  // Done
        try_insert_pending(sink_node, sink_node_compact);
        frontier->erase(fit);
      } else {
        fit->second = ninputs;
      }
    }
  }
}

void CForward_edge_iterator::insert_graph_start_points(LGraph *lg, Hierarchy_index down_hidx) {

  Node::Compact compact(down_hidx, Node::Hardcoded_input_nid);

  pending->insert(compact);

  for(const auto it:lg->get_const_value_map()) {
    pending->insert(Node::Compact(down_hidx, it.second.nid));
  }
  for(const auto it:lg->get_const_sview_map()) {
    pending->insert(Node::Compact(down_hidx, it.second.nid));
  }

  // Add any sub node that has no inputs but has outputs (not hit with forward)
  for(auto it:lg->get_down_nodes_map()) {
    Node n_sub(lg, it.first);
    if (n_sub.has_outputs() && !n_sub.has_inputs()) {
      pending->insert(n_sub.get_compact());
    }
  }
}

void CBackward_edge_iterator::propagate_io(const Node &node) {
  I(false);// FIXME: implement me
}

void CBackward_edge_iterator::insert_graph_start_points(LGraph *lg, Hierarchy_index down_hidx) {

  pending->insert(Node::Compact(down_hidx, Node::Hardcoded_output_nid));

  // Add any sub node that has no outputs but has inputs (not hit with backward)
  for(auto it:lg->get_down_nodes_map()) {
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

  insert_graph_start_points(lg, lg->hierarchy_root());

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

  insert_graph_start_points(lg, lg->hierarchy_root());

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
      LGraph *down_lg = current_node.get_type_sub_lgraph();

      if (!down_lg->empty())
        insert_graph_start_points(down_lg, current_node.hierarchy_go_down());
    }
  }

  for (const auto &e : current_node.inp_edges()) {
    const auto driver_node = e.driver.get_node();
    if (driver_node.get_nid() == Node::Hardcoded_input_nid)
      continue;

    const Node::Compact driver_node_compact = driver_node.get_compact();
    if (global_visited->find(driver_node_compact) != global_visited->end())
      continue;

    Frontier_type::iterator fit = frontier->find(driver_node_compact);

    if (fit == frontier->end()) {
      auto noutputs = driver_node.get_num_outputs()-1; // -1 for self
      I(noutputs >= 0);
      if (noutputs == 0) {  // Done already
        try_insert_pending(driver_node, driver_node_compact);
      } else {
        (*frontier)[driver_node_compact] = noutputs;
      }
    } else {
      auto noutputs = (fit->second) - 1;
      I(noutputs >= 0);
      if (noutputs == 0) {  // Done
        try_insert_pending(driver_node, driver_node_compact);
        frontier->erase(fit);
      } else {
        fit->second = noutputs;
      }
    }
  }

}

