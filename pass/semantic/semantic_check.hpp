#pragma once

#include <string_view>
#include <vector>

#include "lnast.hpp"

class Semantic_check {
private:
protected:
  absl::flat_hash_set<std::string_view> write_list;
  absl::flat_hash_set<std::string_view> read_list;
  
  // Used vectors because now order matters
  std::vector<std::string_view> assign_lhs_list;
  std::vector<std::string_view> assign_rhs_list;

  std::vector<std::string_view> inefficient_LNAST;

  bool is_primitive_op(const Lnast_ntype node_type);
  bool is_tree_structs(const Lnast_ntype node_type);
  bool in_write_list(std::string_view node_name);
  bool in_read_list(std::string_view node_name);
  bool in_assign_lhs_list(std::string_view node_name);
  bool in_assign_rhs_list(std::string_view node_name);
  bool in_inefficient_LNAST(std::string_view node_name);

  void add_to_write_list(std::string_view node_name);
  void add_to_read_list(std::string_view node_name);
  void add_to_assign_lhs_list(std::string_view node_name);
  void add_to_assign_rhs_list(std::string_view node_name);
  void find_lhs_name(int index);

  void resolve_read_write_lists();
  void resolve_assign_lhs_rhs_lists();

  void check_primitive_ops(Lnast* lnast, const Lnast_nid& lnidx_opr, const Lnast_ntype node_type);
  void check_if_op(Lnast* lnast, const Lnast_nid& lnidx_opr);
  void check_for_op(Lnast* lnast, const Lnast_nid& lnidx_opr);
  void check_while_op(Lnast* lnast, const Lnast_nid& lnidx_opr);
  void check_func_def(Lnast* lnast, const Lnast_nid& lnidx_opr);
  void check_func_call(Lnast* lnast, const Lnast_nid& lnidx_opr);

public:
  // NOTE: Only tuple operations implemented are for tuple assignment and tuple concatenation
  void do_check(Lnast* lnast);
};