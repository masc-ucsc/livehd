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

#include "iassert.hpp"

// A hierarchy node, containing parent / children pointers and a list of connections to other nodes.
struct Hier_node {
  
  typedef std::shared_ptr<Hier_node> pnode; // this typedef is useful both inside and outside Hier_node

  std::string name;

  pnode parent;
  std::vector<pnode> children;

  // each connection from a node to another node has as weight attached to it
  std::vector<std::pair<pnode, unsigned int>> connect_list;
  
  // node area (mm^2)
  double area;

  // size of the graph assuming this node is the root node
  // 0 indicates that the size of this subtree has not been calculated yet.
  unsigned int size = 0;
};

typedef std::shared_ptr<Hier_node> pnode;

struct cost_matrix_row {
  pnode node; // the actual hier_node we're referring to
  std::vector<int> connect_cost; // list of connection weights to all other nodes
  int d_cost; // difference between the external and internal cost of the node
  bool active; // whether the node is being considered for a swap or not
  unsigned int set; // what set the node is in
};

typedef std::vector<cost_matrix_row> cost_matrix;

class Hier_tree {
public:
  Hier_tree() { }
  
  // construct a new tree from a node, assuming that node represents a hierarchy
  Hier_tree(const Hier_node& n) { root = std::make_shared<Hier_node>(n); }
  
  // copies require copying the entire tree and are very expensive.
  Hier_tree(const Hier_tree& other) = delete;
	Hier_tree& operator=(const Hier_tree& other) = delete;
  
  // moves defined since copies are deleted
  Hier_tree(Hier_tree&& other) noexcept : 
    root(other.root), 
    min_area(other.min_area),
    num_components(other.num_components) { other.root.reset(); }

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
  
  // walk the netlist, possibly modifying the hierarchy as we go (Algorithm 1 in HiReg)
  pnode discover_hierarchy();

  // collapse tree to avoid enforcing too much hierarchy (Algorithm 2 in HiReg)
  // TODO: this should eventually return a vector of DAGs
  void collapse(const double min_area);

private:
  pnode root;

  // gets the size of the hierarchy starting at root and caches it at the node for later
  unsigned int size(const pnode root);
  
  // create a connectivity matrix for use in min_wire_cut
  cost_matrix make_matrix(pnode root);
  
  // split a cost matrix into two cost matrices
  std::pair<cost_matrix, cost_matrix> halve_matrix(const cost_matrix& old_matrix);

  // fill out the connections in an unfilled matrix
  void wire_matrix(cost_matrix& m);
  
  // clear out any temp nodes, if they exist.
  void prune_matrix(cost_matrix& m);
  
  // make a partition of the graph minimizing the number of edges crossing the cut and keeping in mind area (modified kernighan-lin algorithm)
  std::pair<std::vector<pnode>, std::vector<pnode>> min_wire_cut(cost_matrix& m);
  
  // create a hierarchy out of two existing hierarchies...?
  pnode make_tree(pnode t1, pnode t2);
  
  // do hierarchy discovery starting using a given cost matrix
  pnode discover_hierarchy(cost_matrix& m);

  double min_area;
  unsigned int num_components;
};
