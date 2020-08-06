#pragma once
#include "i_resolve_header.hpp"

typedef decltype(graph::Bi_adjacency_list().insert_vert()) vertex_t;
typedef decltype(graph::Bi_adjacency_list().insert_edge(graph::Bi_adjacency_list().insert_vert(), graph::Bi_adjacency_list().insert_vert())) edge_t;

typedef decltype(graph::Bi_adjacency_list().vert_map<std::string>()) name_map_t;
typedef decltype(graph::Bi_adjacency_list().vert_map<double>()) area_map_t;
typedef decltype(graph::Bi_adjacency_list().edge_map<unsigned int>()) weight_map_t;

typedef decltype(graph::Bi_adjacency_list().vert_set()) set_t;
typedef std::vector<set_t> set_vec_t;

// this holds a graph and all the related map information.
// it looks complicated because everything in the graph lib is a template and has copy constructors removed.
class Graph_info {
public:

  graph::Bi_adjacency_list al;
  name_map_t names;
  area_map_t areas;
  weight_map_t weights;
  set_vec_t sets;

  Graph_info(
    graph::Bi_adjacency_list && new_list,
    name_map_t&& new_names,
    area_map_t&& new_areas,
    weight_map_t&& new_weights,
    set_t&& new_sets
  ) : al(std::move(new_list)), names(std::move(new_names)), areas(std::move(new_areas)), weights(std::move(new_weights)), sets(1, std::move(new_sets)) { }

  Graph_info(Graph_info&& other) noexcept :
    al(std::move(other.al)),
    names(std::move(other.names)),
    areas(std::move(other.areas)),
    weights(std::move(other.weights)),
    sets(std::move(other.sets)) { // TODO: delete stuff here?
  }
};
