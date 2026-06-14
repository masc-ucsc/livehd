// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_path.hpp"

#include <algorithm>
#include <print>

#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"

namespace livehd::color {

using livehd::graph_util::has_name;
using livehd::graph_util::node_name_of;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

// clk/clock/rst/reset are global signals shared across many flops; treating
// them as boundaries creates false aliases, so skip them.
static bool is_special_wire_name(std::string_view wn) {
  if (wn.empty()) {
    return false;
  }
  std::string lower(wn);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  return lower.starts_with("clk") || lower.starts_with("clock") || lower.starts_with("rst") || lower.starts_with("reset");
}

static bool has_real_wname(const hhds::Pin_class& dpin) {
  auto wn = pin_name_of(dpin);
  return !wn.empty() && wn[0] != '_' && !is_special_wire_name(wn);
}

static bool is_alias_passthrough_node(const hhds::Node_class& node) {
  auto op = type_op_of(node);
  return op == Ntype_op::Or || op == Ntype_op::Get_mask;
}

static bool add_color(std::vector<int>& colors, int color) {
  if (std::find(colors.begin(), colors.end(), color) != colors.end()) {
    return false;
  }
  colors.push_back(color);
  return true;
}

// Loop-last regular nodes are the flops/registers/memories we seed from.
static bool is_flop_like(const hhds::Node_class& node) { return is_partitionable(node) && node.is_loop_last(); }

std::vector<std::string> Color_path::parse_instance_names(std::string_view instance_csv) {
  std::vector<std::string> names;
  for (auto token : absl::StrSplit(instance_csv, ',')) {
    auto trimmed = absl::StripAsciiWhitespace(token);
    if (!trimmed.empty()) {
      names.emplace_back(trimmed);
    }
  }
  return names;
}

Color_path::Color_path(Color_opts opts_, std::string_view instance_csv)
    : opts(opts_), instance_names(parse_instance_names(instance_csv)) {}

bool Color_path::should_stop_fwd(const hhds::Node_class& node, int color) const {
  if (instance_names.empty()) {
    if (is_alias_passthrough_node(node)) {
      return false;
    }
    for (const auto& oe : node.out_edges()) {
      if (has_real_wname(oe.driver)) {
        return true;
      }
    }
    return false;
  }

  if (!has_name(node)) {
    return false;
  }
  auto it = color2instance.find(color);
  if (it == color2instance.end()) {
    return true;
  }
  auto prefix = absl::StrCat("_", it->second);
  return !std::string_view(node_name_of(node)).starts_with(prefix);
}

void Color_path::propagate_fwd(const hhds::Node_class& node, int color) {
  for (const auto& e : node.out_edges()) {
    auto succ = e.sink.get_master_node();
    if (!is_partitionable(succ)) {
      continue;
    }
    if (succ.is_loop_last()) {
      add_color(node2colors[succ], color);  // back-to-back flop: share color, stop
      continue;
    }
    if (!add_color(node2colors[succ], color)) {
      continue;  // already propagated here
    }
    if (should_stop_fwd(succ, color)) {
      continue;
    }
    propagate_fwd(succ, color);
  }
}

void Color_path::propagate_bwd(const hhds::Node_class& node, int color) {
  for (const auto& e : node.inp_edges()) {
    auto pred = e.driver.get_master_node();
    if (!is_partitionable(pred)) {
      continue;
    }
    if (pred.is_loop_last()) {
      add_color(node2colors[pred], color);  // back-to-back flop: share color
      continue;
    }
    if (!add_color(node2colors[pred], color)) {
      continue;
    }
    bool has_wname = false;
    for (const auto& oe : pred.out_edges()) {
      if (has_real_wname(oe.driver)) {
        has_wname = true;
        break;
      }
    }
    if (has_wname && !is_alias_passthrough_node(pred)) {
      continue;
    }
    propagate_bwd(pred, color);
  }
}

void Color_path::label(hhds::Graph* g) {
  last_free_id = 0;
  node2colors.clear();
  color2instance.clear();
  instance2color.clear();

  if (!instance_names.empty()) {
    absl::flat_hash_set<std::string_view> instance_set;
    for (const auto& name : instance_names) {
      instance_set.insert(name);
    }
    for (auto node : g->forward_class()) {
      if (!is_partitionable(node) || !has_name(node) || !instance_set.contains(node_name_of(node))) {
        continue;
      }
      auto& colors = node2colors[node];
      int   color  = colors.empty() ? get_free_id() : colors.front();
      if (colors.empty()) {
        colors.push_back(color);
      }
      color2instance.insert_or_assign(color, std::string{node_name_of(node)});
      instance2color.insert_or_assign(std::string{node_name_of(node)}, color);
      propagate_fwd(node, color);
    }
  } else {
    for (auto node : g->forward_class()) {
      if (!is_flop_like(node)) {
        continue;
      }
      auto& colors = node2colors[node];
      int   color  = colors.empty() ? get_free_id() : colors.front();
      if (colors.empty()) {
        colors.push_back(color);
      }
      propagate_bwd(node, color);
      propagate_fwd(node, color);
    }
  }

  // Collapse the per-node color vectors to one id (first color) for the write.
  Node2Id node2id;
  node2id.reserve(node2colors.size());
  for (auto& [n, colors] : node2colors) {
    if (!colors.empty() && is_partitionable(n)) {
      node2id[n] = colors.front();
    }
  }

  int n_colors = apply_coloring(g, node2id, opts);
  if (opts.verbose) {
    std::print("[color.path] {} -> {} paths\n", g->get_name(), n_colors);
  }
}

}  // namespace livehd::color
