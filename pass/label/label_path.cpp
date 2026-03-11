// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_path.hpp"

#include <algorithm>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "cell.hpp"

// Check if a driver pin has a real wname (not a temporary _nXXX name)
static bool has_real_wname(const Node_pin& dpin) {
  if (!dpin.has_name()) {
    return false;
  }
  auto wn = dpin.get_wire_name();
  return !wn.empty() && wn[0] != '_';
}

// Returns true if color was newly added, false if already present
static bool add_color(std::vector<int>& colors, int color) {
  if (std::find(colors.begin(), colors.end(), color) != colors.end()) {
    return false;
  }
  colors.push_back(color);
  return true;
}

std::vector<std::string> Label_path::parse_instance_names(std::string_view instance_csv) {
  std::vector<std::string> names;

  for (auto token : absl::StrSplit(instance_csv, ',')) {
    auto trimmed = absl::StripAsciiWhitespace(token);
    if (trimmed.empty()) {
      continue;
    }
    names.emplace_back(trimmed);
  }

  return names;
}

bool Label_path::should_stop_fwd(const Node& node, int color) const {
  if (instance_names.empty()) {
    for (const auto& oe : node.out_edges()) {
      if (has_real_wname(oe.driver)) {
        return true;
      }
    }
    return false;
  }

  if (!node.has_name()) {
    return false;
  }

  auto it = color2instance.find(color);
  if (it == color2instance.end()) {
    return true;
  }

  auto prefix = absl::StrCat("_", it->second);
  return node.get_name().rfind(prefix, 0) != 0;
}

void Label_path::propagate_fwd(const Node& node, int color) {
  for (const auto& e : node.out_edges()) {
    auto succ = e.sink.get_node();
    if (succ.is_type_loop_last()) {
      // Back-to-back flop: share color
      add_color(node2colors[succ.get_compact()], color);
      continue;
    }

    if (!add_color(node2colors[succ.get_compact()], color)) {
      continue;  // this color already propagated here
    }

    if (should_stop_fwd(succ, color)) {
      continue;
    }

    propagate_fwd(succ, color);
  }
}

void Label_path::propagate_bwd(const Node& node, int color) {
  for (const auto& e : node.inp_edges()) {
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
    for (const auto& oe : pred.out_edges()) {
      if (has_real_wname(oe.driver)) {
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

void Label_path::label(Lgraph* g) {
  last_free_id = 0;
  node2colors.clear();
  color2instance.clear();

  if (!instance_names.empty()) {
    absl::flat_hash_set<std::string_view> instance_set;
    for (const auto& name : instance_names) {
      instance_set.insert(name);
    }

    for (auto node : g->fast(hier)) {
      if (!node.has_name() || !instance_set.contains(node.get_name())) {
        continue;
      }

      auto& colors = node2colors[node.get_compact()];
      int   color  = colors.empty() ? get_free_id() : colors.front();
      if (colors.empty()) {
        colors.push_back(color);
      }
      color2instance.insert_or_assign(color, node.get_name());

      propagate_fwd(node, color);
    }
  } else {
    // For each is_loop_last, propagate forward/backward until hitting
    // a node with a wname on its driver pin or another is_loop_last.
    for (auto node : g->fast(hier)) {
      if (!node.is_type_loop_last()) {
        continue;
      }

      auto nc = node.get_compact();

      int   color;
      auto& colors = node2colors[nc];
      if (colors.empty()) {
        color = get_free_id();
        colors.push_back(color);
      } else {
        color = colors.front();
      }

      propagate_bwd(node, color);
      propagate_fwd(node, color);
    }
  }

  // Apply colors to the graph (pick first color since API supports one)
  if (hier) {
    g->each_hier_unique_sub_bottom_up([](Lgraph* lg) { lg->ref_node_color_map()->clear(); });
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

void Label_path::dump(Lgraph* g) const {
  std::cout << "---- Label Path dump ----\n";

  absl::flat_hash_map<int, std::string>                      dump_color2instance;
  absl::flat_hash_map<int, absl::flat_hash_set<std::string>> color2wnames;
  absl::flat_hash_map<int, absl::flat_hash_set<std::string>> color2sources;

  // Collect all colors
  absl::flat_hash_set<int> all_colors;

  for (auto& [nc, colors] : node2colors) {
    Node node(g, nc);

    for (auto color : colors) {
      all_colors.insert(color);
      auto iit = color2instance.find(color);
      if (iit != color2instance.end()) {
        dump_color2instance.insert_or_assign(color, iit->second);
      }

      // Collect wnames from output edges
      for (const auto& e : node.out_edges()) {
        auto wn = e.driver.get_wire_name();
        if (!wn.empty() && wn[0] != '_') {
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

  for (auto color : all_colors) {
    std::print(":{}", color);

    std::print(" :");
    auto iit = dump_color2instance.find(color);
    if (iit != dump_color2instance.end()) {
      std::print(" instance:{}", iit->second);
    }

    std::print(" :");
    auto sit = color2sources.find(color);
    if (sit != color2sources.end()) {
      for (auto& src : sit->second) {
        std::print(" {}", src);
      }
    }

    std::print(" :");
    auto wit = color2wnames.find(color);
    if (wit != color2wnames.end()) {
      for (auto& wn : wit->second) {
        std::print(" {}", wn);
      }
    }

    std::print("\n");
  }

  std::cout << "---- fin ----\n";
}
