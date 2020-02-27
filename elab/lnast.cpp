#include <mmap_vector.hpp>
#include "lnast.hpp"

void Lnast_node::dump() const {
  fmt::print("type:{}\n", type.debug_name()); // TODO: cleaner API to also dump token
}
/*
note: if not handle ssa cnt on lhs and rhs separately, there will be a race condition in the
      if-subtree between child-True and child-False. For example, in the following source code:

      A = 5
      A = A + 4
      if (condition)
        A = A + 1
        A = A + 2
      else
        A = A + 3
      %out = A

      So the new ssa transformation has two main part. The first and original algorithm
      focus on the lhs ssa cnt, phi-node resolving, and phi-node insertion. The lhs ssa
      cnt only need a global count table and the cnt of lhs on every expression will be
      handled correctly. The second algorithm focus on rhs assignment by using tree rhs
      cnt tables.

      rhs assignment in 2nd algorithm:
      - check current scope
      - if exitst
          use the local count from local table
        else
          if not in parents chain
            compile error
          else
            copy from parents to local tables and
            use it as cnt
      lhs assignment in 2nd algorithm:
      - just copy the subs from the lnast nodes into the local table
        as the lhs subs has been handled in 1st algorithm.
*/
void Lnast::do_ssa_trans(const Lnast_nid &top_nid){
  Lnast_nid top_sts_nid = get_first_child(top_nid);
  default_const_nid = add_child(top_sts_nid, Lnast_node(Lnast_ntype::create_const(), Token(Token_id_alnum, 0, 0, 0, "default_const")));

  Phi_rtable top_phi_resolve_table;
  phi_resolve_tables[get_name(top_sts_nid)] = top_phi_resolve_table;
  for (const auto &opr_nid : children(top_sts_nid)) {
    if (get_type(opr_nid).is_if()) {
      ssa_if_subtree(opr_nid);
    } else if (get_type(opr_nid).is_func_def()) {
      do_ssa_trans(opr_nid);
    } else {
      ssa_handle_a_statement(top_sts_nid, opr_nid);
    }
  }

  resolve_ssa_rhs_subs(top_sts_nid);
}


void Lnast::resolve_ssa_rhs_subs(const Lnast_nid &psts_nid) {
  Cnt_rtable top_ssa_rhs_cnt_table;
  ssa_rhs_cnt_tables[get_name(psts_nid)] = top_ssa_rhs_cnt_table;
  for (const auto &opr_nid : children(psts_nid)) {
    if (get_type(opr_nid).is_func_def()) {
      continue;
    } else if (get_type(opr_nid).is_if()) {
      ssa_rhs_if_subtree(opr_nid);
    } else {
      ssa_rhs_handle_a_statement(psts_nid, opr_nid);
    }
  }
}

void Lnast::ssa_rhs_if_subtree(const Lnast_nid &if_nid) {
  for (const auto &itr_nid : children(if_nid)) {
    if (get_type(itr_nid).is_stmts()) {
      Cnt_rtable if_sts_ssa_rhs_cnt_table;
      ssa_rhs_cnt_tables[get_name(itr_nid)] = if_sts_ssa_rhs_cnt_table;

      for (const auto &opr_nid : children(itr_nid)) {
        I(!get_type(opr_nid).is_func_def());
        if (get_type(opr_nid).is_if())
          ssa_rhs_if_subtree(opr_nid);
        else
          ssa_rhs_handle_a_statement(itr_nid, opr_nid);
      }
    } else if (get_type(itr_nid).is_cstmts()) {
      for (const auto &opr_nid : children(itr_nid)){
        ssa_rhs_handle_a_statement(itr_nid, opr_nid);
      }
    } else if (get_type(itr_nid).is_phi()){
        update_rhs_ssa_cnt_table(get_parent(if_nid), get_first_child(itr_nid));
    } else { //condition node
      continue;
    }
  }
}


