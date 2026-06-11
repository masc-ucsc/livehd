
#pragma once

#include <stack>

#include "absl/container/flat_hash_set.h"
#include "hlop/dlop.hpp"
#include "lnast.hpp"

// Frontend-agnostic LNAST construction surface. Owns the shared lnast and
// the current `idx_stmts` cursor; both `inou/slang` and `inou/prp` build
// against this so tmp-ref minting, scope tracking, and stmt emission stay
// in one place. See cleanup_todo.md §3.4.
class Lnast_builder {
public:
  Lnast_builder();

  int                    tmp_var_cnt{0};
  std::shared_ptr<Lnast> lnast;
  Lnast_nid              idx_stmts;

  absl::flat_hash_map<std::string, std::string> vname2lname;

  // ── cursor primitives ────────────────────────────────────────────────────
  // add_child(node):           append `node` under the current idx_stmts.
  // add_child(parent, node):   append `node` under `parent` (nodes you just
  //                            created, e.g. operands of a fresh op node).
  // append_after(sib, node):   insert `node` right after `sib`.
  // push_stmts(new_stmts):     save the current cursor and switch to
  //                            `new_stmts` (typically the body of a fresh
  //                            if/while/for scope). Pair with pop_stmts().
  // pop_stmts():               restore the cursor saved by the matching
  //                            push_stmts().
  Lnast_nid add_child(Lnast_ntype::Lnast_ntype_int type) { return lnast->add_child(idx_stmts, type); }
  Lnast_nid add_child(const Lnast_node& n) { return lnast->add_child(idx_stmts, n); }
  Lnast_nid add_child(const Lnast_nid& parent, Lnast_ntype::Lnast_ntype_int type) { return lnast->add_child(parent, type); }
  Lnast_nid add_child(const Lnast_nid& parent, const Lnast_node& n) { return lnast->add_child(parent, n); }
  Lnast_nid append_after(const Lnast_nid& sibling, Lnast_ntype::Lnast_ntype_int type) {
    return lnast->append_sibling(sibling, type);
  }
  Lnast_nid append_after(const Lnast_nid& sibling, const Lnast_node& n) { return lnast->append_sibling(sibling, n); }
  void      push_stmts(const Lnast_nid& new_stmts) {
    stmts_stack_.push(idx_stmts);
    idx_stmts = new_stmts;
  }
  void pop_stmts() {
    I(!stmts_stack_.empty());
    idx_stmts = stmts_stack_.top();
    stmts_stack_.pop();
  }
  // True while the cursor is the top-level (file-scope) stmts — nothing has
  // been pushed. Task 1m uses this to validate `pub` placement.
  bool at_top_stmts() const { return stmts_stack_.empty(); }

  // ── naming ───────────────────────────────────────────────────────────────
  std::string      create_lnast_tmp();
  Lnast_node       mint_tmp_ref() { return Lnast_node::create_ref(create_lnast_tmp()); }
  std::string      get_lnast_name(std::string_view val, bool last_value);
  std::string_view get_lnast_lhs_name(std::string_view val);
  void             mark_input_name(std::string_view lname) { input_lnames_.emplace(lname); }

  void new_lnast(std::string_view name);

  // ── stmt emitters ────────────────────────────────────────────────────────
  // Each emitter writes one statement (or a small fixed pattern) under the
  // current `idx_stmts`, returning the result ref's lname for the cases that
  // produce a value.
  std::string create_mask_stmts(std::string_view dest_max_bit);
  std::string create_bitmask_stmts(std::string_view max_bit, std::string_view min_bit);
  std::string create_bit_not_stmts(std::string_view var_name);
  std::string create_log_not_stmts(std::string_view var_name);
  std::string create_red_or_stmts(std::string_view var_name);
  std::string create_red_and_stmts(std::string_view var_name);
  std::string create_red_xor_stmts(std::string_view var_name);

  std::string create_sra_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_pick_bit_stmts(std::string_view a_var, std::string_view pos);
  std::string create_sext_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_bit_and_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_bit_or_stmts(const std::vector<std::string>& var);
  std::string create_bit_xor_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_shl_stmts(std::string_view a_var, std::string_view b_var);
  void        create_dp_assign_stmts(std::string_view a_var, std::string_view b_var);
  void        create_assign_stmts(std::string_view a_var, std::string_view b_var);
  void        create_declare_bits_stmts(std::string_view a_var, bool is_signed, int bits);
  void        create_func_call(std::string_view out_tup, std::string_view fname, std::string_view inp_tup);
  void        create_named_tuple(std::string_view lhs_var, const std::vector<std::pair<std::string, std::string>>& rhs);
  std::string create_tuple_get(std::string_view fields);
  std::string create_tuple_get(std::string_view tup_var, std::string_view field_var);

  std::string create_minus_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_plus_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_mult_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_div_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_mod_stmts(std::string_view a_var, std::string_view b_var);
  std::string create_get_mask_stmts(std::string_view sel_var, std::string_view bitmask);
  void        create_set_mask_stmts(std::string_view sel_var, std::string_view bitmask, std::string_view value);

private:
  Lnast_nid   add_ref_child(const Lnast_nid& parent, std::string_view name);
  Lnast_nid   add_const_child(const Lnast_nid& parent, std::string_view value);
  Lnast_nid   add_value_child(const Lnast_nid& parent, std::string_view value);
  std::string emit_unary_result(Lnast_ntype::Lnast_ntype_int op_type, std::string_view operand);
  std::string emit_binary_result(Lnast_ntype::Lnast_ntype_int op_type, std::string_view lhs, std::string_view rhs);

  std::stack<Lnast_nid>            stmts_stack_;
  absl::flat_hash_set<std::string> input_lnames_;
};
