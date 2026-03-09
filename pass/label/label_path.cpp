// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_path.hpp"

#include <algorithm>

#include "cell.hpp"

// Returns true if color was newly added, false if already present
static bool add_color(std::vector<int> &colors, int color) {
  if (std::find(colors.begin(), colors.end(), color) != colors.end()) {
    return false;
  }
  colors.push_back(color);
  return true;
}

void Label_path::propagate_fwd(const Node &node, int color) {
  for (const auto &e : node.out_edges()) {
    auto succ = e.sink.get_node();
    if (succ.is_type_loop_last()) {
      // Back-to-back flop: share color
      add_color(node2colors[succ.get_compact()], color);
      continue;
    }

    if (!add_color(node2colors[succ.get_compact()], color)) {
      continue;  // this color already propagated here
    }

    // Check if this successor has a wname on any output edge (stop point)
    bool has_wname = false;
    for (const auto &oe : succ.out_edges()) {
      if (oe.driver.has_name()) {
        has_wname = true;
        break;
      }
    }
    if (has_wname) {
      continue;
    }

    // No wname yet, keep propagating forward
    propagate_fwd(succ, color);
  }
}

void Label_path::propagate_bwd(const Node &node, int color) {
  for (const auto &e : node.inp_edges()) {
    auto pred = e.driver.get_node();
    if (pred.is_type_loop_last()) {
      // Back-to-back flop: share color
      add_color(node2colors[pred.get_compact()], color);
      continue;
    }

    if (!add_color(node2colors[pred.get_compact()], color)) {
      continue;  // this color already propagated here
    }

    // Check if this predecessor has a wname on any output edge (stop point)
    bool has_wname = false;
    for (const auto &oe : pred.out_edges()) {
      if (oe.driver.has_name()) {
        has_wname = true;
        break;
      }
    }
    if (has_wname) {
      continue;
    }

    // No wname yet, keep propagating backward
    propagate_bwd(pred, color);
  }
}

void Label_path::label(Lgraph *g) {
  last_free_id = 0;
  node2colors.clear();

  // For each is_loop_last, propagate forward/backward until hitting
  // a node with a wname on its driver pin or another is_loop_last.
  for (auto node : g->fast(hier)) {
    if (!node.is_type_loop_last()) {
      continue;
    }

    auto nc = node.get_compact();

    int   color;
    auto &colors = node2colors[nc];
    if (colors.empty()) {
      color = get_free_id();
      colors.push_back(color);
    } else {
      color = colors.front();
    }

    propagate_bwd(node, color);
    propagate_fwd(node, color);
  }

  // Apply colors to the graph (pick first color since API supports one)
  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph *lg) { lg->ref_node_color_map()->clear(); });
  }
  g->ref_node_color_map()->clear();

  for (auto n : g->fast(hier)) {
    auto it = node2colors.find(n.get_compact());
    if (it != node2colors.end() && !it->second.empty()) {
      n.set_color(it->second.front());
    } else {
      n.del_color();
    }
  }

  if (verbose) {
    dump(g);
  }
}

void Label_path::dump(Lgraph *g) const {
  std::cout << "---- Label Path dump ----\n";

  absl::flat_hash_map<int, absl::flat_hash_set<std::string>> color2wnames;
  absl::flat_hash_map<int, absl::flat_hash_set<std::string>> color2sources;

  for (auto &[nc, colors] : node2colors) {
    Node node(g, nc);

    for (auto color : colors) {
      // Collect wnames from output edges
      for (const auto &e : node.out_edges()) {
        auto wn = e.driver.get_wire_name();
        if (!wn.empty()) {
          color2wnames[color].insert(wn);
        }
      }

      // Collect source files
      auto src = node.get_source();
      if (!src.empty()) {
        color2sources[color].insert(src);
      }
    }
  }

  // Collect all colors
  absl::flat_hash_set<int> all_colors;
  for (auto &[nc, colors] : node2colors) {
    for (auto c : colors) {
      all_colors.insert(c);
    }
  }

  for (auto color : all_colors) {
    std::print(":{}", color);

    auto wit = color2wnames.find(color);
    if (wit != color2wnames.end()) {
      for (auto &wn : wit->second) {
        std::print(" {}", wn);
      }
    }
    std::print(":");  // Delimiter for source

    auto sit = color2sources.find(color);
    if (sit != color2sources.end()) {
      for (auto &src : sit->second) {
        std::print(" {}", src);
      }
    }

    std::print("\n");
  }

  std::cout << "---- fin ----\n";
}
