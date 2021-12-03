//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <queue>
#include <string>

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

void pass_submatch::do_work(Lgraph *g) {
  Lbench b("pass.submatch." + g->get_name().to_s());
  find_mffc_group(g);
  find_subs(g);
}

uint32_t pass_submatch::submatch_depth = 15;

void pass_submatch::work(Eprp_var &var) {
  pass_submatch p(var);

  if (var.dict.count("depth")) {
    submatch_depth = var.dict["depth"].to_i();
  }

  for (const auto &g : var.lgs) {
    fmt::print("finding subgraphs for graph {}...\n", g->get_name());
    p.do_work(g);
  }
}

uint64_t pass_submatch::hash_mffc_root(Node n) {
  return hash_node(n);
}

uint64_t pass_submatch::hash_mffc_node(Node n_driver, uint64_t h_sink, Port_ID pid) {
  uint64_t i_hash[3] = { h_sink, hash_node(n_driver), static_cast<uint64_t>(pid) };
  return mmap_lib::woothash64(i_hash, 24);
}

uint64_t pass_submatch::hash_mffc_leaf(uint64_t h_sink, Port_ID pid) {
  uint64_t i_hash[2] = { h_sink, static_cast<uint64_t>(pid) };
  return mmap_lib::woothash64(i_hash, 16);
}

uint64_t pass_submatch::hash_node(Node n) {
  uint64_t h;
  std::vector<uint16_t> i_hash;
  i_hash.reserve(n.get_num_inp_edges());
  for (auto e : n.inp_edges()) {
    i_hash.push_back(static_cast<uint16_t>(e.sink.get_pid()));
  }
  h = mmap_lib::woothash64(i_hash.data(), i_hash.size() * 2);
  h = mmap_lib::woothash64(&h, 8, static_cast<uint64_t>(n.get_type_op()) & 0xFFFF);
  return h;
}

uint32_t pass_submatch::group_score(uint32_t group_size, uint32_t num_nodes) {
  return group_size * group_size * num_nodes;
}

