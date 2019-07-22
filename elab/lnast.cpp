#include "lnast.hpp"

void Language_neutral_ast::ssa_trans() {
  do_ssa_trans(this->get_root());
}

void Language_neutral_ast::do_ssa_trans(const Tree_index& top){
  std::unique_ptr<Tree<std::vector<Token>>> phi_tree;
  absl::flat_hash_map<std::string_view, u_int8_t > rename_table; //<token string_view, #>
  for (const auto &node: this->depth_preorder(top)) {
    if(this->is_leaf(node))
      continue;

    const auto type = this->get_data(node).type;
    if (type == Lnast_ntype_func_def) {
      ;//do_ssa_trans(node);
    } else if (type == Lnast_ntype_if){
      ;
    } else if (type == Lnast_ntype_pure_assign || type == Lnast_ntype_as){
      auto target_data = this->get_data( this->get_children(node)[0] ); //operator target is the eldest child
      const auto target_name = target_data.token.get_text(buffer);

      if(target_name.substr(0,3) == "___")
        continue;

      auto it = rename_table.find(target_name);
      if (it != rename_table.end()) {
        it->second += 1;
        target_data.subs = it->second;
      } else {
        rename_table[target_name] = 0;
      }
      fmt::print("target name:{}, subs:{}\n", target_name, target_data.subs);
    }
  }
}

