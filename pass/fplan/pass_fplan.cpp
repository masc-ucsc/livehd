
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
// NOTE: sometimes code is repeated in order to process the root node since each_sub_fast_direct() doesn't touch the root.
void Pass_fplan::make_graph(Eprp_var& var) {
  std::cout << "  creating floorplan graph...";

  for (auto lg : var.lgs) {
    bool found = false;
    lg->each_sub_fast_direct([&](Node& n, Lg_type_id id) -> bool {
      for (auto v : gi.al.verts()) {
        if (gi.ids(v) == id) {
          found = true;
          break;
        }
      }

      // node does not already exist, so make a new one
      if (!found) {
        auto new_v            = gi.al.insert_vert();
        gi.ids[new_v]         = id;
        gi.debug_names[new_v] = n.debug_name();
        gi.sets[0].insert(new_v);  // all verts start in set zero, and get dividied up during hierarchy discovery
      }
      return true;
    });

    auto self_node = lg->get_self_sub_node();
    if (self_node.get_lgid() == 1) {
      auto new_v            = gi.al.insert_vert();
      gi.ids[new_v]         = self_node.get_lgid();
      gi.debug_names[new_v] = self_node.get_name();
      gi.sets[0].insert(new_v);  // all verts start in set zero, and get dividied up during hierarchy discovery
    }

    // area is a property of the LGraph (not the node), so we need a seperate pass for each node
    for (auto v : gi.al.verts()) {
      if (gi.ids[v] == lg->get_lgid()) {
        gi.areas[v] = lg->size();
      }
    }
  }

  // create nodes without any connections between them, and fill in as much information as we can.
  auto find_name = [&](const Lg_type_id id) -> vertex_t {
    for (auto v : gi.al.verts()) {
      if (gi.ids(v) == id) {
        return v;
      }
    }

    return gi.al.null_vert();
  };

  // wire up nodes with connections between them
  // obviously, only nodes that are in the same level of hierarchy can be connected to each other.
  for (auto lg : var.lgs) {
    lg->each_sub_fast_direct([&](Node& n, Lg_type_id id) -> bool {
      for (auto p : n.inp_connected_pins()) {
        for (auto other_p : p.inp_driver()) {
          auto v1 = find_name(id);
          auto v2 = find_name(other_p.get_node().get_class_lgraph()->get_lgid());

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
            gi.weights[new_e] = other_p.get_bits();
          } else {
            gi.weights[e_1_2] += other_p.get_bits();
          }

          auto e_2_1 = find_edge(v2, v1);
          if (e_2_1 == gi.al.null_edge()) {
            auto new_e        = gi.al.insert_edge(v2, v1);
            gi.weights[new_e] = other_p.get_bits();
          } else {
            gi.weights[e_2_1] += other_p.get_bits();
          }
        }
      }
      return true;
    });
  }

  // using namespace graph::attributes;
  // std::cout << gi.al.dot_format("weight"_of_edge = gi.weights, "name"_of_vert = gi.debug_names, "area"_of_vert = gi.areas,
  // "id"_of_vert = gi.ids) << std::endl;

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