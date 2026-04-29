//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast.hpp"

#include <format>
#include <iostream>
#include <string>

#include "elab_scanner.hpp"
#include "perf_tracing.hpp"

namespace {
// Pre-register every Lnast attribute tag at static-init time. The HHDS
// attribute registry is not thread-safe on first-touch — two threads racing
// to register the same tag both find the registry empty and both try to
// insert, tripping the assert in register_tag. Pre-registering before main()
// ensures the lazy `attr()` path on Node_class only takes the early-return
// branch.
struct Lnast_attr_init {
  Lnast_attr_init() {
    hhds::register_attr_tag<hhds::attrs::name_t>("hhds::attrs::name");
    hhds::register_attr_tag<lnast::attrs::loc_t>("lnast.loc");
    hhds::register_attr_tag<lnast::attrs::fname_t>("lnast.fname");
    hhds::register_attr_tag<lnast::attrs::ssa_subs_t>("lnast.ssa_subs");
  }
};
[[maybe_unused]] const Lnast_attr_init lnast_attr_init_{};
}  // namespace

void Lnast_node::dump() const {
  std::print("{}, {}, {}\n", type.debug_name(), token.get_text(), subs);
}

Lnast::Lnast(std::string_view _module_name, std::string_view _file_name)
    : forest_(hhds::Forest::create()), top_module_name(_module_name), source_filename(_file_name) {
  // Each Lnast owns one tree body in its forest. The TreeIO carries the
  // module name; the Tree body is created lazily here so that get_root()
  // works only after set_root() has stamped the top node.
  auto tio = forest_->create_io(top_module_name);
  tree_    = tio->create_tree();
}

Lnast::~Lnast() = default;

// ─────────────────────────────────────────────────────────────────────────
// Mutation
// ─────────────────────────────────────────────────────────────────────────

Lnast_nid Lnast::set_root(const Lnast_node& n) {
  auto root = tree_->add_root_node();
  set_data(root, n);
  return root;
}

Lnast_nid Lnast::add_child(const Lnast_nid& parent, const Lnast_node& n) {
  auto child = parent.add_child();
  set_data(child, n);
  return child;
}

Lnast_nid Lnast::append_sibling(const Lnast_nid& sibling, const Lnast_node& n) {
  auto next = sibling.append_sibling();
  set_data(next, n);
  return next;
}

// ─────────────────────────────────────────────────────────────────────────
// Payload accessors. The Lnast_ntype enum lives in the native HHDS Type
// slot (uint16_t). Token/text/fname/subs ride on flat-storage attributes.
// ─────────────────────────────────────────────────────────────────────────

Lnast_ntype Lnast::get_type(const Lnast_nid& nid) const {
  return Lnast_ntype(static_cast<Lnast_ntype::Lnast_ntype_int>(nid.get_type()));
}

void Lnast::set_type(const Lnast_nid& nid, Lnast_ntype t) {
  nid.set_type(static_cast<hhds::Type>(t.get_raw_ntype()));
}

State_token Lnast::get_token(const Lnast_nid& nid) const {
  std::string text;
  auto        name_ref = nid.attr(hhds::attrs::name);
  if (name_ref.has()) {
    text = std::string(name_ref.get());
  }

  std::string fname_str;
  auto        fname_ref = nid.attr(lnast::attrs::fname);
  if (fname_ref.has()) {
    fname_str = std::string(fname_ref.get());
  }

  lnast::attrs::loc_t::value_type loc{};
  auto                            loc_ref = nid.attr(lnast::attrs::loc);
  if (loc_ref.has()) {
    loc = loc_ref.get();
  }

  return State_token(static_cast<Token_id>(loc.tok), loc.pos1, loc.pos2, loc.line, text, fname_str);
}

void Lnast::set_token(const Lnast_nid& nid, const State_token& tok) {
  nid.attr(hhds::attrs::name).set(std::string(tok.get_text()));
  if (!tok.fname.empty()) {
    nid.attr(lnast::attrs::fname).set(tok.fname);
  } else {
    auto fname_ref = nid.attr(lnast::attrs::fname);
    if (fname_ref.has()) {
      fname_ref.del();
    }
  }
  lnast::attrs::loc_t::value_type loc{tok.pos1, tok.pos2, tok.line, static_cast<uint8_t>(tok.tok)};
  nid.attr(lnast::attrs::loc).set(loc);
}

