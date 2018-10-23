
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
    if (!ni.is_master_root())
      continue;

    if (likely(ni.is_node_state())) {
      f1(ni.get_nid());
    }
  }

}

#if 0
void LGraph::each_out_edge_fast(std::function<void(const Node_Pin &pin)> f1) const {

  HERE

}
#endif

