//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_create.hpp"

#include "absl/strings/str_split.h"
#include "bundle.hpp"
#include "iassert.hpp"
#include "str_tools.hpp"

Lnast_create::Lnast_create() {}

std::string Lnast_create::create_lnast_tmp() { return absl::StrCat("___", ++tmp_var_cnt); }

std::string Lnast_create::get_lnast_name(std::string_view vname, bool last_value) {
  std::string_view lname;

  const auto it = vname2lname.find(vname);
  if (it == vname2lname.end())
    lname = vname;
  else
    lname = it->second;

  if (!last_value || lname[0] == '$') { // global input
    return std::string(lname);
  }

  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_attr_get());
  auto tmp_var = create_lnast_tmp();
  lnast->add_child(idx_dot, Lnast_node::create_ref(tmp_var));
  lnast->add_child(idx_dot, Lnast_node::create_ref(lname));
  lnast->add_child(idx_dot, Lnast_node::create_const("__last_value"));

  return tmp_var;
}

std::string_view Lnast_create::get_lnast_lhs_name(std::string_view vname) {
  const auto &it = vname2lname.find(vname);
  if (it == vname2lname.end()) {
    // vname2lname.emplace(vname,vname);
    return vname;
  }

  return it->second;
}

void Lnast_create::new_lnast(std::string_view name) {
  lnast = std::make_unique<Lnast>(name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  auto node_stmts = Lnast_node::create_stmts();
  idx_stmts       = lnast->add_child(lh::Tree_index::root(), node_stmts);

  vname2lname.clear();

  tmp_var_cnt = 0;
}

// std::vector<std::shared_ptr<Lnast>> Lnast_create::pick_lnast() {
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
std::string Lnast_create::create_mask_stmts(std::string_view dest_max_bit) {
  if (dest_max_bit.empty())
    return "";

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

std::string Lnast_create::create_bitmask_stmts(std::string_view max_bit, std::string_view min_bit) {
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

std::string Lnast_create::create_bit_not_stmts(std::string_view var_name) {
  if (var_name.empty())
    return "";

  auto res_var = create_lnast_tmp();
  auto not_idx = lnast->add_child(idx_stmts, Lnast_node::create_bit_not());
  lnast->add_child(not_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(var_name))
    lnast->add_child(not_idx, Lnast_node::create_ref(var_name));
  else
    lnast->add_child(not_idx, Lnast_node::create_const(var_name));

  return res_var;
}

std::string Lnast_create::create_logical_not_stmts(std::string_view var_name) {
  if (var_name.empty())
    return "";

  auto res_var = create_lnast_tmp();
  auto not_idx = lnast->add_child(idx_stmts, Lnast_node::create_logical_not());
  lnast->add_child(not_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(var_name))
    lnast->add_child(not_idx, Lnast_node::create_ref(var_name));
  else
    lnast->add_child(not_idx, Lnast_node::create_const(var_name));

  return res_var;
}

std::string Lnast_create::create_reduce_or_stmts(std::string_view var_name) {
  if (var_name.empty())
    return "";

  auto res_var = create_lnast_tmp();
  auto or_idx  = lnast->add_child(idx_stmts, Lnast_node::create_reduce_or());
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(var_name))
    lnast->add_child(or_idx, Lnast_node::create_ref(var_name));
  else
    lnast->add_child(or_idx, Lnast_node::create_const(var_name));

  return res_var;
}

std::string Lnast_create::create_sra_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_sra());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));

  if (str_tools::is_string(b_var))
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_pick_bit_stmts(std::string_view a_var, std::string_view pos) {
  auto v = create_sra_stmts(a_var, pos);

  return create_bit_and_stmts(v, "1");
}

