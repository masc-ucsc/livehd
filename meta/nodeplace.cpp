//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "nodeplace.hpp"
#include "lgraph.hpp"

LGraph_Node_Place::LGraph_Node_Place(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(path, name, lgid), LGraph_Base(path, name, lgid), node_place(path + "/lgraph_" + name + "_place") {}

void LGraph_Node_Place::clear() { node_place.clear(); }

void LGraph_Node_Place::reload() {
  uint64_t sz = library->get_nentries(lgraph_id);
  node_place.reload(sz);
}

void LGraph_Node_Place::sync() { node_place.sync(); }

void LGraph_Node_Place::emplace_back() {
  node_place.emplace_back();
  // node_place[node_place.size() - 1] = Node_Place();
}

void LGraph_Node_Place::node_place_emplace_back() { node_place.emplace_back(); }
