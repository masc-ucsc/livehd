//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "instance_names.hpp"

LGraph_InstanceNames::LGraph_InstanceNames(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(path, name, lgid)
    , inames(path + "/lgraph_" +  name + "_inst_names")
    , instances(path + "/lgraph_" + name + "_inst") {
}

void LGraph_InstanceNames::clear() {
  inames.clear();
  instances.clear();
}

void LGraph_InstanceNames::reload(size_t sz) {
  // Lazy inames.reload();
  instances.reload(sz);
}

void LGraph_InstanceNames::sync() {
  inames.sync();
  instances.sync();
}

void LGraph_InstanceNames::emplace_back() {
  instances.emplace_back();
}

std::string_view LGraph_InstanceNames::get_instancename(Char_Array_ID cid) const {
  if(cid == 0) {
    static const std::string_view empty = "";
    return empty;
  }
  return inames.get_name(cid);
}

Char_Array_ID LGraph_InstanceNames::get_instance_name_id(Index_ID nid) const {
  assert(nid < instances.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());
  assert(node_internal[nid].get_nid() == nid);

  return instances[nid];
}

void LGraph_InstanceNames::set_node_instance(Index_ID nid, Char_Array_ID wid) {
  assert(nid < instances.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());
  assert(node_internal[nid].get_nid() == nid);

  instances[nid] = wid;
}

bool LGraph_InstanceNames::has_instance_name(std::string_view name) const {
  return inames.include(name);
}

Index_ID LGraph_InstanceNames::get_node_from_instance_name(std::string_view name) const {
  return inames.get_field(inames.get_id(name));
}
