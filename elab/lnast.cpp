#include "lnast.hpp"

void Language_neutral_ast::ssa_trans() {
  do_ssa_trans(this->get_root());
}

void Language_neutral_ast::do_ssa_trans(const Tree_index& top){
  std::unique_ptr<Tree<std::vector<Token>>>   phi_tree;
  absl::flat_hash_map<std::string_view, u_int8_t > rename_table; //<token, #>
  for (const auto &itr: this->depth_preorder(top)) {
    const auto& node_data = this->get_data(itr);
    if (node_data.type == Lnast_ntype_func_def) {
      do_ssa_trans(itr);
    } else if (node_data.type == Lnast_ntype_if){
      ;
    } else {
      ;
    }
  }
}

