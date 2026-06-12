//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_builder.hpp"

#include "absl/strings/str_split.h"
#include "bundle_key.hpp"
#include "iassert.hpp"
#include "str_tools.hpp"

Lnast_builder::Lnast_builder() {}

std::string Lnast_builder::create_lnast_tmp() {
  if (tmp_scope_.empty()) {
    return absl::StrCat("___", ++tmp_var_cnt);
  }
  // `___<label>_<n>` with a per-label monotonic counter. The label always
  // starts with a non-digit (set_tmp_scope keeps only a leading identifier
  // run), so a scoped id can never collide with a `___<digits>` fallback id.
  return absl::StrCat("___", tmp_scope_, "_", tmp_label_cnt_[tmp_scope_]++);
}

void Lnast_builder::set_tmp_scope(std::string_view dest) {
  // Keep only the leading identifier run so the scope tracks the destination
  // variable, not the surrounding syntax: `a:u4` -> a, `a.b.c` -> a,
  // `(a, b)` -> "" (no scope). A leading digit is rejected (identifiers never
  // start with one), which also guarantees the no-collision property above.
  size_t i = 0;
  while (i < dest.size()) {
    const char c  = dest[i];
    const bool ok = c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (i > 0 && c >= '0' && c <= '9');
    if (!ok) {
      break;
    }
    ++i;
  }
  tmp_scope_.assign(dest.substr(0, i));
}

void Lnast_builder::stabilize_fallback_tmps() {
  // A global-counter fallback id: `___` followed by digits only. Scoped ids
  // (`___<label>_<n>`, label starts with a non-digit) never match.
  auto is_fallback = [](std::string_view name) {
    if (name.size() < 4 || name.substr(0, 3) != "___") {
      return false;
    }
    for (size_t i = 3; i < name.size(); ++i) {
      if (name[i] < '0' || name[i] > '9') {
        return false;
      }
    }
    return true;
  };

  absl::flat_hash_map<std::string, std::string> renamed;     // old fallback id -> hash id
  absl::flat_hash_map<uint32_t, int>            occurrence;  // per-hash repeat counter (this lnast)

  for (const Lnast_nid& it : lnast->depth_preorder()) {
    if (!Lnast_ntype::is_ref(lnast->get_type(it))) {
      continue;
    }
    const auto name = lnast->get_name(it);
    if (!is_fallback(name)) {
      continue;
    }
    auto found = renamed.find(name);
    if (found == renamed.end()) {
      // First occurrence = the defining statement (dst is child 0 and defs
      // precede uses in stmt order). Hashing through `renamed` keeps a tmp
      // whose siblings are earlier fallback tmps anchored to their *stable*
      // new names, so stability propagates through chained expressions.
      const auto h = lnast->tmp_site_hash(it, &renamed);
      found        = renamed.emplace(std::string(name), absl::StrCat("___", h, "_", occurrence[h]++)).first;
    }
    lnast->set_name(it, found->second);
  }
}

Lnast_nid Lnast_builder::add_ref_child(const Lnast_nid& parent, std::string_view name) {
  return lnast->add_child(parent, Lnast_node::create_ref(name));
}

Lnast_nid Lnast_builder::add_const_child(const Lnast_nid& parent, std::string_view value) {
  return lnast->add_child(parent, Lnast_node::create_const(value));
}

Lnast_nid Lnast_builder::add_value_child(const Lnast_nid& parent, std::string_view value) {
  return str_tools::is_string(value) ? add_ref_child(parent, value) : add_const_child(parent, value);
}

std::string Lnast_builder::emit_unary_result(Lnast_ntype::Lnast_ntype_int op_type, std::string_view operand) {
  I(!operand.empty());

  auto res_var = create_lnast_tmp();
  auto op_idx  = lnast->add_child(idx_stmts, op_type);
  add_ref_child(op_idx, res_var);
  add_value_child(op_idx, operand);

  return res_var;
}

std::string Lnast_builder::emit_binary_result(Lnast_ntype::Lnast_ntype_int op_type, std::string_view lhs, std::string_view rhs) {
  I(!lhs.empty());
  I(!rhs.empty());

  auto res_var = create_lnast_tmp();
  auto op_idx  = lnast->add_child(idx_stmts, op_type);
  add_ref_child(op_idx, res_var);
  add_value_child(op_idx, lhs);
  add_value_child(op_idx, rhs);

  return res_var;
}

