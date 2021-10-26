//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "hierarchy.hpp"

#include "annotate.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

Hierarchy::Hierarchy(Lgraph *_top) : top(_top) {}

Lgraph *Hierarchy::ref_lgraph(const Hierarchy_index hidx) const {

  if (Hierarchy::is_root(hidx))
    return top;

  auto parent = hidx.get_str_after_last_if_exists(':');

  auto lgid = parent.to_u64_from_hex();
  I(lgid);

  auto *lg = Lgraph::open(top->get_path(), Lg_type_id(lgid));
  I(lg);
  return lg;
}

std::tuple<Hierarchy_index, Lgraph *, Index_id> Hierarchy::get_instance_up(const Hierarchy_index hidx) const {
  Lgraph *lg;
  Hierarchy_index up_hidx;

  {
    auto parent_lgid_pos  = hidx.rfind(':');

    if (parent_lgid_pos == std::string::npos) { // prev to top
      lg      = top;
      up_hidx = Hierarchy::hierarchical_root();
    }else{
      up_hidx = hidx.substr(0, parent_lgid_pos);
      I(!up_hidx.empty()); // up_hidx = Hierarchy::hierarchical_root();

      Lg_type_id  lgid(hidx.substr(parent_lgid_pos+1).to_u64_from_hex());
      lg = Lgraph::open(top->get_path(), lgid);
    }
  }

  Index_id nid;
  {
    auto parent_nid_pos   = hidx.rfind('.');
    I(parent_nid_pos != std::string::npos);
    nid = hidx.substr(parent_nid_pos+1).to_u64_from_hex();
  }

  return std::make_tuple(up_hidx, lg, nid);
}

Node Hierarchy::get_instance_up_node(const Hierarchy_index hidx) const {

  I(!Hierarchy::is_root(hidx));

  auto [up_hidx, up_lg, up_nid] = get_instance_up(hidx);

  return Node(top, up_lg, up_hidx, up_nid);
}

Hierarchy_index Hierarchy::go_up(const Hierarchy_index hidx) {

  auto parent_lgid_pos = hidx.rfind(':');
  if (parent_lgid_pos == std::string::npos) {
    return ""_str;
  }

  return hidx.substr(0, parent_lgid_pos);
}

Hierarchy_index Hierarchy::go_up(const Node &node) {
  return go_up(node.get_hidx());
}

std::tuple<Hierarchy_index, Lgraph *> Hierarchy::go_next_down(Hierarchy_index hidx, Lgraph *lg) {

  for (const auto &ent:lg->get_down_nodes_map()) {
    auto *sub_lg = Lgraph::open(lg->get_path(), ent.second);
    if (sub_lg == nullptr)
      continue;

    if (sub_lg->is_empty())
      continue;

    auto n_hidx = go_down(hidx, ent.second, ent.first.nid);

    return go_next_down(n_hidx, sub_lg);
  }

  return std::make_tuple(hidx, lg);
}

std::tuple<Hierarchy_index, Lgraph *> Hierarchy::get_next(const Hierarchy_index hidx) const {

  if (Hierarchy::is_root(hidx)) { // go to find depth first post order
    return go_next_down(hidx, top);
  }

  auto it_hidx = hidx;
  while(true) {
    auto [up_hidx, up_lg, up_nid] = get_instance_up(it_hidx);

    auto [n_lgid, n_nid] = up_lg->go_next_down(up_nid);
    if (n_lgid) {
      auto  down_hidx = go_down(up_hidx, n_lgid, n_nid);
      auto *down_lg   = Lgraph::open(up_lg->get_path(), n_lgid);

      return go_next_down(down_hidx, down_lg);
    }

    if (up_hidx == Hierarchy::hierarchical_root()) // nothing pending
      return std::make_tuple(Hierarchy::hierarchical_root(), top);

    it_hidx = up_hidx;
  }
}

Hierarchy_index Hierarchy::go_down(Hierarchy_index hidx, Lg_type_id lgid, Index_id nid) {
  if (Hierarchy::is_root(hidx))
    return mmap_lib::str::concat(mmap_lib::str::from_u64_to_hex(lgid), ".", mmap_lib::str::from_u64_to_hex(nid));

  return mmap_lib::str::concat(hidx, ":", mmap_lib::str::from_u64_to_hex(lgid), ".", mmap_lib::str::from_u64_to_hex(nid));
}

Hierarchy_index Hierarchy::go_down(const Node &node) {
  I(node.is_type_sub_present());

  return go_down(node.get_hidx(), node.get_type_sub(), node.get_nid());
}

bool  Hierarchy::is_root(const Node &node) {
  return is_root(node.get_hidx());
}

void Hierarchy::dump() const {

  auto hidx = Hierarchy::hierarchical_root();

  Lgraph *current_g;
  std::tie(hidx, current_g) =  get_next(hidx);
  while (current_g != top) {
    fmt::print("hier:{} lg:{}\n", hidx, current_g->get_name());
    std::tie(hidx, current_g) =  get_next(hidx);
  }

  fmt::print("hier:{} lg:{}\n", hidx, top->get_name());
}
