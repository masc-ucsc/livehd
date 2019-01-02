//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lgwirenames.hpp"

LGraph_WireNames::LGraph_WireNames(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(path, name, lgid)
    , LGraph_Base(path, name, lgid)
    , names(path + "/lgraph_" + name + "_wnames")
    , wires(path + "/lgraph_" + name + "_wid")
    , offsets(path + "/lgraph_" + name + "_offsets") {
}

void LGraph_WireNames::clear() {
  names.clear();
  wires.clear();
  offsets.clear();
}

void LGraph_WireNames::reload() {
  // Lazy names.reload();
  uint64_t sz = library->get_nentries(lgraph_id);
  wires.reload(sz);
  offsets.reload(sz);
}

void LGraph_WireNames::sync() {
  names.sync();
  wires.sync();
  offsets.sync();
}

void LGraph_WireNames::emplace_back() {
  wires.emplace_back();
  //wires[wires.size() - 1] = 0;

  offsets.emplace_back();
  //offsets[offsets.size() - 1] = 0;
}

/*WireName_ID LGraph_WireNames::get_wirename_id(const char *wirename) {
  //checks before creating
  return names.create_id(wirename);
}*/

std::string_view LGraph_WireNames::get_wirename(WireName_ID wid) const {
  return names.get_name(wid);
}

WireName_ID LGraph_WireNames::get_wid(Index_ID nid) const {
  assert(nid < wires.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());

  return wires[nid];
}

void LGraph_WireNames::set_node_wirename(Index_ID nid, WireName_ID wid) {
  assert(nid < wires.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());
  // assert(node_internal[nid].get_nid() == nid);

  wires[nid] = wid;
}

