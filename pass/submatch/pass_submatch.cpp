//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_submatch.hpp"

#include <algorithm>
#include <format>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hhds/graph.hpp"
#include "node_util.hpp"
#include "str_tools.hpp"
#include "waterhash.hpp"
#include "woothash.hpp"

using livehd::graph_util::type_op_of;

static Pass_plugin sample("pass_submatch", pass_submatch::setup);

void pass_submatch::setup() {
  Eprp_method m1("pass.submatch", "Find identical subgraphs", &pass_submatch::work);

  register_pass(m1);
}

pass_submatch::pass_submatch(const Eprp_var& var) : Pass("pass.submatch", var) {}

void pass_submatch::do_work(hhds::Graph* g) {
  find_mffc_group(g);
  find_subs(g);
}

uint32_t pass_submatch::submatch_depth = 15;

void pass_submatch::work(Eprp_var& var) {
  pass_submatch p(var);

  if (var.dict.count("depth")) {
    submatch_depth = str_tools::to_i(var.dict["depth"]);
  }

  for (const auto& g : var.graphs) {
    auto gio  = g->get_io();
    auto name = gio ? std::string{gio->get_name()} : std::string{"<anon>"};
    std::print("finding subgraphs for graph {}...\n", name);
    p.do_work(g.get());
  }
}

uint64_t pass_submatch::hash_mffc_root(hhds::Node_class n) { return hash_node(n); }

uint64_t pass_submatch::hash_mffc_node(hhds::Node_class n_driver, uint64_t h_sink, hhds::Port_id pid) {
  uint64_t i_hash[3] = {h_sink, hash_node(n_driver), static_cast<uint64_t>(pid)};
  return lh::woothash64(i_hash, 24);
}

uint64_t pass_submatch::hash_node(hhds::Node_class n) {
  uint64_t              h;
  std::vector<uint16_t> i_hash;
  auto                  edges = n.inp_edges();
  i_hash.reserve(edges.size());
  for (const auto& e : edges) {
    i_hash.push_back(static_cast<uint16_t>(e.sink.get_port_id()));
  }
  h = lh::woothash64(i_hash.data(), i_hash.size() * 2);
  h = lh::woothash64(&h, 8, static_cast<uint64_t>(type_op_of(n)) & 0xFFFF);
  return h;
}

namespace {

// Walk the immediate driver of every graph output. Mirrors the old
// `each_graph_output` callback shape used by submatch.
template <typename F>
void each_graph_output_driver(hhds::Graph* g, F&& fn) {
  auto gio = g->get_io();
  if (!gio) {
    return;
  }
  for (const auto& d : gio->get_output_pin_decls()) {
    auto out_pin = g->get_output_pin(d.name);
    if (out_pin.is_invalid()) {
      continue;
    }
    auto edges = out_pin.inp_edges();
    if (edges.empty()) {
      continue;
    }
    fn(edges.front().driver);
  }
}

}  // namespace

