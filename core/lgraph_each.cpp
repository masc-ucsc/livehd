
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void LGraph::each_graph_input(std::function<void(Node_pin &pin)> f1) {
  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();

    auto pin = get_node(p.nid).get_driver_pin(p.pos);
    f1(pin);
  }
}

void LGraph::each_graph_output(std::function<void(Node_pin &pin)> f1) {
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();

    auto pin = get_node(p.nid).get_driver_pin(p.pos);
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

void LGraph::each_input_pin_fast(std::function<void(Node_pin &pin)> f1) {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_pin_inputs() && !ni.is_graph_io_input()) continue;

    Node_pin pin(this, 0, ni.get_nid(), ni.get_dst_pid(), false);
    f1(pin);
  }
}

void LGraph::each_output_pin_fast(std::function<void(Node_pin &pin)> f1) {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_pin_outputs() && !ni.is_graph_io_output()) continue;

    Node_pin pin(this, 0, ni.get_nid(), ni.get_dst_pid(), false);
    f1(pin);
  }
}

void LGraph::each_output_edge_fast(std::function<void(Index_ID, Port_ID, Index_ID, Port_ID)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_local_outputs()) continue;

    const Edge_raw *edge_raw = ni.get_output_begin();
    do {
      f1(ni.get_nid(), ni.get_dst_pid(), edge_raw->get_idx(), edge_raw->get_inp_pid());
      edge_raw += edge_raw->next_node_inc();
    } while (edge_raw != ni.get_output_end());
  }
}

void LGraph::each_sub_graph_fast_direct(
    const std::function<bool(const Index_ID &, const Lg_type_id &, std::string_view)> fn) const {
  const bm::bvector<> &bm  = get_sub_graph_ids();
  Index_ID             cid = bm.get_first();
  while (cid) {
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto       iname = get_node_instancename(cid);
    Lg_type_id lgid  = get_type_subgraph(cid);

    bool cont = fn(cid, lgid, iname);
    if (!cont) return;

    cid = bm.get_next(cid);
  }
}

void LGraph::each_root_fast_direct(std::function<bool(const Index_ID &)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;

    bool cont = f1(ni.get_nid());
    if (!cont) return;
  }
}
