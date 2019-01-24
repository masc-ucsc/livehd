//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "nodedelay.hpp"
#include "lgraph.hpp"

LGraph_Node_Delay::LGraph_Node_Delay(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(path, name, lgid), LGraph_Base(path, name, lgid), node_delay(path + "/lgraph_" + name + "_delay") {}

void LGraph_Node_Delay::clear() { node_delay.clear(); }

void LGraph_Node_Delay::reload() {
  uint64_t sz = library->get_nentries(lgraph_id);
  node_delay.reload(sz);
}

void LGraph_Node_Delay::sync() { node_delay.sync(); }

void LGraph_Node_Delay::emplace_back() {
  node_delay.emplace_back();
  // node_delay[node_delay.size() - 1] = Node_Delay();
}
void LGraph_Node_Delay::node_delay_set(Index_ID nid, float t) {
  assert(nid < node_delay.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());

  node_delay[nid].delay = t;
}

float LGraph_Node_Delay::node_delay_get(Index_ID nid) const {
  assert(nid < node_delay.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());

  return node_delay[nid].delay;
}

void LGraph_Node_Delay::node_delay_emplace_back() { node_delay.emplace_back(); }
