#pragma once
#include "i_resolve_header.hpp"

typedef decltype(graph::Bi_adjacency_list().insert_vert()) vertex_t;
typedef decltype(graph::Bi_adjacency_list().insert_edge(graph::Bi_adjacency_list().insert_vert(), graph::Bi_adjacency_list().insert_vert())) edge_t;

typedef decltype(graph::Bi_adjacency_list().vert_map<std::string>()) name_map_t;
typedef decltype(graph::Bi_adjacency_list().vert_map<double>()) area_map_t;
typedef decltype(graph::Bi_adjacency_list().edge_map<unsigned int>()) weight_map_t;

// this holds a graph and all the related map information.
// it looks complicated because everything in the graph lib is a template and has copy constructors removed.
class Graph_info {
public:

  graph::Bi_adjacency_list al;
  name_map_t names;
  area_map_t areas;
  weight_map_t weights;

  Graph_info(
    graph::Bi_adjacency_list && new_list,
    name_map_t&& new_names,
    area_map_t&& new_areas,
    weight_map_t&& new_weights
  ) : al(std::move(new_list)), names(std::move(new_names)), areas(std::move(new_areas)), weights(std::move(new_weights)) { }

  Graph_info(Graph_info&& other) noexcept :
    al(std::move(other.al)),
    names(std::move(other.names)),
    areas(std::move(other.areas)),
    weights(std::move(other.weights)) { // TODO: delete stuff here?
  }

  Graph_info& operator=(Graph_info&& other) noexcept {
    //al = 
    //names = std::move(other.names);
    //areas = std::move(other.areas);
    //weights = std::move(other.weights);
    return *this;
  }
  
  // TODO: make this print in a way nicer format since all nodes need to be connected
#ifndef NDEBUG
  void print() {
    using namespace graph::attributes;
    std::cout << al.dot_format("name"_of_vert = names, "weight"_of_edge = weights) << std::endl;
  }
#endif

};
