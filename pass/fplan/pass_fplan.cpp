
#include "pass_fplan.hpp"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#undef I
#include "graph_info.hpp"
#include "i_resolve_header.hpp"

void setup_pass_fplan() { Pass_fplan::setup(); }

void Pass_fplan::setup() {
  // register the method with lgraph in order to use it
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Pass_fplan::pass);
  register_pass(m);
}

void Pass_fplan::makefp(LGraph *l) {
  std::cout << "LGraph: " << l->get_name() << std::endl;

  graph::Bi_adjacency_list g;

  auto g_name_map     = g.vert_map<std::string>();
  auto g_area_map     = g.vert_map<double>();
  auto g_edge_weights = g.edge_map<unsigned int>();
  auto g_set          = g.vert_set();

  // TODO: use string_views instead of strings
  const std::string name = l->get_name().data();
  const double      area = l->get_down_nodes_map().size();

  auto v = g.null_vert();
  for (const auto &vert : g.verts()) {
    if (g_name_map[vert] == name) {
      v = vert;
    }
  }

  if (g.is_null(v)) {
    // vertex does not exist, so create it
    auto new_v        = g.insert_vert();
    g_name_map[new_v] = name;

    v = new_v;
  }

  g_area_map[v] = area;
  g_set.insert(v);  // all verts start in set zero, and get dividied up during hierarchy discovery

  auto existing_edges = g.edge_set();

  // TODO: use get_down_nodes_map() to list all IOs and submodules
/*
  for (auto node : l->fast()) {
    std::cout << node.debug_name() << std::endl;
    std::cout << "inputs: " << node.has_inputs() << std::endl;
    std::cout << "outputs: " << node.has_outputs() << std::endl;
  }
*/

  // TODO: this prints nothing
  for (auto n : l->get_down_nodes_map()) {
    std::cout << n.first.get_nid() << std::endl;
  }

  /*
      std::string other_name = connection["name"].GetString();
      auto other_v = g.null_vert();
      for (const auto& vert : g.verts()) {
        if (g_name_map[vert] == other_name) {
          other_v = vert;
        }
      }

      if (other_v == g.null_vert()) {
        // other node is not in the map, so create it.
        other_v = g.insert_vert();
        g_name_map[other_v] = other_name;
      }

      auto new_e = g.insert_edge(v, other_v);
      g_edge_weights[new_e] = connection["weight"].GetInt();
      existing_edges.insert(new_e);
    }
  }

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
  Pass_fplan p(var);
  std::cout << "running pass..." << std::endl;
  // loop over each lgraph
  for (auto l : var.lgs) {
    p.makefp(l);
  }

  // make a graph_info thing and call every alg on it
  // 1. print out node information
  // 2. write node information into a Graph_info struct and run code on that
  // 3. <finish HiReg>
  // 4. write code to use the existing hierarchy instead of throwing it away
}