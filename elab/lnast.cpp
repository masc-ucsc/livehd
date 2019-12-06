#include <mmap_vector.hpp>
#include "lnast.hpp"

void Lnast_node::dump() const {
  ;
  //fmt::print("type:{} loc:{}\n", type.debug_name(), loc); // TODO: cleaner API to also dump token
}

void Lnast::do_ssa_trans(const Lnast_nid &top_nid){
  Lnast_nid top_sts_nid = get_first_child(top_nid);
  Rename_table top_phi_resolve_table;
  phi_resolve_tables[get_data(top_sts_nid).token.get_text(buffer)] = top_phi_resolve_table;
  sview2token[get_data(top_sts_nid).token.get_text(buffer)] = get_data(top_sts_nid).token;
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
      Rename_table if_sts_phi_resolve_table;
      phi_resolve_tables[get_data(itr_nid).token.get_text(buffer)] = if_sts_phi_resolve_table;
      sview2token[get_data(itr_nid).token.get_text(buffer)] = get_data(itr_nid).token;

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
  for (const auto & itr : children(if_nid)) {
    if (get_data(itr).type.is_statements()) {
      if_statements_vec.push_back(itr);
    }
  }
  //2 possible cases: (1)if-elif-elif (2) if-elif-else
  for (auto itr = if_statements_vec.rbegin(); itr != if_statements_vec.rend(); ++itr ) {
    if (itr == if_statements_vec.rbegin() && has_else_statements(if_nid))
      continue;
    else if (itr == if_statements_vec.rbegin()+1 && has_else_statements(if_nid)) {
      Rename_table &true_table = phi_resolve_tables[get_data(*itr).token.get_text(buffer)];
      Rename_table &false_table = phi_resolve_tables[get_data(*if_statements_vec.rbegin()).token.get_text(buffer)];
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);
    }
    else {
      Rename_table &true_table = phi_resolve_tables[get_data(*itr).token.get_text(buffer)];
      Rename_table &false_table = new_added_phi_node_table;
      Lnast_nid condition_nid = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_nid, true_table, false_table);
    }
  }
}

void Lnast::resolve_phi_nodes(const Lnast_nid &cond_nid, Rename_table &true_table, Rename_table &false_table) {
  last_sibling_nid = get_parent(cond_nid);

  for (auto const& [key, val] : true_table) {
    if(false_table.find(key)!= false_table.end()){
      auto new_phi_nid = add_phi_node(cond_nid, key, val, false_table[key]);
      //sh:todo: using this phi_node for add next siblings
    }
    ;//have to remove visited keys in both true/false tables or there will be duplicated phi node added
 }

 for (auto const& [key, val] : false_table) {
    ;//have to update the false table
 }

 //I(true_table.empty()); //even it's a new_added_phi_node_table

 //I(false_table.empty()); not really, it's usually the new_added_phi_node_tables, there
}


//Lnast_nid Lnast::add_phi_node(const Lnast_nid &cond_nid, const Token var, const uint8_t tcnt, const uint8_t fcnt) {
Lnast_nid Lnast::add_phi_node(const Lnast_nid &cond_nid, const std::string_view var, const uint8_t tcnt, const uint8_t fcnt) {
  auto new_phi_nid = add_next_sibling(last_sibling_nid, Lnast_node(Lnast_ntype::create_phi(), Token()));
  //sh:todo: add child of new_phi_nid
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_cond(), get_data(cond_nid).token));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  sview2token[var], tcnt));
  add_child(new_phi_nid, Lnast_node(Lnast_ntype::create_ref(),  sview2token[var], fcnt));

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

    update_ssa_cnt_table(target_data);
    update_phi_resolve_table(psts_nid, target_data);
  }
}


void Lnast::update_ssa_cnt_table(Lnast_node& target_data){
  const auto  target_name = target_data.token.get_text(buffer);
  //const auto  target_name = target_data.token;
  auto itr = ssa_cnt_table.find(target_name);
  if (itr != ssa_cnt_table.end()) {
    itr->second += 1;
    target_data.subs = itr->second;
  } else {
    ssa_cnt_table[target_name] = 0;
    sview2token[target_data.token.get_text(buffer)] = target_data.token;
  }
}


//sh:fixme:currently we cannot use Tree_index as absl index...
void Lnast::update_phi_resolve_table(const Lnast_nid &psts_nid, Lnast_node& target_data){
  const auto target_name = target_data.token.get_text(buffer);
  fmt::print("phi_resovle_table:{}\n", get_data(psts_nid).token.get_text(buffer));
  auto &phi_resolve_table = phi_resolve_tables[get_data(psts_nid).token.get_text(buffer)];
  //auto &phi_resolve_table = phi_resolve_tables[get_data(psts_nid).token];
  phi_resolve_table[target_name] = target_data.subs;
  sview2token[target_data.token.get_text(buffer)] = target_data.token;
}

bool Lnast::elder_sibling_is_label(const Lnast_nid &opr_nid) {
  auto prev_idx = get_sibling_prev(opr_nid);
  if (prev_idx.is_invalid())
    return false;

  return get_data(prev_idx).type.is_label();
}


