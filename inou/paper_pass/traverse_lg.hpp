//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "cell.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "pass.hpp"
// #include "absl/container/btree_set.h"
#include "absl/container/internal/raw_hash_set.h"
#define DE_DUP  // use set

class Traverse_lg : public Pass {
public:
private:
  // typedef absl::node_hash_map<Node::Compact_flat, std::pair<absl::btree_set<std::string>, absl::btree_set<std::string>>>
  // setMap_nodeKey; typedef absl::node_hash_map<std::pair<absl::btree_set<std::string>, absl::btree_set<std::string>>,
  // std::vector<Node::Compact_flat> > setMap_pairKey;
  typedef absl::node_hash_map<Node::Compact_flat, std::pair<std::set<std::string>, std::set<std::string>>> setMap_nodeKey;
  typedef absl::node_hash_map<std::pair<std::set<std::string>, std::set<std::string>>, std::vector<Node::Compact_flat>>
      setMap_pairKey;
  typedef absl::node_hash_map<std::pair<std::set<Node::Compact_flat>, std::set<Node::Compact_flat>>,
                              std::vector<Node::Compact_flat>>
                                                                           setMap_pairCompFlatKey;
  absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat>> matched_map;
  // absl::node_hash_map<Node::Compact_flat, std::pair<absl::btree_set<std::string>, absl::btree_set<std::string>>> unmatched_map;
  absl::node_hash_map<Node::Compact_flat, std::pair<std::set<std::string>, std::set<std::string>>> unmatched_map;
  setMap_pairKey                                                                                   full_orig_map;
  absl::node_hash_map<std::set<std::string>, std::vector<Node::Compact_flat>>                      IOtoNodeMap_orig;
  absl::node_hash_map<std::set<std::string>, setMap_pairKey>                                       IOtoNodeMap_synth;
  void print_IOtoNodeMap_synth(const absl::node_hash_map<std::set<std::string>, setMap_pairKey> &mapInMap);
  void print_MapOf_SetPairAndVec(const setMap_pairKey &MapOf_SetPairAndVec);
  absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat>> matching_map;
  absl::node_hash_map<Node::Compact_flat, int>                             matched_color_map;
  std::vector<Node::Compact_flat>                                          crit_flop_list;
  std::vector<Node::Compact_flat>                                          crit_cell_list;
  absl::node_hash_map<Node::Compact_flat, int>                             crit_flop_map;
  absl::node_hash_map<Node::Compact_flat, int>                             crit_cell_map;
  setMap_pairKey                                                           cellIOMap_synth;
  bool                   probabilistic_match(std::set<std::string> synth_set, const std::vector<Node::Compact_flat> &synth_val,
                                             setMap_pairKey &orig_map);
  Node_pin::Compact_flat get_dpin_cf(const Node &node) const;

  template <typename T>
  std::set<T> getUnion(const std::set<T> &a, const std::set<T> &b) {
    std::set<T> result = a;
    result.insert(b.begin(), b.end());
    return result;
  }
  std::set<Node::Compact_flat> combo_loop_vec;
  void                                do_travers(Lgraph *g, Traverse_lg::setMap_pairKey &nodeIOmap, bool do_matching);
  void                                boundary_traversal(Lgraph *lg);
  void                                fast_pass_for_inputs(Lgraph *lg);
  void                                fwd_traversal_for_inp_map(Lgraph *lg);
  std::vector<Node_pin::Compact_flat> traverse_order;
  void                                bwd_traversal_for_out_map();
  void                                make_io_maps(Lgraph *lg);
  void  print_io_map( const absl::node_hash_map<Node_pin::Compact_flat, absl::flat_hash_set<Node_pin::Compact_flat>> &the_map_of_sets) const;
  absl::node_hash_map<Node_pin::Compact_flat, absl::flat_hash_set<Node_pin::Compact_flat>> inp_map_of_sets;
  absl::node_hash_map<Node_pin::Compact_flat, absl::flat_hash_set<Node_pin::Compact_flat>> out_map_of_sets;
  absl::node_hash_map<Node_pin::Compact_flat, int>                                         crit_node_map;
  // void get_input_node(const Node_pin &pin, absl::btree_set<std::string>& in_set);
  // void get_output_node(const Node_pin &pin, absl::btree_set<std::string>& out_set);
  //  Node_pin/*FIXME?: ::Compact_flat*/ get_input_node(const Node_pin &pin, std::set<std::string>& in_set, std::set<std::string>&
  //  io_set, bool addToCFL = false);
  void get_input_node(const Node_pin &pin, std::set<std::string> &in_set, std::set<std::string> &io_set, bool addToCFL = false);
  void get_output_node(const Node_pin &pin, std::set<std::string> &out_set, std::set<std::string> &io_set, bool addToCFL = false);
  std::vector<std::string> get_map_val(absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat>> &find_in_map,
                                       std::string                                                               key_str);
  void                     path_traversal(const Node &startPoint_node, const std::set<std::string> synth_set,
                                          const std::vector<Node::Compact_flat> &synth_val, Traverse_lg::setMap_pairKey &cellIOMap_orig);
  bool check_in_cellIOMap_synth(std::set<std::string> &in_set, std::set<std::string> &out_set, Node &start_node);
  bool is_startpoint(const Node &node_to_eval) const;
  bool is_endpoint(const Node &node_to_eval) const;

public:
  static void travers(Eprp_var &var);

  Traverse_lg(const Eprp_var &var);

  static void setup();
};
