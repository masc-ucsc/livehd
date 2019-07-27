#include "lnast.hpp"

void Language_neutral_ast::ssa_trans() {
  do_ssa_trans(this->get_root());
}

void Language_neutral_ast::do_ssa_trans(const Lnast_index &top){

  Rename_table rename_table; //global table except func_def subtree
  Phi_tree     phi_tree;
  phi_tree.set_root(Phi_tree_table());
  auto  phi_top_sts_idx = phi_tree.get_root();

  const std::vector<Lnast_index > top_sts = this->get_children( this->get_children(top)[0] );
  for(const auto &opr_node : top_sts){
    if(this->get_data(opr_node).type == Lnast_ntype_if){
      ssa_if_subtree(opr_node, rename_table, phi_tree, phi_top_sts_idx);
    } else if (this->get_data(opr_node).type == Lnast_ntype_func_def){
      do_ssa_trans(opr_node);
    } else {
      ssa_normal_subtree(opr_node, rename_table, phi_tree, phi_top_sts_idx);
    }
  }

  auto phi_table = phi_tree.get_data(phi_top_sts_idx);
  fmt::print("\ntop-sts phi_table content\n\n");
  for (auto const& [key, val] : phi_table){
    auto var_subscript = this->get_data(val).subs;
    fmt::print("var:{:<12}, subs:{}\n", key, var_subscript);
  }

  fmt::print("\nrename_table content\n\n");
  for (auto const& [key, val] : rename_table){
    fmt::print("var:{:<12}, subs:{}\n", key, val);
  }
}

void Language_neutral_ast::ssa_if_subtree(const Lnast_index &opr_if, Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_parent){
  auto lnast_if_children = this->get_children(opr_if);
  bool has_else_block = false;
  for (const auto &itr : lnast_if_children){
    I(this->get_parent(itr) == opr_if);
    auto type = this->get_data(itr).type;//this ptr = lnast
    fmt::print("if child, type:{}, level:{}, pos:{}\n", type, itr.level, itr.pos);
    if(type == Lnast_ntype_statements){
      auto phi_sts_idx = phi_tree.add_child(phi_parent, Phi_tree_table());
      for(const auto &opr_node : this->get_children(itr)){
        I(this->get_data(opr_node).type != Lnast_ntype_func_def);
        if(this->get_data(opr_node).type == Lnast_ntype_if)
          ssa_if_subtree(opr_node, rename_table, phi_tree, phi_sts_idx);
        else
          ssa_normal_subtree(opr_node, rename_table, phi_tree, phi_sts_idx);

      }
    }
  }
}

void Language_neutral_ast::ssa_normal_subtree(const Lnast_index &opr_node, Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_sts){
  auto& phi_table = phi_tree.get_data(phi_sts);

  const auto type = this->get_data(opr_node).type;
  if(type == Lnast_ntype_pure_assign || type == Lnast_ntype_as){
    auto  target_idx  = this->get_children(opr_node)[0]; //operator target is the eldest child
    auto& target_data = this->get_data(target_idx);
    auto  target_name = target_data.token.get_text(buffer);

    if (target_name.substr(0,3) == "___")
      return;

    if(elder_sibling_is_label(opr_node))
      return;

    auto itr = rename_table.find(target_name);
    if (itr != rename_table.end()) {
      itr->second += 1;
      target_data.subs = itr->second;
    } else {
      rename_table[target_name] = 0;
    }

    //phi_table[target_name] = target_idx; //operator [] of map won't work as it need default constructor of Tree_index
    phi_table.insert_or_assign(target_name, target_idx); //always keep up with the latest index update on variable

  }
}

bool Language_neutral_ast::elder_sibling_is_label(const Lnast_index &opr_node) {
  const auto all_siblings = this->get_children(this->get_parent(opr_node));
  if(all_siblings.at(0) == opr_node) //itself is the eldest child
    return false;

  const auto elder_sibling = *std::prev( std::find(all_siblings.begin(), all_siblings.end(), opr_node));
  return this->get_data(elder_sibling).type == Lnast_ntype_label;
}

bool Language_neutral_ast::elder_sibling_is_cond(const Lnast_index &sts_node) {
  I(this->get_data(sts_node).type == Lnast_ntype_statements);
  const auto all_siblings = this->get_children(this->get_parent(sts_node));
  if(all_siblings.at(0) == sts_node) //itself is the eldest child
    return false;

  const auto elder_sibling = *std::prev( std::find(all_siblings.begin(), all_siblings.end(), sts_node));
  return this->get_data(elder_sibling).type == Lnast_ntype_cond;
}

