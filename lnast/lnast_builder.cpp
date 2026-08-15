//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_builder.hpp"

#include <cctype>

#include "absl/strings/str_split.h"
#include "bundle_key.hpp"
#include "iassert.hpp"
#include "str_tools.hpp"

Lnast_builder::Lnast_builder() {}

std::string Lnast_builder::create_lnast_tmp() {
  // Compiler SSA temps live in the `%` namespace — `%` is parser-impossible, so
  // a temp can never collide with a user identifier (and a user-written `___6`
  // is now an ordinary, scope-checked name). See Lnast::is_tmp.
  if (tmp_scope_.empty()) {
    return absl::StrCat("%", ++tmp_var_cnt);
  }
  // `%<label>_<n>` with a per-label monotonic counter. The label always starts
  // with a non-digit (set_tmp_scope keeps only a leading identifier run), so a
  // scoped id can never collide with a `%<digits>` fallback id.
  return absl::StrCat("%", tmp_scope_, "_", tmp_label_cnt_[tmp_scope_]++);
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
  // A global-counter fallback id: `%` followed by digits only. Scoped ids
  // (`%<label>_<n>`, label starts with a non-digit) and already-stabilized ids
  // (`%<hash>_<n>`, they contain a `_`) never match.
  auto is_fallback = [](std::string_view name) {
    if (name.size() < 2 || name[0] != '%') {
      return false;
    }
    for (size_t i = 1; i < name.size(); ++i) {
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
      found        = renamed.emplace(std::string(name), absl::StrCat("%", h, "_", occurrence[h]++)).first;
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

void Lnast_builder::new_lnast(std::string_view name) {
  lnast         = std::make_unique<Lnast>(name);
  auto root_nid = lnast->set_root(Lnast_ntype::create_top());
  idx_stmts     = lnast->add_child(root_nid, Lnast_ntype::create_stmts());

  tmp_var_cnt = 0;
  tmp_scope_.clear();
  tmp_label_cnt_.clear();
  tmp_ubits_.clear();
}

void Lnast_builder::note_unsigned_bits(std::string_view name, int bits) {
  if (name.empty() || bits <= 0) {
    return;
  }
  auto [it, inserted] = tmp_ubits_.emplace(name, bits);
  if (!inserted && bits < it->second) {
    it->second = bits;  // a tighter (still exact) claim wins
  }
}

// A leaf text that is an integer LITERAL (not an identifier / temp ref), parsed.
// Returns null for anything else — including a name that merely starts with a
// digit but does not parse.
static const Dlop* literal_value(std::string_view txt) {
  if (txt.empty() || (std::isdigit(static_cast<unsigned char>(txt.front())) == 0 && txt.front() != '-' && txt.front() != '\'')) {
    return nullptr;
  }
  try {
    return &Dlop::from_pyrope_cached(txt);
  } catch (...) {  // NOLINT(bugprone-empty-catch) — unparseable text is simply not a literal
    return nullptr;
  }
}

std::optional<int> Lnast_builder::unsigned_bits(std::string_view name) const {
  if (name.empty()) {
    return std::nullopt;
  }
  if (auto it = tmp_ubits_.find(name); it != tmp_ubits_.end()) {
    return it->second;
  }
  // A literal carries its own width. Only a plain non-negative integer counts:
  // a negative value has no unsigned window, and an unknown-bit pattern
  // (`0ub1?`) is not narrowable by magnitude.
  const auto* v = literal_value(name);
  if (v != nullptr && v->is_integer() && !v->has_unknowns() && !v->is_negative()) {
    return v->get_bits() > 0 ? v->get_bits() - 1 : 0;  // get_bits() counts the sign slot
  }
  return std::nullopt;
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

std::string Lnast_builder::create_concat_stmts(const std::vector<Concat_lane>& lanes) {
  // Unlike create_bit_or_stmts this must NOT chain into binary nodes: a concat
  // is n-ary by nature, and `concat(concat(a,b),c)` -- while legal and equal in
  // value -- would hide the flat lane table every consumer wants (and would
  // re-derive each intermediate's width). One node, all lanes, MSB-first.
  I(!lanes.empty());

  auto res_var = create_lnast_tmp();
  auto op_idx  = lnast->add_child(idx_stmts, Lnast_ntype::create_concat());
  add_ref_child(op_idx, res_var);
  int total_bits = 0;
  for (const auto& l : lanes) {
    I(!l.value.empty());
    add_value_child(op_idx, l.value);
    // `nil`, not 0: a zero-width window is a different (illegal) thing from an
    // unbound one, and upass has to be able to tell them apart to know whether
    // it still owes this lane a width.
    add_value_child(op_idx, l.bits > 0 ? std::to_string(l.bits) : std::string{"nil"});
    total_bits = l.bits > 0 && total_bits >= 0 ? total_bits + l.bits : -1;
  }
  // "The result is the non-negative sum(bits)-wide integer" (Dlop::concat_op),
  // so a fully-sized concat needs no truncation to its own width. An unbound
  // (`nil`) lane leaves the total unknown until upass resolves it.
  if (total_bits > 0) {
    note_unsigned_bits(res_var, total_bits);
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
  // NOT a copy of create_div_stmts: division by 1 is the identity, but REMAINDER
  // by 1 is always ZERO. Returning `a_var` here (the shape one gets by cloning
  // the div builder) would silently make `x % 1 == x`.
  if (b_var == "1") {
    return "0";
  }
  if (b_var.empty()) {
    return "0";  // an absent divisor is the `% 1` case, not `/ 1`
  }

  auto res_var = create_lnast_tmp();
  auto idx     = lnast->add_child(idx_stmts, Lnast_ntype::create_mod());
  add_ref_child(idx, res_var);

  if (a_var.empty()) {
    add_const_child(idx, "1");
  } else {
    add_value_child(idx, a_var);
  }

  add_value_child(idx, b_var);

  return res_var;
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

  // get_mask packs the selected bits LSB-first and zero-extends, so the result
  // is a non-negative popcount(mask)-wide integer. NOT recorded for a one-bit
  // mask: that fold returns the signed -1/0 boolean (see Dlop::get_mask_op),
  // which has no unsigned window.
  if (const auto* m = literal_value(bitmask); m != nullptr && m->is_integer() && !m->has_unknowns() && !m->is_negative()) {
    const int w = m->popcount();
    if (w >= 2) {
      note_unsigned_bits(res_var, w);
    }
  }

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
