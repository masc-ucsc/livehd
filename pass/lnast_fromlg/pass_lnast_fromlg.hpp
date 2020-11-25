//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>
#include "lgraph.hpp"
#include "lnast.hpp"
#include "pass.hpp"
#include "cell.hpp"

class Pass_lnast_fromlg : public Pass {
protected:
  uint64_t temp_var_count = 0;
  uint64_t seq_count      = 0;
  bool     put_bw_in_ln   = true;

  absl::flat_hash_map<Node_pin::Compact_class_driver, std::string> dpin_name_map;

  void do_trans(LGraph* g, Eprp_var& var, std::string_view module_name);

  void initial_tree_coloring(LGraph* g, Lnast &lnast);
  void begin_transformation(LGraph* g, Lnast& lnast, Lnast_nid& ln_node);
  void handle_source_node(LGraph* lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node);

  void attach_to_lnast(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);

  void attach_sum_node     (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_binaryop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pid1_pin);
  void attach_binary_reduc (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_not_node     (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_tposs_node   (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_join_node    (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_pick_node    (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_compar_node  (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_simple_node  (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_mux_node     (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_flop_node    (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_latch_node   (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_subgraph_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void attach_memory_node  (Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);

  void attach_children_to_node(Lnast& lnast, Lnast_nid& op_node, const Node_pin& pin);
  void attach_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin);
  void attach_cond_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin);

  void handle_io(LGraph* g, Lnast_nid& parent_lnast_node, Lnast& lnast);
  //void add_bw_in_ln(Lnast& lnast, Lnast_nid& parent_node, const std::string_view& pin_name, const uint32_t& bits);
  void add_bw_in_ln(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin, const std::string_view& pin_name, const uint32_t& bits);

  std::string_view create_temp_var(Lnast& lnast, std::string_view str_prefix = "___");
  bool has_prefix(std::string_view test_string);
  bool has_prefix(std::string test_string);

  std::string_view dpin_get_name(const Node_pin dpin);
  void dpin_set_map_name(const Node_pin dpin, std::string_view name_part);
  std::string_view get_new_seq_name(Lnast& lnast);

public:
  static void trans(Eprp_var& var);

  Pass_lnast_fromlg(const Eprp_var& var);

  static void setup();
};
