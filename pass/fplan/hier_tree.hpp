/*
  This class holds a hierarchy hypertree (coming from json_inou or elsewhere) and two algorithms that operate on the tree:
  1. Hierarchy discovery, where we walk the tree and subdivide it if the number of components in the tree grows beyond a certain
  number.
  2. Tree collapsing, where nodes with small areas get combined into supernodes with larger areas.
*/

#pragma once

#include <memory>  // for shared_ptr
#include <string>
#include <vector>

#include "graph_info.hpp"

// controls for debug output on various stages
constexpr bool hier_verbose = false;
constexpr bool coll_verbose = false;
constexpr bool reg_verbose  = true;

// a struct representing a node in a hier_tree
struct Hier_node {
  std::string name;

  double area = 0.0;  // area of the leaf (if node is a leaf)

  std::shared_ptr<Hier_node> parent      = {nullptr};
  std::shared_ptr<Hier_node> children[2] = {nullptr, nullptr};

  int graph_subset;  // which part of the graph the leaf is responsible for (if node is a leaf)

  bool is_leaf() const { return children[0] == nullptr && children[1] == nullptr; }
};

typedef std::shared_ptr<Hier_node> phier;

class Hier_tree {
public:
  Hier_tree(Graph_info<g_type>&& netlist) : ginfo(std::move(netlist)) {}

  // copies require copying the entire tree and are very expensive.
  Hier_tree(const Hier_tree& other) = delete;
  Hier_tree& operator=(const Hier_tree& other) = delete;

  // moves defined since copies are deleted
  // moved-from object can be left alone, since contents are "unspecified" after move.
  Hier_tree(Hier_tree&& other) noexcept : ginfo(std::move(other.ginfo)) {}

  // move assignment operator not specified because graph_info contents are really hard to move

  void dump() const;

  // take in a graph of all nodes in the netlist, and convert it to a tree.
  // min_num_components sets the minimum number of components required to trigger analysis of the hierarchy
  // any node with a smaller area than min_area gets folded into a new supernode with area >= min_area
  void discover_hierarchy(unsigned int num_components);

  // returns a new tree with small leaf nodes collapsed together (Algorithm 2 in HiReg)
  void collapse(double threshold_area);

  // discover similar subgraphs in the collapsed hierarchy
  void discover_regularity(size_t hier_index);

private:
  // graph containing the divided netlist
  Graph_info<g_type>&& ginfo;

  // data used by min_cut
  struct Min_cut_data {
    int  d_cost;  // difference between the external and internal cost of the node
    bool active;  // whether the node is being considered for a swap or not
  };

  typedef decltype(graph::Bi_adjacency_list().vert_map<Min_cut_data>()) Min_cut_map;

  // make a partition of the graph minimizing the number of edges crossing the cut and keeping in mind area (modified kernighan-lin
  // algorithm)
  std::pair<int, int> min_wire_cut(Graph_info<g_type>& info, int cut_set);

  // make a node for insertion into the hierarchy
  phier make_hier_node(const int set);

  // create a hierarchy tree out of existing hierarchies
  phier make_hier_tree(phier t1, phier t2);

  // perform hierarchy discovery
  phier discover_hierarchy(Graph_info<g_type>& g, int start_set, unsigned int min_num_components);

  double find_area(phier node) const;

  unsigned int find_tree_size(phier node) const;

  unsigned int find_tree_depth(phier node) const;

  void dump_node(const phier node) const;

  phier collapse(phier node, double threshold_area);

  // generator used to make unique node names
  unsigned int node_number = 0;

  // vector of hierarchy trees with some nodes collapsed
  // 0th element is always uncollapsed hierarchy
  std::vector<phier> hiers;

  // find patterns in the collapsed hierarchy
  set_t find_most_freq_pattern(set_t graph, const size_t bwidth);

  // list of node types in the pattern and total size of pattern
  typedef std::pair<std::unordered_set<Lg_type_id::type>, size_t> generic_set_t;

  unsigned int find_value(const set_t& subgraph, const set_t& pattern);
  set_vec_t   find_other_patterns(const set_t& subgraph, const set_t& pattern);
  set_vec_t   find_all_patterns(const set_t& subgraph, const generic_set_t& gpattern);
};
