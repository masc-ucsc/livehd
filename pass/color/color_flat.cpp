// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_flat.hpp"

#include <print>

namespace livehd::color {

Color_flat::Color_flat(Color_opts opts_) : opts(opts_) {
  // "One color for everything" is incompatible with the per-region continuous
  // split, so force it off regardless of how the driver was invoked (keeps
  // direct callers -- e.g. unit tests -- honest too).
  opts.continuous = false;
}

void Color_flat::label(hhds::Graph* g) {
  Node2Id node2id;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    node2id[n] = 1;  // single region for the whole def
  }

  int n_colors = apply_coloring(g, node2id, opts);
  if (opts.verbose) {
    std::print("[color.flat] {} -> {} color\n", g->get_name(), n_colors);
  }
}

}  // namespace livehd::color
