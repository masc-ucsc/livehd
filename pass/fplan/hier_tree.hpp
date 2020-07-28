/*
  This class holds a hierarchy hypertree (coming from json_inou or elsewhere) and two algorithms that operate on the tree:
  1. Hierarchy discovery, where we walk the tree and subdivide it if the number of components in the tree grows beyond a certain number.
  2. Tree collapsing, where nodes with small areas get combined into supernodes with larger areas.
*/

#pragma once

#include <string> // for strings
#include <memory> // for shared_ptr
#include <vector>
#include <functional> // for recursive lambdas
#include <limits> // for most negative value in min cut

#ifndef NDEBUG
#include <iostream> // include printing facilities if we're debugging things
#include <iomanip>
#endif

#include "json_inou.hpp"

#include "i_resolve_header.hpp"

struct Hier_node {
  
  std::string name;
  double area; // area of the leaf or -1 if not a leaf.

  std::shared_ptr<Hier_node> parent = {nullptr};
  std::shared_ptr<Hier_node> children[2] = {nullptr, nullptr};
  int graph_subset; // which part of the graph the node is responsible for (if a leaf node)

  // size of the graph assuming this node is the root node
  // 0 indicates that the size of this subtree has not been calculated yet.
  unsigned int size = 0;
};

typedef std::shared_ptr<Hier_node> phier;

struct Min_cut_data {
  int d_cost; // difference between the external and internal cost of the node
  bool active; // whether the node is being considered for a swap or not
};

class Hier_tree {
public:
  Hier_tree() { }
  
  // take in a vector of all nodes in the netlist, and convert it to a tree.
  // min_num_components sets the minimum number of components required to trigger analysis of the hierarchy
  // any node with a smaller area than min_area gets folded into a new supernode with area >= min_area
  Hier_tree(Graph_info& g, unsigned int min_num_components, double min_area);
  
  // copies require copying the entire tree and are very expensive.
  Hier_tree(const Hier_tree& other) = delete;
	Hier_tree& operator=(const Hier_tree& other) = delete;
  
  // moves defined since copies are deleted
  Hier_tree(Hier_tree&& other) noexcept : 
    root(other.root), 
    area(other.area),
    num_components(other.num_components) { // TODO: delete stuff here? 
    }

  Hier_tree& operator=(Hier_tree&& other) noexcept {
    std::swap(root, other.root);
    std::swap(area, other.area);
    std::swap(num_components, other.num_components);
    return *this;
  }

  // collapse tree to avoid enforcing too much hierarchy (Algorithm 2 in HiReg)
  // TODO: this should eventually output a list of DAGs
  void collapse();

private:
  // gets the size of the hierarchy starting at root and caches it at the node for later
  //unsigned int size(const phier root);
  
  typedef decltype(graph::Bi_adjacency_list().vert_map<Min_cut_data>()) Min_cut_map;
  typedef decltype(graph::Bi_adjacency_list().vert_map<int>()) Set_map;
  typedef decltype(graph::Bi_adjacency_list().insert_vert()) vertex;
  typedef decltype(graph::Bi_adjacency_list().insert_edge(graph::Bi_adjacency_list().insert_vert(), graph::Bi_adjacency_list().insert_vert())) edge;

  std::pair<int, int> populate_cost_map(const graph::Bi_adjacency_list& g, Min_cut_map& m, Set_map& smap);

  std::pair<int, int> populate_set_map(const graph::Bi_adjacency_list& g, Set_map& smap);
  void populate_cost_map(const graph::Bi_adjacency_list& g, Min_cut_map& cmap);

  // make a partition of the graph minimizing the number of edges crossing the cut and keeping in mind area (modified kernighan-lin algorithm)
  void min_wire_cut(Graph_info& info, Set_map& smap);
  
  // create a hierarchy tree out of existing hierarchies
  //phier make_hier_tree(phier t1, phier t2);
  
  // do hierarchy discovery starting with a given cost matrix
  phier discover_hierarchy(Graph_info& g, Set_map& m, int start_set);

  std::shared_ptr<Hier_node> root;

  double area;
  unsigned int num_components;
};
