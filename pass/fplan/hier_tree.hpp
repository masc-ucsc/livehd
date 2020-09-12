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

#include "Stable_adjacency_list.hpp" // for bounding curves

#include "graph_info.hpp"
#include "i_resolve_header.hpp"

// controls for debug output on various stages
constexpr bool hier_verbose = false;
// constexpr bool coll_verbose = false;
constexpr bool reg_verbose = false;
constexpr bool bound_verbose = true;

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
  Hier_tree(Graph_info<g_type>&& netlist) : ginfo(std::move(netlist)) {}

  // copies require copying the entire tree and are very expensive.
  Hier_tree(const Hier_tree& other) = delete;
  Hier_tree& operator=(const Hier_tree& other) = delete;

  // moves defined since copies are deleted
  // moved-from object can be left alone, since contents are "unspecified" after move.
  Hier_tree(Hier_tree&& other) noexcept : ginfo(std::move(other.ginfo)) {}

  // move assignment operator not specified because graph_info contents are really hard to move

  void dump_hier() const;

  void dump_dag() const;

  // take in a graph of all nodes in the netlist, and convert it to a tree.
  // min_num_components sets the minimum number of components required to trigger analysis of the hierarchy
  // any node with a smaller area than min_area gets folded into a new supernode with area >= min_area
  void discover_hierarchy(const unsigned int num_components);

  // returns a new tree with small leaf nodes collapsed together (Algorithm 2 in HiReg)
  void collapse(const double threshold_area);

  // discover similar subgraphs in the collapsed hierarchy
  void discover_regularity(const size_t hier_index, const size_t beam_width);

  // construct a boundary curve using an exhaustive approach if the number of blocks is < optimal_thresh
  // num_inst indicates how many instantiations of each block we should create
  void construct_bounds(const size_t pat_index, const unsigned int optimal_thresh);

private:
  friend class Pass_fplan_dump;

  using phier = std::shared_ptr<Hier_node>;

  // graph containing the divided netlist
  Graph_info<g_type>&& ginfo;

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
  using pattern_t = std::unordered_map<Lg_type_id::type, unsigned int>;
  using pattern_vec_t = std::vector<pattern_t>;

  unsigned int generic_pattern_size(const pattern_t& gset) const;

  pattern_t make_generic(const set_t& pat) const;

  set_vec_t find_all_patterns(const set_t& subg, const pattern_t& gpattern) const;
  
  // find patterns in the collapsed hierarchy
  pattern_t find_most_freq_pattern(const set_t& subg, const size_t bwidth) const;

  void compress_hier(set_t&, const pattern_t&, std::vector<vertex_t>&);

  std::vector<pattern_vec_t> pattern_lists;

  std::vector<std::pair<double, double>> bounding_curve;

  // encapsulating dag class because we don't really need the actual nodes
  class dag {
    using dag_t = graph::Stable_out_adjacency_list;
    using dag_map_t = graph::Vert_map<dag_t, Lg_type_id::type>;

    dag_t g;
    dag_map_t labels;

    dag() : g(dag_t()), labels(g.vert_map<Lg_type_id::type>()) {}

    void fold(const std::unordered_map<Lg_type_id::type, unsigned int>& pat) {
      for (auto gv : pat) {
        auto new_v = g.insert_vert();
        labels[new_v] = gv.first;
      }
    }
  };
 
};
