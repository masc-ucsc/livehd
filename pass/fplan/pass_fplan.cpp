
#include "pass_fplan.hpp"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "graph_info.hpp"
#include "i_resolve_header.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

void Pass_fplan::setup() {
  // register the method with lgraph in order to use it
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);
  
  register_pass(m);
}

// TODO: make this a part of the pass
// best solution would be to patch iassert so that if "I" or "GI" is already defined, a longer name is used.
static void makefp(Eprp_var &var, Graph_info& gi) {

  std::cout << "Running node creation pass..." << std::endl;

  // create nodes without any connections between them, and fill in as much information as we can.
  for (auto lg : var.lgs) {
    for (auto cn : lg->get_down_nodes_map()) {
      auto n = cn.first.get_node(lg);
      bool found = false;
      for (auto v : gi.al.verts()) {
        if (gi.ids[v] == n.get_hidx()) {
          found = true;
          std::cout << "Already found node " << n.get_name() << std::endl;
          break;
        }
      }

      // node does not already exist, so make a new one
      if (!found) {
        std::cout << "Making new node " << n.get_name() << std::endl;
        auto new_v = gi.al.insert_vert();
        gi.ids[new_v] = n.get_hidx();
        gi.debug_names[new_v] = n.get_name();
        // TODO: find an actual area of a node
        gi.areas[new_v] = n.get_num_outputs() + n.get_num_inputs();
        gi.sets[0].insert(new_v);  // all verts start in set zero, and get dividied up during hierarchy discovery
      }
    }    
  }

  auto existing_edges = gi.al.edge_set();

  std::cout << "Running node connection pass..." << std::endl;

  auto find_name = [&](Hierarchy_index id) -> vertex_t {
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
    for (auto cn : lg->get_down_nodes_map()) {

      auto n = cn.first.get_node(lg);

      std::cout << "Node: " << n.get_name() << std::endl;

      for (auto p : n.inp_connected_pins()) {
        for (auto other_p : p.inp_driver()) {
          auto v1 = find_name(p.get_hidx());
          auto v2 = find_name(other_p.get_hidx());
              
          auto new_e = gi.al.insert_edge(v1, v2);
          gi.weights[new_e] = other_p.get_bits(); // TODO: only driver pins can call get_bits()?
          existing_edges.insert(new_e);
        }

        /*
        for (auto cn2 : lg->get_down_nodes_map()) {
          auto n2 = cn2.first.get_node(lg);
          if (n2.has_outputs()) {
            for (auto p2 : n2.out_connected_pins()) { // this is wrong - gets outputs of the same node
              
              if (p2.get_hidx() == p.get_hidx()) {
                auto v1 = find_name(p.get_hidx());
                auto v2 = find_name(p2.get_hidx());
              
                auto new_e = gi.al.insert_edge(v1, v2);
                gi.weights[new_e] = p2.get_bits(); // TODO: only driver pins can call get_bits()?
                existing_edges.insert(new_e);
              }
            }
          } else {
            std::cout << "node has no outputs, skipping." << std::endl;
          }
          
        }
        */

      }
    }
  }
  
  using namespace graph::attributes;
  std::cout << gi.al.dot_format("weight"_of_edge = gi.weights, "name"_of_vert = gi.debug_names) << std::endl;

  /*
  // if the graph is not fully connected, ker-lin fails to work.
  // TODO: eventually replace this with an adjacency matrix, since it's probably faster.
  for (const auto& v : g.verts()) {
    for (const auto& other_v : g.verts()) {
      bool found = false;
      for (const auto& e : g.out_edges(v)) {
        if (g.head(e) == other_v && g.tail(e) == v) {
          found = true;
          break;
        }
      }
      if (!found) {
        auto temp_e = g.insert_edge(v, other_v);
        g_edge_weights[temp_e] = 0;
      }
    }
  }

  Graph_info info(std::move(g), std::move(g_name_map), std::move(g_area_map), std::move(g_edge_weights), std::move(g_set));
  */
}

void Pass_fplan::pass(Eprp_var &var) {
  std::cout << "running pass..." << std::endl;

  Graph_info gi;

  makefp(var, gi);

  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away

}