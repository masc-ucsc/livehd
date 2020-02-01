#include <mmap_vector.hpp>
#include "lnast.hpp"

void Lnast_node::dump() const {
  fmt::print("type:{}\n", type.debug_name()); // TODO: cleaner API to also dump token
}

void Lnast::do_ssa_trans(const Lnast_nid &top_nid){
  Lnast_nid top_sts_nid = get_first_child(top_nid);
  default_const_nid = add_child(top_sts_nid, Lnast_node(Lnast_ntype::create_const(), Token()));


  Phi_rtable top_phi_resolve_table;
  phi_resolve_tables[get_data(top_sts_nid).token.get_text()] = top_phi_resolve_table;
  for (const auto &opr_nid : children(top_sts_nid)) {
    if (get_data(opr_nid).type.is_if()) {
      ssa_if_subtree(opr_nid);
    } else if (get_data(opr_nid).type.is_func_def()) {
      do_ssa_trans(opr_nid);
    } else {
      ssa_handle_a_statement(top_sts_nid, opr_nid);
    }
  }
}

void Lnast::ssa_if_subtree(const Lnast_nid &if_nid) {
  for (const auto &itr_nid : children(if_nid)) {
    if (get_data(itr_nid).type.is_stmts()) {
      Phi_rtable if_sts_phi_resolve_table;
      phi_resolve_tables[get_data(itr_nid).token.get_text()] = if_sts_phi_resolve_table;

      for (const auto &opr_nid : children(itr_nid)) {
        I(!get_data(opr_nid).type.is_func_def());
        if (get_data(opr_nid).type.is_if())
          ssa_if_subtree(opr_nid);
        else
          ssa_handle_a_statement(itr_nid, opr_nid);
      }
    } else if (get_data(itr_nid).type.is_cstmts()){
      for (const auto &opr_nid : children(itr_nid))
        ssa_handle_a_cstatement(itr_nid, opr_nid);
    } else {
      I(false);
    }
  }

  ssa_handle_phi_nodes(if_nid);
}

void Lnast::ssa_handle_phi_nodes(const Lnast_nid &if_nid) {
  std::vector<Lnast_nid> if_stmts_vec;
  for (const auto &itr : children(if_nid)) {
    if (get_data(itr).type.is_stmts())
      if_stmts_vec.push_back(itr);
  }

  //2 possible cases: (1)if-elif-elif (2) if-elif-else
  //note: handle reversely to get correct mux priority chain
  for (auto itr = if_stmts_vec.rbegin(); itr != if_stmts_vec.rend(); ++itr) {
    if (itr == if_stmts_vec.rbegin() && has_else_stmts(if_nid)) {
      continue;
    } else if (itr == if_stmts_vec.rbegin() && !has_else_stmts(if_nid)) {
      Phi_rtable &true_table  = phi_resolve_tables[get_data(*itr).token.get_text()];
      Phi_rtable fake_false_table ; //for the case of if-elif-elif
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, fake_false_table);

    } else if (itr == if_stmts_vec.rbegin()+1 && has_else_stmts(if_nid)) {
      Phi_rtable &true_table  = phi_resolve_tables[get_data(*itr).token.get_text()];
      Phi_rtable &false_table = phi_resolve_tables[get_data(*if_stmts_vec.rbegin()).token.get_text()];
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);

    } else {
      Phi_rtable &true_table  = phi_resolve_tables[get_data(*itr).token.get_text()];
      Phi_rtable &false_table = new_added_phi_node_table;
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);

    }
  }
}

void Lnast::resolve_phi_nodes(const Lnast_nid &cond_nid, Phi_rtable &true_table, Phi_rtable &false_table) {
  for (auto const&[key, val] : false_table) {
    if (true_table.find(key) != true_table.end()) {
      add_phi_node(cond_nid, true_table[key], false_table[key]);
      true_table.erase(key);
    } else {
      auto if_nid = get_parent(cond_nid);
      auto psts_nid = get_parent(if_nid);
      auto t_nid = get_complement_nid(key, psts_nid, false);
      add_phi_node(cond_nid, t_nid, false_table[key]);
    }
  }

  for (auto const&[key, val] : true_table) {
    if (false_table.find(key) != false_table.end()) {
      add_phi_node(cond_nid, true_table[key], false_table[key]);
      true_table.erase(key);
    } else {
      auto if_nid = get_parent(cond_nid);
      auto psts_nid = get_parent(if_nid);
      auto f_nid = get_complement_nid(key, psts_nid, true);
      add_phi_node(cond_nid, true_table[key], f_nid);
      true_table.erase(key);
    }
  }
  //I(true_table.empty()); not necessarily true
}


Lnast_nid Lnast::get_complement_nid(std::string_view brother_name, const Lnast_nid &psts_nid, bool false_path) {
  if(false_path && new_added_phi_node_table.find(brother_name) != new_added_phi_node_table.end())
    return new_added_phi_node_table[brother_name];
  else
    return check_phi_table_parents_chain(brother_name, psts_nid, false);
}


