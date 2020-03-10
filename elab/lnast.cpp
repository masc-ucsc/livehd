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
      - if exists
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

  //step-1: determine lhs or rhs of tuple dot/sel
  fmt::print("\nStep-1: Determine LHS or RHS of tuple dot/sel\n");
  determine_dot_sel_lrhs(top_sts_nid);

  //step-2: lhs ssa
  fmt::print("\nStep-2: LHS SSA\n");
  for (const auto &opr_nid : children(top_sts_nid)) {
    if (get_type(opr_nid).is_if()) {
      ssa_if_subtree(opr_nid);
    } else if (get_type(opr_nid).is_func_def()) {
      do_ssa_trans(opr_nid);
    } else {
      ssa_handle_a_statement(top_sts_nid, opr_nid);
    }
  }

  //step-3: rhs ssa
  fmt::print("\nStep-3: RHS SSA\n");
  resolve_ssa_rhs_subs(top_sts_nid);
}


void Lnast::determine_dot_sel_lrhs(const Lnast_nid &psts_nid) {
  for (const auto &opr_nid : children(psts_nid)) {

    if (get_type(opr_nid).is_dot() && get_name(get_sibling_next(get_sibling_next(get_first_child(opr_nid)))).substr(0,6) == "__bits") {
      dot_sel_lrhs_table[opr_nid] = false;
      fmt::print("dot/sel:{} is rhs\n", get_name(get_first_child(opr_nid)));
      continue;
    }

    if (get_type(opr_nid).is_dot() || get_type(opr_nid).is_select()) {
      auto dot_nid     = opr_nid;
      auto c0_dot      = get_first_child(dot_nid); //c0 = intermediate target
      auto c0_dot_name = get_name(c0_dot);
      bool hit         = false;
      auto sib_nid     = opr_nid;
      while (!hit) {
        sib_nid = get_sibling_next(sib_nid);
        for (auto sib_child : children(sib_nid)) {
          //only possible for assign_op
          if (sib_child == get_first_child(sib_nid) and get_name(sib_child) == c0_dot_name) {
            hit = true;
            dot_sel_lrhs_table[dot_nid] = true;
            dot_sel_lhs_dst_assign_node_table[dot_nid] = sib_nid;
            fmt::print("dot/sel:{} is lhs\n", get_name(get_first_child(dot_nid)));
          } else if (get_name(sib_child) == c0_dot_name){
            hit = true;
            dot_sel_lrhs_table[dot_nid] = false;
            fmt::print("dot/sel:{} is rhs\n", get_name(get_first_child(dot_nid)));
          }
        }
      } //note: practically, the assign/opr_op related to the dot/sel_op should be very close
    }
  }
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
  const auto type = get_type(opr_nid);

  if (type.is_dot() || type.is_select()) {
    //handle dot/set which is a rhs
    auto c0_opr      = get_first_child(opr_nid);
    auto c1_opr      = get_sibling_next(c0_opr); // c1 of dot/sel is target_nid
    if (!is_lhs(opr_nid) and is_special_case_of_dot_sel_rhs(psts_nid, opr_nid)) {
      ssa_rhs_handle_a_operand_special(psts_nid, c1_opr);
    } else if (!is_lhs(opr_nid)) {
      ssa_rhs_handle_a_operand(psts_nid, c1_opr);
    }
    fmt::print("dot/sel:{}, tuple:{}, new subs:{}\n", get_name(c0_opr), get_name(c1_opr), get_subs(c1_opr));
  } else {
    //handle statement rhs of normal operators
    for (auto itr_opd : children(opr_nid)) {
      if (itr_opd == get_first_child(opr_nid)) continue;
      ssa_rhs_handle_a_operand(psts_nid, itr_opd);
    }
  }

  //handle dot/set which is a lhs
  if (type.is_dot() || type.is_select()) {
    auto c0_opr      = get_first_child(opr_nid);
    auto c1_opr      = get_sibling_next(c0_opr); // c1 of dot/sel is target_nid
    if (is_lhs(opr_nid))
      update_rhs_ssa_cnt_table(psts_nid, c1_opr);
    //fmt::print("dot/sel:{}, tuple:{}, new subs:{}\n", get_name(c0_opr), get_name(c1_opr), get_subs(c1_opr));
  }

  //handle statement lhs
  if (type.is_assign() || type.is_as()) {
    const auto  target_nid  = get_first_child(opr_nid);
    const auto  target_name = get_name(target_nid);

    if (target_name.substr(0,3) == "___") return;

    update_rhs_ssa_cnt_table(psts_nid, target_nid);
  }
}


//handle cases: A.foo = A[2] or A.foo = A[1] + A[2] + A.bar; where lhs rhs are both the struct elements;
//the ssa should be: A_2.foo = A_1[2] or A_6.foo = A_5[1] + A_5[2] + A_5.bar
bool Lnast::is_special_case_of_dot_sel_rhs(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid) {
  I(!is_lhs(opr_nid));

  if (opr_nid == get_first_child(psts_nid))
    return false;

  auto prev_sib_nid = get_sibling_prev(opr_nid);

  if ((get_type(prev_sib_nid).is_dot() or get_type(prev_sib_nid).is_select())) {
    if (not dot_sel_lrhs_table[prev_sib_nid]) {
      return is_special_case_of_dot_sel_rhs(psts_nid, prev_sib_nid);
    } else if (dot_sel_lrhs_table[prev_sib_nid]) {
      return true;
    }
  }
  return false;
}

