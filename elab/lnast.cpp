#include <mmap_vector.hpp>
#include "lnast.hpp"

void Lnast_node::dump() const {
  ;
  //fmt::print("type:{} loc:{}\n", type.debug_name(), loc); // TODO: cleaner API to also dump token
}

void Lnast::do_ssa_trans(const Lnast_nid &top){
  Lnast_nid top_sts_node = get_first_child(top);
  absl::flat_hash_map<std::string_view, uint8_t > top_phi_resolve_table;
  phi_resolve_tables[get_data(top_sts_node).token.get_text(buffer)] = top_phi_resolve_table;
  for (const auto &opr_node : children(top_sts_node)) {
    if (get_data(opr_node).type.is_if()) {
      ssa_if_subtree(opr_node);
    } else if (get_data(opr_node).type.is_func_def()) {
      do_ssa_trans(opr_node);
    } else {
      ssa_handle_statement(top_sts_node, opr_node);
    }
  }
  //fmt::print("\nrename_table content\n\n");
  //for (auto const& [key, val] : rename_table){
    //fmt::print("var:{:<12}, subs:{}\n", key, val);
  //}
}

void Lnast::ssa_if_subtree(const Lnast_nid &if_node){
  for (const auto &itr : children(if_node)) {
    if (get_data(itr).type.is_statements()) {
      absl::flat_hash_map<std::string_view, uint8_t> if_sts_phi_resolve_table;
      phi_resolve_tables[get_data(itr).token.get_text(buffer)] = if_sts_phi_resolve_table;

      for (const auto &opr_node : children(itr)) {
        I(!get_data(opr_node).type.is_func_def());
        if (get_data(opr_node).type.is_if())
          ssa_if_subtree(opr_node);
        else
          ssa_handle_statement(itr, opr_node);
      }
    }
  }
  ssa_handle_phi_nodes(if_node);
}

void Lnast::ssa_handle_phi_nodes(const Lnast_nid &if_node) {
  std::vector<Lnast_nid> if_statements_vec;
  for (const auto & itr : children(if_node)) {
    if (get_data(itr).type.is_statements()) {
      if_statements_vec.push_back(itr);
    }
  }
  //2 possible cases: (1)if-elif-elif (2) if-elif-else
  for (auto itr = if_statements_vec.rbegin(); itr != if_statements_vec.rend(); ++itr ) {
    if (itr == if_statements_vec.rbegin() && has_else_statements(if_node))
      continue;
    else if (itr == if_statements_vec.rbegin()+1 && has_else_statements(if_node)) {
      Rename_table &true_table = phi_resolve_tables[get_data(*itr).token.get_text(buffer)];
      Rename_table &false_table = phi_resolve_tables[get_data(*if_statements_vec.rbegin()).token.get_text(buffer)];
      Lnast_nid condition_node = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_node, true_table, false_table);
    }
    else {
      Rename_table &true_table = phi_resolve_tables[get_data(*itr).token.get_text(buffer)];
      Rename_table &false_table = new_added_phi_node_table;
      Lnast_nid condition_node = get_sibling_prev(*itr);
      resolve_phi_nodes(condition_node, true_table, false_table);
    }
  }
}

void Lnast::resolve_phi_nodes(const Lnast_nid &cond, Rename_table &true_table, Rename_table &false_table) {
 for (auto const& [key, val] : true_table) {
    if(false_table.find(key)!= false_table.end()){
      auto new_phi_node = add_phi_node(cond, key, val, false_table[key]);
      //sh:todo: using this phi_node for add next siblings
    }
    ;//have to remove visited keys in both true/false tables or there will be duplicated phi node added
 }

 for (auto const& [key, val] : false_table) {
    ;//have to update the false table
 }

 I(true_table.empty()); //even it's a new_added_phi_node_table
 //I(false_table.empty()); not really, it's usually the new_added_phi_node_tables, there
}


Lnast_nid Lnast::add_phi_node(const Lnast_nid &cond, const std::string_view val, const uint8_t tcnt, const uint8_t fcnt) {
   last_sibling = get_parent(cond);
   auto new_phi_node = add_next_sibling(last_sibling, Lnast_node());
   //sh:todo: add child of new_phi_node

   return new_phi_node;
}


bool Lnast::has_else_statements(const Lnast_nid &if_node) {
    Lnast_nid last_child = get_last_child(if_node);
    Lnast_nid second_last_child = get_sibling_prev(last_child);
  return !(get_data(last_child).type.is_statements() and get_data(second_last_child).type.is_cstatements());
}

void Lnast::ssa_handle_statement(const Lnast_nid &psts_node, const Lnast_nid &opr_node){
  const auto type = get_data(opr_node).type;
  if(type.is_pure_assign() || type.is_as()){
    const auto  target_node = get_first_child(opr_node);
          auto& target_data = *ref_data(target_node);
    const auto  target_name = target_data.token.get_text(buffer);

    if ((target_name.substr(0,3) == "___") || elder_sibling_is_label(opr_node))
      return;

    update_ssa_cnt_table(target_data);
    update_phi_resolve_table(psts_node, target_data);
  }
}


void Lnast::update_ssa_cnt_table(Lnast_node& target_data){
  const auto  target_name = target_data.token.get_text(buffer);
  auto itr = ssa_cnt_table.find(target_name);
  if (itr != ssa_cnt_table.end()) {
    itr->second += 1;
    target_data.subs = itr->second;
  } else {
    ssa_cnt_table[target_name] = 0;
  }
}


//sh:fixme:currently we cannot use Tree_index as absl index...
void Lnast::update_phi_resolve_table(const Lnast_nid &psts_node, Lnast_node& target_data){
  const auto target_name = target_data.token.get_text(buffer);
  fmt::print("phi_resovle_table:{}\n", get_data(psts_node).token.get_text(buffer));
  auto &phi_resolve_table = phi_resolve_tables[get_data(psts_node).token.get_text(buffer)];
  phi_resolve_table[target_name] = target_data.subs;
}

bool Lnast::elder_sibling_is_label(const Lnast_nid &opr_node) {
  auto prev_lidx = get_sibling_prev(opr_node);
  if (prev_lidx.is_invalid())
    return false;

  return get_data(prev_lidx).type.is_label();
}