int16_t Lnast::get_subs(const Lnast_nid& nid) const {
  auto ref = nid.attr(lnast::attrs::ssa_subs);
  return ref.has() ? ref.get() : int16_t{0};
}

void Lnast::set_subs(const Lnast_nid& nid, int16_t v) { nid.attr(lnast::attrs::ssa_subs).set(v); }

std::string_view Lnast::get_name(const Lnast_nid& nid) const {
  auto ref = nid.attr(hhds::attrs::name);
  if (!ref.has()) {
    return {};
  }
  return ref.get();
}

std::string Lnast::get_sname(const Lnast_nid& nid) const {
  const auto subs = get_subs(nid);
  const auto name = get_name(nid);
  if (get_type(nid).is_const() || subs == 0) {
    return std::string(name);
  }
  return absl::StrCat(name, "|", subs);
}

Lnast_node Lnast::get_data(const Lnast_nid& nid) const {
  if (get_type(nid).is_invalid()) {
    return Lnast_node();  // default invalid
  }
  return Lnast_node(get_type(nid), get_token(nid), get_subs(nid));
}

void Lnast::set_data(const Lnast_nid& nid, const Lnast_node& n) {
  set_type(nid, n.type);
  set_token(nid, n.token);
  set_subs(nid, n.subs);
}

// ─────────────────────────────────────────────────────────────────────────
// SSA transformation
// ─────────────────────────────────────────────────────────────────────────

void Lnast::do_ssa_trans(const Lnast_nid& top_nid) {
  TRACE_EVENT("pass", nullptr, [this](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    auto        str = "lnast_ssa:" + converted_str;
    ctx.event()->set_name(str + top_module_name);
  });
  Lnast_nid top_sts_nid;
  if (get_type(top_nid).is_func_def()) {
    auto c0     = get_first_child(top_nid);
    auto c1     = get_sibling_next(c0);
    top_sts_nid = get_sibling_next(c1);
  } else {
    top_sts_nid = get_first_child(top_nid);
  }

  std::string tmp_str("0b?");
  std::string err_var("err_var");
  auto        tok     = get_token(top_sts_nid);
  auto        asg_nid = add_child(top_sts_nid, Lnast_node::create_assign(State_token(tok.pos1, tok.pos2, tok.fname)));
  add_child(asg_nid, Lnast_node::create_ref(err_var));
  undefined_var_nid = add_child(asg_nid, Lnast_node::create_const(tmp_str));

  Phi_rtable top_phi_resolve_table;
  phi_resolve_tables[top_sts_nid] = top_phi_resolve_table;
  analyze_selc_lrhs(top_sts_nid);

  trans_tuple_opr(top_sts_nid);

  resolve_ssa_lhs_subs(top_sts_nid);
  resolve_ssa_rhs_subs(top_sts_nid);

  opr_lhs_merge(top_sts_nid);
}

void Lnast::trans_tuple_opr(const Lnast_nid& psts_nid) {
  for (const auto& opr_nid : children(psts_nid)) {
    auto type = get_type(opr_nid);
    if (type.is_func_def()) {
      continue;
    } else if (type.is_if()) {
      trans_tuple_opr_if_subtree(opr_nid);
    } else if (type.is_tuple_concat()) {
      merge_tconcat_paired_assign(psts_nid, opr_nid);
    }
  }
}

void Lnast::trans_tuple_opr_if_subtree(const Lnast_nid& if_nid) {
  for (const auto& itr_nid : children(if_nid)) {
    if (get_type(itr_nid).is_stmts()) {
      for (const auto& opr_nid : children(itr_nid)) {
        auto type = get_type(opr_nid);
        I(!type.is_func_def());
        if (type.is_if()) {
          trans_tuple_opr_if_subtree(opr_nid);
        }
      }
    }
  }
}

void Lnast::merge_tconcat_paired_assign(const Lnast_nid& psts_nid, const Lnast_nid& concat_nid) {
  auto& selc_lrhs_table   = selc_lrhs_tables[psts_nid];
  auto  c0_concat         = get_first_child(concat_nid);
  auto  paired_assign_nid = selc_lrhs_table[concat_nid];
  auto  c0_assign         = get_first_child(paired_assign_nid);
  set_data(c0_concat, get_data(c0_assign));
  set_type(paired_assign_nid, Lnast_ntype::create_invalid());
}

