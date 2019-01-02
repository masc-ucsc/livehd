//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "nodesrcloc.hpp"
#include "lgraph.hpp"
#include "nodetype.hpp"

LGraph_Node_Src_Loc::LGraph_Node_Src_Loc(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(path, name, lgid)
    , LGraph_Base(path, name, lgid)
    , src_files(path + "/lgraph_" + name + "_src_files")
    , node_src_loc(path + "/lgraph_" + name + "_src_locs") {
}

void LGraph_Node_Src_Loc::clear() {
  src_files.clear();
  node_src_loc.clear();
}

void LGraph_Node_Src_Loc::reload() {
  // Lazy src_files.reload();
  uint64_t sz = library->get_nentries(lgraph_id);
  node_src_loc.reload(sz);
}

void LGraph_Node_Src_Loc::sync() {
  src_files.sync();
  node_src_loc.sync();
}

void LGraph_Node_Src_Loc::emplace_back() {
  node_src_loc.emplace_back();
  //node_src_loc[node_src_loc.size() - 1] = 0;
}

void LGraph_Node_Src_Loc::node_loc_set(Index_ID nid, const char *file_name, uint32_t offset, uint32_t length) {
  File_Loc loc(offset, length);
  int      loc_id   = src_files.create_id(file_name, loc);
  node_src_loc[nid] = loc_id;
}

std::string_view LGraph_Node_Src_Loc::node_file_name_get(Index_ID nid) const {
  if (node_src_loc[nid])
    return src_files.get_name(node_src_loc[nid]);
  return "nofile";
}

File_Loc LGraph_Node_Src_Loc::node_file_loc_get(Index_ID nid) const {
  if (node_src_loc[nid])
    return src_files.get_field(node_src_loc[nid]);
  return File_Loc(0,0);
}