std::string Lnast_builder::get_lnast_name(std::string_view vname, bool last_value) {
  std::string_view lname;

  const auto it = vname2lname.find(vname);
  if (it == vname2lname.end()) {
    lname = vname;
  } else {
    lname = it->second;
  }

  if (!last_value || input_lnames_.contains(lname)) {
    return std::string(lname);
  }

  auto idx_delay = lnast->add_child(idx_stmts, Lnast_ntype::create_delay_assign());
  auto tmp_var   = create_lnast_tmp();
  add_ref_child(idx_delay, tmp_var);
  add_ref_child(idx_delay, lname);
  add_const_child(idx_delay, "1");

  return tmp_var;
}

std::string_view Lnast_builder::get_lnast_lhs_name(std::string_view vname) {
  const auto& it = vname2lname.find(vname);
  if (it == vname2lname.end()) {
    // vname2lname.emplace(vname,vname);
    return vname;
  }

  return it->second;
}

void Lnast_builder::new_lnast(std::string_view name) {
  lnast         = std::make_unique<Lnast>(name);
  auto root_nid = lnast->set_root(Lnast_ntype::create_top());
  idx_stmts     = lnast->add_child(root_nid, Lnast_ntype::create_stmts());

  vname2lname.clear();
  input_lnames_.clear();
  tmp_var_cnt = 0;
  tmp_scope_.clear();
  tmp_label_cnt_.clear();
}

// std::vector<std::shared_ptr<Lnast>> Lnast_builder::pick_lnast() {
//   std::vector<std::shared_ptr<Lnast>> v;
//
//   for (auto &l : parsed_lnasts) {
//     if (l.second)  // do not push null ptr
//       v.emplace_back(l.second);
//   }
//
//   parsed_lnasts.clear();
//
//   return v;
// }

// Return a __tmp for (1<<expr)-1
std::string Lnast_builder::create_mask_stmts(std::string_view dest_max_bit) {
  if (dest_max_bit.empty()) {
    return "";
  }

  // some fast precomputed values
  if (str_tools::is_i(dest_max_bit)) {
    auto value = str_tools::to_i(dest_max_bit);
    if (value < 63 && value >= 0) {
      uint64_t v = (1ULL << value) - 1;
      return std::to_string(v);
    }
  }

  auto shl_var    = create_shl_stmts("1", dest_max_bit);
  auto mask_h_var = create_minus_stmts(shl_var, "1");

  return mask_h_var;
}

std::string Lnast_builder::create_bitmask_stmts(std::string_view max_bit, std::string_view min_bit) {
  if (str_tools::is_i(max_bit) && str_tools::is_i(min_bit)) {
    auto a = str_tools::to_i(max_bit);
    auto b = str_tools::to_i(min_bit);
    if (a < b) {
      auto tmp = a;
      a        = b;
      b        = tmp;
    }

    auto mask = Dlop::get_mask_value(a, b);
    return mask->to_pyrope();
  }

  if (max_bit == min_bit) {
    return create_shl_stmts("1", max_bit);
  }

  // ((1<<(max_bit-min_bit))-1)<<min_bit

  auto max_minus_min = create_minus_stmts(max_bit, min_bit);
  auto tmp           = create_shl_stmts("1", max_minus_min);
  auto upper_mask    = create_minus_stmts(tmp, "1");

  return create_shl_stmts(upper_mask, min_bit);
}

std::string Lnast_builder::create_bit_not_stmts(std::string_view var_name) {
  if (var_name.empty()) {
    return "";
  }

  return emit_unary_result(Lnast_ntype::create_bit_not(), var_name);
}

std::string Lnast_builder::create_log_not_stmts(std::string_view var_name) {
  if (var_name.empty()) {
    return "";
  }

  return emit_unary_result(Lnast_ntype::create_log_not(), var_name);
}

std::string Lnast_builder::create_red_or_stmts(std::string_view var_name) {
  if (var_name.empty()) {
    return "";
  }

  return emit_unary_result(Lnast_ntype::create_red_or(), var_name);
}

std::string Lnast_builder::create_red_and_stmts(std::string_view var_name) {
  if (var_name.empty()) {
    return "";
  }

  return emit_unary_result(Lnast_ntype::create_red_and(), var_name);
}

std::string Lnast_builder::create_red_xor_stmts(std::string_view var_name) {
  if (var_name.empty()) {
    return "";
  }

  return emit_unary_result(Lnast_ntype::create_red_xor(), var_name);
}

std::string Lnast_builder::create_sext_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  return emit_binary_result(Lnast_ntype::create_sext(), a_var, b_var);
}

std::string Lnast_builder::create_bit_and_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty()) {
    return std::string(b_var);
  }
  if (b_var.empty()) {
    return std::string(a_var);
  }

  return emit_binary_result(Lnast_ntype::create_bit_and(), a_var, b_var);
}