void Lnast::analyze_selc_lrhs(const Lnast_nid& psts_nid) {
  Selc_lrhs_table top_selc_lrhs_table;
  selc_lrhs_tables[psts_nid] = top_selc_lrhs_table;
  for (const auto& opr_nid : children(psts_nid)) {
    auto type = get_type(opr_nid);
    if (type.is_func_def()) {
      do_ssa_trans(opr_nid);
    } else if (type.is_if()) {
      analyze_selc_lrhs_if_subtree(opr_nid);
    } else if (type.is_tuple_concat()) {
      analyze_selc_lrhs_handle_a_statement(psts_nid, opr_nid);
    }
  }
}

void Lnast::analyze_selc_lrhs_handle_a_statement(const Lnast_nid& psts_nid, const Lnast_nid& selc_nid) {
  auto type = get_type(selc_nid);
  I(type.is_tuple_concat());
  auto& selc_lrhs_table = selc_lrhs_tables[psts_nid];
  auto  c0_sel          = get_first_child(selc_nid);
  auto  c0_sel_name     = get_name(c0_sel);
  bool  hit             = false;
  auto  sib_nid         = selc_nid;
  while (!hit) {
    if (sib_nid == get_last_child(psts_nid)) {
      return;
    }

    sib_nid = get_sibling_next(sib_nid);

    for (auto sib_child : children(sib_nid)) {
      if (get_name(sib_child) == c0_sel_name) {
        hit                       = true;
        selc_lrhs_table[selc_nid] = sib_nid;
        break;
      }
    }
  }
}

void Lnast::insert_implicit_dp_parent(const Lnast_nid& dp_nid) {
  auto c0 = get_first_child(dp_nid);
  add_child(dp_nid, get_data(c0));
}

void Lnast::analyze_selc_lrhs_if_subtree(const Lnast_nid& if_nid) {
  for (const auto& itr_nid : children(if_nid)) {
    if (get_type(itr_nid).is_stmts()) {
      Cnt_rtable      if_sts_ssa_rhs_cnt_table;
      Selc_lrhs_table if_sts_selc_lrhs_table;
      selc_lrhs_tables[itr_nid] = if_sts_selc_lrhs_table;

      for (const auto& opr_nid : children(itr_nid)) {
        auto type = get_type(opr_nid);
        I(!type.is_func_def());
        if (type.is_if()) {
          analyze_selc_lrhs_if_subtree(opr_nid);
        } else if (type.is_tuple_concat()) {
          analyze_selc_lrhs_handle_a_statement(itr_nid, opr_nid);
        }
      }
    } else if (get_type(itr_nid).is_phi()) {
      continue;
    } else {
      continue;
    }
  }
}

void Lnast::resolve_ssa_lhs_subs(const Lnast_nid& psts_nid) {
  for (const auto& opr_nid : children(psts_nid)) {
    auto type = get_type(opr_nid);
    if (type.is_func_def()) {
      continue;
    } else if (type.is_if()) {
      ssa_lhs_if_subtree(opr_nid);
    } else if (type.is_dp_assign()) {
      insert_implicit_dp_parent(opr_nid);
      ssa_lhs_handle_a_statement(psts_nid, opr_nid);
    } else {
      ssa_lhs_handle_a_statement(psts_nid, opr_nid);
    }
  }
}

void Lnast::resolve_ssa_rhs_subs(const Lnast_nid& psts_nid) {
  Cnt_rtable top_ssa_rhs_cnt_table;
  ssa_rhs_cnt_tables[psts_nid] = top_ssa_rhs_cnt_table;
  for (const auto& opr_nid : children(psts_nid)) {
    auto type = get_type(opr_nid);
    if (type.is_func_def()) {
      continue;
    } else if (type.is_if()) {
      ssa_rhs_if_subtree(opr_nid);
    } else {
      ssa_rhs_handle_a_statement(psts_nid, opr_nid);
    }
  }
}

