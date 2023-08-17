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
#include "absl/container/internal/raw_hash_set.h"
#define DE_DUP  // use set
// #define BASIC_DBG
// #define EXTENSIVE_DBG
// #define FULL_RUN_FOR_EVAL //if defined then every node is critical node
													//possible to compare matching map for every node
                          // used in nl2nl match
// #define FOR_EVAL // used in orig-to-NL match (just soime prints)

class Traverse_lg : public Pass {
public:
private:
	std::string orig_lg_name;
  std::string synth_tool;
	// std::string synth_lg_name;
  typedef absl::node_hash_map<Node::Compact_flat, std::pair<std::set<std::string>, std::set<std::string>>> setMap_nodeKey;
  typedef absl::node_hash_map<std::pair<std::set<std::string>, std::set<std::string>>, std::vector<Node::Compact_flat>>
      setMap_pairKey;
  typedef absl::node_hash_map<std::pair<std::set<Node::Compact_flat>, std::set<Node::Compact_flat>>,
                              std::vector<Node::Compact_flat>>
                                                                           setMap_pairCompFlatKey;
  absl::node_hash_map<Node::Compact_flat, std::vector<Node::Compact_flat>> matched_map;
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
  bool                   set_theory_match(std::set<std::string> synth_set, const std::vector<Node::Compact_flat> &synth_val,
                                             setMap_pairKey &orig_map);
  void weighted_match_LoopLastOnly() ;
  void weighted_match() ;
  template <typename T>
  std::set<T> getUnion(const std::set<T> &a, const std::set<T> &b) {
    std::set<T> result = a;
    result.insert(b.begin(), b.end());
    return result;
  }
  template <typename T>
  absl::flat_hash_set<T> get_union(const absl::flat_hash_set<T> &a, const absl::flat_hash_set<T> &b) const {
    absl::flat_hash_set<T> result = a;
    result.insert(b.begin(), b.end());
    return result;
  }
  template<class InputIt1, class InputIt2, class OutputIt>
  void get_intersection(InputIt1 first1, InputIt1 last1, InputIt2 first2, InputIt2 last2, OutputIt d_first)  {
      while (first1 != last1 && first2 != last2) {
        if (*first1 < *first2) {
          ++first1;
        } else  {
          if (!(*first2 < *first1)) {
            *d_first++ = *first1++; // *first1 and *first2 are equivalent.
          }
          ++first2;
        }
      }
  }
  std::set<Node::Compact_flat> combo_loop_vec;
  typedef absl::node_hash_map<Node_pin::Compact_flat, absl::flat_hash_set<Node_pin::Compact_flat>> map_of_sets;
  void debug_function(Lgraph *lg);
  void                                do_travers(Lgraph *g, Traverse_lg::setMap_pairKey &nodeIOmap, bool do_matching);
  void                                fast_pass_for_inputs(Lgraph *lg, map_of_sets &inp_map_of_sets, bool is_orig_lg);
  void                                fwd_traversal_for_inp_map(Lgraph *lg, map_of_sets &inp_map_of_sets, bool is_orig_lg);
  std::vector<Node_pin> traverse_order;
  std::vector<Node_pin::Compact_flat> forced_match_vec;
  void                                bwd_traversal_for_out_map( map_of_sets &out_map_of_sets , bool is_orig_lg);
  void                                make_io_maps(Lgraph *lg, map_of_sets &inp_map_of_sets, map_of_sets &out_map_of_sets , bool is_orig_lg);
  void                                make_io_maps_boundary_only(Lgraph *lg, map_of_sets &inp_map_of_sets, map_of_sets &out_map_of_sets, bool is_orig_lg );
  void print_io_map( const map_of_sets &the_map_of_sets) const;
  void print_name2dpin(const absl::flat_hash_map<std::string, Node_pin::Compact_flat> &name2dpin) const;
  void print_name2dpins(const absl::flat_hash_map<std::string, absl::flat_hash_set<Node_pin::Compact_flat>> &name2dpins) const;
  void netpin_to_origpin_default_match(Lgraph *orig_lg, Lgraph *synth_lg);
  void matching_pass_io_boundary_only(map_of_sets &map_of_sets_synth, map_of_sets &map_of_sets_orig);
  bool complete_io_match(bool flop_only);//returns true if any matching took place
  bool surrounding_cell_match();//returns true if any matching took place
  bool surrounding_cell_match_final();//matches any unmatched cell with resolved surrounding cells. returns T if unmatched still left.
  std::vector<Node_pin::Compact_flat> get_surrounding_pins(Node &node, Node_pin::Compact_flat main_node_dpin = Node_pin::Compact_flat(0,0,0)) const;
  std::vector<std::pair<uint64_t, uint64_t>> get_loc_vec(absl::flat_hash_set<Node_pin::Compact_flat> &orig_node_pin_vec) const ;//FIXME: should have fnmae as well?
  void remove_from_crit_node_set(const Node_pin::Compact_flat &dpin_cf);
  void report_critical_matches_with_color();
  void resolution_of_synth_map_of_sets(map_of_sets &synth_map_of_set);
  void set_theory_match_loopLast_only();
  void set_theory_match_final();
  bool set_theory_match(map_of_sets &io_map_of_sets_synth, map_of_sets &io_map_of_sets_orig);
  /* If you are making io union for synth MoS for combinational matching, then why not make the union of only crit nodes' dpins from the synth MoS. We need not resolve other synth MoS entries and thus need not make union for them*/
  map_of_sets make_in_out_union(const map_of_sets &inp_map_of_sets, const  map_of_sets &out_map_of_sets, bool loop_last_only, bool union_of_crit_entries_only) const ;
  map_of_sets convert_io_MoS_to_node_MoS_LLonly(const map_of_sets &io_map_of_sets);
  map_of_sets obtain_MoS_LLonly(const map_of_sets &io_map_of_sets);
  void print_set (const absl::flat_hash_set<Node_pin::Compact_flat> &set_of_dpins) const ;
  void print_everything() ;
  float get_matching_weight(const absl::flat_hash_set<Node_pin::Compact_flat> &synth_set, const absl::flat_hash_set<Node_pin::Compact_flat> &orig_set) const;
  absl::flat_hash_set<Node_pin::Compact_flat> get_matching_map_val(const Node_pin::Compact_flat &dpin_cf) const ;
  absl::node_hash_map<Node_pin::Compact_flat, int> crit_node_map;
  absl::flat_hash_set<Node_pin::Compact_flat> crit_node_set;
  absl::flat_hash_set<Node_pin::Compact_flat> flop_set;
  map_of_sets inp_map_of_sets_synth;
  map_of_sets out_map_of_sets_synth;
  map_of_sets inp_map_of_sets_orig;
  map_of_sets out_map_of_sets_orig;
  absl::flat_hash_set<Node_pin::Compact_flat> mark_loop_stop;//known points can be reliably treated as ins and outs. Thus these entries can be inserted in MoSs.
  absl::node_hash_map<Node_pin::Compact_flat, absl::flat_hash_set<Node_pin::Compact_flat> > net_to_orig_pin_match_map;
  void remove_pound_and_bus(std::string &dpin_name);
  void remove_resolved_from_orig_MoS();
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
