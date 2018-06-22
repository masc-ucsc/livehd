//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "nodesrcloc.hpp"
#include "lgraph.hpp"
#include "nodetype.hpp"

LGraph_Node_Src_Loc::LGraph_Node_Src_Loc(const std::string &path, const std::string &name) noexcept
    : Lgraph_base_core(path, name)
    , LGraph_Base(path, name)
    , src_files(path, name + "_src_files")
    , node_src_loc(path + "/" + name + "_src_locs") {
}

void LGraph_Node_Src_Loc::clear() {
  src_files.clear();
  node_src_loc.clear();
}

void LGraph_Node_Src_Loc::reload() {
  src_files.reload();
  node_src_loc.reload();
}

void LGraph_Node_Src_Loc::sync() {
  src_files.sync();
  node_src_loc.sync();
}

void LGraph_Node_Src_Loc::emplace_back() {
  node_src_loc.emplace_back();
  node_src_loc[node_src_loc.size() - 1] = 0;
}

void LGraph_Node_Src_Loc::node_loc_set(Index_ID nid, const char *file_name, uint32_t offset, uint32_t length) {
  File_Loc loc(offset, length);
  int      loc_id   = src_files.create_id(file_name, loc);
  node_src_loc[nid] = loc_id;
}

const char *LGraph_Node_Src_Loc::node_file_name_get(Index_ID nid) const {
  return src_files.get_char(node_src_loc[nid]);
}

File_Loc LGraph_Node_Src_Loc::node_file_loc_get(Index_ID nid) const {
  return src_files.get_field(node_src_loc[nid]);
}
