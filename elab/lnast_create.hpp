
#pragma once

// #include <vector>

#include "lconst.hpp"
#include "lnast.hpp"

class Lnast_create {
public:
  Lnast_create();

  // static std::vector<std::shared_ptr<Lnast>> pick_lnast();

  // static inline absl::flat_hash_map<mmap_lib::str, std::shared_ptr<Lnast>> parsed_lnasts;

  int                    tmp_var_cnt;
  std::shared_ptr<Lnast> lnast;
  Lnast_nid              idx_stmts;

  enum class Net_attr { Input, Output, Register, Local };

  absl::flat_hash_map<mmap_lib::str, mmap_lib::str> vname2lname;

  mmap_lib::str create_lnast_tmp();
  mmap_lib::str get_lnast_name(mmap_lib::str val);
  mmap_lib::str get_lnast_lhs_name(mmap_lib::str val);

  void new_lnast(mmap_lib::str name);

  // static bool has_lnast(mmap_lib::str name) { return parsed_lnasts.find(name) != parsed_lnasts.end(); }

  mmap_lib::str create_mask_stmts(mmap_lib::str dest_max_bit);
  mmap_lib::str create_bitmask_stmts(mmap_lib::str max_bit, mmap_lib::str min_bit);
  mmap_lib::str create_bit_not_stmts(mmap_lib::str var_name);
  mmap_lib::str create_logical_not_stmts(mmap_lib::str var_name);
  mmap_lib::str create_reduce_or_stmts(mmap_lib::str var_name);

  mmap_lib::str create_sra_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_pick_bit_stmts(mmap_lib::str a_var, mmap_lib::str pos);
  mmap_lib::str create_sext_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_bit_and_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_bit_or_stmts(const std::vector<mmap_lib::str>& var);
  mmap_lib::str create_bit_xor_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_shl_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_mask_xor_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  void          create_dp_assign_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  void          create_assign_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  void          create_declare_bits_stmts(mmap_lib::str a_var, bool is_signed, int bits);
  mmap_lib::str create_minus_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_plus_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_mult_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_div_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  mmap_lib::str create_mod_stmts(mmap_lib::str a_var, mmap_lib::str b_var);
  // mmap_lib::str create_select_stmts(mmap_lib::str sel_var, mmap_lib::str sel_field);
  mmap_lib::str create_get_mask_stmts(mmap_lib::str sel_var, mmap_lib::str bitmask);
  void          create_set_mask_stmts(mmap_lib::str sel_var, mmap_lib::str bitmask, mmap_lib::str value);
};
