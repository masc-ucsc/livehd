#pragma once
#include "i_resolve_header.hpp"
#include "lgraph.hpp"

typedef decltype(graph::Bi_adjacency_list().insert_vert())                                         vertex_t;
typedef decltype(graph::Bi_adjacency_list().insert_edge(graph::Bi_adjacency_list().insert_vert(),
                                                        graph::Bi_adjacency_list().insert_vert())) edge_t;

typedef decltype(graph::Bi_adjacency_list().vert_map<std::string>())  name_map_t;
typedef decltype(graph::Bi_adjacency_list().vert_map<Lg_type_id>())   id_map_t;
typedef decltype(graph::Bi_adjacency_list().vert_map<double>())       area_map_t;
typedef decltype(graph::Bi_adjacency_list().edge_map<unsigned int>()) weight_map_t;

typedef decltype(graph::Bi_adjacency_list().vert_set()) set_t;
typedef std::vector<set_t>                              set_vec_t;

// this holds a graph and all the related map information.
// it looks complicated because everything in the graph lib is a template and has copy constructors removed.
class Graph_info {
public:
  graph::Bi_adjacency_list al;
  name_map_t               debug_names;
  id_map_t                 ids;
  area_map_t               areas;
  weight_map_t             weights;
  set_vec_t                sets;

  Graph_info()
      : al(graph::Bi_adjacency_list())
      , debug_names(al.vert_map<std::string>())
      , ids(al.vert_map<Lg_type_id>())
      , areas(al.vert_map<double>())
      , weights(al.edge_map<unsigned int>())
      , sets(1, al.vert_set()) {}

  Graph_info(Graph_info&& other) noexcept
      : al(std::move(other.al))
      , debug_names(std::move(other.debug_names))
      , ids(std::move(other.ids))
      , areas(std::move(other.areas))
      , weights(std::move(other.weights))
      , sets(std::move(other.sets)) {  // TODO: delete stuff here?
  }
};
