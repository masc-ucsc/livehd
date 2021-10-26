//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <atomic>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "mmap_map.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "sub_node.hpp"
#include "thread_pool.hpp"

//#define NO_BOTTOM_UP_PARALLEL 1

void Lgraph::each_sorted_graph_io(const std::function<void(Node_pin &pin, Port_ID pos)>& f1, bool hierarchical) {
  if (node_internal.size() < Hardcoded_output_nid)
    return;

  struct Pair_type {
    Pair_type(Lgraph *lg, Hierarchy_index hidx, Index_id idx, Port_ID pid, Port_ID _pos)
        : dpin(lg, lg, hidx, idx, pid, false), pos(_pos) {}
    Node_pin dpin;
    Port_ID  pos;
  };
  std::vector<Pair_type> pin_pair;

  auto hidx = hierarchical ? Hierarchy::hierarchical_root() : Hierarchy::non_hierarchical();

  for (const auto &io_pin : get_self_sub_node().get_io_pins()) {
    if (io_pin.is_invalid())
      continue;

    Port_ID pid = get_self_sub_node().get_instance_pid(io_pin.name);

    Index_id nid = Hardcoded_output_nid;
    if (io_pin.is_input())
      nid = Hardcoded_input_nid;
    auto idx = find_idx_from_pid(nid, pid);
    if (idx) {
      Pair_type p(this, hidx, idx, pid, io_pin.graph_io_pos);
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

void Lgraph::each_pin(const Node_pin &dpin, const std::function<bool(Index_id idx)>& f1) const {
  Index_id first_idx2 = dpin.get_root_idx();
  Index_id idx2       = first_idx2;

  bool should_not_find = false;

  while (true) {
    I(!should_not_find);
    bool cont = f1(idx2);
    if (!cont)
      return;

    node_internal.ref_lock();
    do {
      if (node_internal.ref(idx2)->is_last_state()) {
        node_internal.ref_unlock();
        return;
      } else {
        idx2 = node_internal.ref(idx2)->get_next();
      }
      if (idx2 == first_idx2) {
        node_internal.ref_unlock();
        return;
      }
    } while (node_internal.ref(idx2)->get_dst_pid() != dpin.get_pid());
    node_internal.ref_unlock();
  }
}

void Lgraph::each_graph_input(const std::function<void(Node_pin &pin)>& f1, bool hierarchical) {
  if (node_internal.size() < Hardcoded_output_nid)
    return;

  auto hidx = hierarchical ? Hierarchy::hierarchical_root() : Hierarchy::non_hierarchical();

  for (const auto &io_pin : get_self_sub_node().get_io_pins()) {
    if (io_pin.is_input()) {
      Port_ID pid = get_self_sub_node().get_instance_pid(io_pin.name);
      auto    idx = find_idx_from_pid(Hardcoded_input_nid, pid);
      if (idx) {
        Node_pin dpin(this, this, hidx, idx, pid, false);
        if (dpin.has_name())
          f1(dpin);
      }
    }
  }
}

void Lgraph::each_graph_output(const std::function<void(Node_pin &pin)>& f1, bool hierarchical) {
  if (node_internal.size() < Hardcoded_output_nid)
    return;

  auto hidx = hierarchical ? Hierarchy::hierarchical_root() : Hierarchy::non_hierarchical();

  for (const auto &io_pin : get_self_sub_node().get_io_pins()) {
    if (io_pin.is_output()) {
      Port_ID pid = get_self_sub_node().get_instance_pid(io_pin.name);
      auto    idx = find_idx_from_pid(Hardcoded_output_nid, pid);
      if (idx) {
        Node_pin dpin(this, this, hidx, idx, pid, false);
        if (dpin.has_name())  // It could be partially deleted
          f1(dpin);
      }
    }
  }
}

void Lgraph::each_local_sub_fast_direct(const std::function<bool(Node &, Lg_type_id)>& fn) {
  for (auto e : get_down_nodes_map()) {
    Index_id cid = e.first.nid;
    I(cid);

    auto node = Node(this, e.first);

    bool cont = fn(node, e.second);
    if (!cont)
      return;
  }
}

#if 0
// deprecated: Use for(const auto node:lg->fast(true))
void Lgraph::each_hier_fast(const std::function<bool(Node &)>& f) {
  const auto ht = ref_htree();

  for (const auto &hidx : ht->depth_preorder()) {
    Lgraph *lg = ht->ref_lgraph(hidx);
    for (auto fn : lg->fast()) {
      Node hn(this, lg, hidx, fn.nid);

      if (!f(hn)) {
        return;
      }
    }
  }
}
#endif

void Lgraph::each_local_unique_sub_fast(const std::function<bool(Lgraph *sub_lg)>& fn) {

  std::set<Lg_type_id> visited;
  for (auto e : get_down_nodes_map()) {
    Index_id cid = e.first.nid;
    I(cid);

    if (visited.find(e.second) != visited.end())
      continue;

    visited.insert(e.second);

    auto *sub_lg = Lgraph::open(path, e.second);
    if (sub_lg) {
      bool cont = fn(sub_lg);
      if (!cont)
        return;
    }
  }
}

void Lgraph::each_hier_unique_sub_bottom_up_int(std::set<Lg_type_id> &visited, const std::function<void(Lgraph *lg_sub)>& fn) {

  for(const auto &ent:get_down_class_map()) {
    if (visited.find(ent.first) != visited.end())
      continue;
    visited.insert(ent.first);

    auto *down_lg = Lgraph::open(get_path(), Lg_type_id(ent.first));
    if (down_lg == nullptr)
      continue;
    down_lg->each_hier_unique_sub_bottom_up_int(visited, fn);
    fn(down_lg);
  }
}

void Lgraph::each_hier_unique_sub_bottom_up(const std::function<void(Lgraph *lg_sub)>& fn) {
  std::set<Lg_type_id> visited;
  each_hier_unique_sub_bottom_up_int(visited, fn);
}

void Lgraph::bottom_up_visit_wrap(const std::function<void(Lgraph *lg_sub)> *fn
                                 ,      Pending_map                         *pending_map
                                 ,const Parent_map_type                     *parent_map) {

  (*fn)(this);

  const auto it = parent_map->find(this);
  if (it == parent_map->end())
    return;

  for(auto *parent_lg:it->second) {
    auto it2 = pending_map->find(parent_lg);
    I(it2 != pending_map->end());
    // WARNING: NASTY cast to atomic because map does not allow to have an atomic as 2nd entry
    int n_pending = atomic_fetch_sub_explicit((std::atomic<int> *)(&it2->second), 1, std::memory_order_relaxed);
    if (n_pending==1) {
      I(it2->second==0);
#ifdef NO_BOTTOM_UP_PARALLEL
      parent_lg->bottom_up_visit_wrap(fn, pending_map, parent_map);
#else
      thread_pool.add(&Lgraph::bottom_up_visit_wrap, parent_lg, fn, pending_map, parent_map);
#endif
    }
  }
}

void Lgraph::bottom_up_visit_step(Pending_map                    &pending_map
                                 ,Parent_map_type                &parent_map
                                 ,absl::flat_hash_set<Lgraph *>  &leafs_set
                                 ,std::vector<Lgraph *>          &leafs) {

  bool leaf = true;
  for(const auto &ent:get_down_class_map()) {
    auto *down_lg = Lgraph::open(get_path(), Lg_type_id(ent.first));
    if (down_lg != nullptr && !down_lg->is_empty()) {
      leaf = false;
      parent_map[down_lg].emplace_back(this);
      pending_map[this]++;
      down_lg->bottom_up_visit_step(pending_map, parent_map, leafs_set, leafs);
    }
  }

  if (leaf) {
    auto it = leafs_set.insert(this);
    if (it.second) {
      leafs.emplace_back(this);
    }
  }
}

void Lgraph::each_hier_unique_sub_bottom_up_parallel2(const std::function<void(Lgraph *lg_sub)>& fn) {

  Pending_map     pending_map;
  Parent_map_type parent_map;

  absl::flat_hash_set<Lgraph *>  leafs_set;
  std::vector<Lgraph *>          leafs;

  bottom_up_visit_step(pending_map, parent_map, leafs_set, leafs); // single-thread

  if (!leafs.empty()) {
    for(auto *lg:leafs) {
#ifdef NO_BOTTOM_UP_PARALLEL
      lg->bottom_up_visit_wrap(&fn, &pending_map, &parent_map);
#else
      thread_pool.add(&Lgraph::bottom_up_visit_wrap, lg, &fn, &pending_map, &parent_map);
#endif
    }
    thread_pool.wait_all();
  }
}

