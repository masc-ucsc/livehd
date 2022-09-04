//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <string_view>
#include <utility>
#include <vector>
#include <set>

#include "cell.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "pass.hpp"
#include "absl/container/flat_hash_map.h"
// #include "absl/container/btree_set.h"

/*define only 1 of these:*/
//#define DEBUG //print everything
//#define KEEP_DUP //use vector
#define DE_DUP //use set 


class Traverse_lg : public Pass {
public:
  // typedef absl::node_hash_map<Node::Compact_flat, std::pair<absl::btree_set<std::string>, absl::btree_set<std::string>>> setMap_nodeKey;
  // typedef absl::node_hash_map<std::pair<absl::btree_set<std::string>, absl::btree_set<std::string>>, std::vector<Node::Compact_flat> > setMap_pairKey;
  typedef absl::node_hash_map<Node::Compact_flat, std::pair<std::set<std::string>, std::set<std::string>>> setMap_nodeKey;
  typedef absl::node_hash_map<std::pair<std::set<std::string>, std::set<std::string>>, std::vector<Node::Compact_flat> > setMap_pairKey;
private:
  absl::node_hash_map<Node::Compact_flat , std::vector<Node::Compact_flat> > matched_map;
  //absl::node_hash_map<Node::Compact_flat, std::pair<absl::btree_set<std::string>, absl::btree_set<std::string>>> unmatched_map;
  absl::node_hash_map<Node::Compact_flat, std::pair<std::set<std::string>, std::set<std::string>>> unmatched_map;
  absl::node_hash_map<std::pair<std::set<std::string>, std::set<std::string>>, std::vector<Node::Compact_flat> > full_orig_map;
  absl::node_hash_map<std::set<std::string>, std::vector<Node::Compact_flat> > IOtoNodeMap_orig;
  absl::node_hash_map<std::set<std::string>, setMap_pairKey > IOtoNodeMap_synth;
  absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat> > matching_map;
  std::vector<unsigned int> crit_flop_list;//FIXME: currently has node IDs(node.get_nid().value==unsigned int). to be converted to vector of Node::Compact_flat, once the opentimer starts to work.
  std::vector<unsigned int> crit_cell_list;//FIXME: currently has node IDs. to be converted to vector of Node::Compact_flat, once the opentimer starts to work.
  setMap_pairKey cellIOMap_synth;
/*
#ifdef DE_DUP
  setMap nodeIOmap;
#endif
*/
protected:
  //FOR DEBUG:
  void do_travers(Lgraph* g);
  void get_input_node(const Node_pin &pin, std::ofstream& ofs);
  void get_output_node(const Node_pin &pin, std::ofstream& ofs);
  //FOR SET PART:
  void do_travers(Lgraph* g, Traverse_lg::setMap_pairKey &nodeIOmap);
  // void get_input_node(const Node_pin &pin, absl::btree_set<std::string>& in_set);
  // void get_output_node(const Node_pin &pin, absl::btree_set<std::string>& out_set);
  void get_input_node(const Node_pin &pin, std::set<std::string>& in_set, std::set<std::string>& io_set);
  void get_output_node(const Node_pin &pin, std::set<std::string>& out_set, std::set<std::string>& io_set);
  std::vector<std::string> get_map_val(absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat> >& find_in_map, std::string key_str);
  void path_traversal(const Node &startPoint_node);
  bool check_in_cellIOMap_synth(std::set<std::string> &in_set, std::set<std::string> &out_set, Node &start_node);
  bool is_startpoint(Node node_to_eval);
  bool is_endpoint(Node node_to_eval);

public:
  static void travers(Eprp_var& var);

  Traverse_lg(const Eprp_var& var);

  static void setup();
};