std::string Lnast_create::create_sext_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_sext());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_bit_and_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return std::string(b_var);
  if (b_var.empty())
    return std::string(a_var);

  auto res_var = create_lnast_tmp();
  auto and_idx = lnast->add_child(idx_stmts, Lnast_node::create_bit_and());
  lnast->add_child(and_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(and_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(and_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_bit_or_stmts(const std::vector<std::string> &var) {
  std::string res_var;
  Lnast_nid   lid;

  for (auto v : var) {
    if (v.empty())
      continue;

    if (res_var.empty()) {
      res_var = create_lnast_tmp();
      lid     = lnast->add_child(idx_stmts, Lnast_node::create_bit_or());
      lnast->add_child(lid, Lnast_node::create_ref(res_var));
    }

    if (str_tools::is_string(v))
      lnast->add_child(lid, Lnast_node::create_ref(v));
    else
      lnast->add_child(lid, Lnast_node::create_const(v));
  }

  return res_var;
}

std::string Lnast_create::create_bit_xor_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return std::string(b_var);
  if (b_var.empty())
    return std::string(a_var);

  auto res_var = create_lnast_tmp();
  auto or_idx  = lnast->add_child(idx_stmts, Lnast_node::create_bit_xor());
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(or_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(or_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_shl_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  auto res_var = create_lnast_tmp();
  auto shl_idx = lnast->add_child(idx_stmts, Lnast_node::create_shl());
  lnast->add_child(shl_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(shl_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(shl_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_mask_xor_stmts(std::string_view a_var, std::string_view b_var) {
  I(!a_var.empty());  // mask
  I(!b_var.empty());  // data

  auto res_var = create_lnast_tmp();
  auto shl_idx = lnast->add_child(idx_stmts, Lnast_node::create_mask_xor());
  lnast->add_child(shl_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(shl_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(shl_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(b_var));

  return res_var;
}

void Lnast_create::create_dp_assign_stmts(std::string_view lhs_var, std::string_view rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_dp_assign());
  lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
  if (str_tools::is_string(rhs_var))
    lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_var));
  else
    lnast->add_child(idx_assign, Lnast_node::create_const(rhs_var));
}

void Lnast_create::create_assign_stmts(std::string_view lhs_var, std::string_view rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

#ifndef LNASTOP_DONE
  if (!Bundle::is_single_level(lhs_var) || !Bundle::is_single_level(rhs_var)) {
    std::string lhs_dest;

    auto rhs_dest = create_tuple_get(rhs_var);

    if (Bundle::is_single_level(lhs_var)) {
      auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign());
      lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
      if (str_tools::is_string(rhs_dest))
        lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_dest));
      else
        lnast->add_child(idx_assign, Lnast_node::create_const(rhs_dest));
    } else {
      std::vector<std::string> vec = absl::StrSplit(lhs_var, '.');

      if (vec.size() == 2 && (vec[0] == "%" || vec[0] == "$")) {
        return create_assign_stmts(absl::StrCat(vec[0], Bundle::get_first_level_name(vec[1])), rhs_var);
      }

      auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_set());
      lhs_dest     = create_lnast_tmp();
      lnast->add_child(idx_dot, Lnast_node::create_ref(lhs_dest));

      std::string io_declare;

      for (const auto &f : absl::StrSplit(lhs_var, '.')) {
        if (f == "$" || f == "%") {
          io_declare = f;
          continue;
        }

        auto strip_pos = Bundle::get_first_level_name(f);  // WARNING: This is wrong but lnast_tolg has bugs handling this

        if (io_declare.empty()) {
          lnast->add_child(idx_dot, Lnast_node::create_const(strip_pos));
        } else {
          lnast->add_child(idx_dot, Lnast_node::create_const(absl::StrCat(io_declare, strip_pos)));
          io_declare.clear();
        }
      }
      if (str_tools::is_string(rhs_dest))
        lnast->add_child(idx_dot, Lnast_node::create_ref(rhs_dest));
      else
        lnast->add_child(idx_dot, Lnast_node::create_const(rhs_dest));
    }

    return;
  }
#endif

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign());
  lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
  if (str_tools::is_string(rhs_var))
    lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_var));
  else
    lnast->add_child(idx_assign, Lnast_node::create_const(rhs_var));
}

void Lnast_create::create_declare_bits_stmts(std::string_view a_var, bool is_signed, int bits) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_set());

#ifdef LNASTOP_DONE
  lnast->add_child(idx_dot, Lnast_node::create_ref(a_var));
