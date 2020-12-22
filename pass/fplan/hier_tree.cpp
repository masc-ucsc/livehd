#include "hier_tree.hpp"

#include <functional>
#include <random>
#include <stdexcept>  // for std::runtime_error
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "fmt/core.h"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "profile_time.hpp"

// turn an LGraph into a graph suitable for HiReg.
Hier_tree::Hier_tree(Eprp_var& var) : collapsed_gis(), pattern_sets() {
  // if I run this code in lgshell with -c opt on the rocket core, temp.imp_edges hangs.
  // this doesn't happen with a smaller hierarchy like hier_test

  // Hierarchy_tree* root_tree2 = var.lgs[0]->ref_htree();
  // LGraph*         root_lg2   = var.lgs[0];

  // for (auto hidx : root_tree2->depth_preorder()) {
  //   Node temp(root_lg2, hidx, Hardcoded_input_nid);
  //   // std::cout << "creating edges..." << std::endl;
  //   auto edges = temp.inp_edges();
  //   // std::cout << "done creating edges." << std::endl;
  // }

  if (var.lgs.size() > 1) {
    throw std::runtime_error("cannot find root hierarchy, did you pass more than one lgraph?");
  }

  if (!var.lgs.size()) {
    throw std::runtime_error("no hierarchies found!");
  }

  auto t = profile_time::timer();
  fmt::print("    setting up tree...");
  t.start();

  Hierarchy_tree* root_tree = var.lgs[0]->ref_htree();
  LGraph*         root_lg   = var.lgs[0];

  I(root_tree);
  I(root_lg);

  absl::flat_hash_set<std::tuple<Hierarchy_index, Hierarchy_index, uint32_t>> edges;
  absl::flat_hash_map<Hierarchy_index, vertex_t>                              vm;

  collapsed_gis.emplace_back();
  auto& gi = collapsed_gis[0];

  fmt::print("done ({} ms).\n", t.time());

  t.start();
  fmt::print("    traversing hierarchy...");

  for (auto hidx : root_tree->depth_preorder()) {
    LGraph* lg = root_tree->ref_lgraph(hidx);

    Node temp(root_lg, hidx, Hardcoded_input_nid);

    auto new_v = gi.make_vertex(temp.debug_name().substr(18), lg->size(), lg->get_lgid());

    // helpful to have a totally unique label number to use when making collapsed vertices
    if (gi.unique_label_counter < lg->get_lgid()) {
      gi.unique_label_counter = lg->get_lgid();
    }

    vm.emplace(hidx, new_v);

    for (auto e : temp.inp_edges()) {
      auto ei = std::tuple(e.driver.get_hidx(), hidx, e.get_bits());
      if (e.driver.get_hidx() == hidx) {
        continue;
      }
      if (edges.contains(ei)) {
        continue;
      }
      edges.emplace(ei);
    }

    for (auto e : temp.out_edges()) {
      auto ei = std::tuple(hidx, e.sink.get_hidx(), e.get_bits());
      if (hidx == e.sink.get_hidx()) {
        continue;
      }
      if (edges.contains(ei)) {
        continue;
      }
      edges.emplace(ei);
    }
  }

  fmt::print("done ({} ms).\n", t.time());

  auto find_edge = [&](vertex_t v_src, vertex_t v_dst) -> edge_t {
    for (auto e : gi.al.out_edges(v_src)) {
      if (gi.al.head(e) == v_dst) {
        return e;
      }
    }

    return gi.al.null_edge();
  };

  t.start();
  fmt::print("    assigning edges...");

  for (auto ei : edges) {
    auto [src, dst, weight] = ei;

    I(vm.count(src) == 1);
    I(vm.count(dst) == 1);

    auto v1 = vm[src];
    auto v2 = vm[dst];

    // this is done twice to make bidirectional edges for nodes that may only have outputs or inputs
    auto e_1_2 = find_edge(v1, v2);
    if (e_1_2 == gi.al.null_edge()) {
      auto new_e           = gi.al.insert_edge(v1, v2);
      gi.weights[new_e] = weight;
    }

    auto e_2_1 = find_edge(v2, v1);
    if (e_2_1 == gi.al.null_edge()) {
      auto new_e           = gi.al.insert_edge(v2, v1);
      gi.weights[new_e] = weight;
    }
  }

  fmt::print("done ({} ms).\n", t.time());
}

/*
void Hier_tree::dump_node(const phier node) const {
  static int depth = -1;

  depth++;

  std::string prefix;
  if (node->parent == nullptr) {
    prefix.append("root, ");
  }
  if (node->is_leaf()) {
    prefix.append("leaf, ");
  }
  fmt::print("node: {:<30}{}depth: {}, ", node->name, prefix, depth);

  if (node->is_leaf()) {
    fmt::print("area: {:.2f}, containing nodes:\n", node->area);
    for (auto v : node->graph_set) {
      fmt::print("  {}\n", gi.debug_names(v));
    }
  } else {
    fmt::print("area: {:.2f}, children: {} and {}.\n", find_area(node), node->children[0]->name, node->children[1]->name);
    dump_node(node->children[0]);
    dump_node(node->children[1]);
  }

  depth--;
}
*/

// add up the total area of all the leaves in the subtree
double Hier_tree::find_area(phier node) const {
  if (node->is_leaf()) {
    return node->area;
  }

  I(node->children[0] != nullptr);
  I(node->children[1] != nullptr);

  return find_area(node->children[0]) + find_area(node->children[1]);
}

unsigned int Hier_tree::find_tree_size(phier node) const {
  if (node->is_leaf()) {
    return 1;
  }

  I(node->children[0] != nullptr);
  I(node->children[1] != nullptr);

  return find_tree_size(node->children[0]) + find_tree_size(node->children[1]) + 1;
}

unsigned int Hier_tree::find_tree_depth(phier node) const {
  std::function<unsigned int(phier, unsigned int)> find_depth = [&find_depth](phier rnode, unsigned int depth) -> unsigned int {
    if (rnode->is_leaf()) {
      return depth;
    }

    I(rnode->children[0] != nullptr);
    I(rnode->children[1] != nullptr);

    unsigned int dc0 = find_depth(rnode->children[0], depth + 1);
    unsigned int dc1 = find_depth(rnode->children[1], depth + 1);

    return std::max(dc0, dc1);
  };

  return find_depth(node, 0);
}

/*
void Hier_tree::dump_hier() const {
  for (size_t i = 0; i < hiers.size(); i++) {
    fmt::print("printing hierarchy {} ({} nodes):\n", i, find_tree_size(hiers[i]));
    dump_node(hiers[i]);
    fmt::print("\n");
  }
}
*/

void Hier_tree::dump_patterns() const {
  for (size_t hier_index = 0; hier_index < pattern_sets.size(); hier_index++) {
    fmt::print("in hierarchy {} ({} pattern sets):\n", hier_index, pattern_sets[hier_index].size());

    for (size_t pattern_index = 0; pattern_index < pattern_sets[hier_index].size(); pattern_index++) {
      fmt::print("  in pattern {}:\n", pattern_index);

      for (auto vpair : pattern_sets[hier_index][pattern_index].verts) {
        fmt::print("    label: {}, count: {}\n", vpair.first, vpair.second);
      }
    }
  }
}