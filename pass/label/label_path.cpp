// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "label_path.hpp"

#include <algorithm>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "cell.hpp"

// Wire names like clk/clock/rst/reset are global signals shared across many
// flops.  Treating them as boundaries creates false aliases, so skip them.
static bool is_special_wire_name(std::string_view wn) {
  if (wn.empty()) {
    return false;
  }
  std::string lower(wn);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  return lower.starts_with("clk") || lower.starts_with("clock") || lower.starts_with("rst") || lower.starts_with("reset");
}

// Check if a driver pin has a real wname (not a temporary _nXXX name and not a special signal)
static bool has_real_wname(const Node_pin& dpin) {
  if (!dpin.has_name()) {
    return false;
  }
  auto wn = dpin.get_wire_name();
  return !wn.empty() && wn[0] != '_' && !is_special_wire_name(wn);
}

// Returns true if color was newly added, false if already present
static bool add_color(std::vector<int>& colors, int color) {
  if (std::find(colors.begin(), colors.end(), color) != colors.end()) {
    return false;
  }
  colors.push_back(color);
  return true;
}

static std::string sanitize_dump_token(std::string_view text) {
  std::string out(text);
  std::replace(out.begin(), out.end(), ',', '.');
  return out;
}

static std::string_view get_bus_base_name(std::string_view text) {
  if (text.empty() || text.back() != ']') {
    return {};
  }

  auto pos = text.rfind('[');
  if (pos == std::string_view::npos || pos == 0) {
    return {};
  }

  return text.substr(0, pos);
}

static std::string canonicalize_wire_name(std::string_view wname, const absl::flat_hash_set<std::string>& memory_names) {
  auto base = get_bus_base_name(wname);
  if (!base.empty() && memory_names.contains(std::string(base))) {
    return std::string(base);
  }

  return std::string(wname);
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
  return !std::string_view(node.get_name()).starts_with(prefix);
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
  instance2color.clear();

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
      instance2color.insert_or_assign(node.get_name(), color);

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
  absl::flat_hash_map<int, absl::flat_hash_set<std::string>> color2nodenames;
  absl::flat_hash_set<std::string>                           memory_names;

  for (auto node : g->fast(hier)) {
    if (node.is_type(Ntype_op::Memory) && node.has_name()) {
      memory_names.insert(node.get_name());
    }
  }

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

      if (node.has_name()) {
        color2nodenames[color].insert(node.get_name());
      }

      // Collect wnames from output edges (skip special signals like clk/reset)
      for (const auto& e : node.out_edges()) {
        auto wn = e.driver.get_wire_name();
        if (!wn.empty() && wn[0] != '_' && !is_special_wire_name(wn)) {
          color2wnames[color].insert(canonicalize_wire_name(wn, memory_names));
        }
      }

      // Collect source files (with location for flops)
      auto src = node.get_source();
      if (!src.empty()) {
        if (node.is_type_loop_last() && node.has_loc()) {
          auto [loc1, loc2] = node.get_loc();
          color2sources[color].insert(absl::StrCat(src, ":", loc1, "-", loc2));
        } else {
          color2sources[color].insert(src);
        }
      }
    }
  }

  auto dump_color_entry = [&](int color, std::string_view inst_name) {
    std::print("{}", color);
    std::print(" ,");
    if (!inst_name.empty()) {
      std::print(" {}", sanitize_dump_token(inst_name));
    }

    std::print(" ,");
    auto sit = color2sources.find(color);
    if (sit != color2sources.end()) {
      for (auto& src : sit->second) {
        std::print(" {}", sanitize_dump_token(src));
      }
    }

    std::print(" ,");
    auto wit = color2wnames.find(color);
    if (wit != color2wnames.end()) {
      for (auto& wn : wit->second) {
        std::print(" {}", sanitize_dump_token(wn));
      }
    }

    std::print(" ,");
    auto nit = color2nodenames.find(color);
    if (nit != color2nodenames.end()) {
      for (auto& nname : nit->second) {
        std::print(" {}", sanitize_dump_token(nname));
      }
    }

    std::print("\n");
  };

  if (!instance_names.empty()) {
    // Dump one entry per instance name (even if they share the same color)
    for (const auto& iname : instance_names) {
      auto it = instance2color.find(iname);
      if (it == instance2color.end()) {
        continue;
      }
      dump_color_entry(it->second, iname);
    }
  } else {
    for (auto color : all_colors) {
      auto        iit       = dump_color2instance.find(color);
      std::string inst_name = (iit != dump_color2instance.end()) ? iit->second : std::string{};
      dump_color_entry(color, inst_name);
    }
  }

  std::cout << "---- fin ----\n";
}
