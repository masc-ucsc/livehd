//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "sub_node.hpp"

void LGraph::each_graph_io(std::function<void(Node_pin &pin)> f1) {

  std::vector<Node_pin> pins;
  for(const auto &io_pin:get_self_sub_node().get_io_pins()) {
    if (!io_pin.is_mapped())
      continue;

    Index_ID nid;
    if (io_pin.dir == Sub_node::Direction::Input) {
      nid = Node::Hardcoded_input_nid;
    }else{
      I(io_pin.dir == Sub_node::Direction::Output);
      nid = Node::Hardcoded_output_nid;
    }
    Node_pin pin(this, this, 0, nid, io_pin.graph_io_pid, false); // FIXME: hierarchy id???
    pins.emplace_back(pin);
  }
  std::sort(pins.begin(), pins.end()
           ,[](const Node_pin& a, const Node_pin& b)
            {
                return a.get_pid() > b.get_pid();
            });

  for(auto &pin:pins) {
    f1(pin);
  }
}

void LGraph::each_graph_input(std::function<void(Node_pin &pin)> f1) {

  for(const auto &io_pin:get_self_sub_node().get_io_pins()) {
    if (!io_pin.is_mapped())
      continue;
    if (io_pin.dir != Sub_node::Direction::Input)
      continue;

    Node_pin pin(this, this, 0, Node::Hardcoded_input_nid, io_pin.graph_io_pid, false);// FIXME: hid??
    f1(pin);
  }
}

void LGraph::each_graph_output(std::function<void(Node_pin &pin)> f1) {

  for(const auto &io_pin:get_self_sub_node().get_io_pins()) {
    if (!io_pin.is_mapped())
      continue;
    if (io_pin.dir != Sub_node::Direction::Output)
      continue;

    Node_pin pin(this, this, 0, Node::Hardcoded_output_nid, io_pin.graph_io_pid, false); // FIXME: hid???
    f1(pin);
  }
}

void LGraph::each_node_fast(std::function<void(Node &node)> f1) {
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

  const auto &m = get_sub_ids();
  for (auto it = m.begin(), end = m.end(); it != end; ++it) {
    Index_ID    cid = it->nid;
    I(it->hid==0); // subs have hid zero all the time
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto node = Node(this,*it);

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