void pass_submatch::find_mffc_group(hhds::Graph* g) {
  std::cout << "0 - Find MFFCs\n";

  uint32_t mffc_id        = 0;
  uint32_t max_mffc_depth = 0;

  struct FringeNode {
    hhds::Class_index ci;
    uint64_t          h;
    uint32_t          depth;
  };

  struct MFFCRoot {
    hhds::Class_index ci;
    uint64_t          h;
  };

  struct MFFCNode {
    uint32_t id;
    uint64_t h;
    uint32_t depth;
  };

  struct MFFCLeaf {
    uint32_t id;
    uint64_t h;
  };

  struct MFFCTree {
    uint64_t h;
    uint32_t size;
  };

  absl::flat_hash_set<hhds::Class_index>           mffc_root_set;
  std::vector<MFFCRoot>                            mffc_root;
  std::vector<std::vector<MFFCTree>>               mffc_depth_tree;
  absl::flat_hash_map<hhds::Class_index, MFFCNode> mffc;

  each_graph_output_driver(g, [&](const hhds::Pin_class& driver) {
    auto node                       = driver.get_master_node();
    auto ci                         = node.get_class_index();
    auto h_root                     = hash_mffc_root(node);
    mffc[ci]                        = {mffc_id++, h_root, 0};
    mffc_root.push_back({ci, h_root});
    mffc_root_set.insert(ci);
  });

  for (auto node : g->forward_class()) {
    if (node.out_edges().size() == 1) {
      continue;
    }
    auto ci                         = node.get_class_index();
    auto h_root                     = hash_mffc_root(node);
    mffc[ci]                        = {mffc_id++, h_root, 0};
    mffc_root.push_back({ci, h_root});
    mffc_root_set.insert(ci);
  }

  for (uint32_t id = 0; id < mffc_id; ++id) {
    uint32_t               mffc_size = 1;
    std::queue<FringeNode> fringe;
    fringe.push({mffc_root[id].ci, mffc_root[id].h, 0});
    mffc_depth_tree.push_back({{mffc_root[id].h, 1}});
    for (uint32_t mffc_depth = 1; fringe.size() > 0; ++mffc_depth) {
      std::vector<uint64_t> i_hash;
      while (fringe.size()) {
        auto ci_sink = fringe.front().ci;
        auto depth   = fringe.front().depth;
        auto h_sink  = fringe.front().h;
        if (depth + 1 > mffc_depth) {
          break;
        }
        fringe.pop();
        for (auto e : g->get_node(ci_sink).inp_edges()) {
          auto ci_driver = e.driver.get_master_node().get_class_index();
          if (mffc_root_set.contains(ci_driver) || mffc.contains(ci_driver)) {
            continue;
          }
          auto h_driver   = hash_mffc_node(e.driver.get_master_node(), h_sink, e.sink.get_port_id());
          mffc[ci_driver] = {mffc[ci_sink].id, h_driver, depth + 1};
          fringe.push({ci_driver, h_driver, depth + 1});
          i_hash.push_back(h_driver);
        }
        mffc_size++;
      }
      if (i_hash.empty()) {
        break;
      }
      std::sort(i_hash.begin(), i_hash.end());
      uint64_t h_mffc = lh::woothash64(i_hash.data(), i_hash.size() * 8);
      h_mffc ^= mffc_depth_tree[id].back().h;
      mffc_depth_tree[id].push_back({h_mffc, mffc_size});
      max_mffc_depth = std::max(max_mffc_depth, mffc_depth);
    }
  }

  struct MFFCGroup {
    uint32_t                      depth;
    uint32_t                      tree_size;
    absl::flat_hash_set<uint32_t> mffc_set;
  };

  uint32_t                                 group_id = 0;
  std::vector<uint32_t>                    mffc_group_id;
  absl::flat_hash_set<uint32_t>            ungrouped;
  absl::flat_hash_map<uint64_t, uint32_t>  hash2group_id;
  absl::flat_hash_map<uint32_t, MFFCGroup> mffc_group;
  absl::flat_hash_map<uint32_t, uint32_t>  mffc_id2group_id;
  for (uint32_t id = 0; id < mffc_id; ++id) {
    uint32_t d         = mffc_depth_tree[id].size() - 1;
    uint64_t h         = mffc_depth_tree[id][d].h;
    uint32_t tree_size = mffc_depth_tree[id][d].size;
    if (tree_size > 100) {
      ungrouped.insert(id);
    }
    if (hash2group_id.contains(h)) {
      mffc_group[hash2group_id[h]].mffc_set.insert(id);
    } else {
      hash2group_id[h]     = group_id;
      mffc_group[group_id] = {d, tree_size, {id}};
      ++group_id;
    }
  }

  std::print("#MFFCs - {}\n", mffc_id);
  for (auto& [id, info] : mffc_group) {
    std::print("Group #{} - {} x {}\n", id, info.tree_size, info.mffc_set.size());
  }
}

