#pragma once

#include <vector>

#include "mmap_str.hpp"
#include "lnast.hpp"

class Semantic_check {
private:
protected:
  using FlatHashMap = absl::flat_hash_map<mmap_lib::str, mmap_lib::str>;
  using FlatHashSet = absl::flat_hash_set<mmap_lib::str>;

  // Dynamic Hash Maps - change when entering a new scope
  // To be used with in_scope_stack and out_of_scope_stack
  FlatHashMap write_dict;
  FlatHashMap read_dict;

  // Static Hash Maps - do not change when entering a new scope
  FlatHashMap perm_write_dict;
  FlatHashMap perm_read_dict;

  // Hash Set to hold inefficient LNAST node names
  FlatHashSet inefficient_LNAST;

  // Hash Set to hold LNAST output node names when not written to
  FlatHashSet output_vars;

  // Hash Set to hold LNAST node names that are last written to and never read
  FlatHashSet never_read;

  // Hash Set to hold LNAST function names
  FlatHashSet functions;

  // Array of LNAST nodes that hold lhs nodes and Array of Arrays of LNAST nodes that hold rhs nodes
  std::vector<Lnast_nid>              lhs_list;
  std::vector<std::vector<Lnast_nid>> rhs_list;

  // Array of LNAST nodes that are not defined in their scopes
  std::vector<mmap_lib::str> out_of_scope_vars;

  // Stacks used to hold Dynamic Hash Maps when they go in or out of scope
  std::vector<FlatHashMap> in_scope_stack;
  std::vector<FlatHashMap> out_of_scope_stack;

  // Type Check Functions
  static bool is_primitive_op(const Lnast_ntype node_type);
  static bool is_tree_structs(const Lnast_ntype node_type);
  static bool is_temp_var(const mmap_lib::str &node_name);
  static bool is_a_number(const mmap_lib::str &node_name);

  // Existence Check Functions
  static bool             in_map(const FlatHashMap& dict, const mmap_lib::str &node_name);
  bool             in_lhs_list(Lnast_nid node_name);
  bool             in_inefficient_LNAST(const mmap_lib::str &node_name);
  bool             in_output_vars(const mmap_lib::str &node_name);
  mmap_lib::str    in_lhs_list(Lnast* lnast, int index);
  int              in_rhs_list(Lnast* lnast, const mmap_lib::str &node_name, int op_start_index);
  bool             in_in_scope_stack(const mmap_lib::str &node_name);

  // Insert Functions
  void add_to_write_list(Lnast* lnast, const mmap_lib::str &node_name, const mmap_lib::str &stmt_name);
  void add_to_read_list(const mmap_lib::str &node_name, const mmap_lib::str &stmt_name);
  void add_to_output_vars(const mmap_lib::str &node_name);

  // Display Error/Warning Functions
  void print_out_of_scope_vars(Lnast* lnast);
  void error_print_lnast_by_name(Lnast* lnast, const mmap_lib::str &error_name);
  void error_print_lnast_by_type(Lnast* lnast, const mmap_lib::str &error_name);
  void error_print_lnast_var_warn(Lnast* lnast, std::vector<mmap_lib::str> error_names);

  // Miscellaneous Check Functions
  void resolve_read_write_lists(Lnast* lnast);
  void resolve_lhs_rhs_lists(Lnast* lnast);
  void resolve_out_of_scope();
  void resolve_out_of_scope_func_def();

  // Semantic Check Functions
  void check_primitive_ops(Lnast* lnast, const Lnast_nid& lnidx_opr, const Lnast_ntype node_type, const mmap_lib::str &stmt_name);
  void check_tree_struct_ops(Lnast* lnast, const Lnast_nid& lnidx_opr, const Lnast_ntype node_type, const mmap_lib::str &stmt_name);
  void check_if_op(Lnast* lnast, const Lnast_nid& lnidx_opr, const mmap_lib::str &stmt_name);
  void check_for_op(Lnast* lnast, const Lnast_nid& lnidx_opr, const mmap_lib::str &stmt_name);
  void check_while_op(Lnast* lnast, const Lnast_nid& lnidx_opr, const mmap_lib::str &stmt_name);
  void check_func_def(Lnast* lnast, const Lnast_nid& lnidx_opr, const mmap_lib::str &stmt_name);
  void check_func_call(Lnast* lnast, const Lnast_nid& lnidx_opr, const mmap_lib::str &stmt_name);

public:
  // NOTE: Only tuple assignment and tuple concatenation operations implemented
  void do_check(Lnast* lnast);
};