void Lnast::ssa_rhs_if_subtree(const Lnast_nid& if_nid) {
  for (const auto& itr_nid : children(if_nid)) {
    if (get_type(itr_nid).is_stmts()) {
      Cnt_rtable if_sts_ssa_rhs_cnt_table;
      ssa_rhs_cnt_tables[itr_nid] = if_sts_ssa_rhs_cnt_table;

      for (const auto& opr_nid : children(itr_nid)) {
        auto type = get_type(opr_nid);
        I(!type.is_func_def());
        if (type.is_if()) {
          ssa_rhs_if_subtree(opr_nid);
        } else {
          ssa_rhs_handle_a_statement(itr_nid, opr_nid);
        }
      }
    } else if (get_type(itr_nid).is_phi()) {
      auto c0 = get_first_child(itr_nid);
      update_rhs_ssa_cnt_table(get_parent(if_nid), c0);
      ssa_rhs_handle_a_operand(get_parent(if_nid), get_sibling_next(c0));
    } else {
      ssa_rhs_handle_a_operand(get_parent(if_nid), itr_nid);
      continue;
    }
  }
}

void Lnast::ssa_rhs_handle_a_statement(const Lnast_nid& psts_nid, const Lnast_nid& opr_nid) {
  const auto type = get_type(opr_nid);
  if (type.is_invalid()) {
    return;
  }

  const bool is_tuple_op = type.is_tuple_add() || type.is_tuple_set();

  for (auto itr_opd : children(opr_nid)) {
    if (itr_opd == get_first_child(opr_nid)) {
      continue;
    }
    if (is_tuple_op && get_type(itr_opd).is_assign()) {
      ssa_rhs_handle_a_statement(psts_nid, itr_opd);
    } else {
      ssa_rhs_handle_a_operand(psts_nid, itr_opd);
    }
  }

  if (is_leaf(opr_nid)) {
    return;
  }

  auto lhs_nid  = get_first_child(opr_nid);
  auto lhs_name = get_name(lhs_nid);

  if (is_tmp(lhs_name)) {
    return;
  }

  update_rhs_ssa_cnt_table(psts_nid, lhs_nid);
}

void Lnast::opr_lhs_merge(const Lnast_nid& psts_nid) {
  for (const auto& opr_nid : children(psts_nid)) {
    auto type = get_type(opr_nid);
    if (type.is_func_def()) {
      continue;
    } else if (type.is_if()) {
      opr_lhs_merge_if_subtree(opr_nid);
    } else if (type.is_assign() || type.is_dp_assign()) {
      opr_lhs_merge_handle_a_statement(opr_nid);
    }
  }
}

void Lnast::opr_lhs_merge_if_subtree(const Lnast_nid& if_nid) {
  for (const auto& itr_nid : children(if_nid)) {
    if (get_type(itr_nid).is_stmts()) {
      Cnt_rtable if_sts_ssa_rhs_cnt_table;
      ssa_rhs_cnt_tables[itr_nid] = if_sts_ssa_rhs_cnt_table;

      for (const auto& opr_nid : children(itr_nid)) {
        auto opr_type = get_type(opr_nid);
        I(!opr_type.is_func_def());
        if (opr_type.is_if()) {
          opr_lhs_merge_if_subtree(opr_nid);
        } else if (opr_type.is_assign() || opr_type.is_dp_assign()) {
          opr_lhs_merge_handle_a_statement(opr_nid);
        }
      }
    }
  }
}

void Lnast::opr_lhs_merge_handle_a_statement(const Lnast_nid& assign_nid) {
  const auto c0_assign      = get_first_child(assign_nid);
  const auto c1_assign_name = get_name(get_sibling_next(c0_assign));

  if (!is_tmp(c1_assign_name)) {
    return;
  }

  auto opr_nid  = get_sibling_prev(assign_nid);
  auto opr_type = get_type(opr_nid);

  if (opr_type.is_tuple_attr() || opr_type.is_func_call()) {
    return;
  }

  if (get_type(assign_nid).is_dp_assign() && !opr_type.is_attr_get()) {
    return;
  }

  auto c0_opr = get_first_child(opr_nid);

  return;  // FIXME: legacy bail; remainder mirrored from old implementation

  I(get_name(c0_opr) == c1_assign_name);
  set_data(c0_opr, get_data(c0_assign));
  set_type(assign_nid, Lnast_ntype::create_invalid());
}

