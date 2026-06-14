// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_synth.hpp"

#include <print>

#include "iassert.hpp"

namespace livehd::color {

using livehd::graph_util::bits_of;
using livehd::graph_util::type_op_of;

Color_synth::Color_synth(Color_opts opts_, std::string_view alg) : opts(opts_) {
  if (alg == "pipe") {
    synth = false;
  } else {
    synth = true;  // "synth" (default)
  }
}

void Color_synth::set_id(const hhds::Node_class& node, int id) {
  auto [it, inserted] = flat_node2id.insert({node, id});
  if (inserted || id == it->second) {
    return;
  }
  auto it2 = flat_merges.find(it->second);
  if (it2 == flat_merges.end()) {
    flat_merges[id] = it->second;
  } else {
    flat_merges[id] = it2->second;
  }
}

// Driver-pin bits for a node (0 if none materialized yet).
static int driver_bits(const hhds::Node_class& n) {
  for (const auto& e : n.out_edges()) {
    if (auto b = bits_of(e.driver); b != 0) {
      return b;
    }
  }
  return 0;
}

void Color_synth::mark_ids(hhds::Graph* g) {
  // Cluster the consumers of each primary input under fresh ids.
  if (auto gio = g->get_io()) {
    for (const auto& decl : gio->get_input_pin_decls()) {
      auto ipin = g->get_input_pin(decl.name);
      if (ipin.is_invalid()) {
        continue;
      }
      for (const auto& e : ipin.out_edges()) {
        auto node = e.sink.get_master_node();
        if (!is_partitionable(node)) {
          continue;
        }
        if (!node.is_loop_last()) {
          set_id(node, get_free_id());
        }
      }
    }
  }

  for (auto node : g->forward_class()) {
    if (!is_partitionable(node)) {
      continue;
    }
    if (node.is_loop_last()) {
      continue;
    }

    if (synth) {
      auto op = type_op_of(node);
      if (op == Ntype_op::Mult || op == Ntype_op::Div) {
        continue;
      }
      if (op == Ntype_op::Sum && driver_bits(node) > 8) {
        continue;
      }
    }

    int  id = 0;
    auto it = flat_node2id.find(node);
    if (it == flat_node2id.end()) {
      id = get_free_id();
    } else {
      id = it->second;
    }

    for (const auto& e : node.out_edges()) {
      auto snode = e.sink.get_master_node();
      if (is_partitionable(snode)) {
        set_id(snode, id);
      }
    }
  }
}

void Color_synth::collapse_merge(int dst) {
  if (collapse_set_min > dst) {
    collapse_set_min = dst;
  }
  auto it = flat_merges.find(dst);
  if (it == flat_merges.end()) {
    return;
  }
  collapse_set.insert(dst);
  if (collapse_set.contains(it->second)) {
    return;
  }
  collapse_merge(it->second);
  flat_merges[dst] = collapse_set_min;
}

void Color_synth::merge_ids() {
  bool updated;
  do {
    updated = false;
    for (auto& it : flat_merges) {
      collapse_set.clear();
      collapse_set.insert(it.second);
      collapse_set_min = it.second;
      collapse_merge(it.second);
      if (collapse_set_min == it.second) {
        continue;
      }
      it.second = collapse_set_min;
      updated   = true;
    }
  } while (updated);

  for (auto& it : flat_node2id) {
    auto it2 = flat_merges.find(it.second);
    if (it2 == flat_merges.end()) {
      continue;
    }
    it.second = it2->second;
  }
}

void Color_synth::label(hhds::Graph* g) {
  last_free_id = 1;
  flat_node2id.clear();
  flat_merges.clear();
  collapse_set.clear();

  mark_ids(g);
  merge_ids();

  int n_colors = apply_coloring(g, flat_node2id, opts);
  if (opts.verbose) {
    std::print("[color.synth] {} -> {} clusters\n", g->get_name(), n_colors);
  }
}

}  // namespace livehd::color
