
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void LGraph::each_input(std::function<void(Index_ID)> f1) const {
  for(const auto &ent : inputs2node) {
    f1(ent.second.nid);
  }
}

void LGraph::each_input(std::function<void(Index_ID, Port_ID)> f1) const {
  for(const auto &ent : inputs2node) {
    f1(ent.second.nid,ent.second.pos);
  }
}

void LGraph::each_output(std::function<void(Index_ID)> f1) const {
  for(const auto &ent : outputs2node) {
    f1(ent.second.nid);
  }
}

void LGraph::each_output(std::function<void(Index_ID, Port_ID)> f1) const {
  for(const auto &ent : outputs2node) {
    f1(ent.second.nid,ent.second.pos);
  }
}

void LGraph::each_master_root_fast(std::function<void(Index_ID)> f1) const {

  for(const auto &ni: node_internal) {
    if (!ni.is_node_state())
      continue;
    if (!ni.is_master_root())
      continue;

    f1(ni.get_nid());
  }

}

void LGraph::each_root_fast(std::function<void(Index_ID)> f1) const {

  for(const auto &ni: node_internal) {
    if (!ni.is_node_state())
      continue;
    if (!ni.is_root())
      continue;

    f1(ni.get_nid());
  }

}

void LGraph::each_input_root_fast(std::function<void(Index_ID, Port_ID)> f1) const {

  for(const auto &ni: node_internal) {
    if (!ni.is_node_state())
      continue;
    if (!ni.is_root())
      continue;
    if (!ni.has_pid_inputs())
      continue;

    f1(ni.get_nid(), ni.get_out_pid());
  }

}

void LGraph::each_output_root_fast(std::function<void(Index_ID, Port_ID)> f1) const {

  for(const auto &ni: node_internal) {
    if (!ni.is_node_state())
      continue;
    if (!ni.is_root())
      continue;
    if (!ni.has_pid_outputs())
      continue;

    f1(ni.get_nid(), ni.get_out_pid());
  }

}

void LGraph::each_output_edge_fast(std::function<void(Index_ID, Port_ID, Index_ID, Port_ID)> f1) const {

  for(const auto &ni: node_internal) {
    if (!ni.is_node_state())
      continue;
    if (!ni.is_root())
      continue;
    if (!ni.has_local_outputs())
      continue;

    const Edge *edge = ni.get_output_begin();
    do{
      f1(ni.get_nid(), ni.get_out_pid(), edge->get_idx(), edge->get_inp_pid());
      edge += edge->next_node_inc();
    }while(edge != ni.get_output_end());
  }

}
