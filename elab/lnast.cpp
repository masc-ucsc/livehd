#include "lnast.hpp"

void Lnast_node::dump() const {
  fmt::print("type:{} loc:{}\n", Lnast_type_name[type], loc); // TODO: cleaner API to also dump token
}

void Lnast::ssa_trans() {
  do_ssa_trans(this->get_root());
}

void Lnast::do_ssa_trans(const Lnast_index &top){

  Rename_table rename_table; //global table except func_def subtree
  //Phi_sts_table  phi_sts_table;
  //Phi_sts_tables phi_sts_tables;

  for(const auto &opr_node : children(get_first_child(top))) {
    if(this->get_data(opr_node).type == Lnast_ntype_if) {
      ssa_if_subtree(opr_node, rename_table);
    } else if (this->get_data(opr_node).type == Lnast_ntype_func_def) {
      do_ssa_trans(opr_node);
    } else {
      ssa_normal_subtree(opr_node, rename_table);
    }
  }

  //fmt::print("\nrename_table content\n\n");
  //for (auto const& [key, val] : rename_table){
    //fmt::print("var:{:<12}, subs:{}\n", key, val);
  //}
}

void Lnast::ssa_if_subtree(const Lnast_index &if_node, Rename_table &rename_table){

  for (const auto &itr : children(if_node)) {
    I(this->get_parent(itr) == if_node);
    auto type = this->get_data(itr).type;//this ptr = lnast
    if(type == Lnast_ntype_statements) {

      for(const auto &opr_node : children(itr)) {
        I(this->get_data(opr_node).type != Lnast_ntype_func_def);
        if(this->get_data(opr_node).type == Lnast_ntype_if)
          ssa_if_subtree(opr_node, rename_table);
        else
          ssa_normal_subtree(opr_node, rename_table);
      }
    }
  }

  phi_node_insertion(if_node, rename_table);
}


void Lnast::phi_node_insertion(const Lnast_index &if_node, Rename_table &rename_table){
  (void)rename_table;
  check_else_block_existence(if_node);
}



bool Lnast::check_else_block_existence(const Lnast_index &if_node){
  (void)if_node;
  // I(false);
#if 0
  // VERY inneficient to iterate just to get the last
  const auto lnast_if_children = children(if_node);
  const auto last_child = lnast_if_children.back();
  const auto second_last_child = lnast_if_children.end()[-2];
  I(this->get_data(last_child).type == Lnast_ntype_statements);
  return this->get_data(last_child).type == this->get_data(second_last_child).type;
#endif
  return false;
}

void Lnast::ssa_normal_subtree(const Lnast_index &opr_node, Rename_table &rename_table){

  const auto type = this->get_data(opr_node).type;
  if(type == Lnast_ntype_pure_assign || type == Lnast_ntype_as){
    auto  target_idx  = get_first_child(opr_node);
    auto& target_data = *ref_data(target_idx);
    auto  target_name = target_data.token.get_text(buffer);

    if (target_name.substr(0,3) == "___")
      return;

    if(elder_sibling_is_label(opr_node))
      return;

    update_or_insert_rename_table(target_name, target_data, rename_table);

    //phi_table.second.insert_or_assign(target_name, target_idx); //always keep up with the latest index update on variable
  }
}


void Lnast::update_rename_table(std::string_view target_name, Rename_table &rename_table){
  auto itr = rename_table.find(target_name);
  if (itr != rename_table.end())
    itr->second += 1;
  else
    I(false, "variable undefined before");
}


void Lnast::update_or_insert_rename_table(std::string_view target_name, Lnast_node &target_data, Rename_table &rename_table){
  auto itr = rename_table.find(target_name);
  if (itr != rename_table.end()) {
    itr->second += 1;
    target_data.subs = itr->second;
  } else {
    rename_table[target_name] = 0;
  }
}

bool Lnast::elder_sibling_is_label(const Lnast_index &opr_node) {
  auto prev_lidx = get_sibling_prev(opr_node);
  if (prev_lidx.is_invalid()) return false;

  return get_data(prev_lidx).type == Lnast_ntype_label;
}

bool Lnast::elder_sibling_is_cond(const Lnast_index &sts_node) {
  auto prev_lidx = get_sibling_prev(sts_node);
  if (prev_lidx.is_invalid()) return false;

  return get_data(prev_lidx).type == Lnast_ntype_cond;
}

Lnast_index  Lnast::get_elder_sibling(const Lnast_index &self){
  return get_sibling_prev(self);
}