void pass_submatch::find_mffc_group(Lgraph *g) {
  fmt::print("0 - Find MFFCs\n");

  uint32_t mffc_id = 0;
  uint32_t max_mffc_depth = 0;

  struct FringeNode {
    Node::Compact nc;
    uint64_t h;
    uint32_t depth;
  };

  struct MFFCRoot {
    Node::Compact nc;
    uint64_t h;
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

  absl::flat_hash_set<Node::Compact> mffc_root_set;
  std::vector<MFFCRoot> mffc_root;
  std::vector<std::vector<MFFCTree>> mffc_depth_tree;
  absl::flat_hash_map<Node::Compact,MFFCNode> mffc;

  g->each_graph_output([&](const Node_pin &pin) {
    auto node = pin.get_node();
    auto h_root = hash_mffc_root(node);
    mffc[node.get_compact()] = {mffc_id++, h_root, 0};
    mffc_root.push_back({node.get_compact(), h_root});
    mffc_root_set.insert(node.get_compact());
  });

  for (auto &node : g->forward()) {
    if (node.get_num_out_edges() == 1) continue;
    auto h_root = hash_mffc_root(node);
    mffc[node.get_compact()] = {mffc_id++, h_root, 0};
    mffc_root.push_back({node.get_compact(), h_root});
    mffc_root_set.insert(node.get_compact());
  }

  for (uint32_t id = 0; id < mffc_id; ++id) {
    uint32_t mffc_size = 1;
    std::queue<FringeNode> fringe;
    fringe.push({mffc_root[id].nc, mffc_root[id].h, 0});
    mffc_depth_tree.push_back({{mffc_root[id].h, 1}});
    for (uint32_t mffc_depth = 1; fringe.size() > 0; ++mffc_depth) {
      std::vector<uint64_t> i_hash;
      while (fringe.size()) {
        auto nc_sink = fringe.front().nc;
        auto depth = fringe.front().depth;
        auto h_sink = fringe.front().h;
        if (depth+1 > mffc_depth) break;
        fringe.pop();
        for (auto e : nc_sink.get_node(g).inp_edges()) {
          auto nc_driver = e.driver.get_node().get_compact();
          if (mffc_root_set.contains(nc_driver) | mffc.contains(nc_driver)) continue;
          auto h_driver = hash_mffc_node(e.driver.get_node(), h_sink, e.sink.get_pid());
          mffc[nc_driver] = {mffc[nc_sink].id, h_driver, depth+1};
          fringe.push({nc_driver, h_driver, depth+1});
          i_hash.push_back(h_driver);
        }
        mffc_size++;
      }
      if (i_hash.empty()) break;
      std::sort(i_hash.begin(), i_hash.end());
      uint64_t h_mffc = mmap_lib::woothash64(i_hash.data(), i_hash.size() * 8);
      h_mffc ^= mffc_depth_tree[id].back().h;
      mffc_depth_tree[id].push_back({h_mffc, mffc_size});
      max_mffc_depth = std::max(max_mffc_depth, mffc_depth);
    }
  }

  struct MFFCGroup {
    uint32_t depth;
    uint32_t tree_size;
    absl::flat_hash_set<uint32_t> mffc_set;
  };

  uint32_t group_id = 0;
  std::vector<uint32_t> mffc_group_id;
  absl::flat_hash_set<uint32_t> ungrouped;
  absl::flat_hash_map<uint64_t,uint32_t> hash2group_id;
  absl::flat_hash_map<uint32_t,MFFCGroup> mffc_group;
  absl::flat_hash_map<uint32_t,uint32_t> mffc_id2group_id;
  for (uint32_t id = 0; id < mffc_id; ++id) {
    uint32_t d = mffc_depth_tree[id].size() - 1;
    uint64_t h = mffc_depth_tree[id][d].h;
    uint32_t tree_size = mffc_depth_tree[id][d].size;
    if (tree_size > 100) ungrouped.insert(id);
    if (hash2group_id.contains(h)) {
      mffc_group[hash2group_id[h]].mffc_set.insert(id);
    } else {
      hash2group_id[h] = group_id;
      mffc_group[group_id] = {d, tree_size, {id}};
      ++group_id;
    }
  }

  fmt::print("#MFFCs - {}\n", mffc_id);
  for (auto &[id, info]: mffc_group) {
    fmt::print("Group #{} - {} x {}\n", id, info.tree_size, info.mffc_set.size());
  }
}

void pass_submatch::find_subs(Lgraph *g) {
  // Topological Sort
  fmt::print("1 - Topological Sort\n");
  std::vector<Node::Compact> sorted_compact_nodes;
  absl::flat_hash_set<Node::Compact> visited;
  std::queue<Node::Compact> node_queue;
  g->each_graph_output([&](const Node_pin &pin) {
    node_queue.push(pin.get_node().get_compact());
    while (!node_queue.empty()) {
      auto node = Node(g, node_queue.front());
      node_queue.pop();
      if (visited.count(node.get_compact())) continue;
      sorted_compact_nodes.emplace_back(node.get_compact());
      visited.insert(node.get_compact());
      for (auto e : node.inp_edges()) {
        auto node_drv = e.driver.get_node();
        node_queue.push(node_drv.get_compact());
      }
    }
  });
  // FIXME: Backward iterator is not working
  // for (const auto &node : g->backward()) {
  //  fmt::print("node = {}\n", node.debug_name());
  //   sorted_compact_nodes.emplace_back(node.get_compact());
  // }

  std::reverse(sorted_compact_nodes.begin(), sorted_compact_nodes.end());

  // Construct
  // (1) Node <-> (Depth, Hash) Map  : Query node for hash
  // (2) Depth - (Hash <-> Node) Map : Query equivalence trees with given depths
  // Time/Space Complexity = O(V x DEPTH)
  fmt::print("2 - Construct Depth Map\n");
  absl::flat_hash_map<Node::Compact, std::vector<uint64_t>> node2depth_hash;
  std::vector<absl::flat_hash_map<uint64_t, std::vector<Node::Compact>>> depth_hash2node;
  for (const auto &compact_node : sorted_compact_nodes) {
    auto node = Node(g, compact_node);
    for (uint64_t depth = 0; depth < submatch_depth; ++depth) {
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
      if (depth != max_depth) break;
      std::sort(i_hash.begin(), i_hash.end());
      uint64_t h = mmap_lib::woothash64(i_hash.data(), i_hash.size() * 8);
      uint64_t n = static_cast<uint64_t>(node.get_type_op());
      h = mmap_lib::waterhash(&n, 4, h & 0xFFFF);
      if (depth == 0) {
        node2depth_hash[node.get_compact()] = {h};
      } else {
        node2depth_hash[node.get_compact()].emplace_back(h);
      }
      if (depth_hash2node.size() <= depth) {
        depth_hash2node.emplace_back(absl::flat_hash_map<uint64_t, std::vector<Node::Compact>>());
      }
      if (depth_hash2node[depth].count(h)) {
        depth_hash2node[depth][h].emplace_back(node.get_compact());
      } else {
        depth_hash2node[depth][h] = {node.get_compact()};
      }
    }
  }
  
  // Construct Node -> (Height,(Root Node,Hash)) Map
  // Time/Space Complexity = O(V x DEPTH)
  fmt::print("3 - Construct Height Map\n");
  struct Root_hash {
    Node::Compact root;
    uint64_t hash;
    Root_hash(Node::Compact r, uint64_t h) : root(r), hash(h) {}
  };

  absl::flat_hash_map<Node::Compact, std::vector<Root_hash>> node2height_hash;
  std::vector<absl::flat_hash_map<uint64_t, std::vector<Node::Compact>>> height_hash2node;
  for (const auto& compact_node : sorted_compact_nodes) {
    bool has_output = true;
    Node node = Node(g, compact_node);
    uint64_t pid;
    uint64_t n = static_cast<uint64_t>(node.get_type_op());
    uint64_t h = mmap_lib::woothash64(&n, 8);
    node2height_hash[compact_node] = {Root_hash(compact_node, h)};
    for (uint64_t height = 1; has_output; ++height) {
      has_output = false;
      // Only trace one output edge
      // TODO: Better way to get the first out_edges()?
      for (auto e : node.out_edges()) {
        node = e.sink.get_node();
        pid = e.sink.get_pid();
        has_output = true;
        break;
      }
      if (!has_output) break;
      if (node2depth_hash[node.get_compact()].size() < height) break;
      h ^= node2depth_hash[node.get_compact()][height-1];
      h = mmap_lib::waterhash(&h, 4, pid & 0xFFFF);
      node2height_hash[compact_node].emplace_back(Root_hash(node.get_compact(), h));

      if (height_hash2node.size() < height) {
        height_hash2node.emplace_back(absl::flat_hash_map<uint64_t, std::vector<Node::Compact>>());
      }
      if (height_hash2node[height-1].count(h)) {
        height_hash2node[height-1][h].emplace_back(compact_node);
      } else {
        height_hash2node[height-1][h] = {compact_node};
      }
    }
  }
  
  // Iterative Matching
  // Heuristic:
  // (1) Pick trees with the highest score (#shared * depth^2) as primary candidate trees
  // (2) Add matching subtrees while traversing the primary candidate trees
  // (3) Commit matching trees
  // (4) Mark nodes in matching trees as "used"
  fmt::print("4 - Iterative Matching\n");
  const int MAX_ITER = 1;
  for (size_t iter = 0; iter < MAX_ITER; ++iter) { 
    // Step (1) 
    int best_score = 0;
    uint64_t h_best;
    uint8_t d_best;
    for (size_t depth = 1; depth < depth_hash2node.size(); ++depth) {
      for (const auto& [hash, vec] : depth_hash2node[depth]) {
        int score = (vec.size() - 1) * depth * depth;
        if (score >= best_score) {
          h_best = hash;
          d_best = depth;
          best_score = score;
        }
      }
    }
    if (best_score == 0) {
      break;
    }
    fmt::print("Candidate - D={} : {}\n", d_best, depth_hash2node[d_best][h_best].size());

    // Step (2)
    absl::flat_hash_set<Node::Compact> root_set;
    absl::flat_hash_set<Node::Compact> global_node_set;
    absl::flat_hash_set<Node::Compact> shared_node_set;

    for (auto root : depth_hash2node[d_best][h_best]) {
      root_set.insert(root);
    }

    for (auto root : root_set) {
      absl::flat_hash_set<Node::Compact> tree_node_set; 
      std::function<void(Node::Compact,int)> traverse_tree = [&](Node::Compact nc, int depth) -> void {
        if (depth > d_best) return;
        if (tree_node_set.count(nc)) return;
        tree_node_set.insert(nc);
        if (global_node_set.count(nc)) {
          shared_node_set.insert(nc);
        }
        //for (int i = 0; i < depth; ++i) fmt::print(" ");
        //fmt::print("{}\n", Node(g, nc).debug_name());
        for (auto e : Node(g, nc).inp_edges()) {
          traverse_tree(e.driver.get_node().get_compact(), depth+1);
        }
      };
      traverse_tree(root, 0);
      for (auto nc : tree_node_set) global_node_set.insert(nc);
    }

    fmt::print("#Nodes:         {}\n", sorted_compact_nodes.size());
    fmt::print("#Nodes covered: {}\n", global_node_set.size());
    fmt::print("#Nodes shared:  {}\n", shared_node_set.size());
    
    absl::flat_hash_map<Node::Compact, Node::Compact> leaf2root;
    absl::flat_hash_map<uint64_t, absl::flat_hash_set<Node::Compact>> hash2leaf;
    for (const auto &[nc, vec] : node2height_hash) {
      for (size_t d = 0; d < d_best && d < vec.size(); ++d) {
        if (shared_node_set.count(nc)) continue;
        if (root_set.count(vec[d].root)) {
          leaf2root[nc] = vec[d].root;
          hash2leaf[vec[d].hash].insert(nc);
        }
      }
    }

  for (const auto &[hash, leaves] : hash2leaf) {
    fmt::print("Subtree: {} -> {}", hash, leaves.size());
    fmt::print("\n");
  }

    //fmt::print("\nMatched Subtrees\n");
    //for (const auto &[hash, vec] : hash2root_leaf) {
    //  if (vec.size() < 2) continue;
    //  fmt::print(" >");
    //  for (const auto &rl : vec) {
    //    fmt::print("( {} -> {}({}) )", Node(g, rl.root).debug_name(), Node(g, rl.leaf).debug_name(), rl.depth);
    //  }
    //  fmt::print("\n");
    //}
  }
}
