// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_cgen.hpp"

#include <algorithm>
#include <map>
#include <print>
#include <vector>

#include "node_util.hpp"

namespace livehd::color {

Color_cgen::Color_cgen(Color_opts opts_) : opts(opts_) {}

void Color_cgen::label(hhds::Graph* g) {
  // node -> the set of cone-sink ids it forward-reaches (kept as a small sorted
  // vector; most nodes feed exactly one). Index 0 is the shared "state" bucket;
  // each primary output gets a fresh id >= 1.
  absl::flat_hash_map<hhds::Node_class, std::vector<int>> sig;

  // Backward BFS from `start` through combinational logic, tagging every reached
  // partitionable node with cone-sink `idx`. Stops at loop_break (flop/mem): a
  // registered node feeds the sink but its own din-cone is a separate region.
  std::vector<hhds::Node_class> wl;
  auto mark = [&](const hhds::Node_class& start, int idx) {
    wl.clear();
    wl.push_back(start);
    while (!wl.empty()) {
      auto n = wl.back();
      wl.pop_back();
      if (!is_partitionable(n)) {
        continue;  // skip const / IO
      }
      auto& s = sig[n];
      if (std::find(s.begin(), s.end(), idx) != s.end()) {
        continue;  // already reached this node for this cone-sink
      }
      s.push_back(idx);
      if (n.is_loop_break()) {
        continue;  // flop/mem boundary -- do not cross into its din-cone
      }
      for (const auto& ie : n.inp_edges()) {
        wl.push_back(ie.driver.get_master_node());
      }
    }
  };

  constexpr int STATE = 0;

  // Each primary output is its own cone-sink.
  int next_sink = 1;
  if (auto gio = g->get_io()) {
    for (const auto& decl : gio->get_output_pin_decls()) {
      auto opin = g->get_output_pin(decl.name);
      if (opin.is_invalid()) {
        continue;
      }
      int idx = next_sink++;
      for (const auto& e : opin.inp_edges()) {
        mark(e.driver.get_master_node(), idx);
      }
    }
  }

  // All flop/mem next-state (din etc.) logic shares the single STATE cone-sink:
  // it is off the input->output combinational feedthrough (flops break the loop),
  // so it never needs per-element splitting to break a false loop.
  for (auto n : g->forward_class()) {
    if (!n.is_loop_break()) {
      continue;
    }
    for (const auto& ie : n.inp_edges()) {
      mark(ie.driver.get_master_node(), STATE);
    }
  }

  // Collapse each distinct signature to a dense color id (>= 1).
  Node2Id                          node2id;
  std::map<std::vector<int>, int>  sig2id;
  int                              next_color = 1;
  for (auto& [node, s] : sig) {
    std::sort(s.begin(), s.end());
    auto [it, inserted] = sig2id.try_emplace(s, next_color);
    if (inserted) {
      ++next_color;
    }
    node2id[node] = it->second;
  }

  int n_colors = apply_coloring(g, node2id, opts);
  if (opts.verbose) {
    // stderr (not stdout): the kernel captures pass stdout for the JSON envelope.
    std::print(stderr, "[color.cgen] {} -> {} colors ({} nodes)\n", g->get_name(), n_colors, node2id.size());
  }
}

}  // namespace livehd::color
