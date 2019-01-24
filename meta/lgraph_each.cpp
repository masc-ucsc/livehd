
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void LGraph::each_input(std::function<void(Index_ID)> f1) const {
  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    f1(p.nid);
  }
}

void LGraph::each_input(std::function<void(Index_ID, Port_ID)> f1) const {
  for (auto it = input_array.begin(); it != input_array.end(); ++it) {
    const auto &p = it.get_field();
    f1(p.nid, p.pos);
  }
}

void LGraph::each_output(std::function<void(Index_ID)> f1) const {
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    f1(p.nid);
  }
}

void LGraph::each_output(std::function<void(Index_ID, Port_ID)> f1) const {
  for (auto it = output_array.begin(); it != output_array.end(); ++it) {
    const auto &p = it.get_field();
    f1(p.nid, p.pos);
  }
}

void LGraph::each_master_root_fast(std::function<void(Index_ID)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_master_root()) continue;

    f1(ni.get_nid());
  }
}

void LGraph::each_input_root_fast(std::function<void(Index_ID, Port_ID)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_pid_inputs()) continue;

    f1(ni.get_nid(), ni.get_dst_pid());
  }
}

void LGraph::each_output_root_fast(std::function<void(Index_ID, Port_ID)> f1) const {
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;
    if (!ni.has_pid_outputs() && !ni.is_graph_io_output()) continue;

    f1(ni.get_nid(), ni.get_dst_pid());
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