std::string Lnast_builder::create_bit_or_stmts(const std::vector<std::string>& var) {
  // The verifier's binary-op contract wants exactly two operands per node: a
  // single operand passes through untouched, more than two chain.
  std::string acc;
  for (const auto& v : var) {
    if (v.empty()) {
      continue;
    }
    acc = acc.empty() ? v : emit_binary_result(Lnast_ntype::create_bit_or(), acc, v);
  }
  return acc;
}

std::string Lnast_builder::create_bit_xor_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty()) {
    return std::string(b_var);
  }
  if (b_var.empty()) {
    return std::string(a_var);
  }

  return emit_binary_result(Lnast_ntype::create_bit_xor(), a_var, b_var);
}

std::string Lnast_builder::create_shl_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  return emit_binary_result(Lnast_ntype::create_shl(), a_var, b_var);
}

void Lnast_builder::create_assign_stmts(std::string_view lhs_var, std::string_view rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

#ifndef LNASTOP_DONE
  if (!bundle_key::is_single_level(lhs_var) || !bundle_key::is_single_level(rhs_var)) {
    std::string lhs_dest;

    auto rhs_dest = create_tuple_get(rhs_var);

    if (bundle_key::is_single_level(lhs_var)) {
      auto idx_assign = lnast->add_child(idx_stmts, Lnast_ntype::create_store());
      add_ref_child(idx_assign, lhs_var);
      add_value_child(idx_assign, rhs_dest);
    } else {
      auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_store());
      lhs_dest     = create_lnast_tmp();
      add_ref_child(idx_dot, lhs_dest);

      for (const auto& f : absl::StrSplit(lhs_var, '.')) {
        auto strip_pos = bundle_key::get_first_level_name(f);  // WARNING: This is wrong but lnast_tolg has bugs handling this
        add_const_child(idx_dot, strip_pos);
      }
      add_value_child(idx_dot, rhs_dest);
    }

    return;
  }
#endif

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_ntype::create_store());
  add_ref_child(idx_assign, lhs_var);
  add_value_child(idx_assign, rhs_var);
}

void Lnast_builder::create_declare_bits_stmts(std::string_view a_var, bool is_signed, int bits) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_store());

#ifdef LNASTOP_DONE
  add_ref_child(idx_dot, a_var);
#else
  bool first = true;

  for (const auto& f : absl::StrSplit(a_var, '.')) {
    if (first) {
      first = false;
      add_ref_child(idx_dot, f);
    } else {
      auto strip_pos = bundle_key::get_first_level_name(f);  // WARNING: This is wrong but lnast_tolg has bugs handling this
      add_const_child(idx_dot, strip_pos);
    }
  }
#endif

  if (is_signed) {
    add_const_child(idx_dot, "__sbits");
  } else {
    add_const_child(idx_dot, "__ubits");
  }
  add_const_child(idx_dot, std::to_string(bits));
}

void Lnast_builder::create_func_call(std::string_view out_tup, std::string_view fname, std::string_view inp_tup) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_func_call());

  add_ref_child(idx_dot, out_tup);
  add_ref_child(idx_dot, fname);
  add_ref_child(idx_dot, inp_tup);
}

std::string Lnast_builder::create_tuple_get(std::string_view var) {
#ifdef LNASTOP_DONE
  return std::string(var);
#else
  if (bundle_key::is_single_level(var)) {
    return std::string(var);
  }

  auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_get());

  auto res_var = create_lnast_tmp();
  add_ref_child(idx_dot, res_var);
  bool first = true;

  for (const auto& f : absl::StrSplit(var, '.')) {
    if (first) {
      first = false;
      add_ref_child(idx_dot, f);
    } else {
      add_const_child(idx_dot, f);
    }
  }

  return res_var;
#endif
}

std::string Lnast_builder::create_tuple_get(std::string_view tup_var, std::string_view field_var) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_get());

  auto res_var = create_lnast_tmp();
  add_ref_child(idx_dot, res_var);
  add_ref_child(idx_dot, tup_var);

#ifdef LNASTOP_DONE
  add_const_child(idx_dot, field_var);
#else
  for (const auto& f : absl::StrSplit(field_var, '.')) {
    add_const_child(idx_dot, f);
  }
#endif

  return res_var;
}

