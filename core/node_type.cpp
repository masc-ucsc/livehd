//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "node_type.hpp"

#include "annotate.hpp"
#include "graph_library.hpp"
#include "node.hpp"

static_assert(static_cast<int>(Ntype_op::Last_invalid) < 127, "lgedge has 8 bits for type");

Lgraph_Node_Type::Lgraph_Node_Type(const mmap_lib::str &_path, const mmap_lib::str &_name, Lg_type_id _lgid, Graph_library *_lib) noexcept
    : Lgraph_Base(_path, _name, _lgid, _lib)
    , const_map(_path.to_s(), absl::StrCat("lg_", std::to_string(_lgid), "_const"))
    , subid_map(_path.to_s(), absl::StrCat("lg_", std::to_string(_lgid), "_subid"))
    , down_class_map(_path.to_s(), absl::StrCat("lg_", std::to_string(_lgid), "_downc"))
    , lut_map(_path.to_s(), absl::StrCat("lg_", std::to_string(_lgid), "_lut")) {

}

void Lgraph_Node_Type::clear() {
  const_map.clear();
  subid_map.clear();
  down_class_map.clear();
  lut_map.clear();
}

void Lgraph_Node_Type::set_type(Index_id nid, const Ntype_op op) {
  node_internal.ref_lock();

  I(node_internal.ref(nid)->is_master_root());

  auto type = node_internal.ref(nid)->get_type();
  if (type == Ntype_op::Sub) {
    auto it = subid_map.find(Node::Compact_class(nid));
    I(it != subid_map.end());
    auto it2 = down_class_map.find(it->second);
    I(it2 != down_class_map.end());
    it2->second--;
    if (it2->second==0) {
      down_class_map.erase(it2);
    }
    subid_map.erase(it);
  }else if (type == Ntype_op::LUT)
    lut_map.erase(Node::Compact_class(nid));

  node_internal.ref(nid)->set_type(op);
  node_internal.ref_unlock();
}

bool Lgraph_Node_Type::is_type_const(Index_id nid) const {
  node_internal.ref_lock();
  bool b = node_internal.ref(nid)->get_type() == Ntype_op::Const;
  node_internal.ref_unlock();

  return b;
}

void Lgraph_Node_Type::set_type_sub(Index_id nid, Lg_type_id subgraphid) {

  subid_map.set(Node::Compact_class(nid), subgraphid.value);
  if (down_class_map.has(subgraphid) ) {
    auto i = down_class_map.get(subgraphid);
    down_class_map.set(subgraphid, i+1);
  }else{
    down_class_map.set(subgraphid, 1);
  }

  node_internal.ref_lock();
  node_internal.ref(nid)->set_type(Ntype_op::Sub);
  node_internal.ref_unlock();
}

Lg_type_id Lgraph_Node_Type::get_type_sub(Index_id nid) const {
  I(node_internal[nid].get_type() == Ntype_op::Sub);

  return subid_map.get(Node::Compact_class(nid));
}

std::tuple<Lg_type_id, Index_id> Lgraph_Node_Type::go_next_down(Index_id nid) const {

  Index_id   n_nid=0;
  Lg_type_id n_lgid=0;

  subid_map.ref_lock();
  auto it = subid_map.find(nid);
  if (it != subid_map.end()) {
    ++it;
    if (it != subid_map.end()) {
      n_nid  = it->first.nid;
      n_lgid = it->second;
    }
  }

  subid_map.ref_unlock();

  return std::make_tuple(n_lgid, n_nid);
}

const Sub_node &Lgraph_Node_Type::get_type_sub_node(Index_id nid) const {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid != lgid);  // No recursion
  return library->get_sub(sub_lgid);
}

const Sub_node &Lgraph_Node_Type::get_type_sub_node(const mmap_lib::str &sub_name) const {
  I(name != sub_name);  // No recursion
  return library->get_sub(sub_name);
}

Sub_node *Lgraph_Node_Type::ref_type_sub_node(Index_id nid) {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid != lgid);  // No recursion
  return library->ref_sub(sub_lgid);
}

Sub_node *Lgraph_Node_Type::ref_type_sub_node(const mmap_lib::str &sub_name) {
  I(name != sub_name);  // No recursion
  return library->ref_sub(sub_name);
}

void Lgraph_Node_Type::set_type_lut(Index_id nid, const Lconst &lutid) {
  node_internal.ref_lock();
  node_internal.ref(nid)->set_type(Ntype_op::LUT);
  node_internal.ref_unlock();

  lut_map.set(Node::Compact_class(nid), lutid.serialize());
}

Lconst Lgraph_Node_Type::get_type_lut(Index_id nid) const {
  I(node_internal[nid].get_type() == Ntype_op::LUT);

  return Lconst::unserialize(lut_map.get(Node::Compact_class(nid)));
}

Lconst Lgraph_Node_Type::get_type_const(Index_id nid) const {

  return Lconst::unserialize(const_map.get(Node::Compact_class(nid)));
}

void Lgraph_Node_Type::set_type_const(Index_id nid, const Lconst &value) {
  const_map.set(Node::Compact_class(nid), value.serialize());

  node_internal.ref_lock();
  auto *ptr = node_internal.ref(nid);
  ptr->set_type(Ntype_op::Const);
  ptr->set_bits(value.get_bits());
  node_internal.ref_unlock();
}

void Lgraph_Node_Type::set_type_const(Index_id nid, const mmap_lib::str &sv) { set_type_const(nid, Lconst::from_pyrope(sv)); }

void Lgraph_Node_Type::set_type_const(Index_id nid, int64_t value) { set_type_const(nid, Lconst(value)); }
