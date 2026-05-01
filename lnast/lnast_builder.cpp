//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_builder.hpp"

#include "absl/strings/str_split.h"
#include "bundle.hpp"
#include "iassert.hpp"
#include "str_tools.hpp"

Lnast_builder::Lnast_builder() {}

std::string Lnast_builder::create_lnast_tmp() { return absl::StrCat("___", ++tmp_var_cnt); }

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

  if (!last_value || Lnast::is_input(lname)) {  // global input
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
  tmp_var_cnt = 0;
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

    auto mask = Lconst::get_mask_value(a, b);
    return mask.to_pyrope();
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

std::string Lnast_builder::create_sra_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  return emit_binary_result(Lnast_ntype::create_sra(), a_var, b_var);
}

std::string Lnast_builder::create_pick_bit_stmts(std::string_view a_var, std::string_view pos) {
  auto v = create_sra_stmts(a_var, pos);

  return create_bit_and_stmts(v, "1");
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
  std::string res_var;
  Lnast_nid   lid;

  for (auto v : var) {
    if (v.empty()) {
      continue;
    }

    if (res_var.empty()) {
      res_var = create_lnast_tmp();
      lid     = lnast->add_child(idx_stmts, Lnast_ntype::create_bit_or());
      add_ref_child(lid, res_var);
    }

    add_value_child(lid, v);
  }

  return res_var;
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

void Lnast_builder::create_dp_assign_stmts(std::string_view lhs_var, std::string_view rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_ntype::create_dp_assign());
  add_ref_child(idx_assign, lhs_var);
  add_value_child(idx_assign, rhs_var);
}

void Lnast_builder::create_assign_stmts(std::string_view lhs_var, std::string_view rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

#ifndef LNASTOP_DONE
  if (!Bundle::is_single_level(lhs_var) || !Bundle::is_single_level(rhs_var)) {
    std::string lhs_dest;

    auto rhs_dest = create_tuple_get(rhs_var);

    if (Bundle::is_single_level(lhs_var)) {
      auto idx_assign = lnast->add_child(idx_stmts, Lnast_ntype::create_assign());
      add_ref_child(idx_assign, lhs_var);
      add_value_child(idx_assign, rhs_dest);
    } else {
      std::vector<std::string> vec = absl::StrSplit(lhs_var, '.');

      if (vec.size() == 2 && (vec[0] == "%" || vec[0] == "$")) {
        return create_assign_stmts(absl::StrCat(vec[0], Bundle::get_first_level_name(vec[1])), rhs_var);
      }

      auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_set());
      lhs_dest     = create_lnast_tmp();
      add_ref_child(idx_dot, lhs_dest);

      std::string io_declare;

      for (const auto& f : absl::StrSplit(lhs_var, '.')) {
        if (f == "$" || f == "%") {
          io_declare = f;
          continue;
        }

        auto strip_pos = Bundle::get_first_level_name(f);  // WARNING: This is wrong but lnast_tolg has bugs handling this

        if (io_declare.empty()) {
          add_const_child(idx_dot, strip_pos);
        } else {
          add_const_child(idx_dot, absl::StrCat(io_declare, strip_pos));
          io_declare.clear();
        }
      }
      add_value_child(idx_dot, rhs_dest);
    }

    return;
  }
#endif

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_ntype::create_assign());
  add_ref_child(idx_assign, lhs_var);
  add_value_child(idx_assign, rhs_var);
}

void Lnast_builder::create_declare_bits_stmts(std::string_view a_var, bool is_signed, int bits) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_set());

#ifdef LNASTOP_DONE
  add_ref_child(idx_dot, a_var);
#else
  bool        first = true;
  std::string io_declare;

  for (const auto& f : absl::StrSplit(a_var, '.')) {
    if (first) {
      first = false;
      if (f == "$" || f == "%") {
        io_declare = f;
        continue;
      }
      add_ref_child(idx_dot, f);
    } else {
      if (io_declare.empty()) {
        auto strip_pos = Bundle::get_first_level_name(f);  // WARNING: This is wrong but lnast_tolg has bugs handling this
        add_const_child(idx_dot, strip_pos);
      } else {
        auto n = Bundle::get_first_level_name(f);
        add_ref_child(idx_dot, absl::StrCat(io_declare, n));

        io_declare.clear();
      }
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

void Lnast_builder::create_named_tuple(std::string_view lhs_var, const std::vector<std::pair<std::string, std::string>>& rhs) {
#ifdef LNASTOP_DONE
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_add());

  add_ref_child(idx_dot, lhs_var);

  for (const auto& it : rhs) {
    auto idx_assign = lnast->add_child(idx_dot, Lnast_ntype::create_assign());
    add_ref_child(idx_assign, it.first);
    add_value_child(idx_assign, it.second);
  }
#else

  std::vector<std::pair<std::string, std::string>> tup_expanded_rhs;

  for (const auto& it : rhs) {
    if (!str_tools::is_string(it.second) || Bundle::is_single_level(it.second)) {
      tup_expanded_rhs.emplace_back(std::make_pair(it.first, it.second));
      continue;
    }

    tup_expanded_rhs.emplace_back(std::make_pair(it.first, create_tuple_get(it.second)));
  }

  auto idx_dot = lnast->add_child(idx_stmts, Lnast_ntype::create_tuple_add());

  add_ref_child(idx_dot, lhs_var);

  for (const auto& it : tup_expanded_rhs) {
    auto idx_assign = lnast->add_child(idx_dot, Lnast_ntype::create_assign());
    add_ref_child(idx_assign, it.first);
    add_value_child(idx_assign, it.second);
  }
#endif
}

std::string Lnast_builder::create_tuple_get(std::string_view var) {
#ifdef LNASTOP_DONE
  return std::string(var);
#else
  if (Bundle::is_single_level(var)) {
    return std::string(var);
  }

  auto first_level = Bundle::get_first_level(var);
  if (first_level == "$" || first_level == "%") {
    // fuse with next levels (bugs in lnast_opt otherwise)

    auto f = Bundle::get_all_but_first_level(var);

    auto n    = Bundle::get_first_level_name(f);
    auto rest = Bundle::get_all_but_first_level(n);

    if (rest.empty()) {
      return absl::StrCat(first_level, n);
    }

    return create_tuple_get(absl::StrCat(first_level, n, ".", rest));
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

std::string Lnast_builder::create_get_mask_stmts(std::string_view sel_var, std::string_view bitmask) {
  I(sel_var.size() && bitmask.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_ntype::create_get_mask());
  add_ref_child(idx, res_var);
  add_ref_child(idx, sel_var);
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
