#include <mmap_vector.hpp>
#include "lnast.hpp"

void Lnast_node::dump() const {
  ;
  //fmt::print("type:{} loc:{}\n", type.debug_name(), loc); // TODO: cleaner API to also dump token
}

void Lnast::do_ssa_trans(const Lnast_nid &top_nid){
  Lnast_nid top_sts_nid = get_first_child(top_nid);
  default_const_nid = add_child(top_sts_nid, Lnast_node(Lnast_ntype::create_const(), Token()));


  Phi_rtable top_phi_resolve_table;
  phi_resolve_tables[get_data(top_sts_nid).token.get_text(buffer)] = top_phi_resolve_table;
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

void Lnast::ssa_if_subtree(const Lnast_nid &if_nid){
  for (const auto &itr_nid : children(if_nid)) {
    if (get_data(itr_nid).type.is_statements()) {
      Phi_rtable if_sts_phi_resolve_table;
      phi_resolve_tables[get_data(itr_nid).token.get_text(buffer)] = if_sts_phi_resolve_table;

      for (const auto &opr_nid : children(itr_nid)) {
        I(!get_data(opr_nid).type.is_func_def());
        if (get_data(opr_nid).type.is_if())
          ssa_if_subtree(opr_nid);
        else
          ssa_handle_a_statement(itr_nid, opr_nid);
      }
    }
  }
  ssa_handle_phi_nodes(if_nid);
}

void Lnast::ssa_handle_phi_nodes(const Lnast_nid &if_nid) {
  std::vector<Lnast_nid> if_statements_vec;
  for (const auto &itr : children(if_nid)) {
    if (get_data(itr).type.is_statements()) {
      if_statements_vec.push_back(itr);
    }
  }
  //2 possible cases: (1)if-elif-elif (2) if-elif-else
  //note: handle reversely to get correct mux priority chain
  for (auto itr = if_statements_vec.rbegin(); itr != if_statements_vec.rend(); ++itr ) {
    if (itr == if_statements_vec.rbegin() && has_else_statements(if_nid))
      continue;
    else if (itr == if_statements_vec.rbegin()+1 && has_else_statements(if_nid)) {
      Phi_rtable &true_table  = phi_resolve_tables[get_data(*itr).token.get_text(buffer)];
      Phi_rtable &false_table = phi_resolve_tables[get_data(*if_statements_vec.rbegin()).token.get_text(buffer)];
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);
    }
    else {
      Phi_rtable &true_table  = phi_resolve_tables[get_data(*itr).token.get_text(buffer)];
      Phi_rtable &false_table = new_added_phi_node_table;
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);
    }
  }
}

void Lnast::resolve_phi_nodes(const Lnast_nid &cond_nid, Phi_rtable &true_table, Phi_rtable &false_table) {
  for (auto const& [key, val] : true_table) {
    if(false_table.find(key)!= false_table.end()){
      auto new_phi_nid = add_phi_node(cond_nid, true_table[key], false_table[key]);
      (void) new_phi_nid;
      false_table.erase(key);
    } else {
      //(1)check new_added_phi_node_table first
      //(2)check parent chain until top phi-resolve table
      auto if_nid = get_parent(cond_nid);
      auto psts_nid = get_parent(if_nid);
      auto f_nid = get_complement_nid(key, psts_nid);
      auto new_phi_nid = add_phi_node(cond_nid, true_table[key], f_nid);
      (void) new_phi_nid;
    }
    //have to remove visited keys in both true/false tables or there will be duplicated phi node added
    true_table.erase(key);
  }

  for (auto const& [key, val] : false_table) {
    //this case should be resovled by previous for loop
    I(true_table.find(key)== true_table.end());

    //(1)check new_added_phi_node_table first
    //(2)check parent chain until top phi-resolve table
    auto if_nid = get_parent(cond_nid);
    auto psts_nid = get_parent(if_nid);
    auto t_nid = get_complement_nid(key, psts_nid);
    auto new_phi_nid = add_phi_node(cond_nid, t_nid, false_table[key]);
    (void) new_phi_nid;
  }

  I(true_table.empty());
  //I(false_table.empty()); not really, it's usually the new_added_phi_node_tables
}

