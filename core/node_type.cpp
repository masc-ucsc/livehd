//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "annotate.hpp"
#include "node_type.hpp"
#include "graph_library.hpp"
#include "pass.hpp"

LGraph_Node_Type::LGraph_Node_Type(std::string_view path, std::string_view name, Lg_type_id lgid) noexcept
    : LGraph_Base(path, name, lgid)
    , node_type_table(path, absl::StrCat("lg_", std::to_string(lgid), "_type"))
    , const_sview(path, absl::StrCat("lg_", std::to_string(lgid), "_sview"))
    , const_value(path, absl::StrCat("lg_", std::to_string(lgid), "_value"))
    , down_nodes(path, absl::StrCat("lg_", std::to_string(lgid)  , "_down")) {
}

void LGraph_Node_Type::clear() {

  const_sview.clear();
  const_value.clear();
  down_nodes.clear();

  node_type_table.clear();
}

static_assert(StrConstMin_Op == U32ConstMax_Op+1); // Check opt in reload

void LGraph_Node_Type::emplace_back() { node_type_table.emplace_back(Invalid_Op); }

void LGraph_Node_Type::set_type(Index_ID nid, Node_Type_Op op) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());
  I(op != Invalid_Op);

  I(node_internal[nid].get_nid() < node_type_table.size());

  I(node_type_table[node_internal[nid].get_nid()] == Invalid_Op || node_type_table[node_internal[nid].get_nid()] == op);

  node_type_table.set(node_internal[nid].get_nid(), op);
}

const Node_Type &LGraph_Node_Type::get_type(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  Node_Type_Op op = node_type_table[nid];

  if (op >= U32ConstMin_Op && op <= U32ConstMax_Op)      op = U32Const_Op;
  else if (op >= StrConstMin_Op && op <= StrConstMax_Op) op = StrConst_Op;
  else if (op >= LUTMin_Op      && op <= LUTMax_Op)      op = LUT_Op;

  return Node_Type::get(op);
}

bool LGraph_Node_Type::is_type_const(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  const Node_Type_Op &op = node_type_table[nid];

  return (op >= U32ConstMin_Op && op <= U32ConstMax_Op) || (op >=
      StrConstMin_Op && op <= StrConstMax_Op);
}

bool LGraph_Node_Type::is_type_sub(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  const Node_Type_Op &op = node_type_table[nid];

  return (op >= SubGraphMin_Op && op <= SubGraphMax_Op);
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

  down_nodes.set(Node::Compact_class(nid), subgraphid.value);
  Ann_node_tree_pos::ref(static_cast<const LGraph *>(this))->set(Node::Compact_class(nid), down_nodes.size());

  node_type_table.set(nid, (Node_Type_Op)(SubGraphMin_Op + subgraphid));
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

  node_type_table.set(node_internal[nid].get_nid(), (Node_Type_Op)(LUTMin_Op + lutid));
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

  set_type_const_sview(nid, value);
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

  const_value.set(value, Node::Compact_class(nid));

  node_type_table.set(node_internal[nid].get_nid(), (Node_Type_Op)(U32ConstMin_Op + value));
  int bits = (64 - __builtin_clzll(value));
  I(bits<=32);

  set_bits(nid, bits);
}

Index_ID LGraph_Node_Type::find_type_const_sview(std::string_view value) const {

  auto it = const_sview.find(value);
  if (it == const_sview.end())
    return 0;

  return const_sview.get(it).nid;
}

Index_ID LGraph_Node_Type::find_type_const_value(uint32_t value) const {

  auto it = const_value.find(value);
  if (it == const_value.end())
    return 0;

  return const_value.get(it).nid;
}

void LGraph_Node_Type::set_type_const_sview(Index_ID nid, std::string_view value) {

  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].get_nid() < node_type_table.size());

  const auto it = const_sview.find(value);
  uint32_t char_id;
  if (it==const_sview.end()) {
    auto it2 = const_sview.set(value, Node::Compact_class(nid));
    char_id = it2->first;
  }else{
    char_id = it->first;
  }
  I(char_id < (uint32_t)(StrConstMax_Op - StrConstMin_Op));

  node_type_table.set(node_internal[nid].get_nid(), static_cast<Node_Type_Op>(StrConstMin_Op + char_id));
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

std::string_view LGraph_Node_Type::get_constant(uint32_t const_id) const {
  return const_sview.get_sview(const_id);
}

