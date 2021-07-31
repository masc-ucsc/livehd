
#pragma once

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
  static inline absl::flat_hash_map<mmap_lib::str, std::shared_ptr<Lnast>> parsed_lnasts;

  int                    tmp_var_cnt;
  std::shared_ptr<Lnast> lnast;
  Lnast_nid              idx_stmts;

  enum class Net_attr { Input, Output, Register, Local };

  absl::flat_hash_map<mmap_lib::str, Net_attr> net2attr;

  mmap_lib::str create_lnast_tmp();
  mmap_lib::str create_lnast_var(mmap_lib::str val);
  mmap_lib::str create_lnast_lhs_var(mmap_lib::str val);

  void new_lnast(mmap_lib::str name);

  static bool has_lnast(mmap_lib::str name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }

  bool             process_top_instance(const slang::InstanceSymbol& symbol);
  bool             process(const slang::AssignmentExpression& expr);
  mmap_lib::str process_expression(const slang::Expression& expr);
  mmap_lib::str process_reduce_and(const slang::UnaryExpression& uexpr);

  mmap_lib::str create_mask_stmts(mmap_lib::str dest_max_bit);
  mmap_lib::str create_bit_not_stmts(mmap_lib::str var_name);
  mmap_lib::str create_logical_not_stmts(mmap_lib::str var_name);
  mmap_lib::str create_reduce_or_stmts(mmap_lib::str var_name);
  mmap_lib::str create_reduce_xor_stmts(mmap_lib::str var_name);

  mmap_lib::str create_sra_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_pick_bit_stmts(mmap_lib::str a_var, mmap_lib::str pos);
  mmap_lib::str create_sext_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_bit_and_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_bit_or_stmts(const std::vector<mmap_lib::str>& var);
  mmap_lib::str create_bit_xor_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_shl_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  void             create_dp_assign_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  void             create_assign_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  void             create_declare_bits_stmts(mmap_lib::str a_var, bool is_signed, int bits);
  mmap_lib::str create_minus_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_plus_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_mult_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_div_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_mod_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
};
