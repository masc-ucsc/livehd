#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void LGraph::each_graph_io(std::function<void(Node_pin &pin)> f1) {

  std::vector<Node_pin> pins;
  for(const auto &io_pin:get_self_sub_node().get_io_pins()) {
    if (io_pin.graph_io_idx==0)
      continue;

    Node_pin pin(this, 0, io_pin.graph_io_idx, io_pin.graph_io_pid, false);
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
    if (io_pin.graph_io_idx==0)
      continue;
    if (io_pin.dir != Sub_node::Direction::Input)
      continue;

    Node_pin pin(this, 0, io_pin.graph_io_idx, io_pin.graph_io_pid, false);
    f1(pin);
  }
}

void LGraph::each_graph_output(std::function<void(Node_pin &pin)> f1) {

  for(const auto &io_pin:get_self_sub_node().get_io_pins()) {
    if (io_pin.graph_io_idx==0)
      continue;
    if (io_pin.dir != Sub_node::Direction::Output)
      continue;

    Node_pin pin(this, 0, io_pin.graph_io_idx, io_pin.graph_io_pid, false);
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

    auto dpin = Node_pin(this,0,ni.get_nid(), ni.get_dst_pid(), false);

    const Edge_raw *edge_raw = ni.get_output_begin();
    do {
      XEdge edge(dpin, Node_pin(this,0,edge_raw->get_idx(), edge_raw->get_inp_pid(), true));

      f1(edge);
      edge_raw += edge_raw->next_node_inc();
    } while (edge_raw != ni.get_output_end());
  }
}

void LGraph::each_sub_fast_direct(const std::function<bool(Node &, const Lg_type_id &)> fn) {

  const auto &bm  = get_sub_ids();
  bmsparse::bvector_type::enumerator en = bm.first();
  for(; en.valid(); ++en) {
    Index_ID    cid = *en;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto lgid = get_type_sub(cid);
    auto node = Node(this,0,Node::Compact(cid));

    bool cont = fn(node, lgid);
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
