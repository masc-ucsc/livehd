//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_type.hpp"

#include "annotate.hpp"
#include "graph_library.hpp"

static_assert(Last_invalid_Op < 512, "lgedge has 9 bits for type");

LGraph_Node_Type::LGraph_Node_Type(std::string_view _path, std::string_view _name, Lg_type_id _lgid) noexcept
    : LGraph_Base(_path, _name, _lgid)
    , const_map(_path, absl::StrCat("lg_", std::to_string(_lgid), "_const"))
    , subid_map(_path, absl::StrCat("lg_", std::to_string(_lgid), "_subid"))
    , lut_map(_path, absl::StrCat("lg_", std::to_string(_lgid), "_lut")) {}

void LGraph_Node_Type::clear() {
  const_map.clear();
  subid_map.clear();
  lut_map.clear();
}

void LGraph_Node_Type::set_type(Index_ID nid, Node_Type_Op op) {
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());
  I(op != Invalid_Op);

  node_internal.ref(nid)->set_type(static_cast<uint8_t>(op));
}

const Node_Type &LGraph_Node_Type::get_type(Index_ID nid) const {
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  return Node_Type::get(static_cast<Node_Type_Op>(node_internal[nid].get_type()));
}

Node_Type_Op LGraph_Node_Type::get_type_op(Index_ID nid) const {
  I(node_internal[nid].is_master_root());

  return static_cast<Node_Type_Op>(node_internal[nid].get_type());
}

bool LGraph_Node_Type::is_type_const(Index_ID nid) const {
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  return node_internal[nid].get_type() == Const_Op;
}

bool LGraph_Node_Type::is_type_loop_breaker(Index_ID nid) const {
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  auto op = node_internal[nid].get_type();

  return op > Loop_breaker_begin && op < Loop_breaker_end;
}

bool LGraph_Node_Type::is_type_sub(Index_ID nid) const {
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  return node_internal[nid].get_type() == SubGraph_Op;
}

void LGraph_Node_Type::set_type_sub(Index_ID nid, Lg_type_id subgraphid) {
  I(node_internal[nid].is_node_state());
  I(node_internal[nid].is_master_root());

  subid_map.set(Node::Compact_class(nid), subgraphid.value);

  Ann_node_tree_pos::ref(static_cast<const LGraph *>(this))->set(Node::Compact_class(nid), subid_map.size());

  node_internal.ref(nid)->set_type(SubGraph_Op);
}

Lg_type_id LGraph_Node_Type::get_type_sub(Index_ID nid) const {
  I(node_internal[nid].get_type() == SubGraph_Op);

  return subid_map.get(Node::Compact_class(nid));
}

const Sub_node &LGraph_Node_Type::get_type_sub_node(Index_ID nid) const {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid != lgid);  // No recursion
  return library->get_sub(sub_lgid);
}

const Sub_node &LGraph_Node_Type::get_type_sub_node(std::string_view sub_name) const {
  I(name != sub_name);  // No recursion
  return library->get_sub(sub_name);
}

Sub_node *LGraph_Node_Type::ref_type_sub_node(Index_ID nid) {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid != lgid);  // No recursion
  return library->ref_sub(sub_lgid);
}

Sub_node *LGraph_Node_Type::ref_type_sub_node(std::string_view sub_name) {
  I(name != sub_name);  // No recursion
  return library->ref_sub(sub_name);
}

void LGraph_Node_Type::set_type_lut(Index_ID nid, const Lconst &lutid) {
  I(node_internal[nid].get_type() == LUT_Op);

  lut_map.set(Node::Compact_class(nid), lutid.serialize());
}

Lconst LGraph_Node_Type::get_type_lut(Index_ID nid) const {
  I(node_internal[nid].get_type() == LUT_Op);

  return Lconst(lut_map.get(Node::Compact_class(nid)));
}

Lconst LGraph_Node_Type::get_type_const(Index_ID nid) const {
  I(node_internal[nid].is_master_root());

  return Lconst(const_map.get(Node::Compact_class(nid)));
}

void LGraph_Node_Type::set_type_const(Index_ID nid, const Lconst &value) {

  const_map.set(Node::Compact_class(nid), value.serialize());
  auto *ptr = node_internal.ref(nid);
  ptr->set_type(Const_Op);
  ptr->set_bits(value.get_bits());

  I(value.get_bits());
}

void LGraph_Node_Type::set_type_const(Index_ID nid, std::string_view sv) { set_type_const(nid, Lconst(sv)); }

void LGraph_Node_Type::set_type_const(Index_ID nid, uint32_t value, Bits_t bits) { set_type_const(nid, Lconst(value, bits)); }