void pass_submatch::find_subs(hhds::Graph* g) {
  // Topological Sort: start from drivers of graph outputs and walk back.
  std::cout << "1 - Topological Sort\n";
  std::vector<hhds::Class_index>         sorted_class_nodes;
  absl::flat_hash_set<hhds::Class_index> visited;
  std::queue<hhds::Class_index>          node_queue;
  each_graph_output_driver(g, [&](const hhds::Pin_class& driver) {
    node_queue.push(driver.get_master_node().get_class_index());
    while (!node_queue.empty()) {
      auto ci = node_queue.front();
      node_queue.pop();
      if (visited.count(ci)) {
        continue;
      }
      sorted_class_nodes.emplace_back(ci);
      visited.insert(ci);
      for (auto e : g->get_node(ci).inp_edges()) {
        node_queue.push(e.driver.get_master_node().get_class_index());
      }
    }
  });

  std::reverse(sorted_class_nodes.begin(), sorted_class_nodes.end());

  // Construct
  // (1) Node <-> (Depth, Hash) Map  : Query node for hash
  // (2) Depth - (Hash <-> Node) Map : Query equivalence trees with given depths
  // Time/Space Complexity = O(V x DEPTH)
  std::cout << "2 - Construct Depth Map\n";
  absl::flat_hash_map<hhds::Class_index, std::vector<uint64_t>>              node2depth_hash;
  std::vector<absl::flat_hash_map<uint64_t, std::vector<hhds::Class_index>>> depth_hash2node;
  for (const auto& ci : sorted_class_nodes) {
    auto node = g->get_node(ci);
    for (size_t depth = 0; depth < submatch_depth; ++depth) {
      size_t                max_depth = 0;
      std::vector<uint64_t> i_hash;
      for (auto e : node.inp_edges()) {
        auto drv_ci = e.driver.get_master_node().get_class_index();
        auto it     = node2depth_hash.find(drv_ci);
        if (it == node2depth_hash.end() || depth == 0) {
          i_hash.emplace_back(e.sink.get_port_id());
        } else {
          auto subtree_depth = std::min(it->second.size(), depth);
          i_hash.emplace_back(it->second[subtree_depth - 1] ^ e.sink.get_port_id());
          max_depth = std::max(subtree_depth, max_depth);
        }
      }
      if (depth != max_depth) {
        break;
      }
      std::sort(i_hash.begin(), i_hash.end());
      uint64_t h = lh::woothash64(i_hash.data(), i_hash.size() * 8);
      uint64_t n = static_cast<uint64_t>(type_op_of(node));
      h          = lh::waterhash(&n, 4, h & 0xFFFF);
      if (depth == 0) {
        node2depth_hash[ci].clear();
      }
      node2depth_hash[ci].emplace_back(h);

      if (depth_hash2node.size() <= depth) {
        depth_hash2node.emplace_back();
      }
      if (depth_hash2node[depth].count(h)) {
        depth_hash2node[depth][h].emplace_back(ci);
      } else {
        depth_hash2node[depth][h].clear();
        depth_hash2node[depth][h].emplace_back(ci);
      }
    }
  }

  // Construct Node -> (Height,(Root Node,Hash)) Map
  // Time/Space Complexity = O(V x DEPTH)
  std::cout << "3 - Construct Height Map\n";
  struct Root_hash {
    hhds::Class_index root;
    uint64_t          hash;
    Root_hash(hhds::Class_index r, uint64_t h) : root(r), hash(h) {}
  };

  absl::flat_hash_map<hhds::Class_index, std::vector<Root_hash>>             node2height_hash;
  std::vector<absl::flat_hash_map<uint64_t, std::vector<hhds::Class_index>>> height_hash2node;
  for (const auto& ci : sorted_class_nodes) {
    bool             has_output = true;
    hhds::Node_class node       = g->get_node(ci);
    uint64_t         pid        = 0;
    uint64_t         n          = static_cast<uint64_t>(type_op_of(node));
    uint64_t         h          = lh::woothash64(&n, 8);
    node2height_hash[ci]        = {Root_hash(ci, h)};
    for (uint64_t height = 1; has_output; ++height) {
      has_output = false;
      // Only trace one output edge
      // TODO: Better way to get the first out_edges()?
      for (auto e : node.out_edges()) {
        node       = e.sink.get_master_node();
        pid        = e.sink.get_port_id();
        has_output = true;
        break;
      }
      if (!has_output) {
        break;
      }
      auto node_ci = node.get_class_index();
      if (node2depth_hash[node_ci].size() < height) {
        break;
      }
      h ^= node2depth_hash[node_ci][height - 1];
      h = lh::waterhash(&h, 4, pid & 0xFFFF);
      node2height_hash[ci].emplace_back(Root_hash(node_ci, h));

      if (height_hash2node.size() < height) {
        height_hash2node.emplace_back();
      }
      if (height_hash2node[height - 1].count(h)) {
        height_hash2node[height - 1][h].emplace_back(ci);
      } else {
        height_hash2node[height - 1][h] = {ci};
      }
    }
  }

  // Iterative Matching
  std::cout << "4 - Iterative Matching\n";
  const int MAX_ITER = 1;
  for (size_t iter = 0; iter < MAX_ITER; ++iter) {
    int      best_score = 0;
    uint64_t h_best     = 0;
    uint8_t  d_best     = 0;
    for (size_t depth = 1; depth < depth_hash2node.size(); ++depth) {
      for (const auto& [hash, vec] : depth_hash2node[depth]) {
        int score = (static_cast<int>(vec.size()) - 1) * static_cast<int>(depth) * static_cast<int>(depth);
        if (score >= best_score) {
          h_best     = hash;
          d_best     = static_cast<uint8_t>(depth);
          best_score = score;
        }
      }
    }
    if (best_score == 0) {
      break;
    }
    std::print("Candidate - D={} : {}\n", d_best, depth_hash2node[d_best][h_best].size());

    absl::flat_hash_set<hhds::Class_index> root_set;
    absl::flat_hash_set<hhds::Class_index> global_node_set;
    absl::flat_hash_set<hhds::Class_index> shared_node_set;

    for (auto root : depth_hash2node[d_best][h_best]) {
      root_set.insert(root);
    }

    for (auto root : root_set) {
      absl::flat_hash_set<hhds::Class_index>      tree_node_set;
      std::function<void(hhds::Class_index, int)> traverse_tree = [&](hhds::Class_index ci, int depth) -> void {
        if (depth > d_best) {
          return;
        }
        if (tree_node_set.count(ci)) {
          return;
        }
        tree_node_set.insert(ci);
        if (global_node_set.count(ci)) {
          shared_node_set.insert(ci);
        }
        for (auto e : g->get_node(ci).inp_edges()) {
          traverse_tree(e.driver.get_master_node().get_class_index(), depth + 1);
        }
      };
      traverse_tree(root, 0);
      for (auto ci : tree_node_set) {
        global_node_set.insert(ci);
      }
    }

    std::print("#Nodes:         {}\n", sorted_class_nodes.size());
    std::print("#Nodes covered: {}\n", global_node_set.size());
    std::print("#Nodes shared:  {}\n", shared_node_set.size());

    absl::flat_hash_map<hhds::Class_index, hhds::Class_index>                 leaf2root;
    absl::flat_hash_map<uint64_t, absl::flat_hash_set<hhds::Class_index>>     hash2leaf;
    for (const auto& [ci, vec] : node2height_hash) {
      for (size_t d = 0; d < d_best && d < vec.size(); ++d) {
        if (shared_node_set.count(ci)) {
          continue;
        }
        if (root_set.count(vec[d].root)) {
          leaf2root[ci] = vec[d].root;
          hash2leaf[vec[d].hash].insert(ci);
        }
      }
    }

    for (const auto& [hash, leaves] : hash2leaf) {
      std::print("Subtree: {} -> {}", hash, leaves.size());
      std::cout << "\n";
    }
  }
}
