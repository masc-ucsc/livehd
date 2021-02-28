//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "mmap_map.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "sub_node.hpp"

void LGraph::each_sorted_graph_io(std::function<void(Node_pin &pin, Port_ID pos)> f1, bool hierarchical) {
  if (node_internal.size() < Hardcoded_output_nid)
    return;

  struct Pair_type {
    Pair_type(LGraph *lg, Hierarchy_index hidx, Index_ID idx, Port_ID pid, Port_ID _pos)
      : dpin(lg, lg, hidx, idx, pid, false)
      , pos(_pos) {
      }
    Node_pin dpin;
    Port_ID  pos;
  };
  std::vector<Pair_type> pin_pair;

  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  for(const auto *io_pin:get_self_sub_node().get_io_pins()) {
    Port_ID pid = get_self_sub_node().get_instance_pid(io_pin->name);

    Index_ID nid = Hardcoded_output_nid;
    if (io_pin->is_input())
      nid = Hardcoded_input_nid;
    auto idx = find_idx_from_pid(nid, pid);
    if (idx) {
      Pair_type p(this, hidx, idx, pid, io_pin->graph_io_pos);
      if (p.dpin.has_name()) {
        pin_pair.emplace_back(p);
      }
    }
  }

  std::sort(pin_pair.begin(), pin_pair.end(), [](const Pair_type &a, const Pair_type &b) -> bool {
    if (a.pos == Port_invalid && b.pos == Port_invalid) {
      if (a.dpin.is_graph_input() && b.dpin.is_graph_output()) {
        return true;
      }
      if (a.dpin.is_graph_output() && b.dpin.is_graph_input()) {
        return false;
      }
      if (a.dpin.is_graph_input() && b.dpin.is_graph_input()) {
        auto a_name = a.dpin.get_name();
        if (a_name == "clock")
          return true;
        if (a_name == "reset")
          return true;
        auto b_name = b.dpin.get_name();
        if (b_name == "clock")
          return false;
        if (b_name == "reset")
          return false;
      }

      return a.dpin.get_name() < b.dpin.get_name();
    }
    if (a.pos == Port_invalid)
      return true;
    if (b.pos == Port_invalid)
      return false;

    return a.pos < b.pos;
  });

  for (auto &pp : pin_pair) {
    f1(pp.dpin, pp.pos);
  }
}

void LGraph::each_pin(const Node_pin &dpin, std::function<bool(Index_ID idx)> f1) const {

  Index_ID first_idx2 = dpin.get_root_idx();
  Index_ID idx2 = first_idx2;

  bool should_not_find = false;

  while (true) {
    I(!should_not_find);
    bool cont = f1(idx2);
    if (!cont)
      return;

    do{
      if (node_internal[idx2].is_last_state()) {
#ifndef NDEBUG
        idx2 = node_internal[idx2].get_master_root_nid();
        I(idx2 == dpin.get_node().get_nid());
        should_not_find = true; // loop and try the others (should not have it before root)
#else
        return;
#endif
      }else{
        idx2 = node_internal[idx2].get_next();
      }
      if (idx2==first_idx2)
        return;
    } while (node_internal[idx2].get_dst_pid() != dpin.get_pid());
  }
}

void LGraph::each_graph_input(std::function<void(Node_pin &pin)> f1, bool hierarchical) {
  if (node_internal.size() < Hardcoded_output_nid)
    return;

  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  for(const auto *io_pin:get_self_sub_node().get_io_pins()) {
    I(!io_pin->is_invalid());
    if (io_pin->is_input()) {
      Port_ID pid = get_self_sub_node().get_instance_pid(io_pin->name);
      auto idx = find_idx_from_pid(Hardcoded_input_nid, pid);
      if (idx) {
        Node_pin dpin(this, this, hidx, idx, pid, false);
        if (dpin.has_name())
          f1(dpin);
      }
    }
  }
}

void LGraph::each_graph_output(std::function<void(Node_pin &pin)> f1, bool hierarchical) {
  if (node_internal.size() < Hardcoded_output_nid)
    return;

  auto hidx = hierarchical? Hierarchy_tree::root_index() : Hierarchy_tree::invalid_index();

  for(const auto *io_pin:get_self_sub_node().get_io_pins()) {
    I(!io_pin->is_invalid());
    if (io_pin->is_output()) {
      Port_ID pid = get_self_sub_node().get_instance_pid(io_pin->name);
      auto idx = find_idx_from_pid(Hardcoded_output_nid, pid);
      if (idx) {
        Node_pin dpin(this, this, hidx, idx, pid, false);
        if (dpin.has_name()) // It could be partially deleted
          f1(dpin);
      }
    }
  }
}

void LGraph::each_sub_fast_direct(const std::function<bool(Node &, Lg_type_id)> fn) {
  const auto &m = get_down_nodes_map();
  for (auto it = m.begin(), end = m.end(); it != end; ++it) {
    Index_ID cid = it->first.nid;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto node = Node(this, it->first);

    bool cont = fn(node, it->second);
    if (!cont)
      return;
  }
}

void LGraph::each_hier_fast_direct(const std::function<bool(Node &)> f) {
  const auto ht = ref_htree();

  for (const auto& hidx : ht->depth_preorder()) {
    LGraph* lg = ht->ref_lgraph(hidx);
    for (auto fn : lg->fast()) {
      Node hn(this, lg, hidx, fn.nid);

      if (!f(hn)) {
        return;
      }
    }
  }
}

void LGraph::each_sub_unique_fast(const std::function<bool(Node &, Lg_type_id)> fn) {
  const auto &m = get_down_nodes_map();
  std::set<Lg_type_id> visited;
  for (auto it = m.begin(), end = m.end(); it != end; ++it) {
    Index_ID cid = it->first.nid;
    I(cid);
    I(node_internal[cid].is_node_state());
    I(node_internal[cid].is_master_root());

    auto node = Node(this, it->first);

    bool cont = true;
    if (visited.find(it->second) == visited.end()) {
      cont = fn(node, it->second);
      visited.insert(it->second);
    }
    if (!cont) return;
  }
}

