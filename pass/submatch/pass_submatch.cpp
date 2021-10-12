//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <queue>

#include "pass_submatch.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "waterhash.hpp"
#include "woothash.hpp"

static Pass_plugin sample("pass_submatch", pass_submatch::setup);

void pass_submatch::setup() {
  Eprp_method m1("pass.submatch", mmap_lib::str("Find identical subgraphs"), &pass_submatch::work);

  register_pass(m1);
}

pass_submatch::pass_submatch(const Eprp_var &var) : Pass("pass.submatch", var) {}

void pass_submatch::do_work(Lgraph *g) { find_subs(g); }

void pass_submatch::work(Eprp_var &var) {
  pass_submatch p(var);

  for (const auto &g : var.lgs) {
    fmt::print("finding subgraphs for graph {}...\n", g->get_name());
    p.do_work(g);
  }
}

void pass_submatch::find_subs(Lgraph *g) {
  fmt::print("TODO: implement pass\n");

  const int SUBGRAPH_DEPTH = 10;
  
  // Topological Sort
  std::vector<Node::Compact> sorted_compact_nodes;
  absl::flat_hash_set<Node::Compact> visited;
  std::queue<Node::Compact> node_queue;
  g->each_graph_output([&](const Node_pin &pin) {
    sorted_compact_nodes.emplace_back(pin.get_node().get_compact());
    node_queue.push(pin.get_node().get_compact());
    while (!node_queue.empty()) {
      auto node = Node(g, node_queue.front());
      node_queue.pop();
      sorted_compact_nodes.emplace_back(node.get_compact());
      for (auto e : node.inp_edges()) {
        auto node_drv = e.driver.get_node();
        if (visited.count(node_drv.get_compact())) continue;
        node_queue.push(node_drv.get_compact());
      }
    }
  });
  // FIXME: Backward iterator is not working
  // for (const auto &node : g->backward()) {
  //   fmt::print("node = {}\n", node.debug_name());
  //   sorted_compact_nodes.emplace_back(node.get_compact());
  // }
  std::reverse(sorted_compact_nodes.begin(), sorted_compact_nodes.end());

  // Construct Node <-> (Depth,Hash) Map
  // Complexity = O(V x DEPTH)
  absl::flat_hash_map<Node::Compact, std::vector<uint64_t>> node2depth_hash;
  for (const auto &compact_node : sorted_compact_nodes) {
    auto node = Node(g, compact_node);
    for (uint64_t depth = 0; depth < SUBGRAPH_DEPTH; ++depth) {
      uint64_t max_depth = 0;
      std::vector<uint64_t> i_hash;
      for (auto e : node.inp_edges()) {
        auto it = node2depth_hash.find(e.driver.get_node().get_compact());
        if (it == node2depth_hash.end() || depth == 0) {
          i_hash.emplace_back(e.sink.get_pid());
        } else {
          auto subtree_depth = std::min(it->second.size(), depth);
          i_hash.emplace_back(it->second[subtree_depth-1] ^ e.sink.get_pid());
          max_depth = std::max(subtree_depth, max_depth);
        }
      }
      if (depth != max_depth) {
        break;
      }
      uint64_t h1;
      std::sort(i_hash.begin(), i_hash.end());
      h1 = mmap_lib::woothash64(i_hash.data(), i_hash.size() * 8);
      uint64_t n = static_cast<uint64_t>(node.get_type_op());
      uint64_t h2 = mmap_lib::waterhash(&n, 4, h1 & 0xFFFF);
      if (depth == 0) {
        node2depth_hash[node.get_compact()] = {h2};
      } else {
        node2depth_hash[node.get_compact()].emplace_back(h2);
      }
      fmt::print("Node = {}\tDepth = {}\tHash = {}\n", node.debug_name(), depth, h2);
    }
  }
}
