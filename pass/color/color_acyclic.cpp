// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_acyclic.hpp"

#include <algorithm>
#include <print>

namespace livehd::color {

using livehd::graph_util::debug_name;
using livehd::graph_util::is_graph_output_pin;

Color_acyclic::Color_acyclic(Color_opts opts_, int cutoff_, bool merge_en_)
    : opts(opts_), cutoff(cutoff_), merge_en(merge_en_) {}

bool Color_acyclic::node_set_cmp(const NodeSet& a, const NodeSet& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const auto& n : a) {
    if (!b.contains(n)) {
      return false;
    }
  }
  return true;
}

bool Color_acyclic::int_set_cmp(const IntSet& a, const IntSet& b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (const auto& n : a) {
    if (!b.contains(n)) {
      return false;
    }
  }
  return true;
}

void Color_acyclic::node_set_write(NodeSet& tgt, const NodeSet& ref) {
  if (!node_set_cmp(tgt, ref)) {
    tgt = ref;
  }
}

void Color_acyclic::int_set_write(IntSet& tgt, const IntSet& ref) {
  if (!int_set_cmp(tgt, ref)) {
    tgt = ref;
  }
}

// All potential partition roots: nodes that drive a primary output, plus
// fan-out>1, dead (no out edges), and single-fanout-to-IO nodes.
void Color_acyclic::gather_roots(hhds::Graph* g) {
  auto add_root = [&](const hhds::Node_class& n) {
    if (roots.contains(n)) {
      return;
    }
    roots.insert(n);
    node2id[n] = part_id;
    id2nodes[part_id].insert(n);
    part_id += 1;
  };

  // Drivers feeding the graph's primary outputs.
  if (auto gio = g->get_io()) {
    for (const auto& decl : gio->get_output_pin_decls()) {
      auto opin = g->get_output_pin(decl.name);
      if (opin.is_invalid()) {
        continue;
      }
      for (const auto& e : opin.inp_edges()) {
        auto dn = e.driver.get_master_node();
        if (is_partitionable(dn)) {
          add_root(dn);
        }
      }
    }
  }

  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    bool       root      = false;
    const auto out_edges = n.out_edges();
    if (out_edges.size() > 1) {
      root = true;
    } else if (out_edges.empty()) {
      root = true;
    } else {  // exactly one out edge: a root if that single sink is a primary output
      if (is_graph_output_pin(out_edges.front().sink)) {
        root = true;
      }
    }
    if (root) {
      add_root(n);
    }
  }
}

// Grow each root's partition backward through inp_edges, claiming unlabeled,
// non-root, non-IO/const predecessors.
void Color_acyclic::grow_partitions(hhds::Graph* g) {
  (void)g;
  if (roots.empty()) {
    return;
  }
  for (const auto& root : roots) {
    node_preds.clear();
    auto curr_id = node2id[root];
    node_preds.push_back(root);
    while (!node_preds.empty()) {
      auto curr = node_preds.back();
      node_preds.pop_back();
      if (!is_partitionable(curr)) {
        continue;
      }
      for (const auto& ie : curr.inp_edges()) {
        auto pot_pred = ie.driver.get_master_node();
        if (!is_partitionable(pot_pred)) {
          continue;  // skip const / IO
        }
        bool is_not_root    = !roots.contains(pot_pred);
        bool is_not_labeled = !node2id.contains(pot_pred);
        if (is_not_root && is_not_labeled) {
          node2id[pot_pred] = curr_id;
          id2nodes[curr_id].insert(pot_pred);
          node_preds.push_back(pot_pred);
        }
      }
    }
  }
}

// Recompute incoming/outgoing neighbor nodes + partition ids for every part.
void Color_acyclic::gather_inou(hhds::Graph* g) {
  (void)g;
  for (auto& it : id2nodes) {
    auto    curr_id = it.first;
    NodeSet common_out;
    NodeSet common_inc;
    IntSet  common_outparts;
    IntSet  common_incparts;

    for (const auto& n : it.second) {
      if (!is_partitionable(n)) {
        continue;
      }
      for (const auto& e : n.out_edges()) {
        auto snode = e.sink.get_master_node();
        if (!is_partitionable(snode)) {
          continue;  // const / IO sink
        }
        auto out_it = node2id.find(snode);
        if (out_it == node2id.end()) {
          continue;
        }
        auto outgoing_id = out_it->second;
        if (curr_id != outgoing_id) {
          common_out.insert(snode);
          common_outparts.insert(outgoing_id);
        }
      }
      for (const auto& e : n.inp_edges()) {
        auto dnode = e.driver.get_master_node();
        if (!is_partitionable(dnode)) {
          continue;
        }
        auto inc_it = node2id.find(dnode);
        if (inc_it == node2id.end()) {
          continue;
        }
        auto incoming_id = inc_it->second;
        if (curr_id != incoming_id) {
          common_inc.insert(dnode);
          common_incparts.insert(incoming_id);
        }
      }
    }
    node_set_write(id2out[curr_id], common_out);
    node_set_write(id2inc[curr_id], common_inc);
    int_set_write(id2outparts[curr_id], common_outparts);
    int_set_write(id2incparts[curr_id], common_incparts);
  }
}