void Lnast::ssa_rhs_handle_a_operand(const Lnast_nid& gpsts_nid, const Lnast_nid& opd_nid) {
  auto&      ssa_rhs_cnt_table = ssa_rhs_cnt_tables[gpsts_nid];
  auto       opd_name          = get_name(opd_nid);
  const auto opd_type          = get_type(opd_nid);
  if (opd_type.is_invalid()) {
    return;
  }

  auto ori_token = get_token(opd_nid);

  if (ssa_rhs_cnt_table.find(opd_name) != ssa_rhs_cnt_table.end()) {
    auto new_subs = ssa_rhs_cnt_table[opd_name];
    set_data(opd_nid, Lnast_node(opd_type, ori_token, new_subs));
  } else {
    auto new_subs               = check_rhs_cnt_table_parents_chain(gpsts_nid, opd_nid);
    ssa_rhs_cnt_table[opd_name] = new_subs;
    set_data(opd_nid, Lnast_node(opd_type, ori_token, new_subs));
  }
}

void Lnast::ssa_lhs_if_subtree(const Lnast_nid& if_nid) {
  for (const auto& itr_nid : children(if_nid)) {
    if (get_type(itr_nid).is_stmts()) {
      Phi_rtable if_sts_phi_resolve_table;
      phi_resolve_tables[itr_nid] = if_sts_phi_resolve_table;

      for (const auto& opr_nid : children(itr_nid)) {
        auto type = get_type(opr_nid);
        I(!type.is_func_def());
        if (type.is_if()) {
          ssa_lhs_if_subtree(opr_nid);
        } else if (type.is_dp_assign()) {
          insert_implicit_dp_parent(opr_nid);
          ssa_lhs_handle_a_statement(itr_nid, opr_nid);
        } else {
          ssa_lhs_handle_a_statement(itr_nid, opr_nid);
        }
      }
    }
  }
  ssa_handle_phi_nodes(if_nid);
}

void Lnast::ssa_handle_phi_nodes(const Lnast_nid& if_nid) {
  std::vector<Lnast_nid> if_stmts_vec;
  for (const auto& itr : children(if_nid)) {
    if (get_type(itr).is_stmts()) {
      if_stmts_vec.push_back(itr);
    }
  }

  for (auto itr = if_stmts_vec.rbegin(); itr != if_stmts_vec.rend(); ++itr) {
    if (itr == if_stmts_vec.rbegin() && has_else_stmts(if_nid)) {
      continue;
    } else if (itr == if_stmts_vec.rbegin() && !has_else_stmts(if_nid)) {
      Phi_rtable& true_table = phi_resolve_tables[*itr];
      Phi_rtable  fake_false_table;
      Lnast_nid   condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, fake_false_table);
    } else if (itr == if_stmts_vec.rbegin() + 1 && has_else_stmts(if_nid)) {
      Phi_rtable& true_table    = phi_resolve_tables[*itr];
      Phi_rtable& false_table   = phi_resolve_tables[*if_stmts_vec.rbegin()];
      Lnast_nid   condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);
    } else {
      Phi_rtable& true_table    = phi_resolve_tables[*itr];
      Phi_rtable& false_table   = new_added_phi_node_tables[get_parent(*itr)];
      Lnast_nid   condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);
    }
  }

  for (auto it : candidates_update_phi_resolve_table) {
    auto psts_nid = get_parent(if_nid);
    update_phi_resolve_table(psts_nid, it.second);
  }
  candidates_update_phi_resolve_table.clear();
}

void Lnast::resolve_phi_nodes(const Lnast_nid& cond_nid, Phi_rtable& true_table, Phi_rtable& false_table) {
  auto if_nid   = get_parent(cond_nid);
  auto psts_nid = get_parent(if_nid);
  for (auto const& [vname, f_nid] : false_table) {
    if (check_phi_table_parents_chain(vname, psts_nid).is_invalid()) {
      if (!is_output(vname)) {
        continue;
      }

      if (true_table.find(vname) != true_table.end()) {
        add_phi_node(cond_nid, true_table[vname], false_table[vname]);
        true_table.erase(vname);
      } else {
        add_phi_node(cond_nid, undefined_var_nid, false_table[vname]);
      }
      continue;
    }

    if (true_table.find(vname) != true_table.end()) {
      add_phi_node(cond_nid, true_table[vname], false_table[vname]);
      true_table.erase(vname);
    } else {
      auto t_nid = get_complement_nid(vname, psts_nid, false);
      add_phi_node(cond_nid, t_nid, false_table[vname]);
    }
  }

  std::vector<std::string> var_list;
  for (auto const& [vname, t_nid] : true_table) {
    if (true_table.empty()) {
      break;
    }

    if (check_phi_table_parents_chain(vname, psts_nid).is_invalid()) {
      if (!is_output(vname)) {
        continue;
      }
      if (false_table.find(vname) != false_table.end()) {
        add_phi_node(cond_nid, true_table[vname], false_table[vname]);
        var_list.push_back(vname);
      } else {
        add_phi_node(cond_nid, true_table[vname], undefined_var_nid);
        var_list.push_back(vname);
      }
      continue;
    }

    if (false_table.find(vname) != false_table.end()) {
      add_phi_node(cond_nid, true_table[vname], false_table[vname]);
      var_list.push_back(vname);
    } else {
      auto f_nid = get_complement_nid(vname, psts_nid, true);
      add_phi_node(cond_nid, true_table[vname], f_nid);
      var_list.push_back(vname);
    }
  }

  for (auto vname : var_list) {
    true_table.erase(vname);
  }
}

