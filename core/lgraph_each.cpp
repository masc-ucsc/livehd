//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "sub_node.hpp"

void LGraph::each_sorted_graph_io(std::function<void(const Node_pin &pin, Port_ID pos)> f1) {

  std::vector<std::pair<Node_pin,Port_ID>> pin_pair;

  auto out = Node(this, 0, Node::Hardcoded_output_nid);
  for(const auto &o_pin:out.out_setup_pins()) {
    auto pos = get_self_sub_node().get_graph_pos_from_instance_pid(o_pin.get_pid());
    pin_pair.emplace_back(std::make_pair(o_pin, pos));
  }

  auto inp = Node(this, 0, Node::Hardcoded_input_nid);
  for(const auto &i_pin:inp.out_setup_pins()) {
    auto pos = get_self_sub_node().get_graph_pos_from_instance_pid(i_pin.get_pid());
    pin_pair.emplace_back(std::make_pair(i_pin, pos));
  }

  std::sort(pin_pair.begin(), pin_pair.end()
           ,[](const std::pair<Node_pin,Port_ID>& a, const std::pair<Node_pin,Port_ID>& b)
            {
                return a.second > b.second;
            });

  for(auto &pp:pin_pair) {
    f1(pp.first, pp.second);
  }
}

void LGraph::each_graph_input(std::function<void(const Node_pin &pin)> f1) {

  auto node = Node(this, 0, Node::Hardcoded_input_nid);
  for(const auto &pin:node.out_setup_pins()) {
    f1(pin);
  }
}

void LGraph::each_graph_output(std::function<void(const Node_pin &pin)> f1) {

  auto node = Node(this, 0, Node::Hardcoded_output_nid);
  for(const auto &pin:node.out_setup_pins()) {
    f1(pin);
  }
}

void LGraph::each_node_fast(std::function<void(const Node &node)> f1) {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_master_root()) continue;

    Node node(this, 0, ni.get_nid());
    f1(node);
  }
}

void LGraph::each_output_edge_fast(std::function<void(XEdge &edge)> f1) {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_local_outputs()) continue;

    auto dpin = Node_pin(this,this, 0,ni.get_nid(), ni.get_dst_pid(), false); // FIXME: hid

    const Edge_raw *edge_raw = ni.get_output_begin();
    do {
      XEdge edge(dpin, Node_pin(this,this,0,edge_raw->get_idx(), edge_raw->get_inp_pid(), true)); // FIXME: hid

      f1(edge);
      edge_raw += edge_raw->next_node_inc();
    } while (edge_raw != ni.get_output_end());
  }
}

void LGraph::each_sub_fast_direct(const std::function<bool(Node &)> fn) {

  const auto &m = get_sub_nodes_map();
  for (auto it = m.begin(), end = m.end(); it != end; ++it) {
    Index_ID    cid = it->first.nid;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto node = Node(this,it->first);

    bool cont = fn(node);
    if (!cont) return;
  }
}

void LGraph::each_root_fast_direct(std::function<bool(Node &)> f1) {

  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;

    auto node = Node(this,0,ni.get_nid());

    bool cont = f1(node);
    if (!cont) return;
  }
}
