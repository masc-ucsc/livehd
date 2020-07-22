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

#include "Adjacency_list.hpp" // graph support
#undef I // graph and iassert both declare "I" macros, but we only need the one from iassert

#include "hier_tree.hpp"
#include "iassert.hpp"

class Graph_data {
public:
  graph::Bi_adjacency_list g;
  decltype(g.vert_map<std::string>()) names;
  decltype(g.vert_map<double>()) areas;
  decltype(g.edge_map<unsigned int>()) edge_weights;
};

class Json_inou_parser {
public:
  
  // load and check json being passed to us
  Json_inou_parser(const std::string& path);
  
  // create a graph from json
  void make_tree() const;
  
  // get area for the whole design...?
  double get_area() const;

  // get aspect ratio for the whole design...?
  double get_aspect_ratio() const;
private:
  rapidjson::Document d;
};
