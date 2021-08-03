//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string_view>

#include "cell.hpp"
#include "lgraph.hpp"
#include "lnast.hpp"
#include "pass.hpp"

class Pass_lnast_fromlg : public Pass {
protected:
  uint64_t temp_var_count = 0;
  uint64_t seq_count      = 0;
  bool     put_bw_in_ln   = true;

  absl::flat_hash_map<Node_pin::Compact_class_driver, mmap_lib::str> dpin_name_map;

  void do_trans(Lgraph* g, Eprp_var& var, const mmap_lib::str &module_name);

  void initial_tree_coloring(Lgraph* g);
  void begin_transformation(Lgraph* g, Lnast& lnast, Lnast_nid& ln_node);
  void handle_source_node(Lgraph* lg, Node_pin& pin, Lnast& lnast, Lnast_nid& ln_node);

  void attach_to_lnast(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);

  void        attach_sum_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_binaryop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pid1_pin);
  void        attach_binary_reduc(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_not_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_mask_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_join_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_pick_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_compar_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_simple_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_mux_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_flop_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_latch_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_subgraph_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);
  void        attach_memory_node(Lnast& lnast, Lnast_nid& parent_node, const Node_pin& pin);

  void attach_children_to_node(Lnast& lnast, Lnast_nid& op_node, const Node_pin& pin);
  void attach_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin);
  void attach_cond_child(Lnast& lnast, Lnast_nid& op_node, const Node_pin& dpin);

  void handle_io(Lgraph* g, Lnast_nid& parent_lnast_node, Lnast& lnast);
  void add_bw_in_ln(Lnast& lnast, Lnast_nid& parent_node, bool is_pos, const mmap_lib::str &pin_name, const uint32_t& bits);

  const mmap_lib::str create_temp_var(const mmap_lib::str &str_prefix = "___");
  bool             has_prefix(const mmap_lib::str &test_string);

  const mmap_lib::str dpin_get_name(const Node_pin dpin);
  void             dpin_set_map_name(const Node_pin dpin, const mmap_lib::str &name_part);
  const mmap_lib::str get_new_seq_name();

public:
  static void trans(Eprp_var& var);

  Pass_lnast_fromlg(const Eprp_var& var);

  static void setup();
};