Lnast_nid Lnast::check_phi_table_parents_chain(std::string_view target_name, const Lnast_nid &psts_nid, bool originate_from_csts) {
  auto &parent_table = phi_resolve_tables[get_data(psts_nid).token.get_text()];

  if(parent_table.find(target_name) != parent_table.end())
    return parent_table[target_name];

  if (get_parent(psts_nid) == get_root() && originate_from_csts) {//current sts is top_sts
    I(false); //variable not defined
  } else if (get_parent(psts_nid) == get_root() && !originate_from_csts){
    return default_const_nid;
  } else {
    auto tmp_if_nid = get_parent(psts_nid);
    auto new_psts_nid = get_parent(tmp_if_nid);
    return check_phi_table_parents_chain(target_name, new_psts_nid, originate_from_csts);
  }
}


Lnast_nid Lnast::add_phi_node(const Lnast_nid &cond_nid, const Lnast_nid &t_nid, const Lnast_nid &f_nid) {
  auto new_phi_nid = add_child(get_parent(cond_nid), Lnast_node(Lnast_ntype::create_phi(), Token()));
  auto target_nid  = add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(), get_data(t_nid).token, get_data(t_nid).subs));
  update_ssa_cnt_table(target_nid);
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_cond(), get_data(cond_nid).token, get_data(cond_nid).subs));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  get_data(t_nid).token, get_data(t_nid).subs));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  get_data(f_nid).token, get_data(f_nid).subs));
  new_added_phi_node_table[get_data(target_nid).token.get_text()] = target_nid;
  return target_nid;
}


bool Lnast::has_else_stmts(const Lnast_nid &if_nid) {
  Lnast_nid last_child = get_last_child(if_nid);
  Lnast_nid second_last_child = get_sibling_prev(last_child);
  return (get_data(last_child).type.is_stmts() and get_data(second_last_child).type.is_stmts());
}

void Lnast::ssa_handle_a_statement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid) {
  //handle statement rhs
  for (auto itr_opd : children(opr_nid)) {
    if (itr_opd == get_first_child(opr_nid))
      continue;
    if (ssa_cnt_table.find(get_data(itr_opd).token.get_text()) != ssa_cnt_table.end()){
      const auto itr_opd_type = get_data(itr_opd).type;
      uint8_t new_subs = ssa_cnt_table[get_data(itr_opd).token.get_text()];
      fmt::print("new subs:{}\n", new_subs);
      Token   ori_token = get_data(itr_opd).token;
      set_data(itr_opd, Lnast_node(itr_opd_type, ori_token, new_subs));
    }
  }

  //handle statement lhs
  const auto type = get_data(opr_nid).type;
  if (type.is_assign() || type.is_as()) {
    const auto  target_nid  = get_first_child(opr_nid);
          auto& target_data = *ref_data(target_nid);
    const auto  target_name = target_data.token.get_text();

//    if ((target_name.substr(0,3) == "___") || elder_sibling_is_label(opr_nid))
//      return;
    if (target_name.substr(0,3) == "___")
      return;

    update_ssa_cnt_table(target_nid);
    update_phi_resolve_table(psts_nid, target_nid);
  }
}



void Lnast::ssa_handle_a_cstatement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid){
  //handle statement rhs
  for (auto itr_opd : children(opr_nid)){
    if(itr_opd == get_first_child(opr_nid))
      continue;

    const auto itr_opd_type = get_data(itr_opd).type;
    const auto itr_opd_name = get_data(itr_opd).token.get_text();
    if(itr_opd_type.is_const())
      continue;
    if(itr_opd_name.substr(0,3) == "___")
      continue;

    const auto ref_nid      = check_phi_table_parents_chain(itr_opd_name, psts_nid, true);
    uint8_t    new_subs     = get_data(ref_nid).subs;
    Token      ori_token    = get_data(itr_opd).token;


    set_data(itr_opd, Lnast_node(itr_opd_type, ori_token, new_subs));
  }
  //no need to handle statement lhs in csts
}


void Lnast::update_ssa_cnt_table(const Lnast_nid &target_nid) {
  auto& target_data = *ref_data(target_nid);
  const auto  target_name =target_data.token.get_text();
  auto itr = ssa_cnt_table.find(target_name);
  if (itr != ssa_cnt_table.end()) {
    itr->second += 1;
    target_data.subs = itr->second;
  } else {
    ssa_cnt_table[target_name] = 0;
  }
}

void Lnast::update_phi_resolve_table(const Lnast_nid &psts_nid, const Lnast_nid &target_nid) {
  auto       &phi_resolve_table = phi_resolve_tables[get_data(psts_nid).token.get_text()];
  auto       &target_data       = *ref_data(target_nid);
  const auto  target_name       = target_data.token.get_text();
  phi_resolve_table[target_name] = target_nid; //for a variable string, always update to latest Lnast_nid
}

bool Lnast::elder_sibling_is_label(const Lnast_nid &opr_nid) {
  auto prev_idx = get_sibling_prev(opr_nid);
  if (prev_idx.is_invalid())
    return false;

  return get_data(prev_idx).type.is_label();
}


