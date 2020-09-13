#pragma once

#include <string>

#include "i_resolve_header.hpp"
#include "lgraph_base_core.hpp"

// specific kind of graph used elsewhere
// Out_adjacency_list doesn't work yet
using g_type = graph::Bi_adjacency_list;

using vertex_t  = g_type::Vert;
using edge_t    = g_type::Edge;
using set_t     = g_type::Vert_set;
using set_vec_t = std::vector<set_t>;

// this holds a graph and all the related map information.
template <typename GImp>
class Graph_info {
public:
  GImp                                 al;
  graph::Vert_map<GImp, std::string>   debug_names;  // can remove later, mostly for debugging
  graph::Vert_map<GImp, unsigned long> ids;          // TODO: might be useful to uniquely identify a node without debug_names
  graph::Vert_map<GImp, double>        areas;        // area of the node
  graph::Vert_map<GImp, Lg_type_id>    labels;       // what LGraph a node represents
  graph::Edge_map<GImp, unsigned int>  weights;      // number of wires in a connection between two nodes
  std::vector<graph::Vert_set<GImp>>   sets;         // map of hierarchy nodes -> node(s) in the graph

  // We have to use the "template" keyword here because the compiler doesn't know how to compile Graph_info since al is not an
  // explicit type.  The type given at instantiation time could drastically change the behavior of how the class behaves, so we use
  // "template" to indicate that we're working with template member functions.

  // Note that vert maps and vert sets have no default constructor and need to be created using al.

  Graph_info()
      : al()
      , debug_names(al.template vert_map<std::string>())
      , ids(al.template vert_map<unsigned long>())
      , areas(al.template vert_map<double>())
      , labels(al.template vert_map<Lg_type_id>())
      , weights(al.template edge_map<unsigned int>())
      , sets(1, al.vert_set()) {}

  Graph_info(Graph_info&& other) noexcept
      : al(std::move(other.al))
      , debug_names(std::move(other.debug_names))
      , ids(std::move(other.ids))
      , areas(std::move(other.areas))
      , labels(std::move(other.labels))
      , weights(std::move(other.weights))
      , sets(std::move(other.sets)) {}

  vertex_t make_temp_vertex(const std::string& debug_name, double area, size_t set) {
    auto nv = al.insert_vert();

    ids[nv]         = ++unique_id_counter;
    areas[nv]       = area;
    debug_names[nv] = debug_name;

    for (auto other_v : al.verts()) {
      edge_t temp_edge_t   = al.insert_edge(nv, other_v);
      weights[temp_edge_t] = 0;

      temp_edge_t          = al.insert_edge(other_v, nv);
      weights[temp_edge_t] = 0;
    }

    sets[set].insert(nv);

    return nv;
  }

  vertex_t make_vertex(const std::string& debug_name, double area, const Lg_type_id label, const size_t set) {
    auto nv = al.insert_vert();

    ids[nv]   = ++unique_id_counter;
    areas[nv] = area;
    // debug_names[nv] = debug_name.append(std::string("_").append(std::to_string(unique_id_counter)));
    debug_names[nv] = debug_name;
    labels[nv]      = label;

    sets[set].insert(nv);

    return nv;
  }

  vertex_t make_bare_vertex(const std::string& debug_name, const double area) {
    auto nv = al.insert_vert();

    ids[nv]   = ++unique_id_counter;
    areas[nv] = area;
    // debug_names[nv] = debug_name.append(std::string("_").append(std::to_string(unique_id_counter)));
    debug_names[nv] = debug_name;

    return nv;
  }

  edge_t find_edge(const vertex_t& v_src, const vertex_t& v_dst) const {
    for (auto e : al.out_edges(v_src)) {
      if (al.head(e) == v_dst) {
        return e;
      }
    }

    return al.null_edge();
  }

private:
  unsigned long unique_id_counter = 0;
};
