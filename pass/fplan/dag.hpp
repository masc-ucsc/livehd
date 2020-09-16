#pragma once

#include <iostream>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>  // for std::pair
#include <vector>

#include "graph_info.hpp"
#include "i_resolve_header.hpp"
#include "lgraph_base_core.hpp"
#include "pattern.hpp"

class Dag_node {
public:
  using pdag = std::shared_ptr<Dag_node>;

  pdag              parent;
  std::vector<pdag> children;

  // TODO: insert node content here

  Dag_node() : parent(nullptr), children() {}
  bool is_leaf() { return children.size() == 0; }
};

class Dag {
public:
  using pdag = std::shared_ptr<Dag_node>;

  Dag() : root(std::make_shared<Dag_node>()) {}

  // initialize a dag from a vector of patterns with all leaves being unique,
  // and all patterns either containing leaves or other patterns.
  void init(std::vector<Pattern> pat_set, const Graph_info<g_type>& ginfo);

  std::unordered_set<pdag> select_points();

  void dump();

private:
  std::unordered_map<Pattern, pdag> pat_dag_map;
  pdag                              root;
};