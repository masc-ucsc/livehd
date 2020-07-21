#pragma once

#include <string_view>
#include <vector>

#include "lnast.hpp"

class Semantic_check {
private:
protected:
  absl::flat_hash_map<std::string_view, std::string_view> write_dict;
  absl::flat_hash_map<std::string_view, std::string_view> read_dict;
  
  std::vector<std::string_view> lhs_list;
  std::vector<std::vector<std::string_view>> rhs_list;

  std::vector<std::string_view>         inefficient_LNAST;
  std::vector<std::string_view> output_vars;

  bool is_primitive_op(const Lnast_ntype node_type);
  bool is_tree_structs(const Lnast_ntype node_type);
  bool in_write_list(std::string_view node_name, std::string_view stmt_name);
  bool in_read_list(std::string_view node_name, std::string_view stmt_name);
  bool in_lhs_list(std::string_view node_name);
  int  in_rhs_list(std::string_view node_name, int lhs_index);
  bool in_inefficient_LNAST(std::string_view node_name);
  bool in_output_vars(std::string_view node_name);

  void add_to_write_list(Lnast* lnast, std::string_view node_name, std::string_view stmt_name);
  void add_to_read_list(std::string_view node_name, std::string_view stmt_name);
  void add_to_lhs_list(std::string_view node_name);
  void add_to_rhs_list(std::vector<std::string_view> node_name);

  void add_to_output_vars(std::string_view node_name);
  void find_lhs_name(int index);
  void error_print_lnast_by_name(Lnast* lnast, std::string_view error_name);
  void error_print_lnast_by_type(Lnast* lnast, std::string_view error_name);
  void error_print_lnast_var_warn(Lnast* lnast, std::vector<std::string_view> error_names);

  void resolve_read_write_lists(Lnast* lnast);
  void resolve_lhs_rhs_lists();

  void check_primitive_ops(Lnast* lnast, const Lnast_nid& lnidx_opr, const Lnast_ntype node_type, std::string_view
                           stmt_name);
  void check_tree_struct_ops(Lnast* lnast, const Lnast_nid& lnidx_opr, const Lnast_ntype node_type, std::string_view
                             stmt_name);
  void check_if_op(Lnast* lnast, const Lnast_nid& lnidx_opr, std::string_view stmt_name);
  void check_for_op(Lnast* lnast, const Lnast_nid& lnidx_opr, std::string_view stmt_name);
  void check_while_op(Lnast* lnast, const Lnast_nid& lnidx_opr, std::string_view stmt_name);
  void check_func_def(Lnast* lnast, const Lnast_nid& lnidx_opr, std::string_view stmt_name);
  void check_func_call(Lnast* lnast, const Lnast_nid& lnidx_opr, std::string_view stmt_name);

public:
  // NOTE: Only tuple operations implemented are for tuple assignment and tuple concatenation
  void do_check(Lnast* lnast);
};