Lnast_nid Lnast::get_complement_nid(std::string_view brother_name, const Lnast_nid& psts_nid, bool false_path) {
  auto brother_nid = check_phi_table_parents_chain(brother_name, psts_nid);
  if (brother_nid.is_invalid()) {
    auto        if_nid                   = get_parent(psts_nid);
    Phi_rtable& new_added_phi_node_table = new_added_phi_node_tables[if_nid];
    if (false_path && new_added_phi_node_table.find(brother_name) != new_added_phi_node_table.end()) {
      return new_added_phi_node_table[brother_name];
    }
  }
  return brother_nid;
}

Lnast_nid Lnast::check_phi_table_parents_chain(std::string_view target_name, const Lnast_nid& psts_nid) {
  auto& parent_table = phi_resolve_tables[psts_nid];
  if (parent_table.find(target_name) != parent_table.end()) {
    return parent_table[target_name];
  }

  if (is_root(get_parent(psts_nid))) {
    return Lnast_nid();
  } else {
    auto tmp_if_nid   = get_parent(psts_nid);
    auto new_psts_nid = get_parent(tmp_if_nid);
    return check_phi_table_parents_chain(target_name, new_psts_nid);
  }
}

void Lnast::add_phi_node(const Lnast_nid& cond_nid, const Lnast_nid& t_nid, const Lnast_nid& f_nid) {
  auto        if_nid                   = get_parent(cond_nid);
  Phi_rtable& new_added_phi_node_table = new_added_phi_node_tables[if_nid];
  auto        if_tok                   = get_token(if_nid);
  auto        new_phi_nid
      = add_child(if_nid, Lnast_node(Lnast_ntype::create_phi(), State_token(if_tok.pos1, if_tok.pos2, if_tok.fname)));
  Lnast_nid lhs_phi_nid;

  lhs_phi_nid = add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(), get_token(t_nid), get_subs(t_nid)));

  update_global_lhs_ssa_cnt_table(lhs_phi_nid);
  auto first_char = get_token(cond_nid).get_text()[0];
  if (isdigit(first_char) || first_char == '-' || first_char == '+') {
    add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_const(), get_token(cond_nid), get_subs(cond_nid)));
  } else {
    add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(), get_token(cond_nid), get_subs(cond_nid)));
  }

  add_child(new_phi_nid, Lnast_node(get_type(t_nid), get_token(t_nid), get_subs(t_nid)));
  add_child(new_phi_nid, Lnast_node(get_type(f_nid), get_token(f_nid), get_subs(f_nid)));
  new_added_phi_node_table.insert_or_assign(get_name(lhs_phi_nid), lhs_phi_nid);

  candidates_update_phi_resolve_table.insert_or_assign(get_name(lhs_phi_nid), lhs_phi_nid);
}

bool Lnast::has_else_stmts(const Lnast_nid& if_nid) {
  Lnast_nid last_child        = get_last_child(if_nid);
  Lnast_nid second_last_child = get_sibling_prev(last_child);
  return (get_type(last_child).is_stmts() && get_type(second_last_child).is_stmts());
}

void Lnast::ssa_lhs_handle_a_statement(const Lnast_nid& psts_nid, const Lnast_nid& opr_nid) {
  const auto type = get_type(opr_nid);
  if (type.is_invalid()) {
    return;
  }

  const auto lhs_nid  = get_first_child(opr_nid);
  const auto lhs_name = get_name(lhs_nid);

  if (is_tmp(lhs_name)) {
    return;
  }

  update_global_lhs_ssa_cnt_table(lhs_nid);
  update_phi_resolve_table(psts_nid, lhs_nid);
}

