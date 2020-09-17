#pragma once

#include <iostream>
#include <memory>  // for shared_ptr, unique_ptr
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
  std::shared_ptr<Dag_node>              parent;
  std::vector<std::shared_ptr<Dag_node>> children;
  std::vector<unsigned int>              child_edge_count;

  unsigned int     dag_id;

  // label of the dag node in the hierarchy used to generate the pattern
  Lg_type_id::type dag_label;

  double area;
  double width, height;
  // width and height of the node (if a child), or
  // width and height of the pattern (if not a child)

  // iterate over pat_dag_map to find the pattern represented by a Dag_node for now

  Dag_node() : parent(nullptr), children(), dag_id(0), dag_label(0), area(0.0), width(0.0), height(0.0) {}
  Dag_node(const unsigned int id) : parent(nullptr), children(), dag_id(id), dag_label(0), area(0.0), width(0.0), height(0.0) {}
  Dag_node(const unsigned int id, const Lg_type_id::type label, const double narea)
      : parent(nullptr), children(), dag_id(id), dag_label(label), area(narea), width(0.0), height(0.0) {}
  bool is_leaf() { return children.size() == 0; }
  bool is_root() { return parent == nullptr; }
};

class Dag {
public:
  using pdag = std::shared_ptr<Dag_node>;

  pdag root;

  std::unordered_map<Pattern, pdag> pat_dag_map;

  Dag() : root(std::make_shared<Dag_node>()), dag_id_counter(0) {}

  // initialize a dag from a vector of patterns with all leaves being unique,
  // and all patterns either containing leaves or other patterns.
  void init(std::vector<Pattern> pat_set, const Graph_info<g_type>& gi);

  std::unordered_set<pdag> select_points();

  void dump();

private:
  unsigned int dag_id_counter;

  void add_edge(pdag parent, pdag child, unsigned int count);
  pdag add_pat_vert();
  pdag add_leaf_vert(const Lg_type_id::type label, const double area);
};