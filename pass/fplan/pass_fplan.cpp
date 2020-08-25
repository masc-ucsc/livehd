
#include "pass_fplan.hpp"

#include "graph_info.hpp"
#include "hier_tree.hpp"
#include "i_resolve_header.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

void Pass_fplan::setup() {
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);

  register_pass(m);
}

// turn an LGraph into a graph suitable for HiReg.
void Pass_fplan::make_graph(Eprp_var& var) {
  std::cout << "  creating floorplan graph...";

  Hierarchy_tree* root_tree;
  LGraph*         root_lg;

  for (auto lg : var.lgs) {
    if (lg->get_lgid() == 1) {
      root_tree = lg->ref_htree();
      root_lg   = lg;
    }
  }

  // nodes and compact nodes are basically just pointers to the internal lgraph rep.
  // they can be deleted and re-created at will.

  root_tree->dump();

  absl::flat_hash_map<Node, vertex_t> nvmap;

  unsigned long unique_value_counter = 0;

  for (auto hidx : root_tree->depth_preorder()) {
  auto lg = root_tree->ref_lgraph(hidx);
  lg->each_sub_fast([&](Node& n, Lg_type_id id) -> bool {
    Node hnode(lg, hidx, n.get_compact_class());
    for(auto e:hnode.out_edges()) {}
    for(auto e:hnode.inp_edges()) {}
  });
}

/*
  for (auto hidx : root_tree->depth_preorder()) {
    auto lg = root_tree->ref_lgraph(hidx);
    lg->each_sub_fast([&](Node& n, Lg_type_id id) -> bool {
      auto new_v = gi.al.insert_vert();

      //LGraph* child_graph = LGraph::open(lg->get_path(), n.get_type_sub());

      gi.ids[new_v]         = unique_value_counter++;
      gi.debug_names[new_v] = n.debug_name();
      //gi.areas[new_v]       = child_graph->size();
      gi.sets[0].insert(new_v);  // all verts start in set zero, and get dividied up during hierarchy discovery

      nvmap.emplace(Node(lg, hidx, n.get_compact_class()), new_v);
      fmt::print("Node: {}, (height: {}, pos: {}), nid: {}\n",
                 n.debug_name(),
                 int32_t(hidx.level),
                 int32_t(hidx.pos),
                 n.get_compact().get_nid()
                 );
      return true;
    });
  }
  */

  for (auto hidx : root_tree->depth_preorder()) {
    auto lg = root_tree->ref_lgraph(hidx);
    lg->each_sub_fast_direct([&](Node& n, Lg_type_id id) -> bool {
      
      return true;
    });
  }

  // we need the hidx in order to identify nodes.
  // we need the nid in order for nv.first.get_node(root_lg) to return an actual node.

  // not sure about including the root node right now...
  // auto root_v = gi.al.insert_vert();
  // gi.ids[root_v] = unique_value_counter++;
  // gi.debug_names[root_v] = root_lg->get_self_sub_node().get_name();
  // gi.areas[root_v] = root_lg->size();
  // gi.sets[0].insert(root_v);

  // nvmap.emplace(Node::Compact(Hierarchy_index(), 1), gi.al.insert_vert());

  for (auto nv : nvmap) {
    for (auto lg_e : nv.first.inp_edges()) {

      vertex_t v1 = gi.al.null_vert();
      vertex_t v2 = gi.al.null_vert();

      for (auto cnv : nvmap) {
        if (cnv.first.get_hidx() == lg_e.driver.get_hidx()) {
          v1 = nvmap.at(cnv.first);
        } else if (cnv.first.get_hidx() == lg_e.sink.get_hidx()) {
          v2 = nvmap.at(cnv.first);
        }
      }

      I(v1 != gi.al.null_vert());
      I(v2 != gi.al.null_vert());

      auto find_edge = [&](vertex_t v_src, vertex_t v_dst) -> edge_t {
        for (auto e : gi.al.edges()) {
          if (gi.al.tail(e) == v_src && gi.al.head(e) == v_dst) {
            return e;
          }
        }

        return gi.al.null_edge();
      };

      // all connections have to be symmetrical
      auto e_1_2 = find_edge(v1, v2);
      if (e_1_2 == gi.al.null_edge()) {
        auto new_e        = gi.al.insert_edge(v1, v2);
        gi.weights[new_e] = lg_e.driver.get_bits();
      } else {
        gi.weights[e_1_2] += lg_e.driver.get_bits();
      }

      auto e_2_1 = find_edge(v2, v1);
      if (e_2_1 == gi.al.null_edge()) {
        auto new_e        = gi.al.insert_edge(v2, v1);
        gi.weights[new_e] = lg_e.driver.get_bits();
      } else {
        gi.weights[e_2_1] += lg_e.driver.get_bits();
      }
    }
  }

  using namespace graph::attributes;
  std::cout << gi.al.dot_format("weight"_of_edge = gi.weights, "name"_of_vert = gi.debug_names, "area"_of_vert = gi.areas) << std::endl;

/*



// if the graph is not fully connected, ker-lin fails to work.
for (const auto& v : gi.al.verts()) {
  for (const auto& other_v : gi.al.verts()) {
    bool found = false;
    for (const auto& e : gi.al.out_edges(v)) {
      if (gi.al.head(e) == other_v && gi.al.tail(e) == v) {
        found = true;
        break;
      }
    }
    if (!found) {
      auto temp_e        = gi.al.insert_edge(v, other_v);
      gi.weights[temp_e] = 0;
    }
  }
}
std::cout << "done." << std::endl;
*/
}

void Pass_fplan::pass(Eprp_var& var) {
  std::cout << "\ngenerating floorplan..." << std::endl;
  Pass_fplan p(var);

  p.make_graph(var);

  Hier_tree h(std::move(p.gi), 1);
  h.collapse(0.0);
  h.discover_regularity();

  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away...?
  // HiReg specifies area requirements that a normal LGraph may not fill

  std::cout << "floorplan generated." << std::endl << std::endl;
}
