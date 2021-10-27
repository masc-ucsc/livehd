
#include "lnast_create.hpp"

#include "iassert.hpp"
#include "mmap_str.hpp"

Lnast_create::Lnast_create() {}

mmap_lib::str Lnast_create::create_lnast_tmp() { return mmap_lib::str::concat("___", ++tmp_var_cnt); }

mmap_lib::str Lnast_create::get_lnast_name(mmap_lib::str vname) {
  const auto &it = vname2lname.find(vname);
  if (it == vname2lname.end()) {  // OOPS, use before assignment (can not be IOs mapped before)
    auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_attr_get());
    auto tmp_var = create_lnast_tmp();
    lnast->add_child(idx_dot, Lnast_node::create_ref(tmp_var));
    lnast->add_child(idx_dot, Lnast_node::create_ref(vname));
    lnast->add_child(idx_dot, Lnast_node::create_const("__last_value"));

    // vname2lname.emplace(vname, tmp_var);
    return tmp_var;
  }

  return it->second;
}

mmap_lib::str Lnast_create::get_lnast_lhs_name(mmap_lib::str vname) {
  const auto &it = vname2lname.find(vname);
  if (it == vname2lname.end()) {
    // vname2lname.emplace(vname,vname);
    return vname;
  }

  return it->second;
}

