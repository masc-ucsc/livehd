//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "sub_node.hpp"

void LGraph::each_sorted_graph_io(std::function<void(Node_pin &pin, Port_ID pos)> f1, bool hierarchical) {
  if (node_internal.size() < Node::Hardcoded_output_nid)
    return;

  std::vector<std::pair<Node_pin, Port_ID>> pin_pair;

  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  auto out = Node(this, hidx, Node::Hardcoded_output_nid);
  for (auto &o_pin : out.out_setup_pins()) {
    auto pos = get_self_sub_node().get_graph_pos_from_instance_pid(o_pin.get_pid());
    pin_pair.emplace_back(std::make_pair(o_pin, pos));
  }

  auto inp = Node(this, hidx, Node::Hardcoded_input_nid);
  for (auto &i_pin : inp.out_setup_pins()) {
    auto pos = get_self_sub_node().get_graph_pos_from_instance_pid(i_pin.get_pid());
    pin_pair.emplace_back(std::make_pair(i_pin, pos));
  }

  std::sort(pin_pair.begin(), pin_pair.end(), [](const std::pair<Node_pin, Port_ID> &a, const std::pair<Node_pin, Port_ID> &b) {
    if (a.second == Port_invalid && b.second == Port_invalid)
      return a.first.get_pid() < b.first.get_pid();
    if (a.second == Port_invalid)
      return true;
    if (b.second == Port_invalid)
      return false;

    return a.second < b.second;
  });

  for (auto &pp : pin_pair) {
    f1(pp.first, pp.second);
  }
}

void LGraph::each_pin(const Node_pin &dpin, std::function<bool(Index_ID idx)> f1) const {

  Index_ID first_idx2 = dpin.get_idx();
  Index_ID idx2 = first_idx2;

  bool should_not_find = false;

  while (true) {
    I(!should_not_find);
    bool cont = f1(idx2);
    if (!cont)
      return;

    do{
      if (node_internal[idx2].is_last_state()) {
#ifndef NDEBUG
        idx2 = dpin.get_node().get_nid();
        should_not_find = true; // loop and try the others (should not have it before root)
#else
        idx2 = node_internal[idx2].get_nid();
#endif
      }else{
        idx2 = node_internal[idx2].get_next();
      }
      if (idx2==first_idx2)
        return;
    } while (node_internal[idx2].get_dst_pid() != dpin.get_pid());
  }
}

void LGraph::each_graph_input(std::function<void(Node_pin &pin)> f1, bool hierarchical) {
  if (node_internal.size() < Node::Hardcoded_output_nid)
    return;

  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  auto node = Node(this, hidx, Node::Hardcoded_input_nid);
  for (auto &pin : node.out_setup_pins()) {
    f1(pin);
  }
}

void LGraph::each_graph_output(std::function<void(Node_pin &pin)> f1, bool hierarchical) {
  if (node_internal.size() < Node::Hardcoded_output_nid)
    return;

  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  auto node = Node(this, hidx, Node::Hardcoded_output_nid);
  for (auto &pin : node.out_setup_pins()) {
    f1(pin);
  }
}

void LGraph::each_sub_fast_direct(const std::function<bool(Node &, Lg_type_id)> fn) {
  const auto &m = get_down_nodes_map();
  for (auto it = m.begin(), end = m.end(); it != end; ++it) {
    Index_ID cid = it->first.nid;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto node = Node(this, it->first);

    bool cont = fn(node, it->second);
    if (!cont)
      return;
  }
}

/*
// go over each subgraph, but also include hierarchy information.
// not really going to work - the hierarchy should be seperate from nodes.
void LGraph::each_sub_with_hier(const Hierarchy_index hidx, const std::function<bool(Node &, Lg_type_id)> fn) {
  const auto &m = get_down_nodes_map();
  for (auto it = m.begin(), end = m.end(); it != end; ++it) {
    Index_ID cid = it->first.nid;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    //auto hidx = LGraph::open(current_g->get_path(), current_g)
    
    for (auto child_hidx : ref_htree()->children(hidx)) {
      if (child_hidx != ref_htree()->invalid_index()) {

      } else {
        std::cout "invalid index" << std::endl;
      }
    }

    auto node = Node(this, it->first);

    bool cont = fn(node, it->second);
    if (!cont)
      return;
  }
}
*/

/*

Hierarchy_index LGraph::find_hidx_from_node(const Node& n) {
  const auto &m = get_down_nodes_map();
  for (auto it = m.begin(); it != m.end(); it++) {
    // compact_class -> lgid
    Hierarchy_index h();
  }
}
*/

void LGraph::each_sub_unique_fast(const std::function<bool(Node &, Lg_type_id)> fn) {
  const auto &m = get_down_nodes_map();
  std::set<Lg_type_id> visited;
  for (auto it = m.begin(), end = m.end(); it != end; ++it) {
    Index_ID cid = it->first.nid;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto node = Node(this, it->first);

    bool cont = true;
    if (visited.find(it->second) == visited.end()) {
      cont = fn(node, it->second);
      visited.insert(it->second);
    }
    if (!cont) return;
  }
}

