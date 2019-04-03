//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_type.hpp"
#include "graph_library.hpp"

LGraph_Node_Type::LGraph_Node_Type(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : LGraph_Base(path, name, lgid)
    , consts(path + "/lgraph_" + std::to_string(lgid) + "_consts")
    , node_type_table(path + "/lgraph_" + std::to_string(lgid) + "_type") {}

void LGraph_Node_Type::clear() {
  node_type_table.clear();
  consts.clear();
}

void LGraph_Node_Type::reload() {

  uint64_t sz = library->get_nentries(lgraph_id);
  node_type_table.reload(sz);

  // Note: if you change this, make sure to change u32_type_set and
  // const_type_set functions accordingly
  for (const auto &ni : node_internal) {
    if (!ni.is_node_state()) continue;
    if (!ni.is_root()) continue;

    Index_ID nid = ni.get_nid();

    if (get_type(nid).op == U32Const_Op || get_type(nid).op == StrConst_Op) {
      const_nodes.set_bit(nid);
    } else if (get_type(nid).op == SubGraph_Op) {
      sub_graph_nodes.set_bit(nid);
    }
  }
}

void LGraph_Node_Type::sync() {
  node_type_table.sync();
  consts.sync();

  // FIXME: const_nodes and sub_graph_nodes SERIALIZATION???
}

void LGraph_Node_Type::emplace_back() { node_type_table.emplace_back(Invalid_Op); }

void LGraph_Node_Type::set_type(Index_ID nid, Node_Type_Op op) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());
  I(op != Invalid_Op);

  I(node_internal[nid].get_nid() < node_type_table.size());

  node_type_table[node_internal[nid].get_nid()] = op;
}

const Node_Type &LGraph_Node_Type::get_type(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  Node_Type_Op op = node_type_table[node_internal[nid].get_nid()];

  if (op >= SubGraphMin_Op && op <= SubGraphMax_Op) op = SubGraph_Op;
  if (op >= TechMapMin_Op  && op <= TechMapMax_Op ) op = TechMap_Op;
  if (op >= U32ConstMin_Op && op <= U32ConstMax_Op) op = U32Const_Op;
  if (op >= StrConstMin_Op && op <= StrConstMax_Op) op = StrConst_Op;

  return Node_Type::get(op);
}

void LGraph_Node_Type::set_type_subgraph(Index_ID nid, Lg_type_id subgraphid) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());
  I(subgraphid.value <= (uint32_t)(SubGraphMax_Op - SubGraphMin_Op));

  sub_graph_nodes.set_bit(node_internal[nid].get_nid());

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(SubGraphMin_Op + subgraphid);
}

Lg_type_id LGraph_Node_Type::get_type_subgraph(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  I(node_type_table[node_internal[nid].get_nid()] >= SubGraphMin_Op);
  I(node_type_table[node_internal[nid].get_nid()] <= SubGraphMax_Op);

  return Lg_type_id((uint32_t)(node_type_table[node_internal[nid].get_nid()] - SubGraphMin_Op));
}

void LGraph_Node_Type::set_type_tmap_id(Index_ID nid, uint32_t tmapid) {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());
  I(tmapid <= (uint32_t)(TechMapMax_Op - TechMapMin_Op));

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(TechMapMin_Op + tmapid);
}

uint32_t LGraph_Node_Type::get_type_tmap_id(Index_ID nid) const {
  I(nid < node_type_table.size());
  I(node_internal[nid].is_node_state());

  I(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  I(node_type_table[node_internal[nid].get_nid()] >= TechMapMin_Op);
  I(node_type_table[node_internal[nid].get_nid()] <= TechMapMax_Op);

  return (uint32_t)(node_type_table[node_internal[nid].get_nid()] - TechMapMin_Op);
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
  const_nodes.set_bit(nid);

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

  const bm::bvector<> &bm  = const_nodes;
  Index_ID             cid = bm.get_first();
  while (cid) {
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    if (op == node_type_table[cid])
      return cid;

    cid = bm.get_next(cid);
  }

  return 0;
}

Index_ID LGraph_Node_Type::find_type_const_value(uint32_t value) const {

  // FIXME: This should be fast, but in a bad case we can have LOTS of
  // constants in a graph. Then, it is pretty bad. This should be weird but
  // possible.

  auto op = static_cast<Node_Type_Op>(U32ConstMin_Op + value);

  const bm::bvector<> &bm  = const_nodes;
  Index_ID             cid = bm.get_first();
  while (cid) {
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    if (op == node_type_table[cid])
      return cid;

    cid = bm.get_next(cid);
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
  const_nodes.set_bit(nid);

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

