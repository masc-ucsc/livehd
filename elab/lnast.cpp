#include "lnast.hpp"

void Lnast_node::dump() const {
  ;
  //fmt::print("type:{} loc:{}\n", type.debug_name(), loc); // TODO: cleaner API to also dump token
}

void Lnast::do_ssa_trans(const Lnast_nid &top){
  Lnast_nid top_sts_node = get_first_child(top);
  absl::flat_hash_map<std::string_view, uint8_t > top_sts_rename_table;
  rename_tables[get_data(top_sts_node).token.get_text(buffer)] = top_sts_rename_table;
  for (const auto &opr_node : children(top_sts_node)) {
    if (get_data(opr_node).type.is_if()) {
      ssa_if_subtree(opr_node);
    } else if (get_data(opr_node).type.is_func_def()) {
      do_ssa_trans(opr_node);
    } else {
      ssa_top_statements(top_sts_node, opr_node);
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
      absl::flat_hash_map<std::string_view, uint8_t> if_sts_rename_table;
      rename_tables[get_data(itr).token.get_text(buffer)] = if_sts_rename_table;
      for (const auto &opr_node : children(itr)) {
        I(!get_data(opr_node).type.is_func_def());
        if (get_data(opr_node).type.is_if())
          ssa_if_subtree(opr_node);
        else
          ssa_if_statements(itr, opr_node);
      }
    }
  }
  phi_node_insertion(if_node);
}

void Lnast::phi_node_insertion(const Lnast_nid &if_node) {
  ;
}


void Lnast::ssa_top_statements(const Lnast_nid &psts_node, const Lnast_nid &opr_node){
  const auto type = get_data(opr_node).type;
  if(type.is_pure_assign() || type.is_as()){
    const auto  target_node = get_first_child(opr_node);
          auto& target_data = *ref_data(target_node);
    const auto  target_name = target_data.token.get_text(buffer);

    if ((target_name.substr(0,3) == "___") || elder_sibling_is_label(opr_node))
      return;

    update_or_insert_rename_table(psts_node, target_data);
  }
}

void Lnast::ssa_if_statements(const Lnast_nid &psts_node, const Lnast_nid &opr_node){
  const auto type = get_data(opr_node).type;
  if(type.is_pure_assign() || type.is_as()){
    const auto  target_node = get_first_child(opr_node);
    auto& target_data = *ref_data(target_node);
    const auto  target_name = target_data.token.get_text(buffer);

    if ((target_name.substr(0,3) == "___") || elder_sibling_is_label(opr_node))
      return;

    update_or_insert_rename_table(psts_node, target_data);
  }
}
void Lnast::update_or_insert_rename_table(const Lnast_nid &psts_node, Lnast_node& target_data){
  const auto  target_name = target_data.token.get_text(buffer);
  fmt::print("table name :{}\n", get_data(psts_node).token.get_text(buffer));
  auto &rename_table = rename_tables[get_data(psts_node).token.get_text(buffer)];
  auto itr = rename_table.find(target_name);
  if (itr != rename_table.end()) {
    itr->second += 1;
    target_data.subs = itr->second;
  } else {
    rename_table[target_name] = 0;
  }
}

bool Lnast::elder_sibling_is_label(const Lnast_nid &opr_node) {
  auto prev_lidx = get_sibling_prev(opr_node);
  if (prev_lidx.is_invalid())
    return false;

  return get_data(prev_lidx).type.is_label();
}


