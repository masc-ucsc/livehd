
#pragma once

#ifndef NDEBUG
#define DEBUG
#endif

// clang-format off
#include "slang/symbols/ASTVisitor.h"

#include <vector>

#include "lnast.hpp"
#include "pass.hpp"
// clang-format on

class Slang_tree {
public:
  Slang_tree();

  static void setup();
  static void process_root(const slang::RootSymbol& root);

  static std::vector<std::shared_ptr<Lnast>> pick_lnast();

protected:
  static inline absl::flat_hash_map<std::string, std::shared_ptr<Lnast>> parsed_lnasts;

  int                    tmp_var_cnt;
  std::shared_ptr<Lnast> lnast;
  Lnast_nid              idx_stmts;

  enum class Net_attr { Input, Output, Register, Local };

  absl::flat_hash_map<std::string, Net_attr> net2attr;

  std::string_view create_lnast_tmp();
  std::string_view create_lnast(int val);
  std::string_view create_lnast(const std::string& val);
  std::string_view create_lnast(std::string_view val);
  std::string_view create_lnast_var(std::string_view val);
  std::string_view create_lnast_lhs_var(std::string_view val);

  void new_lnast(std::string_view name);

  static bool has_lnast(const std::string& name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }
  static bool has_lnast(std::string_view name) { return has_lnast(std::string(name)); }

  bool             process_top_instance(const slang::InstanceSymbol& symbol);
  bool             process(const slang::AssignmentExpression& expr);
  std::string_view process_expression(const slang::Expression& expr);
  std::string_view process_reduce_and(const slang::UnaryExpression& uexpr);

  std::string_view create_mask_stmts(std::string_view dest_max_bit);
  std::string_view create_bit_not_stmts(std::string_view var_name);
  std::string_view create_logical_not_stmts(std::string_view var_name);
  std::string_view create_reduce_or_stmts(std::string_view var_name);
  std::string_view create_reduce_xor_stmts(std::string_view var_name);

  std::string_view create_sra_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_pick_bit_stmts(std::string_view a_var, std::string_view pos);
  std::string_view create_sext_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_bit_and_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_bit_or_stmts(std::vector<std::string_view> var);
  std::string_view create_bit_xor_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_shl_stmts(std::string_view a_var, std::string_view b_var);
  void             create_dp_assign_stmts(std::string_view a_var, std::string_view b_var);
  void             create_assign_stmts(std::string_view a_var, std::string_view b_var);
  void             create_declare_bits_stmts(std::string_view a_var, bool is_signed, int bits);
  std::string_view create_minus_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_plus_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_mult_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_div_stmts(std::string_view a_var, std::string_view b_var);
  std::string_view create_mod_stmts(std::string_view a_var, std::string_view b_var);
};