Lnast_nid Lnast::get_complement_nid(std::string_view brother_name, const Lnast_nid &psts_nid) {
  if(new_added_phi_node_table.find(brother_name) != new_added_phi_node_table.end())
    return new_added_phi_node_table[brother_name];
  else
    return check_phi_table_parents_chain(brother_name, psts_nid);
}

Lnast_nid Lnast::check_phi_table_parents_chain(std::string_view brother_name, const Lnast_nid &psts_nid) {
  auto &parent_table = phi_resolve_tables[get_data(psts_nid).token.get_text(buffer)];

  if(parent_table.find(brother_name) != parent_table.end())
    return parent_table[brother_name];

  if (get_parent(psts_nid) == get_root()) {//current sts is top_sts
    return default_const_nid;
  } else {
    auto tmp_if_nid = get_parent(psts_nid);
    auto new_psts_nid = get_parent(tmp_if_nid);
    return check_phi_table_parents_chain(brother_name, new_psts_nid);
  }
}


Lnast_nid Lnast::add_phi_node(const Lnast_nid &cond_nid, const Lnast_nid &t_nid, const Lnast_nid &f_nid) {
  auto new_phi_nid = add_child(get_parent(cond_nid), Lnast_node(Lnast_ntype::create_phi(), Token()));
  auto target_nid = add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  get_data(t_nid).token, get_data(t_nid).subs));
  update_ssa_cnt_table(target_nid);
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_cond(), get_data(cond_nid).token, get_data(cond_nid).subs));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  get_data(t_nid).token, get_data(t_nid).subs));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  get_data(f_nid).token, get_data(f_nid).subs));
  new_added_phi_node_table[get_data(target_nid).token.get_text(buffer)] = target_nid;

  return new_phi_nid;
}


bool Lnast::has_else_statements(const Lnast_nid &if_nid) {
  Lnast_nid last_child = get_last_child(if_nid);
  Lnast_nid second_last_child = get_sibling_prev(last_child);
  return !(get_data(last_child).type.is_statements() and get_data(second_last_child).type.is_cstatements());
}

void Lnast::ssa_handle_a_statement(const Lnast_nid &psts_nid, const Lnast_nid &opr_nid){
  const auto type = get_data(opr_nid).type;
  if(type.is_pure_assign() || type.is_as()){
    const auto  target_nid  = get_first_child(opr_nid);
          auto& target_data = *ref_data(target_nid);
    const auto  target_name = target_data.token.get_text(buffer);

    if ((target_name.substr(0,3) == "___") || elder_sibling_is_label(opr_nid))
      return;

    update_ssa_cnt_table(target_nid);
    update_phi_resolve_table(psts_nid, target_nid);
  }
}


void Lnast::update_ssa_cnt_table(const Lnast_nid& target_nid){
  auto& target_data = *ref_data(target_nid);
  const auto  target_name =target_data.token.get_text(buffer);
  auto itr = ssa_cnt_table.find(target_name);
  if (itr != ssa_cnt_table.end()) {
    itr->second += 1;
    target_data.subs = itr->second;
  } else {
    ssa_cnt_table[target_name] = 0;
  }
}

void Lnast::update_phi_resolve_table(const Lnast_nid &psts_nid, const Lnast_nid& target_nid){
  fmt::print("phi_resovle_table:{}\n", get_data(psts_nid).token.get_text(buffer));
  auto &phi_resolve_table = phi_resolve_tables[get_data(psts_nid).token.get_text(buffer)];
  auto& target_data = *ref_data(target_nid);
  const auto target_name = target_data.token.get_text(buffer);
  phi_resolve_table[target_name] = target_nid; //for a variable string, always update to latest Lnast_nid
}

bool Lnast::elder_sibling_is_label(const Lnast_nid &opr_nid) {
  auto prev_idx = get_sibling_prev(opr_nid);
  if (prev_idx.is_invalid())
    return false;

  return get_data(prev_idx).type.is_label();
}


