
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void LGraph::each_input(std::function<void(const Node_pin &pin)> f1) const {
  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();

    auto pin = get_node(p.nid).get_driver_pin(p.pos);
    f1(pin);
  }
}

void LGraph::each_output(std::function<void(const Node_pin &pin)> f1) const {
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();

    auto pin = get_node(p.nid).get_driver_pin(p.pos);
    f1(pin);
  }
}

void LGraph::each_node_fast(std::function<void(ConstNode &node)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_master_root()) continue;

    ConstNode node(this, ni.get_nid());
    f1(node);
  }
}

void LGraph::each_node_fast(std::function<void(Node &node)> f1) {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_master_root()) continue;

    Node node(this, ni.get_nid());
    f1(node);
  }
}

void LGraph::each_input_pin_fast(std::function<void(const Node_pin &pin)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_pin_inputs() && !ni.is_graph_io_input()) continue;

    Node_pin pin(ni.get_nid(), ni.get_dst_pid(), false);
    f1(pin);
  }
}

void LGraph::each_output_pin_fast(std::function<void(const Node_pin &pin)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_pin_outputs() && !ni.is_graph_io_output()) continue;

    Node_pin pin(ni.get_nid(), ni.get_dst_pid(), false);
    f1(pin);
  }
}

void LGraph::each_output_edge_fast(std::function<void(Index_ID, Port_ID, Index_ID, Port_ID)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_local_outputs()) continue;

    const Edge *edge = ni.get_output_begin();
    do {
      f1(ni.get_nid(), ni.get_dst_pid(), edge->get_idx(), edge->get_inp_pid());
      edge += edge->next_node_inc();
    } while (edge != ni.get_output_end());
  }
}