#else
  bool        first = true;
  std::string io_declare;

  for (const auto &f : absl::StrSplit(a_var, '.')) {
    if (first) {
      first = false;
      if (f == "$" || f == "%") {
        io_declare = f;
        continue;
      }
      lnast->add_child(idx_dot, Lnast_node::create_ref(f));
    } else {
      if (io_declare.empty()) {
        auto strip_pos = Bundle::get_first_level_name(f);  // WARNING: This is wrong but lnast_tolg has bugs handling this
        lnast->add_child(idx_dot, Lnast_node::create_const(strip_pos));
      } else {
        auto n = Bundle::get_first_level_name(f);
        lnast->add_child(idx_dot, Lnast_node::create_ref(absl::StrCat(io_declare, n)));

        io_declare.clear();
      }
    }
  }
#endif

  if (is_signed) {
    lnast->add_child(idx_dot, Lnast_node::create_const("__sbits"));
  } else {
    lnast->add_child(idx_dot, Lnast_node::create_const("__ubits"));
  }
  lnast->add_child(idx_dot, Lnast_node::create_const(bits));
}

void Lnast_create::create_func_call(std::string_view out_tup, std::string_view fname, std::string_view inp_tup) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_func_call());

  lnast->add_child(idx_dot, Lnast_node::create_ref(out_tup));
  lnast->add_child(idx_dot, Lnast_node::create_ref(fname));
  lnast->add_child(idx_dot, Lnast_node::create_ref(inp_tup));
}

void Lnast_create::create_named_tuple(std::string_view lhs_var, const std::vector<std::pair<std::string, std::string>> &rhs) {
#ifdef LNASTOP_DONE
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_add());

  lnast->add_child(idx_dot, Lnast_node::create_ref(lhs_var));

  for (const auto &it : rhs) {
    auto idx_assign = lnast->add_child(idx_dot, Lnast_node::create_assign());
    lnast->add_child(idx_assign, Lnast_node::create_ref(it.first));
    if (str_tools::is_string(it.second))
      lnast->add_child(idx_assign, Lnast_node::create_ref(it.second));
    else
      lnast->add_child(idx_assign, Lnast_node::create_const(it.second));
  }
#else

  std::vector<std::pair<std::string, std::string>> tup_expanded_rhs;

  for (const auto &it : rhs) {
    if (!str_tools::is_string(it.second) || Bundle::is_single_level(it.second)) {
      tup_expanded_rhs.emplace_back(std::make_pair(it.first, it.second));
      continue;
    }

    tup_expanded_rhs.emplace_back(std::make_pair(it.first, create_tuple_get(it.second)));
  }

  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_add());

  lnast->add_child(idx_dot, Lnast_node::create_ref(lhs_var));

  for (const auto &it : tup_expanded_rhs) {
    auto idx_assign = lnast->add_child(idx_dot, Lnast_node::create_assign());
    lnast->add_child(idx_assign, Lnast_node::create_ref(it.first));
    if (str_tools::is_string(it.second))
      lnast->add_child(idx_assign, Lnast_node::create_ref(it.second));
    else
      lnast->add_child(idx_assign, Lnast_node::create_const(it.second));
  }
#endif
}

std::string Lnast_create::create_tuple_get(std::string_view var) {
#ifdef LNASTOP_DONE
  return std::string(var);
#else
  if (Bundle::is_single_level(var))
    return std::string(var);

  auto first_level = Bundle::get_first_level(var);
  if (first_level == "$" || first_level == "%") {
    // fuse with next levels (bugs in lnast_opt otherwise)

    auto f = Bundle::get_all_but_first_level(var);

    auto n    = Bundle::get_first_level_name(f);
    auto rest = Bundle::get_all_but_first_level(n);

    if (rest.empty())
      return absl::StrCat(first_level, n);

    return create_tuple_get(absl::StrCat(first_level, n, ".", rest));
  }

  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_get());

  auto res_var = create_lnast_tmp();
  lnast->add_child(idx_dot, Lnast_node::create_ref(res_var));
  bool first = true;

  for (const auto &f : absl::StrSplit(var, '.')) {
    if (first) {
      first = false;
      lnast->add_child(idx_dot, Lnast_node::create_ref(f));
    } else {
      lnast->add_child(idx_dot, Lnast_node::create_const(f));
    }
  }

  return res_var;
