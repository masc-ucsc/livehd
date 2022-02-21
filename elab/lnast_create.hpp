
#pragma once

// #include <vector>

#include "lconst.hpp"
#include "lnast.hpp"

class Lnast_create {
public:
  Lnast_create();

  // static std::vector<std::shared_ptr<Lnast>> pick_lnast();

  int                    tmp_var_cnt;
  std::shared_ptr<Lnast> lnast;
  Lnast_nid              idx_stmts;

  enum class Net_attr { Input, Output, Register, Local };

  absl::flat_hash_map<std::string, std::string> vname2lname;

  std::string create_lnast_tmp();
  std::string get_lnast_name(std::string_view val);
  std::string get_lnast_lhs_name(std::string_view val);

  void new_lnast(std::string name);

  // static bool has_lnast(std::string name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }

  std::string create_mask_stmts(std::string_view dest_max_bit);
  std::string create_bitmask_stmts(std::string_view max_bit, std::string_view min_bit);
  std::string create_bit_not_stmts(std::string_view var_name);
  std::string create_logical_not_stmts(std::string_view var_name);
  std::string create_reduce_or_stmts(std::string_view var_name);

  std::string create_sra_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_pick_bit_stmts(std::string_view a_var, std::string_view pos);
  std::string create_sext_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_bit_and_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_bit_or_stmts(const std::vector<std::string>& var);
  std::string create_bit_xor_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_shl_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_mask_xor_stmts(std::string_view a_var, std::string_view b_var);
  void          create_dp_assign_stmts(std::string_view a_var, std::string_view b_var);
  void          create_assign_stmts(std::string_view a_var, std::string_view b_var);
  void          create_declare_bits_stmts(std::string_view a_var, bool is_signed, int bits);
  std::string create_minus_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_plus_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_mult_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_div_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_mod_stmts(std::string_view a_var, std::string_view b_var);
  // std::string create_select_stmts(std::string_view sel_var, std::string_view sel_field);
  std::string create_get_mask_stmts(std::string_view sel_var, std::string_view bitmask);
  void          create_set_mask_stmts(std::string_view sel_var, std::string_view bitmask, std::string_view value);
};