void Lnast::ssa_rhs_handle_a_operand_special(const Lnast_nid &gpsts_nid, const Lnast_nid &opd_nid) {
  //note: immediate struct self assignment: A.foo = A[2], which will leads to consecutive dot and sel,
  //the sel should follow the subscript before the dot increments it.
  auto &ssa_rhs_cnt_table = ssa_rhs_cnt_tables[get_name(gpsts_nid)];
  auto       opd_name  = get_name(opd_nid);
  const auto opd_type  = get_type(opd_nid);
  Token      ori_token = get_token(opd_nid);

  if (ssa_rhs_cnt_table.find(opd_name) != ssa_rhs_cnt_table.end()) {
    auto  new_subs = ssa_rhs_cnt_table[opd_name] - 1;
    set_data(opd_nid, Lnast_node(opd_type, ori_token, new_subs));
    fmt::print("variable:{}, new subs:{}\n", opd_name, new_subs);
  }
}


void Lnast::ssa_rhs_handle_a_operand(const Lnast_nid &gpsts_nid, const Lnast_nid &opd_nid) {
  auto &ssa_rhs_cnt_table = ssa_rhs_cnt_tables[get_name(gpsts_nid)];
  auto       opd_name  = get_name(opd_nid);
  const auto opd_type  = get_type(opd_nid);
  Token      ori_token = get_token(opd_nid);

  if (ssa_rhs_cnt_table.find(opd_name) != ssa_rhs_cnt_table.end()) {

    auto  new_subs = ssa_rhs_cnt_table[opd_name];
    set_data(opd_nid, Lnast_node(opd_type, ori_token, new_subs));
    fmt::print("variable:{}, new subs:{}\n", opd_name, new_subs);
  } else {
    int8_t  new_subs = check_rhs_cnt_table_parents_chain(gpsts_nid, opd_nid);
    if (new_subs == -1) {
      new_subs = 0; //FIXME->sh: actually, here is a good place to check undefined variable
    }
    ssa_rhs_cnt_table[opd_name] = new_subs;
    set_data(opd_nid, Lnast_node(opd_type, ori_token, new_subs));
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



//FIXME->sh: what if the phi-tables are already empty?
//FIXME->sh: is it a correct case? if yes, what action should be taken to avoid
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
  if (type.is_assign() || type.is_as() || type.is_tuple()) {
    const auto  target_nid  = get_first_child(opr_nid);
    const auto  target_name = get_name(target_nid);

    if (target_name.substr(0,3) == "___") return;

    update_global_lhs_ssa_cnt_table(target_nid);
    fmt::print("variable:{}, new subs:{}\n", get_name(target_nid), get_subs(target_nid));
    update_phi_resolve_table(psts_nid, target_nid);
    return;
  }


  if (type.is_dot() || type.is_select()) {
    if (is_lhs(opr_nid)) {
      auto c0_opr      = get_first_child(opr_nid);
      auto c1_opr      = get_sibling_next(c0_opr); // c1 of dot/sel is target_nid
      update_global_lhs_ssa_cnt_table(c1_opr);
      update_phi_resolve_table(psts_nid, c1_opr);
      fmt::print("dot/sel:{}, tuple:{}, new subs:{}\n", get_name(c0_opr), get_name(c1_opr), get_subs(c1_opr));
    }
  }

  // // when opr is dot/sel, it might represent both lhs or rhs, need to check future siblings to know
  // if (type.is_dot() || type.is_select()) {
  //   auto c0_opr      = get_first_child(opr_nid);
  //   auto c0_opr_name = get_name(c0_opr);
  //   auto target_nid  = get_sibling_next(c0_opr); // c1 of dot/sel is target_nid
  //   bool hit = false;
  //   auto sib = opr_nid;
  //   while (!hit) {
  //     sib = get_sibling_next(sib);
  //     if (get_type(sib).is_assign()) {
  //       auto lhs_assign = get_first_child(sib);
  //       auto rhs_assign = get_sibling_next(lhs_assign);
  //       if (get_name(lhs_assign) == c0_opr_name) {
  //         hit = true;
  //         update_global_lhs_ssa_cnt_table(target_nid);
  //         update_phi_resolve_table(psts_nid, target_nid);
  //         fmt::print("target_nid->name:{}, target_nid->subs:{}\n", get_name(target_nid), get_subs(target_nid));
  //       } else if (get_name(rhs_assign) == c0_opr_name) {
  //         hit = true;
  //       }
  //     } else if (get_type(sib).is_binary_op() || get_type(sib).is_logical_op()) { //FIXME->sh: more op?
  //       for (auto opr_child = children(sib).begin(); opr_child != children(sib).end(); ++opr_child) {
  //         if (opr_child == children(sib).begin())         continue;
  //         else if (get_name(*opr_child) == c0_opr_name)   hit = true;
  //       }
  //     }
  //   } //note: practically, the assign/opr_op related to the dot/sel_op should be very close
  //   return;
  // }
}

bool Lnast::is_lhs(const Lnast_nid &opr_nid) {
  I(get_type(opr_nid).is_dot() or get_type(opr_nid).is_select());
  if (dot_sel_lrhs_table.find(opr_nid)!= dot_sel_lrhs_table.end())
    return dot_sel_lrhs_table[opr_nid];
  I(false);
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

