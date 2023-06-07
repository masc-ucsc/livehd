//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "hierarchy.hpp"

#include "absl/strings/str_cat.h"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "str_tools.hpp"

Hierarchy::Hierarchy(Lgraph *_top) : top(_top) {
  // 1st entry (hidx=0) is for root
  up_entry_t h_entry;
  h_entry.parent_lg   = top;
  h_entry.parent_nid  = 0;
  h_entry.parent_hidx = 0;

  up_vector.emplace_back(h_entry);
}

Lgraph *Hierarchy::ref_lgraph(const Hierarchy_index hidx) const {
  if (hidx <= 0) {  // -1 if no hierarchical
    return top;
  }

  I(hidx < up_vector.size());  // invalid hierarchy hidx

  const auto &h_entry = up_vector[hidx];

  auto *parent_lg = h_entry.parent_lg;
  auto  lgid      = parent_lg->get_type_sub(h_entry.parent_nid);

  return parent_lg->ref_library()->open_lgraph(lgid);
}

std::tuple<Hierarchy_index, Lgraph *, Index_id> Hierarchy::get_instance_up(const Hierarchy_index hidx) const {
  I(hidx > 0 && hidx < up_vector.size());  // Do not ask for up for root or invalid hidx

  const auto &h_entry = up_vector[hidx];

  return std::make_tuple(h_entry.parent_hidx, h_entry.parent_lg, h_entry.parent_nid);
}

Node Hierarchy::get_instance_up_node(const Hierarchy_index hidx) const {
  I(!Hierarchy::is_root(hidx));

  auto [up_hidx, up_lg, up_nid] = get_instance_up(hidx);

  return {top, up_lg, up_hidx, up_nid};
}

Hierarchy_index Hierarchy::go_up(const Hierarchy_index hidx) const {
  if (hidx <= 0) {
    return -1;
  }

  I(hidx < up_vector.size());  // Do not ask for up for root or invalid hidx

  const auto &h_entry = up_vector[hidx];

  return h_entry.parent_hidx;
}

std::string_view Hierarchy::get_name(const Hierarchy_index hidx) const {
  if (hidx <= 0) {
    return "";
  }

  I(hidx < up_vector.size());  // invalid hierarchy hidx
  if (!up_vector[hidx].name.empty()) {
    return up_vector[hidx].name;
  }

  // auto    i = up_vector[hidx].parent_hidx;
  auto        i = hidx;
  std::string name;
  while (i > 0) {
    Node parent_node(up_vector[i].parent_lg, up_vector[i].parent_lg, 0, up_vector[i].parent_nid);
    I(parent_node.is_type_sub());

    auto parent_name = parent_node.default_instance_name();
    if (name.empty()) {
      name = parent_name;
    } else if (!parent_name.empty()) {
      name = absl::StrCat(parent_name, ".", name);
    }

    i = up_vector[i].parent_hidx;
  }

  up_vector[hidx].name = name;

  return up_vector[hidx].name;
}

Hierarchy_index Hierarchy::go_up(const Node &node) const { return go_up(node.get_hidx()); }

std::tuple<Hierarchy_index, Lgraph *> Hierarchy::get_next(const Hierarchy_index hidx) const {
  auto h = hidx + 1;

  if (h >= static_cast<int>(up_vector.size())) {
    return std::make_tuple(-1, top);
  }

  return std::make_tuple(h, ref_lgraph(h));
}

Hierarchy_index Hierarchy::go_down(Hierarchy_index parent_hidx, Lgraph *parent_lg, Index_id parent_nid) {
  // Look for a up_vector that has parent_hidx == hidx and parent_nid == nid
  // down_map

  key_entry_t key(parent_nid, parent_hidx);

  auto it = down_map.find(key);
  if (it != down_map.end()) {
    return it->second;
  }

  up_entry_t h_entry;
  h_entry.parent_lg   = parent_lg;
  h_entry.parent_nid  = parent_nid;
  h_entry.parent_hidx = parent_hidx;

  auto hidx = up_vector.size();

  up_vector.emplace_back(h_entry);

  down_map.insert_or_assign(key, hidx);

  return hidx;
}

Hierarchy_index Hierarchy::go_down(const Node &node) {
  I(node.is_type_sub_present());

  return go_down(node.get_hidx(), node.get_class_lgraph(), node.get_nid());
}

bool Hierarchy::is_root(const Node &node) { return is_root(node.get_hidx()); }

void Hierarchy::dump() const {
  auto hidx = Hierarchy::hierarchical_root();

  Lgraph *current_g;
  std::tie(hidx, current_g) = get_next(hidx);
  while (current_g != top) {
    fmt::print("hier:{} lg:{}\n", hidx, current_g->get_name());
    std::tie(hidx, current_g) = get_next(hidx);
  }

  fmt::print("hier:{} lg:{}\n", hidx, top->get_name());
}
