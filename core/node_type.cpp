//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_type.hpp"
#include "graph_library.hpp"
#include "pass.hpp"

#include "bmsparsevec_serial.h"

LGraph_Node_Type::LGraph_Node_Type(std::string_view path, std::string_view name, Lg_type_id lgid) noexcept
    : LGraph_Base(path, name, lgid)
    , consts(absl::StrCat(path, "/lgraph_", std::to_string(lgid), "_consts"))
    , node_type_table(absl::StrCat(path, "/lgraph_", std::to_string(lgid), "_type")) {}

void LGraph_Node_Type::clear() {
  node_type_table.clear();
  consts.clear();
}

static_assert(StrConstMin_Op == U32ConstMax_Op+1); // Check opt in reload

void LGraph_Node_Type::reload() {

  uint64_t sz = library->get_nentries(get_lgid());
  node_type_table.reload(sz);

  BM_DECLARE_TEMP_BLOCK(tb);

  auto const_file = absl::StrCat(path, "lgraph_", std::to_string(lgid), "_const_nodes");
  if (access(const_file.c_str(), F_OK) == 0) {
    int fd = open(const_file.c_str(), O_RDONLY);
    if (fd < 0) {
      Pass::error("reload could not open const_file {}", const_file);
      return;
    }
    struct stat sb;
    fstat(fd, &sb);
    unsigned char *memblock = (unsigned char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (memblock == MAP_FAILED) {
      Pass::error("reload mmap failed");
      return;
    }

    int res = bm::sparse_vector_deserialize(const_nodes, memblock, tb);

    munmap(memblock, sb.st_size);
    close(fd);

    if (res != 0) {
      Pass::error("reload unable de deserialize {}",const_file);
      return;
    }
  }

  auto sub_file = absl::StrCat(path, "lgraph_", std::to_string(lgid), "_sub_nodes");
  if (access(sub_file.c_str(), F_OK) == 0) {
    int fd = open(sub_file.c_str(), O_RDONLY);
    if (fd < 0) {
      Pass::error("reload could not open sub_file {}", sub_file);
      return;
    }
    struct stat sb;
    fstat(fd, &sb);
    unsigned char *memblock = (unsigned char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (memblock == MAP_FAILED) {
      Pass::error("reload mmap failed");
      return;
    }

    int res = bm::sparse_vector_deserialize(sub_nodes, memblock, tb);

    munmap(memblock, sb.st_size);
    close(fd);

    if (res != 0) {
      Pass::error("reload unable de deserialize {}",sub_file);
      return;
    }
  }

#if 0
  // Note: if you change this, make sure to change u32_type_set and
  // const_type_set functions accordingly
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;

    Index_ID nid = ni.get_nid();

    auto raw_op = node_type_table[nid];
    if (raw_op >= SubGraphMin_Op && raw_op <= SubGraphMax_Op) {
      sub_nodes.set_bit_no_check(nid);
    }else if (raw_op >= U32ConstMin_Op && raw_op <= StrConstMax_Op) {
      const_nodes.set_bit_no_check(nid);
    }else{
      I(get_type(nid).op != U32Const_Op);
      I(get_type(nid).op != StrConst_Op);
      I(get_type(nid).op != SubGraph_Op);
    }
  }
#endif
}

void LGraph_Node_Type::sync() {
  node_type_table.sync();
  consts.sync();

  BM_DECLARE_TEMP_BLOCK(tb);

  if (const_nodes.first() != const_nodes.end()) {

    const_nodes.optimize(tb);

    bm::sparse_vector_serial_layout<bmsparse> const_lay;
    bm::sparse_vector_serialize(const_nodes, const_lay, tb);

    auto const_file = absl::StrCat(path, "lgraph_", std::to_string(lgid), "_const_nodes");
    int fd = open(const_file.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd<0) {
      Pass::error("lgraph const_file unable to open {}", const_file);
      return;
    }
    int ret = write(fd, const_lay.buf(), const_lay.size());
    if (ret != (int)const_lay.size()) {
      Pass::error("lgraph const_file {} unable to serialize", const_file);
      return;
    }
    close(fd);
  }

  if (!sub_nodes.empty()) {

    sub_nodes.optimize(tb);

    bm::sparse_vector_serial_layout<bmsparse> sub_lay;
    bm::sparse_vector_serialize(sub_nodes, sub_lay, tb);

    auto sub_file = absl::StrCat(path, "lgraph_", std::to_string(lgid), "_sub_nodes");
    int fd = open(sub_file.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd<0) {
      Pass::error("lgraph sub_file unable to open {}", sub_file);
      return;
    }
    int ret = write(fd, sub_lay.buf(), sub_lay.size());
    if (ret != (int)sub_lay.size()) {
      Pass::error("lgraph sub_file {} unable to serialize", sub_file);
      return;
    }
    close(fd);
  }
}

void LGraph_Node_Type::emplace_back() { node_type_table.emplace_back(Invalid_Op); }

void LGraph_Node_Type::set_type(Index_ID nid, Node_Type_Op op) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());
  I(op != Invalid_Op);

  I(node_internal[nid].get_nid() < node_type_table.size());

  I(node_type_table[node_internal[nid].get_nid()] == Invalid_Op || node_type_table[node_internal[nid].get_nid()] == op);

  node_type_table[node_internal[nid].get_nid()] = op;
}

const Node_Type &LGraph_Node_Type::get_type(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  Node_Type_Op op = node_type_table[node_internal[nid].get_nid()];

  if (op >= U32ConstMin_Op && op <= U32ConstMax_Op)      op = U32Const_Op;
  else if (op >= StrConstMin_Op && op <= StrConstMax_Op) op = StrConst_Op;
  else if (op >= LUTMin_Op      && op <= LUTMax_Op)      op = LUT_Op;

  return Node_Type::get(op);
}