void Color_acyclic::merge_op(int merge_from, int merge_into) {
  for (auto& it : node2id) {
    if (it.second == merge_from) {
      it.second = merge_into;
    }
  }
  id2nodes[merge_into].merge(id2nodes[merge_from]);
  id2nodes.erase(merge_from);

  if (id2inc.contains(merge_from) || id2inc.contains(merge_into)) {
    id2inc[merge_into].merge(id2inc[merge_from]);
    id2inc.erase(merge_from);
    id2incparts[merge_into].erase(merge_from);
    id2incparts[merge_into].merge(id2incparts[merge_from]);
    id2incparts.erase(merge_from);
  }
  if (id2out.contains(merge_from) || id2out.contains(merge_into)) {
    id2out[merge_into].merge(id2out[merge_from]);
    id2out.erase(merge_from);
    id2outparts[merge_into].erase(merge_from);
    id2outparts[merge_into].merge(id2outparts[merge_from]);
    id2outparts.erase(merge_from);
  }
}

void Color_acyclic::merge_partitions_same_parents() {
  std::vector<int> pwi;
  for (int i = 0; i < part_id; ++i) {
    if (id2inc.contains(i)) {
      pwi.push_back(i);
    }
  }
  if (pwi.empty()) {
    return;
  }

  auto pivot      = pwi.begin();
  bool keep_going = true;
  while (keep_going) {
    keep_going      = false;
    bool merge_flag = false;
    int  merge_into = -1;
    int  merge_from = -1;

    for (auto i = pwi.begin(); i != pwi.end(); ++i) {
      if ((i != pivot) && node_set_cmp(id2inc[*i], id2inc[*pivot])) {
        auto pivot_part_size = id2nodes[*pivot].size();
        auto i_part_size     = id2nodes[*i].size();
        if (!id2inc[*i].empty() || !id2inc[*pivot].empty()) {
          if (pivot_part_size <= static_cast<size_t>(cutoff) || i_part_size <= static_cast<size_t>(cutoff)) {
            merge_flag = true;
            keep_going = true;
            merge_into = *pivot;
            merge_from = *i;
            break;
          }
        }
      }
    }

    if (merge_flag) {
      merge_op(merge_from, merge_into);
      auto rm = std::find(pwi.begin(), pwi.end(), merge_from);
      if (rm != pwi.end()) {
        pwi.erase(rm);
      }
      pivot = pwi.begin();
    } else if ((pivot + 1) != pwi.end()) {
      ++pivot;
      keep_going = true;
    }
  }
}

void Color_acyclic::merge_partitions_one_parent() {
  std::vector<int> pwi;
  for (int i = 0; i < part_id; ++i) {
    if (id2inc.contains(i)) {
      pwi.push_back(i);
    }
  }
  if (pwi.empty()) {
    return;
  }

  auto pivot      = pwi.begin();
  bool keep_going = true;
  while (keep_going) {
    keep_going      = false;
    bool merge_flag = false;
    int  merge_into = -1;
    int  merge_from = -1;

    if (id2incparts[*pivot].size() == 1) {
      merge_flag = true;
      keep_going = true;
      merge_into = *pivot;
      merge_from = *id2incparts[*pivot].begin();
    }

    if (merge_flag && merge_from != merge_into && id2nodes.contains(merge_from)) {
      merge_op(merge_from, merge_into);
      for (auto* vec : {&pwi}) {
        auto rm = std::find(vec->begin(), vec->end(), merge_from);
        if (rm != vec->end()) {
          vec->erase(rm);
        }
      }
      pivot = pwi.begin();
    } else if ((pivot + 1) != pwi.end()) {
      ++pivot;
      keep_going = true;
    }
  }
}

void Color_acyclic::label(hhds::Graph* g) {
  node2id.clear();
  id2nodes.clear();
  id2inc.clear();
  id2out.clear();
  id2incparts.clear();
  id2outparts.clear();
  roots.clear();
  part_id = 1;

  gather_roots(g);
  grow_partitions(g);
  gather_inou(g);

  if (merge_en) {
    merge_partitions_same_parents();
    gather_inou(g);
    merge_partitions_one_parent();
    gather_inou(g);
  }

  int n_colors = apply_coloring(g, node2id, opts);

  if (opts.verbose) {
    std::print("[color.acyclic] {} -> {} partitions\n", g->get_name(), n_colors);
  }
}

}  // namespace livehd::color