void Lnast::ssa_rhs_handle_a_statement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid) {
  auto &ssa_rhs_cnt_table = ssa_rhs_cnt_tables[get_name(psts_nid)];
  //handle statement rhs
  for (auto itr_opd : children(opr_nid)) {
    if (itr_opd == get_first_child(opr_nid))
      continue;

    auto       opd_name  = get_name(itr_opd);
    const auto opd_type  = get_type(itr_opd);
    Token      ori_token = get_token(itr_opd);

    if (ssa_rhs_cnt_table.find(opd_name) != ssa_rhs_cnt_table.end()){
      uint8_t new_subs = ssa_rhs_cnt_table[get_name(itr_opd)];
      set_data(itr_opd, Lnast_node(opd_type, ori_token, new_subs));
      fmt::print("variable:{}, new subs:{}\n", get_name(itr_opd), new_subs);
    } else {
      int8_t  new_subs = check_rhs_cnt_table_parents_chain(psts_nid, itr_opd);
      if (new_subs == -1) {
        new_subs = 0; //FIXME: sh: actually, here is a good place to check undefined variable
      }
      ssa_rhs_cnt_table[opd_name] = new_subs;
      set_data(itr_opd, Lnast_node(opd_type, ori_token, new_subs));
    }
  }

  //handle statement lhs
  const auto type = get_type(opr_nid);
  if (type.is_assign() || type.is_as()) {
    const auto  target_nid  = get_first_child(opr_nid);
    const auto  target_name = get_name(target_nid);

    if (target_name.substr(0,3) == "___")
      return;


    update_rhs_ssa_cnt_table(psts_nid, target_nid);
  }
}

void Lnast::ssa_if_subtree(const Lnast_nid &if_nid) {
  for (const auto &itr_nid : children(if_nid)) {
    if (get_type(itr_nid).is_stmts()) {
      Phi_rtable if_sts_phi_resolve_table;
      phi_resolve_tables[get_name(itr_nid)] = if_sts_phi_resolve_table;

      for (const auto &opr_nid : children(itr_nid)) {
        I(!get_type(opr_nid).is_func_def());
        if (get_type(opr_nid).is_if())
          ssa_if_subtree(opr_nid);
        else
          ssa_handle_a_statement(itr_nid, opr_nid);
      }
    } else { //condition node or csts
      continue;
    }
  }
  ssa_handle_phi_nodes(if_nid);
}

void Lnast::ssa_handle_phi_nodes(const Lnast_nid &if_nid) {
  std::vector<Lnast_nid> if_stmts_vec;
  for (const auto &itr : children(if_nid)) {
    if (get_type(itr).is_stmts())
      if_stmts_vec.push_back(itr);
  }

  //2 possible cases: (1)if-elif-elif (2) if-elif-else
  //note: handle reversely to get correct mux priority chain
  for (auto itr = if_stmts_vec.rbegin(); itr != if_stmts_vec.rend(); ++itr) {
    if (itr == if_stmts_vec.rbegin() && has_else_stmts(if_nid)) {
      continue;
    } else if (itr == if_stmts_vec.rbegin() && !has_else_stmts(if_nid)) {
      Phi_rtable &true_table  = phi_resolve_tables[get_name(*itr)];
      Phi_rtable fake_false_table ; //for the case of if-elif-elif
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, fake_false_table);

    } else if (itr == if_stmts_vec.rbegin()+1 && has_else_stmts(if_nid)) {
      Phi_rtable &true_table  = phi_resolve_tables[get_name(*itr)];
      Phi_rtable &false_table = phi_resolve_tables[get_name(*if_stmts_vec.rbegin())];
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);

    } else {
      Phi_rtable &true_table  = phi_resolve_tables[get_name(*itr)];
      Phi_rtable &false_table = new_added_phi_node_table;
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);

    }
  }
}



//FIXME: SH: what if the phi-tables are already empty?
//FIXME: SH: is it a correct case? if yes, what action should be taken to avoid
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
    if (true_table.empty()) // it might be empty due to the erase from previous for loop
      break;

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
  auto &parent_table = phi_resolve_tables[get_name(psts_nid)];

  if(parent_table.find(target_name) != parent_table.end())
    return parent_table[target_name];

  if (get_parent(psts_nid) == get_root() && originate_from_csts) {//current sts is top_sts
    //I(false); //variable not defined
  } else if (get_parent(psts_nid) == get_root() && !originate_from_csts){
    return default_const_nid;
  } else {
    auto tmp_if_nid = get_parent(psts_nid);
    auto new_psts_nid = get_parent(tmp_if_nid);
    return check_phi_table_parents_chain(target_name, new_psts_nid, originate_from_csts);
  }
  I(false);
}