void LGraph_Node_Type::set_type_sub(Index_ID nid, Lg_type_id subgraphid) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());
  I(subgraphid.value <= (uint32_t)(SubGraphMax_Op - SubGraphMin_Op));
  I(node_internal[nid].is_master_root());
  //auto nid = node_internal[nid].get_nid();

  I((node_type_table[nid] >=SubGraphMin_Op && node_type_table[nid] <SubGraphMax_Op)
  || node_type_table[nid] == Invalid_Op);

  sub_nodes.set(nid,true);

  node_type_table[nid] = (Node_Type_Op)(SubGraphMin_Op + subgraphid);
}

Lg_type_id LGraph_Node_Type::get_type_sub(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  I(node_type_table[node_internal[nid].get_nid()] >= SubGraphMin_Op);
  I(node_type_table[node_internal[nid].get_nid()] <= SubGraphMax_Op);

  return Lg_type_id((uint32_t)(node_type_table[node_internal[nid].get_nid()] - SubGraphMin_Op));
}

void LGraph_Node_Type::set_type_lut(Index_ID nid, Lut_type_id lutid) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());
  I(lutid.value <= (uint32_t)(LUTMax_Op - LUTMin_Op));

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(LUTMin_Op + lutid);
}

Lut_type_id LGraph_Node_Type::get_type_lut(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  I(node_type_table[node_internal[nid].get_nid()] >= LUTMin_Op);
  I(node_type_table[node_internal[nid].get_nid()] <= LUTMax_Op);

  return Lut_type_id((uint32_t)(node_type_table[node_internal[nid].get_nid()] - LUTMin_Op));
}

const Sub_node &LGraph_Node_Type::get_type_sub_node(Index_ID nid) const {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid!=lgid); // No recursion
  return library->get_sub(sub_lgid);
}

const Sub_node &LGraph_Node_Type::get_type_sub_node(std::string_view sub_name) const {
  I(name!=sub_name); // No recursion
  return library->get_sub(sub_name);
}

Sub_node &LGraph_Node_Type::get_type_sub_node(Index_ID nid) {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid!=lgid); // No recursion
  return library->get_sub(sub_lgid);
}

Sub_node &LGraph_Node_Type::get_type_sub_node(std::string_view sub_name) {
  I(name!=sub_name); // No recursion
  return library->get_sub(sub_name);
}

void LGraph_Node_Type::set_type_const_value(Index_ID nid, std::string_view value) {

  for (auto &digit : value) {
    I(digit == '0' || digit == '1' || digit == 'z' || digit == 'x');
  }

  set_type_const_sview(nid,value);
}

std::string_view LGraph_Node_Type::get_type_const_sview(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  I(node_type_table[node_internal[nid].get_nid()] >= StrConstMin_Op);
  I(node_type_table[node_internal[nid].get_nid()] <= StrConstMax_Op);

  return get_constant(node_type_table[node_internal[nid].get_nid()] - StrConstMin_Op);
}

void LGraph_Node_Type::set_type_const_value(Index_ID nid, uint32_t value) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());
  I(value <= (uint32_t)(U32ConstMax_Op - U32ConstMin_Op));

  // when a node is set as const, adds it to the const nodes list
  // Note: if the lazy initialization is changed to something that is
  // destructive, this needs to be changed
  const_nodes.set(nid, true);

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(U32ConstMin_Op + value);
}

Index_ID LGraph_Node_Type::find_type_const_sview(std::string_view value) const {

  auto id = consts.get_id(value);
  if (id==0)
    return 0;

#if 0
  bool all_one_zero = true;
  for(auto &c:value) {
    if (c=='0' && c=='1')
      continue;

    all_one_zero = false;
    break;
  }
#endif

  auto op = static_cast<Node_Type_Op>(StrConstMin_Op + id);

  for(auto it = const_nodes.begin() ; it != const_nodes.end() ; ++it) {
    Index_ID    cid = *it;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    if (op == node_type_table[cid])
      return cid;
  }

  return 0;
}

Index_ID LGraph_Node_Type::find_type_const_value(uint32_t value) const {

  // FIXME: This should be fast, but in a bad case we can have LOTS of
  // constants in a graph. Then, it is pretty bad. This should be weird but
  // possible.

  auto op = static_cast<Node_Type_Op>(U32ConstMin_Op + value);

  for(auto it = const_nodes.begin() ; it != const_nodes.end() ; ++it) {
    Index_ID cid = *it;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    if (op == node_type_table[cid])
      return cid;
  }

  return 0;
}

void LGraph_Node_Type::set_type_const_sview(Index_ID nid, std::string_view value) {

  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());

  uint32_t char_id = consts.create_id(value);
  I(char_id < (uint32_t)(StrConstMax_Op - StrConstMin_Op));

  // when a node is set as const, adds it to the const nodes list
  // Note: if the lazy initialization is changed to something that is
  // destructive, this needs to be changed
  const_nodes.set(nid, true);

  node_type_table[node_internal[nid].get_nid()] = static_cast<Node_Type_Op>(StrConstMin_Op + char_id);
}

uint32_t LGraph_Node_Type::get_type_const_value(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  I(node_type_table[node_internal[nid].get_nid()] >= U32ConstMin_Op);
  I(node_type_table[node_internal[nid].get_nid()] <= U32ConstMax_Op);

  return (uint32_t)(node_type_table[node_internal[nid].get_nid()] - U32ConstMin_Op);
}

std::string_view LGraph_Node_Type::get_constant(Const_ID const_id) const { return consts.get_name(const_id); }
