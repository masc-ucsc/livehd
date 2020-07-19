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

#include "Adjacency_list.hpp"
#undef I // graph and iassert both declare "I" macros, but we only need the one from iassert

#include "iassert.hpp"
#include "hier_tree.hpp"

class Json_inou_parser {
public:
  
  // load and check json being passed to us
  Json_inou_parser(const std::string& path);
  
  // load json hierarchy into vector for use in HiReg
  std::vector<pnetl> make_tree() const;
  
  // get area for the whole design
  double get_area() const;

  // get aspect ratio for the whole design
  double get_aspect_ratio() const;
private:
  rapidjson::Document d;
};