Lnast_nid Lnast::add_phi_node(const Lnast_nid &cond_nid, const Lnast_nid &t_nid, const Lnast_nid &f_nid) {
  auto new_phi_nid = add_child(get_parent(cond_nid), Lnast_node(Lnast_ntype::create_phi(), Token()));
  auto target_nid  = add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(), get_token(t_nid), get_subs(t_nid)));
  update_global_lhs_ssa_cnt_table(target_nid);
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_cond(), get_token(cond_nid), get_subs(cond_nid)));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  get_token(t_nid), get_subs(t_nid)));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  get_token(f_nid), get_subs(f_nid)));
  new_added_phi_node_table[get_name(target_nid)] = target_nid;

  auto psts_nid = get_parent(get_parent(cond_nid));
  update_phi_resolve_table(psts_nid, target_nid);
  return target_nid;
}


bool Lnast::has_else_stmts(const Lnast_nid &if_nid) {
  Lnast_nid last_child = get_last_child(if_nid);
  Lnast_nid second_last_child = get_sibling_prev(last_child);
  return (get_type(last_child).is_stmts() and get_type(second_last_child).is_stmts());
}

void Lnast::ssa_handle_a_statement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid) {
  //note: handle statement rhs in the 2nd part SSA

  //handle statement lhs
  const auto type = get_type(opr_nid);
  if (type.is_assign() || type.is_as()) {
    const auto  target_nid  = get_first_child(opr_nid);
    const auto  target_name = get_name(target_nid);

    if (target_name.substr(0,3) == "___")
      return;

    update_global_lhs_ssa_cnt_table(target_nid);
    update_phi_resolve_table(psts_nid, target_nid);
  }
}


void Lnast::update_global_lhs_ssa_cnt_table(const Lnast_nid &target_nid) {
  const auto  target_name = get_name(target_nid);
  auto itr = global_ssa_lhs_cnt_table.find(target_name);
  if (itr != global_ssa_lhs_cnt_table.end()) {
    itr->second += 1;
    ref_data(target_nid)->subs = itr->second;
  } else {
    global_ssa_lhs_cnt_table[target_name] = 0;
  }
}


//note: the subs of the lhs of the operator has already handled clearly in first round ssa process, just copy into the rhs_ssa_cnt_table fine.
void Lnast::update_rhs_ssa_cnt_table(const Lnast_nid &psts_nid, const Lnast_nid &target_key) {
  auto &ssa_rhs_cnt_table = ssa_rhs_cnt_tables[get_name(psts_nid)];
  const auto target_name = get_name(target_key);
  ssa_rhs_cnt_table[target_name] = ref_data(target_key)->subs;
}

int8_t Lnast::check_rhs_cnt_table_parents_chain(const Lnast_nid &psts_nid, const Lnast_nid &target_key) {
  auto &ssa_rhs_cnt_table = ssa_rhs_cnt_tables[get_name(psts_nid)];
  const auto  target_name = get_name(target_key);
  auto itr = ssa_rhs_cnt_table.find(target_name);

  if (itr != ssa_rhs_cnt_table.end()) {
    return ssa_rhs_cnt_table[target_name];
  } else if (get_parent(psts_nid) == get_root()) {
    return -1;
  } else {
    auto tmp_if_nid = get_parent(psts_nid);
    auto new_psts_nid = get_parent(tmp_if_nid);
    return check_rhs_cnt_table_parents_chain(new_psts_nid, target_key);
  }
}

void Lnast::update_phi_resolve_table(const Lnast_nid &psts_nid, const Lnast_nid &target_nid) {
  auto       &phi_resolve_table = phi_resolve_tables[get_name(psts_nid)];
  const auto  target_name       = get_name(target_nid);
  phi_resolve_table[target_name] = target_nid; //for a variable string, always update to latest Lnast_nid
}