std::string Lnast_builder::create_minus_stmts(std::string_view a_var, std::string_view b_var) {
  if (b_var.empty()) {
    return std::string(a_var);
  }

  auto res_var = create_lnast_tmp();
  auto sub_idx = lnast->add_child(idx_stmts, Lnast_ntype::create_minus());
  add_ref_child(sub_idx, res_var);
  if (a_var.empty()) {
    add_const_child(sub_idx, "0");
  } else {
    add_value_child(sub_idx, a_var);
  }
  add_value_child(sub_idx, b_var);

  return res_var;
}

std::string Lnast_builder::create_plus_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty()) {
    return std::string(b_var);
  }
  if (b_var.empty()) {
    return std::string(a_var);
  }

  return emit_binary_result(Lnast_ntype::create_plus(), a_var, b_var);
}

std::string Lnast_builder::create_mult_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty() || a_var == "1") {
    return std::string(b_var);
  }
  if (b_var.empty() || b_var == "1") {
    return std::string(a_var);
  }

  return emit_binary_result(Lnast_ntype::create_mult(), a_var, b_var);
}

std::string Lnast_builder::create_div_stmts(std::string_view a_var, std::string_view b_var) {
  if (b_var.empty() || b_var == "1") {
    return std::string(a_var);
  }

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_ntype::create_div());
  add_ref_child(idx, res_var);

  if (a_var.empty()) {
    add_const_child(idx, "1");
  } else {
    add_value_child(idx, a_var);
  }

  add_value_child(idx, b_var);

  return res_var;
}

std::string Lnast_builder::create_mod_stmts(std::string_view a_var, std::string_view b_var) {
  I(a_var.size() && b_var.size());

  return emit_binary_result(Lnast_ntype::create_mod(), a_var, b_var);
}

std::string Lnast_builder::create_sra_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_sra(), a_var, b_var);
}

std::string Lnast_builder::create_eq_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_eq(), a_var, b_var);
}

std::string Lnast_builder::create_ne_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_ne(), a_var, b_var);
}

std::string Lnast_builder::create_lt_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_lt(), a_var, b_var);
}

std::string Lnast_builder::create_le_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_le(), a_var, b_var);
}

std::string Lnast_builder::create_gt_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_gt(), a_var, b_var);
}

std::string Lnast_builder::create_ge_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_ge(), a_var, b_var);
}

std::string Lnast_builder::create_log_and_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_log_and(), a_var, b_var);
}

std::string Lnast_builder::create_log_or_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty() && !b_var.empty());
  return emit_binary_result(Lnast_ntype::create_log_or(), a_var, b_var);
}

std::string Lnast_builder::create_settled_read_stmts(std::string_view var) {
  I(!var.empty());
  auto idx_delay = lnast->add_child(idx_stmts, Lnast_ntype::create_delay_assign());
  auto tmp_var   = create_lnast_tmp();
  add_ref_child(idx_delay, tmp_var);
  add_ref_child(idx_delay, var);
  add_const_child(idx_delay, "1");
  return tmp_var;
}

void Lnast_builder::create_declare_stmts(std::string_view var, std::string_view mode, std::string_view max_txt,
                                         std::string_view min_txt, std::string_view init) {
  I(!var.empty() && !mode.empty());
  auto idx = lnast->add_child(idx_stmts, Lnast_ntype::create_declare());
  add_ref_child(idx, var);
  if (max_txt.empty()) {
    lnast->add_child(idx, Lnast_ntype::create_prim_type_none());
  } else {
    auto ty = lnast->add_child(idx, Lnast_ntype::create_prim_type_int());
    add_const_child(ty, max_txt);
    add_const_child(ty, min_txt);
  }
  add_const_child(idx, mode);
  if (!init.empty()) {
    if (init == "nil") {
      add_const_child(idx, init);  // the no-reset sentinel is a const, never a ref
    } else {
      add_value_child(idx, init);
    }
  }
}

void Lnast_builder::add_value_child_pub(const Lnast_nid& parent, std::string_view value) { add_value_child(parent, value); }

std::string Lnast_builder::create_get_mask_stmts(std::string_view sel_var, std::string_view bitmask) {
  I(sel_var.size() && bitmask.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_ntype::create_get_mask());
  add_ref_child(idx, res_var);
  add_value_child(idx, sel_var);  // a constant selectee is legal (constprop folds it)
  add_value_child(idx, bitmask);

  return res_var;
}

void Lnast_builder::create_set_mask_stmts(std::string_view sel_var, std::string_view bitmask, std::string_view value) {
  I(sel_var.size() && bitmask.size() && value.size());

  auto idx = lnast->add_child(idx_stmts, Lnast_ntype::create_set_mask());
  add_ref_child(idx, sel_var);
  add_ref_child(idx, sel_var);
  add_value_child(idx, bitmask);
  add_value_child(idx, value);
}
