/*
  This class converts a json file representing a hierarchical netlist into a Hier_tree data structure.
*/
#pragma once

#include <fstream>
#include <string>

#include <unordered_map>
#include <algorithm>
#include <iterator>

#include "rapidjson/document.h" // for json parsing

#include "i_resolve_header.hpp"

// this holds a graph and all the related map information.
// it looks complicated because everything in the graph lib is a template and has copy constructors removed.
class Graph_info {
public:
  graph::Bi_adjacency_list al;
  decltype(al.vert_map<std::string>()) names;
  decltype(al.vert_map<double>()) areas;
  decltype(al.edge_map<unsigned int>()) weights;

  Graph_info(
    graph::Bi_adjacency_list && new_list,
    decltype(graph::Bi_adjacency_list().vert_map<std::string>()) && new_names,
    decltype(graph::Bi_adjacency_list().vert_map<double>()) && new_areas,
    decltype(graph::Bi_adjacency_list().edge_map<unsigned int>()) && new_weights
  ) : al(std::move(new_list)), names(std::move(new_names)), areas(std::move(new_areas)), weights(std::move(new_weights)) { }

#ifndef NDEBUG
  void print() {
    using namespace graph::attributes;
    std::cout << al.dot_format("name"_of_vert = names, "weight"_of_edge = weights) << std::endl;
  }
#endif

};

class Json_inou_parser {
public:
  
  // load and check json being passed to us
  Json_inou_parser(const std::string& path);
  
  // create a graph from json
  Graph_info make_tree() const;
  
  // get area for the whole design...?
  double get_area() const;

  // get aspect ratio for the whole design...?
  double get_aspect_ratio() const;
private:
  rapidjson::Document d;
};
