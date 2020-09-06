
#include "pass_fplan.hpp"

#include <chrono>

#include "graph_info.hpp"
#include "hier_tree.hpp"
#include "i_resolve_header.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

void Pass_fplan::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);
  register_pass(m);

  auto dm = Eprp_method("pass.fplan.dumpfp", "dump a DOT file representing the recreated hierarchy", &Pass_fplan_dump::pass);
  register_pass(dm);
}

// turn an LGraph into a graph suitable for HiReg.
void Pass_fplan::make_graph(Eprp_var& var) {
  // TODO: check to make sure the lgraph(s) are actually valid before traversing them
  // TODO: after I get this working, harden this code.  Assert stuff.

  Hierarchy_tree* root_tree = nullptr;
  LGraph*         root_lg   = nullptr;

  // TODO: finding the root lgraph may require the user to pass in a root lgraph name.
  // not sure if I can legally find it automatically.
  for (auto lg : var.lgs) {
    if (lg->get_lgid() == 1) {
      root_tree = lg->ref_htree();
      root_lg   = lg;
    }
  }

  if (!root_tree) {
    throw std::runtime_error("empty input hierarchy!");
  }

  I(root_tree);
  I(root_lg);

  // TODO: optimize this.

  absl::flat_hash_set<std::tuple<Hierarchy_index, Hierarchy_index, uint32_t>> edges;
  absl::flat_hash_map<Hierarchy_index, vertex_t>                              vm;

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

  auto find_edge = [&](vertex_t v_src, vertex_t v_dst) -> edge_t {
    for (auto e : gi.al.out_edges(v_src)) {
      if (gi.al.head(e) == v_dst) {
        return e;
      }
    }

    return gi.al.null_edge();
  };

  for (auto ei : edges) {
    auto [src, dst, weight] = ei;

    auto v1 = vm.at(src);
    auto v2 = vm.at(dst);

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
}

void Pass_fplan::pass(Eprp_var& var) {
  auto time = []() -> auto { return std::chrono::system_clock::now(); };
  auto dur  = [](auto e, auto s) -> auto { return std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count(); };

  fmt::print("\ngenerating floorplan...\n");
  auto       whole_s = time();
  Pass_fplan p(var);

  fmt::print("  making floorplan graph...");
  auto s = time();
  p.make_graph(var);
  auto e = time();
  fmt::print("done ({} ms).\n", dur(e, s));

  Hier_tree h(std::move(p.gi));

  fmt::print("  discovering hierarchy...");
  s = time();
  h.discover_hierarchy(1);
  e = time();
  fmt::print("done ({} ms).\n", dur(e, s));

  fmt::print("  collapsing hierarchy...");
  s = time();
  h.collapse(30.0);
  h.collapse(60.0);
  e = time();
  fmt::print("done ({} ms).\n", dur(e, s));

  // h.dump();

  fmt::print("  discovering regularity...");
  s = time();
  h.discover_regularity(0, 15);
  e = time();
  fmt::print("done ({} ms).\n", dur(e, s));

  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away...?
  // HiReg specifies area requirements that a normal LGraph may not fill
  auto whole_e = time();
  fmt::print("floorplan generated ({} ms).\n\n", dur(whole_e, whole_s));
}