#endif
}

std::string Lnast_create::create_tuple_get(std::string_view tup_var, std::string_view field_var) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_get());

  auto res_var = create_lnast_tmp();
  lnast->add_child(idx_dot, Lnast_node::create_ref(res_var));
  lnast->add_child(idx_dot, Lnast_node::create_ref(tup_var));

#ifdef LNASTOP_DONE
  lnast->add_child(idx_dot, Lnast_node::create_const(field_var));
#else
  for (const auto &f : absl::StrSplit(field_var, '.')) {
    lnast->add_child(idx_dot, Lnast_node::create_const(f));
  }
#endif

  return res_var;
}

std::string Lnast_create::create_minus_stmts(std::string_view a_var, std::string_view b_var) {
  if (b_var.empty())
    return std::string(a_var);

  auto res_var = create_lnast_tmp();
  auto sub_idx = lnast->add_child(idx_stmts, Lnast_node::create_minus());
  lnast->add_child(sub_idx, Lnast_node::create_ref(res_var));
  if (a_var.empty()) {
    lnast->add_child(sub_idx, Lnast_node::create_const("0"));
  } else {
    if (str_tools::is_string(a_var))
      lnast->add_child(sub_idx, Lnast_node::create_ref(a_var));
    else
      lnast->add_child(sub_idx, Lnast_node::create_const(a_var));
  }
  if (str_tools::is_string(b_var))
    lnast->add_child(sub_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(sub_idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_plus_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty())
    return std::string(b_var);
  if (b_var.empty())
    return std::string(a_var);

  auto res_var = create_lnast_tmp();
  auto add_idx = lnast->add_child(idx_stmts, Lnast_node::create_plus());
  lnast->add_child(add_idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(add_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(add_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_mult_stmts(std::string_view a_var, std::string_view b_var) {
  if (a_var.empty() || a_var == "1")
    return std::string(b_var);
  if (b_var.empty() || b_var == "1")
    return std::string(a_var);

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_mult());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_div_stmts(std::string_view a_var, std::string_view b_var) {
  if (b_var.empty() || b_var == "1")
    return std::string(a_var);

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_div());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));

  if (a_var.empty()) {
    lnast->add_child(idx, Lnast_node::create_const("1"));
  } else {
    if (str_tools::is_string(a_var))
      lnast->add_child(idx, Lnast_node::create_ref(a_var));
    else
      lnast->add_child(idx, Lnast_node::create_const(a_var));
  }

  if (str_tools::is_string(b_var))
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_mod_stmts(std::string_view a_var, std::string_view b_var) {
  I(a_var.size() && b_var.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_mod());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (str_tools::is_string(a_var))
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  if (str_tools::is_string(b_var))
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

std::string Lnast_create::create_get_mask_stmts(std::string_view sel_var, std::string_view bitmask) {
  I(sel_var.size() && bitmask.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_get_mask());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  lnast->add_child(idx, Lnast_node::create_ref(sel_var));
  if (str_tools::is_string(bitmask))
    lnast->add_child(idx, Lnast_node::create_ref(bitmask));
  else
    lnast->add_child(idx, Lnast_node::create_const(bitmask));

  return res_var;
}

void Lnast_create::create_set_mask_stmts(std::string_view sel_var, std::string_view bitmask, std::string_view value) {
  I(sel_var.size() && bitmask.size() && value.size());

  auto idx = lnast->add_child(idx_stmts, Lnast_node::create_set_mask());
  lnast->add_child(idx, Lnast_node::create_ref(sel_var));
  lnast->add_child(idx, Lnast_node::create_ref(sel_var));
  if (str_tools::is_string(bitmask))
    lnast->add_child(idx, Lnast_node::create_ref(bitmask));
  else
    lnast->add_child(idx, Lnast_node::create_const(bitmask));
  if (str_tools::is_string(value))
    lnast->add_child(idx, Lnast_node::create_ref(value));
  else
    lnast->add_child(idx, Lnast_node::create_const(value));
}
