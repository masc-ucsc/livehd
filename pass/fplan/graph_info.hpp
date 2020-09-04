#pragma once

#include <unordered_set>
#include <unordered_map>

#include "i_resolve_header.hpp"
#include "lgraph_base_core.hpp"

typedef graph::Bi_adjacency_list g_type;

typedef g_type::Vert       vertex_t;
typedef g_type::Edge       edge_t;
typedef g_type::Vert_set   set_t;
typedef std::vector<set_t> set_vec_t;

// this holds a graph and all the related map information.
template <typename GImp>
class Graph_info {
public:
  GImp                                 al;
  graph::Vert_map<GImp, std::string>   debug_names;  // can remove later, mostly for debugging
  graph::Vert_map<GImp, unsigned long> ids;          // TODO: is this required?
  graph::Vert_map<GImp, double>        areas;
  graph::Vert_map<GImp, Lg_type_id>    labels;  // what LGraph a node represents
  graph::Edge_map<GImp, unsigned int>  weights;
  std::vector<graph::Vert_set<GImp>>   sets;

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

  vertex_t make_temp_vertex(std::string debug_name, double area, size_t set) {
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

  vertex_t make_vertex(std::string debug_name, double area, Lg_type_id label, size_t set) {
    auto nv = al.insert_vert();

    ids[nv]         = ++unique_id_counter;
    areas[nv]       = area;
    debug_names[nv] = debug_name.append(std::string("_").append(std::to_string(unique_id_counter)));
    labels[nv]      = label;

    sets[set].insert(nv);

    return nv;
  }

  edge_t find_edge(vertex_t v_src, vertex_t v_dst) {
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
