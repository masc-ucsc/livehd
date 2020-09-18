// main class that hold the whole hierarchy tree and all the operations that are performed on it

#pragma once

#include <memory>  // for shared_ptr
#include <sstream>
#include <string>
#include <utility>  // for std::pair
#include <vector>

#include "dag.hpp"
#include "eprp_var.hpp"
#include "graph_info.hpp"
#include "i_resolve_header.hpp"
#include "pattern.hpp"

// controls for debug output on various stages that have verbose output
constexpr bool hier_verbose  = false;
constexpr bool reg_verbose   = false;
constexpr bool bound_verbose = false;
constexpr bool floor_verbose = false;

// a struct representing a node in a hier_tree
class Hier_node {
public:
  std::string name;

  // which vertices in the graph the (leaf) node maps to
  set_t graph_set;

  std::shared_ptr<Hier_node> parent;
  std::shared_ptr<Hier_node> children[2] = {nullptr, nullptr};

  double area;  // area of the leaf (if node is a leaf)
  double width, height;
  double xpos, ypos;

  Hier_node(const Graph_info<g_type>& gi)
      : graph_set(gi.al.vert_set()), parent(nullptr), area(0.0), width(0.0), height(0.0), xpos(0.0), ypos(0.0) {}

  bool is_leaf() const { return children[0] == nullptr && children[1] == nullptr; }
  bool is_root() const { return parent == nullptr; }
};

struct Dim {
  double width;
  double height;
};

class Hier_tree {
public:
  Hier_tree(Eprp_var& var);

  // copies require copying the entire tree and are very expensive.
  Hier_tree(const Hier_tree& other) = delete;
  Hier_tree& operator=(const Hier_tree& other) = delete;

  // move assignment operator not specified because graph_info contents are really hard to move

  void dump_patterns() const;

  // take in a graph of all nodes in the netlist, and convert it to a tree.
  // min_num_components sets the minimum number of components required to trigger analysis of the hierarchy
  // any node with a smaller area than min_area gets folded into a new supernode with area >= min_area
  void discover_hierarchy(const unsigned int min_size);

  // returns a new tree with small leaf nodes collapsed together
  void collapse(const size_t num_chiers, const double threshold_area);

  // discover similar subgraphs in the collapsed hierarchy and make a dag
  void discover_regularity(const size_t beam_width);

  // gets floorplan dimensions of all patterns using an exhaustive approach if the number of blocks is < optimal_thresh
  // invariant: bounding curves for patterns can only generated after leaf dimensions have been set
  void construct_bounds(const unsigned int optimal_thresh);

  // make actual floorplans based on the patterns previously selected
  void construct_floorplans();

private:
  friend class Pass_fplan_dump;
  using phier = std::shared_ptr<Hier_node>;

  // graphs containing the collapsed netlists (seperated from ginfo for now so things compile)
  // the 0th element is always the uncollapsed graph.
  std::vector<Graph_info<g_type>> collapsed_gis;

  // vector of hierarchy trees with some nodes collapsed
  // 0th element is always uncollapsed hierarchy
  std::vector<phier> hiers;

  // data used by min_cut
  struct Min_cut_data {
    int  d_cost;  // difference between the external and internal cost of the node
    bool active;  // whether the node is being considered for a swap or not
  };

  // make a partition of the graph minimizing the number of edges crossing the cut and keeping in mind area (modified kernighan-lin
  // algorithm)
  std::pair<set_t, set_t> min_wire_cut(set_t& cut_set);

  // make a node for insertion into the hierarchy
  phier make_hier_node(Graph_info<g_type>& gi, const set_t& v);

  // create a hierarchy tree out of existing hierarchies
  phier make_hier_tree(Graph_info<g_type>& gi, phier t1, phier t2);

  // perform hierarchy discovery
  phier discover_hierarchy(set_t& set, unsigned int min_num_components);

  double find_area(phier node) const;

  unsigned int find_tree_size(phier node) const;

  unsigned int find_tree_depth(phier node) const;

  // void dump_node(const phier node) const;

  phier dup_tree(phier oldn, Graph_info<g_type>& new_gi);

  phier collapse(phier node, Graph_info<g_type>& gi, double threshold_area);

  // generator used to make unique node names
  unsigned int unique_node_counter = 0;

  // make a (specific) set of vertices into a (generic) pattern
  Pattern make_generic(Graph_info<g_type>& gi, const set_t& pat) const;

  // find all instantiations of a specific pattern consisting of verts in subg
  set_vec_t find_all_patterns(Graph_info<g_type>& gi, const set_t& subg, const Pattern& gpattern) const;

  // find the most frequently occuring pattern subg
  std::pair<Pattern, unsigned int> find_most_freq_pattern(Graph_info<g_type>& gi, const set_t& subg, const size_t bwidth) const;

  // compress a pattern instantiation so it is represented by a single vertex
  vertex_t compress_inst(Graph_info<g_type>& gi, set_t& subg, set_t& inst);

  // vector of pattern sets, 1 per hierarchy tree
  using pattern_set = std::vector<Pattern>;
  std::vector<pattern_set> pattern_sets;

  using pattern_count = std::unordered_map<Pattern, unsigned int>;
  std::vector<pattern_count> pattern_counts;

  // discover patterns in the hierarchy
  void discover_regularity(const size_t hier_index, const size_t beam_width);

  // dag representing a hierarchy of types
  std::vector<Dag> dags;

  // construct bounding boxes for patterns discovered
  void construct_bounds(const size_t dag_id, const unsigned int optimal_thresh);

  void floorplan_dag_node(const Dag::pdag pd, std::stringstream& outstr, const unsigned int optimal_thresh);

  // how fast blobb should run
  enum speed { blobb_good, blobb_fast, blobb_enum };

  // shell out to BloBB (a quick floorplanner that is used for floorplanning patterns)
  // set hier to true for worse but faster floorplans
  void invoke_blobb(const std::stringstream& instr, std::stringstream& outstr, const bool hier);

  // ask the user for the patterns worth exploring
  void manual_select_points();

  // choose points at random for benchmarking
  void auto_select_points();

  struct pattern_id {
    size_t pset;
    size_t p;
  };

  void floorplan_point();

  void generate_floorplans();

  void floorplan_set(const set_t& set);

  using floorplan = std::vector<Pos>;
  std::vector<floorplan> floorplans;

  void floorplan_dag_set(const std::list<Dag::pdag>& set, std::stringstream& outstr);
};