void Lnast::respect_latest_global_lhs_ssa(const Lnast_nid& lhs_nid) {
  const auto lhs_name = get_name(lhs_nid);
  auto       itr      = global_ssa_lhs_cnt_table.find(lhs_name);
  if (itr != global_ssa_lhs_cnt_table.end()) {
    set_subs(lhs_nid, itr->second);
  } else {
    global_ssa_lhs_cnt_table.insert_or_assign(lhs_name, 0);
  }
}

void Lnast::update_global_lhs_ssa_cnt_table(const Lnast_nid& lhs_nid) {
  const auto& lhs_name = get_name(lhs_nid);
  auto        itr      = global_ssa_lhs_cnt_table.find(lhs_name);

  if (itr != global_ssa_lhs_cnt_table.end()) {
    itr->second += 1;
    set_subs(lhs_nid, itr->second);
  } else {
    global_ssa_lhs_cnt_table.insert_or_assign(lhs_name, 0);
  }
}

void Lnast::update_rhs_ssa_cnt_table(const Lnast_nid& psts_nid, const Lnast_nid& target_key) {
  auto&      ssa_rhs_cnt_table   = ssa_rhs_cnt_tables[psts_nid];
  const auto target_name         = get_name(target_key);
  ssa_rhs_cnt_table[target_name] = get_subs(target_key);
}

int8_t Lnast::check_rhs_cnt_table_parents_chain(const Lnast_nid& psts_nid, const Lnast_nid& target_key) {
  auto&      ssa_rhs_cnt_table = ssa_rhs_cnt_tables[psts_nid];
  const auto target_name       = get_name(target_key);
  auto       itr               = ssa_rhs_cnt_table.find(target_name);

  if (itr != ssa_rhs_cnt_table.end()) {
    return ssa_rhs_cnt_table[target_name];
  } else if (is_root(get_parent(psts_nid))) {
    return 0;
  } else if (get_type(get_parent(psts_nid)).is_func_def()) {
    return 0;
  } else {
    auto tmp_if_nid   = get_parent(psts_nid);
    auto new_psts_nid = get_parent(tmp_if_nid);
    return check_rhs_cnt_table_parents_chain(new_psts_nid, target_key);
  }
}

void Lnast::update_phi_resolve_table(const Lnast_nid& psts_nid, const Lnast_nid& lhs_nid) {
  auto&       phi_resolve_table = phi_resolve_tables[psts_nid];
  const auto& lhs_name          = get_name(lhs_nid);
  phi_resolve_table[lhs_name]   = lhs_nid;
}

bool Lnast::is_in_bw_table(std::string_view name) const { return from_lgraph_bw_table.contains(name); }

uint32_t Lnast::get_bitwidth(std::string_view name) const {
  I(is_in_bw_table(name));
  const auto it = from_lgraph_bw_table.find(name);
  I(it != from_lgraph_bw_table.end());
  return it->second;
}

void Lnast::set_bitwidth(std::string_view name, uint32_t bitwidth) {
  I(bitwidth > 0);
  from_lgraph_bw_table[name] = bitwidth;
}

void Lnast::dump(const Lnast_nid& root_nid) const {
  // Use HHDS's UTF-8 dump with custom node text, so output stays close to the
  // legacy "type, text|subs" form. Source location is shown via PrintOptions
  // attributes.
  hhds::Tree::PrintOptions opts;
  opts.show_types = false;
  opts.node_text  = [this](const hhds::Tree::Node_class& node) {
    const auto t = get_type(node);
    const auto s = get_subs(node);
    const auto n = get_name(node);
    if (t.is_ref() && s != 0 && !is_tmp(n)) {
      return std::format("{}: {}|{}", t.to_sv(), n, s);
    }
    return std::format("{}: {}", t.to_sv(), n);
  };
  opts.attributes.emplace_back("loc", [this](const hhds::Tree::Node_class& node) -> std::optional<std::string> {
    auto tok = get_token(node);
    if (tok.pos1 == 0 && tok.pos2 == 0 && tok.line == 0) {
      return std::nullopt;
    }
    return std::format("{}-{}@{}", tok.pos1, tok.pos2, tok.fname);
  });
  tree_->print(std::cout, root_nid, opts);
}
