#include "graph_info.hpp"

// make a temporary vertex and attach it to all other vertices with edge weights of zero
vertex_t Graph_info::make_temp_vertex(std::string debug_name, double area, size_t set) {
  auto nv = al.insert_vert();

  ids[nv]         = ++unique_id_counter;
  areas[nv]       = area;
  debug_names[nv] = debug_name;

  for (auto other_v : al.verts()) {
    edge_t temp_edge_t        = al.insert_edge(nv, other_v);
    weights[temp_edge_t] = 0;

    temp_edge_t               = al.insert_edge(other_v, nv);
    weights[temp_edge_t] = 0;
  }

  sets[set].insert(nv);

  return nv;
}

// make a vertex, but don't attach it to anything
vertex_t Graph_info::make_vertex(std::string debug_name, double area, Lg_type_id label, size_t set) {
  auto nv = al.insert_vert();

  ids[nv]         = ++unique_id_counter;
  areas[nv]       = area;
  debug_names[nv] = debug_name;
  labels[nv]      = label;

  sets[set].insert(nv);

  return nv;
}