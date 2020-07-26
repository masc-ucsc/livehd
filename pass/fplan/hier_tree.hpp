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

  std::shared_ptr<Hier_node> parent;
  std::shared_ptr<Hier_node> children[2];

  // size of the graph assuming this node is the root node
  // 0 indicates that the size of this subtree has not been calculated yet.
  unsigned int size = 0;
};

typedef std::shared_ptr<Hier_node> phier;

struct Min_cut_data {
  int d_cost; // difference between the external and internal cost of the node
  unsigned int set; // what set the node is in
  bool active; // whether the node is being considered for a swap or not
};

class Hier_tree {
public:
  Hier_tree() { }
  
  // take in a vector of all nodes in the netlist, and convert it to a tree.
  Hier_tree(Graph_info && g);
  
  // copies require copying the entire tree and are very expensive.
  Hier_tree(const Hier_tree& other) = delete;
	Hier_tree& operator=(const Hier_tree& other) = delete;
  
  // moves defined since copies are deleted
  Hier_tree(Hier_tree&& other) noexcept : 
    root(other.root), 
    min_area(other.min_area),
    num_components(other.num_components) { // TODO: delete stuff here? 
    }

  Hier_tree& operator=(Hier_tree&& other) noexcept {
    std::swap(root, other.root);
    std::swap(min_area, other.min_area);
    std::swap(num_components, other.num_components);
    return *this;
  }

  // set the minimum number of components required to trigger analysis of the hierarchy
  // (the reason we do a pass on the hierarchy is because there may be ways to get a better result by shuffling the hierarchy around)
  void set_num_components(const unsigned int new_num_components) { num_components = new_num_components; }
  
  // any node with a smaller area than min_area gets folded into a new node with area >= min_area
  void set_min_node_area(const double new_area) { min_area = new_area; }

  // collapse tree to avoid enforcing too much hierarchy (Algorithm 2 in HiReg)
  // TODO: this should eventually output a list of DAGs
  void collapse();

private:

  // walk the netlist, possibly modifying the hierarchy as we go (Algorithm 1 in HiReg)
  Hier_node discover_hierarchy();

  // gets the size of the hierarchy starting at root and caches it at the node for later
  unsigned int size(const phier root);
  
  // split a cost matrix into two cost matrices
  //std::pair<Graph_info, Graph_info> halve_matrix(const Cost_matrix& old_matrix);

  // fill out the connections in an unfilled matrix
  //void wire_matrix(Cost_matrix& m);
  
  // clear out any temp nodes, if they exist.
  //void prune_matrix(Cost_matrix& m);
  
  typedef decltype(graph::Bi_adjacency_list().vert_map<Min_cut_data>()) Min_cut_map;
  typedef decltype(graph::Bi_adjacency_list().insert_vert()) vertex;
  typedef decltype(graph::Bi_adjacency_list().insert_edge(graph::Bi_adjacency_list().insert_vert(), graph::Bi_adjacency_list().insert_vert())) edge;

  void populate_cost_map(const graph::Bi_adjacency_list& g, Min_cut_map& m);

  // make a partition of the graph minimizing the number of edges crossing the cut and keeping in mind area (modified kernighan-lin algorithm)
  void min_wire_cut(Graph_info& info);
  
  // create a hierarchy tree out of existing hierarchies
  //phier make_hier_tree(phier t1, phier t2);
  
  // do hierarchy discovery starting with a given cost matrix
  phier discover_hierarchy(Graph_info && g);

  std::shared_ptr<Hier_node> root;

  double min_area;
  unsigned int num_components;
};
