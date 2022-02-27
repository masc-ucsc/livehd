//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgraph_attributes.hpp"

#include "graph_library.hpp"
#include "node.hpp"

static_assert(static_cast<int>(Ntype_op::Last_invalid) < 127, "lgedge has 8 bits for type");

Lgraph_attributes::Lgraph_attributes(std::string_view _path, std::string_view _name, Lg_type_id _lgid, Graph_library *_lib) noexcept
    : Lgraph_Base(_path, _name, _lgid, _lib) {}

void Lgraph_attributes::clear() {
  const_map.clear();
  subid_map.clear();
  down_class_map.clear();

  lut_map.clear();

  node_pin_offset_map.clear();
  node_pin_name_map.clear();
  node_pin_name_rmap.clear();
  node_pin_delay_map.clear();
  node_pin_unsigned_map.clear();

  node_name_map.clear();
  node_color_map.clear();
  node_place_map.clear();

  Lgraph_Base::clear();  // last. Removes lock at the end
}

void Lgraph_attributes::set_type(Index_id nid, const Ntype_op op) {
  I(node_internal[nid].is_master_root());

  auto type = node_internal[nid].get_type();
  if (type == Ntype_op::Sub) {
    auto it = subid_map.find(Node::Compact_class(nid));
    I(it != subid_map.end());
    auto it2 = down_class_map.find(it->second);
    I(it2 != down_class_map.end());
    it2->second--;
    if (it2->second == 0) {
      down_class_map.erase(it2);
    }
    subid_map.erase(it);
  } else if (type == Ntype_op::LUT)
    lut_map.erase(Node::Compact_class(nid));

  node_internal[nid].set_type(op);
}

bool Lgraph_attributes::is_type_const(Index_id nid) const {
  bool b = node_internal[nid].get_type() == Ntype_op::Const;

  return b;
}

void Lgraph_attributes::set_type_sub(Index_id nid, Lg_type_id subgraphid) {
  subid_map.insert_or_assign(Node::Compact_class(nid), subgraphid.value);

  auto it = down_class_map.find(subgraphid);
  if (it == down_class_map.end()) {
    down_class_map.insert_or_assign(subgraphid.value, 1);
  } else {
    it->second += 1;
  }

  node_internal[nid].set_type(Ntype_op::Sub);
}

Lg_type_id Lgraph_attributes::get_type_sub(Index_id nid) const {
  I(node_internal[nid].get_type() == Ntype_op::Sub);

  auto it = subid_map.find(Node::Compact_class(nid));
  I(it != subid_map.end());

  return it->second;
}

std::tuple<Lg_type_id, Index_id> Lgraph_attributes::go_next_down(Index_id nid) const {
  Index_id   n_nid  = 0;
  Lg_type_id n_lgid = 0;

  auto it = subid_map.find(nid);
  if (it != subid_map.end()) {
    ++it;
    if (it != subid_map.end()) {
      n_nid  = it->first.nid;
      n_lgid = it->second;
    }
  }

  return std::make_tuple(n_lgid, n_nid);
}

const Sub_node &Lgraph_attributes::get_type_sub_node(Index_id nid) const {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid != lgid);  // No recursion
  return library->get_sub(sub_lgid);
}

const Sub_node &Lgraph_attributes::get_type_sub_node(std::string_view sub_name) const {
  I(name != sub_name);  // No recursion
  return library->get_sub(sub_name);
}

Sub_node *Lgraph_attributes::ref_type_sub_node(Index_id nid) {
  auto sub_lgid = get_type_sub(nid);
  I(sub_lgid != lgid);  // No recursion
  return library->ref_sub(sub_lgid);
}

Sub_node *Lgraph_attributes::ref_type_sub_node(std::string_view sub_name) {
  I(name != sub_name);  // No recursion
  return library->ref_sub(sub_name);
}

void Lgraph_attributes::set_type_lut(Index_id nid, const Lconst &lutid) {
  node_internal[nid].set_type(Ntype_op::LUT);

  lut_map.insert_or_assign(Node::Compact_class(nid), lutid.serialize());
}

Lconst Lgraph_attributes::get_type_lut(Index_id nid) const {
  I(node_internal[nid].get_type() == Ntype_op::LUT);

  auto it = lut_map.find(Node::Compact_class(nid));
  I(it != lut_map.end());

  return Lconst::unserialize(it->second);
}

Lconst Lgraph_attributes::get_type_const(Index_id nid) const {
  auto it = const_map.find(Node::Compact_class(nid));
  I(it != const_map.end());

  return Lconst::unserialize(it->second);
}

void Lgraph_attributes::set_type_const(Index_id nid, const Lconst &value) {
  const_map.insert_or_assign(Node::Compact_class(nid), value.serialize());

  auto *ptr = &node_internal[nid];
  ptr->set_type(Ntype_op::Const);
  ptr->set_bits(value.get_bits());
}

void Lgraph_attributes::set_type_const(Index_id nid, std::string_view sv) { set_type_const(nid, Lconst::from_pyrope(sv)); }

void Lgraph_attributes::set_type_const(Index_id nid, int64_t value) { set_type_const(nid, Lconst(value)); }
