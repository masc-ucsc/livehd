// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "color_common.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace livehd::color {

namespace {

using livehd::graph_util::del_color;
using livehd::graph_util::set_color;

// Union-find over node identities for the continuous (per-region) split.
class Union_find {
public:
  hhds::Node_class find(const hhds::Node_class& n) {
    auto it = parent_.find(n);
    if (it == parent_.end()) {
      parent_[n] = n;
      return n;
    }
    if (it->second == n) {
      return n;
    }
    auto root      = find(it->second);
    parent_[n]     = root;  // path compression
    return root;
  }
  void merge(const hhds::Node_class& a, const hhds::Node_class& b) {
    auto ra = find(a);
    auto rb = find(b);
    if (ra != rb) {
      parent_[ra] = rb;
    }
  }

private:
  absl::flat_hash_map<hhds::Node_class, hhds::Node_class> parent_;
};

// Re-number node2id so that each maximal connected region of equal-id nodes
// gets a distinct fresh id. Two same-id nodes are connected when an edge runs
// directly between them.
Node2Id split_continuous(hhds::Graph* g, const Node2Id& node2id) {
  Union_find uf;
  for (auto n : g->forward_class()) {
    if (!node2id.contains(n)) {
      continue;
    }
    auto id = node2id.at(n);
    uf.find(n);  // ensure present even if isolated
    for (const auto& e : n.out_edges()) {
      auto snode = e.sink.get_master_node();
      auto it    = node2id.find(snode);
      if (it != node2id.end() && it->second == id) {
        uf.merge(n, snode);
      }
    }
  }

  absl::flat_hash_map<hhds::Node_class, int> root2new;
  int                                        next = 1;
  Node2Id                                    out;
  out.reserve(node2id.size());
  for (const auto& [n, id] : node2id) {
    (void)id;
    auto root = uf.find(n);
    auto it   = root2new.find(root);
    if (it == root2new.end()) {
      it = root2new.emplace(root, next++).first;
    }
    out[n] = it->second;
  }
  return out;
}

}  // namespace

int apply_coloring(hhds::Graph* g, const Node2Id& node2id_in, const Color_opts& opts) {
  const Node2Id  local    = opts.continuous ? split_continuous(g, node2id_in) : node2id_in;
  const Node2Id& node2id  = opts.continuous ? local : node2id_in;

  absl::flat_hash_set<int> seen_ids;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto it = node2id.find(n);
    if (it != node2id.end()) {
      set_color(n, it->second);
      seen_ids.insert(it->second);
    } else if (!opts.keep_colored) {
      del_color(n);
    }
  }
  return static_cast<int>(seen_ids.size());
}

void clear_coloring(hhds::Graph* g) {
  for (auto n : g->forward_class()) {
    livehd::graph_util::del_color(n);  // flat color (class context)
  }
  // The per-instance hier color is a hier_storage attr; its entries cannot be
  // reached from a class-context node handle, so clear the whole store directly.
  g->attr_clear(livehd::attrs::hier_color);
  del_coloring_info(g);
}

void set_coloring_info(hhds::Graph* g, const std::string& json) { g->get_input_node().attr(livehd::attrs::coloring_info).set(json); }

std::string get_coloring_info(const hhds::Graph* g) {
  auto a = g->get_input_node().attr(livehd::attrs::coloring_info);
  return a.has() ? std::string{a.get()} : std::string{};
}

void del_coloring_info(hhds::Graph* g) {
  auto a = g->get_input_node().attr(livehd::attrs::coloring_info);
  if (a.has()) {
    a.del();
  }
}

std::string build_coloring_info_json(hhds::Graph* g, std::string_view top, std::string_view algorithm,
                                     std::string_view params_json) {
  // Gather the active flat coloring on g's regular nodes.
  Node2Id node2id;
  for (auto n : g->forward_class()) {
    if (!is_partitionable(n)) {
      continue;
    }
    auto c = livehd::graph_util::node_color_of(n);
    if (c != NO_COLOR) {
      node2id[n] = c;
    }
  }

  // Region count per color = connected components of same-color nodes.
  Union_find                     uf;
  absl::flat_hash_map<int, int>  color_node_cnt;
  for (auto n : g->forward_class()) {
    auto it = node2id.find(n);
    if (it == node2id.end()) {
      continue;
    }
    color_node_cnt[it->second]++;
    uf.find(n);
    for (const auto& e : n.out_edges()) {
      auto snode = e.sink.get_master_node();
      auto sit   = node2id.find(snode);
      if (sit != node2id.end() && sit->second == it->second) {
        uf.merge(n, snode);
      }
    }
  }
  // Count distinct roots per color.
  absl::flat_hash_map<int, absl::flat_hash_set<hhds::Node_class>> color_roots;
  for (const auto& [n, id] : node2id) {
    color_roots[id].insert(uf.find(n));
  }

  std::string out = "{";
  out += "\"schema_version\":1,";
  out += std::format("\"top\":\"{}\",", top);
  out += std::format("\"algorithm\":\"{}\",", algorithm);
  out += std::format("\"params\":{},", params_json.empty() ? std::string_view{"{}"} : params_json);
  out += "\"colors\":{";
  bool first = true;
  // Deterministic order: sort color ids.
  std::vector<int> ids;
  ids.reserve(color_node_cnt.size());
  for (const auto& [id, cnt] : color_node_cnt) {
    (void)cnt;
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  for (int id : ids) {
    if (!first) {
      out += ",";
    }
    first        = false;
    int regions  = static_cast<int>(color_roots[id].size());
    int instcnt  = color_node_cnt[id];
    out += std::format("\"{}\":{{\"name\":\"{}__c{}\",\"region_cnt\":{},\"instance_cnt\":{}}}", id, top, id, regions, instcnt);
  }
  out += "}}";
  return out;
}

}  // namespace livehd::color
