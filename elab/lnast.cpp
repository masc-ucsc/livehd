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
  phi_node_resolve(if_node);
}

void Lnast::phi_node_resolve(const Lnast_nid &if_node) {
  std::vector<Lnast_nid> if_statements_vec;
  for (const auto & itr : children(if_node)) {
    if (get_data(itr).type.is_statements()) {
      if_statements_vec.push_back(itr);
    }
  }

  for (auto itr = if_statements_vec.rbegin(); itr != if_statements_vec.rend(); ++itr ) {
    //2 possible cases: (1)if-elif-elif (2) if-elif-else
    if (itr == if_statements_vec.rbegin()) {
      if (has_else_statements(if_node)) {
        //handle last two statements first

      } else {
        ;//handle last and upper-scope statements first
      }
    } else {
      ;//resolve normal case
    }
  }
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


