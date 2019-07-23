#include "lnast.hpp"

void Language_neutral_ast::ssa_trans() {
  do_ssa_trans(this->get_root());
}

void Language_neutral_ast::do_ssa_trans(const Tree_index& top){
  std::unique_ptr<Tree<std::vector<Token>>> phi_tree;
  absl::flat_hash_map<std::string_view, u_int8_t > rename_table; //<token string_view, #>

  const std::vector<Tree_index> top_sts = this->get_children( this->get_children(top)[0] );
  for(const auto &opr_node : top_sts){
    if(this->get_data(opr_node).type == Lnast_ntype_if){
      ssa_if_subtree(opr_node, rename_table, phi_tree);
    } else if (this->get_data(opr_node).type == Lnast_ntype_func_def){
      do_ssa_trans(opr_node);
    } else {
      ssa_normal_subtree(opr_node, rename_table, phi_tree);
    }
  }
}

void Language_neutral_ast::ssa_if_subtree(const Tree_index& opr_node, Rename_table& rename_table, Phi_tree& phi_tree){
  ;
}

void Language_neutral_ast::ssa_normal_subtree(const Tree_index& opr_node, Rename_table& rename_table, Phi_tree& phi_tree){
  const auto type = this->get_data(opr_node).type;
  if(type == Lnast_ntype_pure_assign || type == Lnast_ntype_as){
    auto target_data = this->get_data( this->get_children(opr_node)[0] ); //operator target is the eldest child
    const auto target_name = target_data.token.get_text(buffer);
    if (target_name.substr(0,3) == "___"){
      return;
    }

    auto itr = rename_table.find(target_name);
    if (itr != rename_table.end()) {
      itr->second += 1;
      target_data.subs = itr->second;
    } else {
      rename_table[target_name] = 0;
    }
    fmt::print("target name:{}{}\n", target_name, target_data.subs);
  }
}
