/*
  This class holds a hierarchy hypertree (coming from json_inou or elsewhere) and two algorithms that operate on the tree:
  1. Hierarchy discovery, where we walk the tree and subdivide it if the number of components in the tree grows beyond a certain
  number.
  2. Tree collapsing, where nodes with small areas get combined into supernodes with larger areas.
*/

#pragma once

#include <algorithm>  // for std::max, std::min, std::sort
#include <memory>     // for shared_ptr
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>  // for std::pair
#include <vector>

#include "dag.hpp"
#include "eprp_var.hpp"
#include "graph_info.hpp"
#include "i_resolve_header.hpp"
#include "pattern.hpp"

// controls for debug output on various stages
constexpr bool hier_verbose = false;
// constexpr bool coll_verbose = false;
constexpr bool reg_verbose   = false;
constexpr bool bound_verbose = false;

// a struct representing a node in a hier_tree
struct Hier_node {
  std::string name;

  double area = 0.0;  // area of the leaf (if node is a leaf)

  std::shared_ptr<Hier_node> parent      = {nullptr};
  std::shared_ptr<Hier_node> children[2] = {nullptr, nullptr};

  int graph_subset;  // which part of the graph the leaf is responsible for (if node is a leaf)

  bool is_leaf() const { return children[0] == nullptr && children[1] == nullptr; }
};

class Hier_tree {
public:
  Hier_tree(Eprp_var& var);
  // Hier_tree(Graph_info<g_type>&& netlist) : ginfo(std::move(netlist)), hier_patterns({}) {}

  // copies require copying the entire tree and are very expensive.
  Hier_tree(const Hier_tree& other) = delete;
  Hier_tree& operator=(const Hier_tree& other) = delete;

  // moves defined since copies are deleted
  // moved-from object can be left alone, since contents are "unspecified" after move.
  // Hier_tree(Hier_tree&& other) noexcept : ginfo(std::move(other.ginfo)) {}

  // move assignment operator not specified because graph_info contents are really hard to move

  void dump_hier() const;

  void dump_patterns() const;

  // take in a graph of all nodes in the netlist, and convert it to a tree.
  // min_num_components sets the minimum number of components required to trigger analysis of the hierarchy
  // any node with a smaller area than min_area gets folded into a new supernode with area >= min_area
  void discover_hierarchy(const unsigned int min_size);

  // allocates hierarchies
  void make_hierarchies(const size_t num_hiers) { hiers.resize(num_hiers); }

  // returns a new tree with small leaf nodes collapsed together
  void collapse(const size_t hier_index, const double threshold_area);

  // discover similar subgraphs in the collapsed hierarchy (and make a dag)
  void discover_regularity(const size_t beam_width);

  // generate dags, 1 per set of patterns
  void make_dags();

  // gets floorplan dimensions of all patterns using an exhaustive approach if the number of blocks is < optimal_thresh
  // invariant: bounding curves for patterns can only generated after leaf dimensions have been set
  void construct_bounds(const unsigned int optimal_thresh);

  // generate various leaf implementations to use for constructing patterns
  void generate_leaf_dims(const unsigned int num_dims);

  void construct_floorplans();

private:
  friend class Pass_fplan_dump;

  using phier = std::shared_ptr<Hier_node>;

  // graph containing the divided netlist
  Graph_info<g_type> ginfo;

  // data used by min_cut
  struct Min_cut_data {
    int  d_cost;  // difference between the external and internal cost of the node
    bool active;  // whether the node is being considered for a swap or not
  };

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

  // keep track of all the kinds of vertices we can have, as well as how many there are

  unsigned int generic_pattern_size(const Pattern& gset) const;

  Pattern make_generic(const set_t& pat) const;

  set_vec_t find_all_patterns(const set_t& subg, const Pattern& gpattern) const;

  Pattern find_most_freq_pattern(const set_t& subg, const size_t bwidth) const;

  vertex_t compress_inst(set_t& subg, set_t& inst);

  // vector of pattern sets, 1 per hierarchy tree
  std::vector<std::vector<Pattern>> hier_patterns;

  void discover_regularity(const size_t hier_index, const size_t beam_width);

  // dag representing a hierarchy of types
  std::vector<Dag> dags;

  // for each leaf node, there can be various different dimensions used
  std::unordered_map<Lg_type_id::type, std::vector<Layout>> leaf_dims;

  unsigned int num_dims;
};
