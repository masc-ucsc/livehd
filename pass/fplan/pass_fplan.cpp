
#include "pass_fplan.hpp"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "graph_info.hpp"
#include "i_resolve_header.hpp"

#include "hier_tree.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

void Pass_fplan::setup() {
  // register the method with lgraph in order to use it
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);
  
  register_pass(m);
}

void Pass_fplan::make_graph(Eprp_var &var) {

  std::cout << "Running node creation pass..." << std::endl;

  for (auto lg : var.lgs) {
    fmt::print("LGraph: {}\n", lg->get_name());
    for (auto cn : lg->get_down_nodes_map()) {
      
      // root node is not printed
      fmt::print("name: {}\n nid: {}\n hidx: {}\n", cn.first.get_node(lg).get_name(), cn.first.get_nid().value, cn.first.get_node(lg).get_hidx().get_hash());

    }
  }

  // create nodes without any connections between them, and fill in as much information as we can.
  for (auto lg : var.lgs) {
    for (auto cn : lg->get_down_nodes_map()) {
      auto n = cn.first.get_node(lg);
      bool found = false;
      for (auto v : gi.al.verts()) {
        if (gi.ids[v] == cn.first.get_nid()) {
          found = true;
          std::cout << "Already found node " << n.get_name() << " with nid " << n.get_compact().get_nid() << std::endl;
          break;
        }
      }

      // node does not already exist, so make a new one
      if (!found) {
        std::cout << "Making new node " << n.get_name() << " with nid " << n.get_compact().get_nid() << std::endl;
        auto new_v = gi.al.insert_vert();
        gi.ids[new_v] = cn.first.get_nid();
        gi.debug_names[new_v] = n.get_name();
        // TODO: find an actual area of a node
        gi.areas[new_v] = n.get_num_outputs() + n.get_num_inputs();
        gi.sets[0].insert(new_v);  // all verts start in set zero, and get dividied up during hierarchy discovery
      }
    }    
  }

  auto existing_edges = gi.al.edge_set();

  std::cout << "Running node connection pass..." << std::endl;

  auto find_name = [&](Index_ID id) -> vertex_t {
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

      for (auto p : n.inp_connected_pins()) {
        for (auto other_p : p.inp_driver()) {
          
          auto v1 = find_name(n.get_compact().get_nid());
          auto v2 = find_name(other_p.get_node().get_compact().get_nid());
              
          auto new_e = gi.al.insert_edge(v1, v2);
          gi.weights[new_e] = other_p.get_bits(); // TODO: only driver pins can call get_bits()?
          existing_edges.insert(new_e);
        }

      }
    }
  }
  
  using namespace graph::attributes;
  std::cout << gi.al.dot_format("weight"_of_edge = gi.weights, "name"_of_vert = gi.debug_names) << std::endl;

  // if the graph is not fully connected, ker-lin fails to work.
  // TODO: eventually replace this with an adjacency matrix, since it's probably faster.
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
        auto temp_e = gi.al.insert_edge(v, other_v);
        gi.weights[temp_e] = 0;
      }
    }
  }
}

void Pass_fplan::pass(Eprp_var &var) {
  std::cout << "running pass..." << std::endl;
  Pass_fplan p(var);

  p.make_graph(var);
  
  Hier_tree h(std::move(p.gi), 1);
  h.collapse(0.0);
  h.discover_regularity();

  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away...?
  // HiReg specifies area requirements that a normal LGraph may not fill

  std::cout << "pass completed." << std::endl;
}