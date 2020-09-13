#include "pass_fplan.hpp"

#include <chrono>
#include <functional>
#include <stdexcept>  // for std::runtime_error
#include <string>     // for std::to_string
#include <thread>
#include <tuple>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "hier_tree.hpp"
#include "i_resolve_header.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "profile_time.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

constexpr unsigned int def_min_tree_nodes    = 1;
constexpr double       def_min_tree_area     = 0.0;
constexpr unsigned int def_max_pats          = 15;
constexpr unsigned int def_max_optimal_nodes = 15;
unsigned int           def_max_threads       = std::thread::hardware_concurrency() / 2;

void Pass_fplan::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);
  m.add_label_optional("min_tree_nodes",
                       "minimum number of components to trigger analysis of a subtree",
                       std::to_string(def_min_tree_nodes));
  m.add_label_optional("min_tree_area",
                       "area (mm^2) threshold below which nodes will be collapsed together",
                       std::to_string(def_min_tree_area));
  m.add_label_optional("max_pats", "maximum number of intermediate patterns to keep", std::to_string(def_max_pats));
  m.add_label_optional("max_optimal_nodes",
                       "crossover point between exhaustive branch-and-bound and simulated annealing",
                       std::to_string(def_max_optimal_nodes));
  m.add_label_optional("max_threads", "maximum number of threads to spawn", std::to_string(def_max_threads));

  register_pass(m);

  auto dhm
      = Eprp_method("pass.fplan.dumphier", "dump a DOT file representing the recreated hierarchy", &Pass_fplan_dump::dump_hier);
  register_pass(dhm);

  auto dtm = Eprp_method("pass.fplan.dumptree", "dump a DOT file representing the hierarchy tree", &Pass_fplan_dump::dump_tree);
  register_pass(dtm);
  m.add_label_optional("min_tree_count", "minimum number of components to trigger analysis of a subtree", "1");
}

// turn an LGraph into a graph suitable for HiReg.
void Pass_fplan::make_graph(Eprp_var& var) {
  // TODO: check to make sure the lgraph(s) are actually valid before traversing them
  // TODO: after I get this working, harden this code.  Assert stuff.

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

  fmt::print("done ({} ms).\n", t.time());

  t.start();
  fmt::print("    traversing hierarchy...");

  for (auto hidx : root_tree->depth_preorder()) {
    LGraph* lg = root_tree->ref_lgraph(hidx);

    Node temp(root_lg, hidx, Node::Hardcoded_input_nid);

    auto new_v = gi.make_vertex(temp.debug_name().substr(18), lg->size(), lg->get_lgid(), 0);

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
      auto new_e        = gi.al.insert_edge(v1, v2);
      gi.weights[new_e] = weight;
    }

    auto e_2_1 = find_edge(v2, v1);
    if (e_2_1 == gi.al.null_edge()) {
      auto new_e        = gi.al.insert_edge(v2, v1);
      gi.weights[new_e] = weight;
    }
  }

  fmt::print("done ({} ms).\n", t.time());
}

void Pass_fplan::pass(Eprp_var& var) {
  auto t       = profile_time::timer();
  auto whole_t = profile_time::timer();

  if (std::thread::hardware_concurrency() > 12) {
    fmt::print("\ncomfortable sheets detected!\n");
  }

  fmt::print("generating floorplan...\n");
  whole_t.start();
  Pass_fplan p(var);

  fmt::print("  making floorplan graph...\n");
  t.start();
  p.make_graph(var);
  fmt::print("  done ({} ms).\n", t.time());

  Hier_tree h(std::move(p.gi));

  fmt::print("  discovering hierarchy...\n");
  t.start();
  unsigned int mtn = std::stoi(var.get("min_tree_nodes").data());
  if (mtn == def_min_tree_nodes) {
    fmt::print("  using default param {}.\n", mtn);
  }
  h.discover_hierarchy(mtn);

  fmt::print("  done ({} ms).\n", t.time());

  fmt::print("  collapsing hierarchy...\n");
  t.start();

  const double mta = std::stod(var.get("min_tree_area").data());
  if (mta == def_min_tree_area) {
    fmt::print("  using default param {} mm^2.\n", mta);
  }

  const unsigned int mt = std::stod(var.get("max_threads").data());
  if (mt == def_max_threads) {
    fmt::print("  using {} threads.\n", mt);
  }
  h.make_hierarchies(mt - 1); // make mt - 1 additional hierarchies

  h.collapse(1, mta);

  fmt::print("done ({} ms).\n", t.time());

  fmt::print("  discovering regularity...\n");
  t.start();
  const unsigned int mp = std::stoi(var.get("max_pats").data());
  if (mp == def_max_pats) {
    fmt::print("  using default param {}.\n", def_max_pats);
  }
  h.discover_regularity(0, mp);

  fmt::print("done ({} ms).\n", t.time());

  h.dump_patterns();

  fmt::print("  constructing boundary curve...\n");
  t.start();
  const unsigned int mon = std::stoi(var.get("max_optimal_nodes").data());
  if (mon == def_max_optimal_nodes) {
    fmt::print("  using default param {}.\n", def_max_optimal_nodes);
  }
  h.construct_bounds(mon);
  fmt::print("  done ({} ms).\n", t.time());

  fmt::print("  constructing dag...");
  t.start();
  h.make_dag(0);
  fmt::print("  done ({} ms).\n", t.time());

  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away...?
  fmt::print("floorplan generated ({} ms).\n\n", whole_t.time());
}
