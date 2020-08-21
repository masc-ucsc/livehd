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
    fmt::print("1.name:{} pos:{} pid:{}\n",o_pin.get_name(), pos, o_pin.get_pid());
    pin_pair.emplace_back(std::make_pair(o_pin, pos));
  }

  auto inp = Node(this, hidx, Node::Hardcoded_input_nid);
  for (auto &i_pin : inp.out_setup_pins()) {
    auto pos = get_self_sub_node().get_graph_pos_from_instance_pid(i_pin.get_pid());
    fmt::print("2.name:{} pos:{} pid:{}\n",i_pin.get_name(), pos, i_pin.get_pid());
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
    fmt::print("3.name:{} pos:{} pid:{}\n", pp.first.get_name(), pp.second, pp.first.get_pid());
    f1(pp.first, pp.second);
  }
}

void LGraph::each_pin(const Node_pin &dpin, std::function<bool(Index_ID idx)> f1) const {

  Index_ID root_idx2 = dpin.get_root_idx();
  Index_ID idx2 = root_idx2;

  bool should_not_find = false;

  while (true) {
    if (node_internal[idx2].get_dst_pid() == dpin.get_pid()) {
      I(!should_not_find);
      bool cont = f1(idx2);
      if (!cont)
        return;
    }
#ifndef NDEBUG
    if (node_internal[idx2].is_last_state()) {
      idx2 = dpin.get_node().get_nid();
      should_not_find = true; // loop and try the others (should not have it before root)
    } else {
      idx2 = node_internal[idx2].get_next();
    }
    if (idx2 == root_idx2) {  // already visited
      return;
    }
#else
    if (node_internal[idx2].is_last_state()) {
      return;
    }
    idx2 = node_internal[idx2].get_next();
    I(idx2 != root_idx2);
#endif
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

void LGraph::each_top_node_fast(std::function<void(Node &node)> f1, bool hierarchical) {
  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  for (auto &ni : node_internal) {
    if (!ni.is_node_state())
      continue;
    if (!ni.is_master_root())
      continue;
    if (ni.is_graph_io())
      continue;

    Node node(this, hidx, ni.get_nid());
    f1(node);
  }
}

void LGraph::each_top_output_edge_fast(std::function<void(XEdge &edge)> f1, bool hierarchical) {
  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  for (const auto &ni : node_internal) {
    if (!ni.is_node_state())
      continue;
    if (!ni.is_root())
      continue;
    if (!ni.has_local_outputs())
      continue;

    auto dpin = Node_pin(this, this, hidx, ni.get_nid(), ni.get_dst_pid(), false);

    const Edge_raw *edge_raw = ni.get_output_begin();
    do {
      XEdge edge(dpin, Node_pin(this, this, hidx, edge_raw->get_idx(), edge_raw->get_inp_pid(), true));

      f1(edge);
      edge_raw += edge_raw->next_node_inc();
    } while (edge_raw != ni.get_output_end());
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