void Lnast_create::new_lnast(mmap_lib::str name) {
  lnast = std::make_unique<Lnast>(name);
  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  auto node_stmts = Lnast_node::create_stmts();
  idx_stmts       = lnast->add_child(mmap_lib::Tree_index::root(), node_stmts);

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
mmap_lib::str Lnast_create::create_mask_stmts(mmap_lib::str dest_max_bit) {
  if (dest_max_bit.empty())
    return dest_max_bit;

  // some fast precomputed values
  if (dest_max_bit.is_i()) {
    auto value = dest_max_bit.to_i();
    if (value < 63 && value >= 0) {
      uint64_t v = (1ULL << value) - 1;
      return v;
    }
  }

  auto shl_var    = create_shl_stmts("1", dest_max_bit);
  auto mask_h_var = create_minus_stmts(shl_var, "1");

  return mask_h_var;
}

mmap_lib::str Lnast_create::create_bitmask_stmts(mmap_lib::str max_bit, mmap_lib::str min_bit) {
  if (max_bit.is_i() && min_bit.is_i()) {
    auto a = max_bit.to_i();
    auto b = min_bit.to_i();
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

mmap_lib::str Lnast_create::create_bit_not_stmts(mmap_lib::str var_name) {
  if (var_name.empty())
    return var_name;

  auto res_var = create_lnast_tmp();
  auto not_idx = lnast->add_child(idx_stmts, Lnast_node::create_bit_not());
  lnast->add_child(not_idx, Lnast_node::create_ref(res_var));
  if (var_name.is_string())
    lnast->add_child(not_idx, Lnast_node::create_ref(var_name));
  else
    lnast->add_child(not_idx, Lnast_node::create_const(var_name));

  return res_var;
}

mmap_lib::str Lnast_create::create_logical_not_stmts(mmap_lib::str var_name) {
  if (var_name.empty())
    return var_name;

  auto res_var = create_lnast_tmp();
  auto not_idx = lnast->add_child(idx_stmts, Lnast_node::create_logical_not());
  lnast->add_child(not_idx, Lnast_node::create_ref(res_var));
  if (var_name.is_string())
    lnast->add_child(not_idx, Lnast_node::create_ref(var_name));
  else
    lnast->add_child(not_idx, Lnast_node::create_const(var_name));

  return res_var;
}

mmap_lib::str Lnast_create::create_reduce_or_stmts(mmap_lib::str var_name) {
  if (var_name.empty())
    return var_name;
  auto res_var = create_lnast_tmp();
  auto or_idx  = lnast->add_child(idx_stmts, Lnast_node::create_reduce_or());
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (var_name.is_string())
    lnast->add_child(or_idx, Lnast_node::create_ref(var_name));
  else
    lnast->add_child(or_idx, Lnast_node::create_const(var_name));

  return res_var;
}

mmap_lib::str Lnast_create::create_sra_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_sra());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));

  if (b_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_pick_bit_stmts(mmap_lib::str a_var, mmap_lib::str pos) {
  auto v = create_sra_stmts(a_var, pos);

  return create_bit_and_stmts(v, 1);
}

mmap_lib::str Lnast_create::create_sext_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  I(!a_var.empty());
  I(!b_var.empty());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_sext());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_bit_and_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto and_idx = lnast->add_child(idx_stmts, Lnast_node::create_bit_and());
  lnast->add_child(and_idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(and_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(and_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(and_idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_bit_or_stmts(const std::vector<mmap_lib::str> &var) {
  mmap_lib::str res_var;
  Lnast_nid     lid;

  for (auto v : var) {
    if (v.empty())
      continue;

    if (res_var.empty()) {
      res_var = create_lnast_tmp();
      lid     = lnast->add_child(idx_stmts, Lnast_node::create_bit_or());
      lnast->add_child(lid, Lnast_node::create_ref(res_var));
    }

    if (v.is_string())
      lnast->add_child(lid, Lnast_node::create_ref(v));
    else
      lnast->add_child(lid, Lnast_node::create_const(v));
  }

  return res_var;
}

mmap_lib::str Lnast_create::create_bit_xor_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto or_idx  = lnast->add_child(idx_stmts, Lnast_node::create_bit_xor());
  lnast->add_child(or_idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(or_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(or_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(or_idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_shl_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return a_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto shl_idx = lnast->add_child(idx_stmts, Lnast_node::create_shl());
  lnast->add_child(shl_idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(shl_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(shl_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_mask_xor_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  I(!a_var.empty());  // mask
  I(!b_var.empty());  // data

  auto res_var = create_lnast_tmp();
  auto shl_idx = lnast->add_child(idx_stmts, Lnast_node::create_mask_xor());
  lnast->add_child(shl_idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(shl_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(shl_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(shl_idx, Lnast_node::create_const(b_var));

  return res_var;
}

void Lnast_create::create_dp_assign_stmts(mmap_lib::str lhs_var, mmap_lib::str rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_dp_assign());
  lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
  if (rhs_var.is_string())
    lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_var));
  else
    lnast->add_child(idx_assign, Lnast_node::create_const(rhs_var));
}

void Lnast_create::create_assign_stmts(mmap_lib::str lhs_var, mmap_lib::str rhs_var) {
  I(lhs_var.size());
  I(rhs_var.size());

  auto idx_assign = lnast->add_child(idx_stmts, Lnast_node::create_assign());
  lnast->add_child(idx_assign, Lnast_node::create_ref(lhs_var));
  if (rhs_var.is_string())
    lnast->add_child(idx_assign, Lnast_node::create_ref(rhs_var));
  else
    lnast->add_child(idx_assign, Lnast_node::create_const(rhs_var));
}

void Lnast_create::create_declare_bits_stmts(mmap_lib::str a_var, bool is_signed, int bits) {
  auto idx_dot = lnast->add_child(idx_stmts, Lnast_node::create_tuple_add());
  lnast->add_child(idx_dot, Lnast_node::create_ref(a_var));
  if (is_signed) {
    lnast->add_child(idx_dot, Lnast_node::create_const("__sbits"));
  } else {
    lnast->add_child(idx_dot, Lnast_node::create_const("__ubits"));
  }
  lnast->add_child(idx_dot, Lnast_node::create_const(bits));
}

mmap_lib::str Lnast_create::create_minus_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto sub_idx = lnast->add_child(idx_stmts, Lnast_node::create_minus());
  lnast->add_child(sub_idx, Lnast_node::create_ref(res_var));
  if (a_var.empty()) {
    lnast->add_child(sub_idx, Lnast_node::create_const("0"));
  } else {
    if (a_var.is_string())
      lnast->add_child(sub_idx, Lnast_node::create_ref(a_var));
    else
      lnast->add_child(sub_idx, Lnast_node::create_const(a_var));
  }
  if (b_var.is_string())
    lnast->add_child(sub_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(sub_idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_plus_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty())
    return b_var;
  if (b_var.empty())
    return a_var;

  auto res_var = create_lnast_tmp();
  auto add_idx = lnast->add_child(idx_stmts, Lnast_node::create_plus());
  lnast->add_child(add_idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(add_idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(add_idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(add_idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_mult_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (a_var.empty() || a_var == "1")
    return b_var;
  if (b_var.empty() || b_var == "1")
    return a_var;

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_mult());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_div_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  if (b_var.empty() || b_var == "1")
    return a_var;

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_div());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));

  if (a_var.empty()) {
    lnast->add_child(idx, Lnast_node::create_const("1"));
  } else {
    if (a_var.is_string())
      lnast->add_child(idx, Lnast_node::create_ref(a_var));
    else
      lnast->add_child(idx, Lnast_node::create_const(a_var));
  }

  if (b_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

mmap_lib::str Lnast_create::create_mod_stmts(mmap_lib::str a_var, mmap_lib::str b_var) {
  I(a_var.size() && b_var.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_mod());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  if (a_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(a_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(a_var));
  if (b_var.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(b_var));
  else
    lnast->add_child(idx, Lnast_node::create_const(b_var));

  return res_var;
}

#if 0
mmap_lib::str Lnast_create::create_select_stmts(mmap_lib::str sel_var, mmap_lib::str sel_field) {
  I(sel_var.size() && sel_field.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_select());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  lnast->add_child(idx, Lnast_node::create_ref(sel_var));
  if (sel_field.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(sel_field));
  else
    lnast->add_child(idx, Lnast_node::create_const(sel_field));

  return res_var;
}
#endif

mmap_lib::str Lnast_create::create_get_mask_stmts(mmap_lib::str sel_var, mmap_lib::str bitmask) {
  I(sel_var.size() && bitmask.size());

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_node::create_get_mask());
  lnast->add_child(idx, Lnast_node::create_ref(res_var));
  lnast->add_child(idx, Lnast_node::create_ref(sel_var));
  if (bitmask.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(bitmask));
  else
    lnast->add_child(idx, Lnast_node::create_const(bitmask));

  return res_var;
}

void Lnast_create::create_set_mask_stmts(mmap_lib::str sel_var, mmap_lib::str bitmask, mmap_lib::str value) {
  I(sel_var.size() && bitmask.size() && value.size());

  auto idx = lnast->add_child(idx_stmts, Lnast_node::create_set_mask());
  lnast->add_child(idx, Lnast_node::create_ref(sel_var));
  lnast->add_child(idx, Lnast_node::create_ref(sel_var));
  if (bitmask.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(bitmask));
  else
    lnast->add_child(idx, Lnast_node::create_const(bitmask));
  if (value.is_string())
    lnast->add_child(idx, Lnast_node::create_ref(value));
  else
    lnast->add_child(idx, Lnast_node::create_const(value));
}
