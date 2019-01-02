//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "nodetype.hpp"
#include "lgraphbase.hpp"

#include "lgedgeiter.hpp"

Node_Type *                        Node_Type::table[StrConst_Op + 1];
std::map<std::string, Node_Type *> Node_Type::name2node;

Node_Type::_init Node_Type::_static_initializer;

Node_Type::_init::_init() {
  // Static initialization

  Node_Type::table[Invalid_Op]          = new Node_Type_Invalid();
  Node_Type::table[Sum_Op]              = new Node_Type_Sum();
  Node_Type::table[Mult_Op]             = new Node_Type_Mult();
  Node_Type::table[Div_Op]              = new Node_Type_Div();
  Node_Type::table[Mod_Op]              = new Node_Type_Mod();
  Node_Type::table[Not_Op]              = new Node_Type_Not();
  Node_Type::table[Join_Op]             = new Node_Type_Join();
  Node_Type::table[Pick_Op]             = new Node_Type_Pick();
  Node_Type::table[And_Op]              = new Node_Type_And();
  Node_Type::table[Or_Op]               = new Node_Type_Or();
  Node_Type::table[Xor_Op]              = new Node_Type_Xor();
  Node_Type::table[SFlop_Op]            = new Node_Type_Flop();
  Node_Type::table[AFlop_Op]            = new Node_Type_AFlop();
  Node_Type::table[Latch_Op]            = new Node_Type_Latch();
  Node_Type::table[FFlop_Op]            = new Node_Type_FFlop();
  Node_Type::table[Memory_Op]           = new Node_Type_Memory();
  Node_Type::table[LessThan_Op]         = new Node_Type_LessThan();
  Node_Type::table[GreaterThan_Op]      = new Node_Type_GreaterThan();
  Node_Type::table[LessEqualThan_Op]    = new Node_Type_LessEqualThan();
  Node_Type::table[GreaterEqualThan_Op] = new Node_Type_GreaterEqualThan();
  Node_Type::table[Equals_Op]           = new Node_Type_Equals();
  Node_Type::table[Mux_Op]              = new Node_Type_Mux();
  Node_Type::table[ShiftRight_Op]       = new Node_Type_ShiftRight();
  Node_Type::table[ShiftLeft_Op]        = new Node_Type_ShiftLeft();
  Node_Type::table[GraphIO_Op]          = new Node_Type_GraphIO();
  Node_Type::table[SubGraph_Op]         = new Node_Type_SubGraph();
  // IDs between SubGraph_Op and SubGraphMax_Op are allowed, but they just mean a different type of subgraph
  Node_Type::table[TechMap_Op]  = new Node_Type_TechMap();
  Node_Type::table[BlackBox_Op] = new Node_Type_BlackBox();

  Node_Type::table[U32Const_Op]        = new Node_Type_U32Const();
  Node_Type::table[StrConst_Op]        = new Node_Type_StrConst();
  Node_Type::table[CfgAssign_Op]       = new Node_Type_CfgAssign();
  Node_Type::table[CfgIf_Op]           = new Node_Type_CfgIf();
  Node_Type::table[CfgFunctionCall_Op] = new Node_Type_CfgFunctionCall();
  Node_Type::table[CfgFor_Op]          = new Node_Type_CfgFor();
  Node_Type::table[CfgWhile_Op]        = new Node_Type_CfgWhile();
  Node_Type::table[CfgIfMerge_Op]      = new Node_Type_CfgIfMerge();
  Node_Type::table[CfgBeenRead_Op]     = new Node_Type_CfgBeenRead();
  Node_Type::table[DontCare_Op]        = new Node_Type_DontCare();
  Node_Type::table[DfgRef_Op]          = new Node_Type_DfgRef();
  Node_Type::table[DfgPendingGraph_Op] = new Node_Type_DfgPendingGraph();

  assert(Invalid_Op == 0);
  for(size_t i = Invalid_Op; i <= SubGraph_Op; i++) {
    assert(table[i]);
    name2node[table[i]->get_name()] = table[i];
  }
}

Node_Type &Node_Type::get(Node_Type_Op op) {
  if(op >= SubGraphMin_Op && op <= SubGraphMax_Op)
    op = SubGraph_Op;

  if(op >= U32ConstMin_Op && op <= U32ConstMax_Op)
    op = U32Const_Op;

  if(op >= StrConstMin_Op && op <= StrConstMax_Op)
    op = StrConst_Op;

  assert(table[op] != nullptr);
  return *table[op];
}

Node_Type_Op Node_Type::get(const std::string &opname) {
  assert(is_type(opname));
  return name2node[opname]->op;
}

bool Node_Type::is_type(const std::string &opname) {
  return (name2node.find(opname) != name2node.end());
}

LGraph_Node_Type::LGraph_Node_Type(const std::string &path, const std::string &name, Lg_type_id lgid) noexcept
    : Lgraph_base_core(path, name, lgid)
    , consts(path + "/lgraph_" + name + "_consts")
    , node_type_table(path + "/lgraph_" + name + "_type") {
}

std::string_view LGraph_Node_Type::get_constant(Const_ID const_id) const {
  return consts.get_name(const_id);
}

void LGraph_Node_Type::emplace_back() {
  node_type_table.emplace_back(Invalid_Op);
}

void LGraph_Node_Type::clear() {
  node_type_table.clear();
  consts.clear();
}

