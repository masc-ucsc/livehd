#include "fplan_pass.hpp"

void setup_pass_fplan() {
  Livehd_parser::setup();
}

void Livehd_parser::setup() {
  // register the method with lgraph in order to use it
  auto m = Eprp_method("pass.fplan.makefp", "generate a floorplan from an LGraph", &Livehd_parser::pass);
  register_pass(m);
}

void Livehd_parser::makefp(LGraph *l) {
  std::cout << l->get_name() << std::endl;

  graph::Bi_adjacency_list g;

  auto g_name_map = g.vert_map<std::string>();
  auto g_area_map = g.vert_map<double>();
  auto g_edge_weights = g.edge_map<unsigned int>();
  auto g_set = g.vert_set();
  
  // TODO: use string_views instead of strings
  const std::string name = l->get_name().data();
  const double area = l->get_down_nodes_map().size();
    
  auto v = g.null_vert();
  for (const auto& vert : g.verts()) {
    if (g_name_map[vert] == name) {
      v = vert;
    }
  }

  if (g.is_null(v)) {
    // vertex does not exist, so create it
    auto new_v = g.insert_vert();
    g_name_map[new_v] = name;
    
    v = new_v;
  }

  g_area_map[v] = area;
  g_set.insert(v); // all verts start in set zero, and get dividied up during hierarchy discovery

  auto existing_edges = g.edge_set();
  
  l->each_graph_output([this, &g](const Node_pin p) {
    const unsigned int weight = p.get_bits();
    // TODO: get iassert working in lambda?
    assert(weight != 0);

    //std::string other_name = p.get_sink_from_output().get_node().get_name().data();
    for (auto e : p.out_edges()) {
      std::cout << "oe: " << e.sink.get_node().get_name() << std::endl;
    }
  });

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

void Livehd_parser::pass(Eprp_var &var) {
  Livehd_parser p(var);
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