void LGraph_Node_Type::reload(size_t sz) {
  node_type_table.reload(sz);

  // Note: if you change this, make sure to change u32_type_set and
  // const_type_set functions accordingly
  for(const Index_ID &node : Lgraph_base_core::fast()) {
    if(node_type_get(node).op == U32Const_Op || node_type_get(node).op == StrConst_Op) {
      const_nodes.set_bit(node);
    }else if(node_type_get(node).op == SubGraph_Op) {
      sub_graph_nodes.set_bit(node);
    }
  }
}

void LGraph_Node_Type::sync() {
  node_type_table.sync();
  consts.sync();

  // FIXME: const_nodes and sub_graph_nodes SERIALIZATION???
}

void LGraph_Node_Type::node_type_set(Index_ID nid, Node_Type_Op op) {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());
  assert(op != Invalid_Op);

  assert(node_internal[nid].get_nid() < node_type_table.size());

  node_type_table[node_internal[nid].get_nid()] = op;
}

void LGraph_Node_Type::node_subgraph_set(Index_ID nid, uint32_t subgraphid) {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());
  assert(subgraphid <= (uint32_t)(SubGraphMax_Op - SubGraphMin_Op));

  sub_graph_nodes.set_bit(node_internal[nid].get_nid());

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(SubGraphMin_Op + subgraphid);
}

uint32_t LGraph_Node_Type::subgraph_id_get(Index_ID nid) const {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  assert(node_type_table[node_internal[nid].get_nid()] >= SubGraphMin_Op);
  assert(node_type_table[node_internal[nid].get_nid()] <= SubGraphMax_Op);

  return (uint32_t)(node_type_table[node_internal[nid].get_nid()] - SubGraphMin_Op);
}

void LGraph_Node_Type::node_tmap_set(Index_ID nid, uint32_t tmapid) {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());
  assert(tmapid <= (uint32_t)(TechMapMax_Op - TechMapMin_Op));

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(TechMapMin_Op + tmapid);
}

uint32_t LGraph_Node_Type::tmap_id_get(Index_ID nid) const {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  assert(node_type_table[node_internal[nid].get_nid()] >= TechMapMin_Op);
  assert(node_type_table[node_internal[nid].get_nid()] <= TechMapMax_Op);

  return (uint32_t)(node_type_table[node_internal[nid].get_nid()] - TechMapMin_Op);
}

void LGraph_Node_Type::node_u32type_set(Index_ID nid, uint32_t value) {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());
  assert(value <= (uint32_t)(U32ConstMax_Op - U32ConstMin_Op));

  // when a node is set as const, adds it to the const nodes list
  // Note: if the lazy initialization is changed to something that is
  // destructive, this needs to be changed
  const_nodes.set_bit(nid);

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(U32ConstMin_Op + value);
}

uint32_t LGraph_Node_Type::node_value_get(Index_ID nid) const {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  assert(node_type_table[node_internal[nid].get_nid()] >= U32ConstMin_Op);
  assert(node_type_table[node_internal[nid].get_nid()] <= U32ConstMax_Op);

  return (uint32_t)(node_type_table[node_internal[nid].get_nid()] - U32ConstMin_Op);
}

void LGraph_Node_Type::node_const_type_set(Index_ID nid, std::string_view value
                                           ,bool enforce_bits
) {

#ifndef NDEBUG
  if(enforce_bits)
    for(auto &digit : value) {
      if(digit != '0' && digit != '1' && digit != 'z' && digit != 'x')
        assert(false); // unrecognized bit value
    }
#endif

  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());

  uint32_t char_id = consts.create_id(value);
  assert(char_id < (uint32_t)(StrConstMax_Op - StrConstMin_Op));

  // when a node is set as const, adds it to the const nodes list
  // Note: if the lazy initialization is changed to something that is
  // destructive, this needs to be changed
  const_nodes.set_bit(nid);

  node_type_table[node_internal[nid].get_nid()] = (Node_Type_Op)(StrConstMin_Op + char_id);
}

std::string_view LGraph_Node_Type::node_const_value_get(Index_ID nid) const {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());

  assert(node_internal[nid].get_nid() < node_type_table.size());

  // only supported for constants
  assert(node_type_table[node_internal[nid].get_nid()] >= StrConstMin_Op);
  assert(node_type_table[node_internal[nid].get_nid()] <= StrConstMax_Op);

  return get_constant(node_type_table[node_internal[nid].get_nid()] - StrConstMin_Op);
}

const Node_Type &LGraph_Node_Type::node_type_get(Index_ID nid) const {
  assert(nid < node_type_table.size());
  assert(node_internal[nid].is_node_state());
  assert(node_internal[nid].is_root());
  assert(node_internal[nid].get_nid() < node_type_table.size());

  Node_Type_Op op = node_type_table[node_internal[nid].get_nid()];

  if(op >= SubGraphMin_Op && op <= SubGraphMax_Op)
    op = SubGraph_Op;

  if(op >= TechMapMin_Op && op <= TechMapMax_Op)
    op = TechMap_Op;

  if(op >= U32ConstMin_Op && op <= U32ConstMax_Op)
    op = U32Const_Op;

  if(op >= StrConstMin_Op && op <= StrConstMax_Op)
    op = StrConst_Op;

  return Node_Type::get(op);
}
