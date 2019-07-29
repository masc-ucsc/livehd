#include "lnast.hpp"

void Lnast::ssa_trans() {
  do_ssa_trans(this->get_root());
}

void Lnast::do_ssa_trans(const Lnast_index &top){

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

  //for (const auto &itr: phi_tree.depth_preorder()) {
  //  const auto& phi_table = phi_tree.get_data(itr);
  //  fmt::print("\nphi_table[{}][{}] content\n\n", itr.level, itr.pos);
  //  for (auto const& [key, val] : phi_table){
  //    auto var_subscript = this->get_data(val).subs;
  //    fmt::print("var:{:<12}, subs:{}\n", key, var_subscript);
  //  }
  //}

  //fmt::print("\nrename_table content\n\n");
  //for (auto const& [key, val] : rename_table){
  //  fmt::print("var:{:<12}, subs:{}\n", key, val);
  //}
}

void Lnast::ssa_if_subtree(const Lnast_index &if_node, Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx){

  auto lnast_if_children = this->get_children(if_node);

  for (const auto &itr : lnast_if_children){
    I(this->get_parent(itr) == if_node);
    auto type = this->get_data(itr).type;//this ptr = lnast
    if(type == Lnast_ntype_statements){
      auto phi_sts_idx = phi_tree.add_child(phi_psts_idx, Phi_tree_table());
      for(const auto &opr_node : this->get_children(itr)){
        I(this->get_data(opr_node).type != Lnast_ntype_func_def);
        if(this->get_data(opr_node).type == Lnast_ntype_if)
          ssa_if_subtree(opr_node, rename_table, phi_tree, phi_sts_idx);
        else
          ssa_normal_subtree(opr_node, rename_table, phi_tree, phi_sts_idx);
      }
    }
  }

  phi_node_insertion(if_node, rename_table, phi_tree, phi_psts_idx);
}


void Lnast::phi_node_insertion(const Lnast_index &if_node, Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx){

  bool has_the_else_block = check_else_block_existence(if_node);
  const auto phi_psts_children_idxes = phi_tree.get_children(phi_psts_idx);

  for(int i = phi_psts_children_idxes.size()-1; i < 1; i--){
    if(i == phi_psts_children_idxes.size()-1 && !has_the_else_block){
      auto& phi_true_table = phi_tree.get_data(phi_psts_children_idxes[i-1]);
      for (auto const& [key, val] : phi_true_table){
        Lnast_index lnast_true_idx  = val;
        Lnast_index lnast_false_idx = get_complement_lnast_idx_from_parent(key, phi_tree, phi_psts_idx);
        Lnast_index lnast_cond_idx  = get_elder_sibling( this->get_parent(lnast_true_idx));

        auto& lnast_tidx_data = this->get_data(lnast_true_idx);  //tidx = true_idx
        auto& lnast_fidx_data = this->get_data(lnast_false_idx); //fidx = false_idx
        auto& lnast_cidx_data = this->get_data(lnast_cond_idx);
        I(this->get_data(lnast_cond_idx).type == Lnast_ntype_cond);

        Lnast_index phi_node = this->add_younger_sibling(if_node, Lnast_node(Lnast_ntype_phi, Token()));
        auto target_token = this->get_data(lnast_true_idx).token;

        auto itr = rename_table.find(target_token.get_text(buffer));
        if (itr != rename_table.end())
          itr->second += 1;
        auto target_subs = rename_table[target_token.get_text(buffer)];

        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  target_token, target_subs));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_cond, lnast_cidx_data.token));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  lnast_tidx_data.token, lnast_tidx_data.subs));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  lnast_fidx_data.token, lnast_fidx_data.subs));
      }
    } else {
      auto& phi_false_table = phi_tree.get_data(phi_psts_children_idxes[i]);
      auto& phi_true_table  = phi_tree.get_data(phi_psts_children_idxes[i-1]);

      for (auto const& [key, val] : phi_true_table){
        Lnast_index lnast_true_idx  = val;
        Lnast_index lnast_false_idx = get_complement_lnast_idx(key, phi_false_table, phi_tree, phi_psts_idx);
        Lnast_index lnast_cond_idx  = get_elder_sibling( this->get_parent(lnast_true_idx));



        auto& lnast_tidx_data = this->get_data(lnast_true_idx);  //tidx = true_idx
        auto& lnast_fidx_data = this->get_data(lnast_false_idx); //fidx = false_idx
        auto& lnast_cidx_data = this->get_data(lnast_cond_idx);
        I(this->get_data(lnast_cond_idx).type == Lnast_ntype_cond);

        Lnast_index phi_node = this->add_younger_sibling(if_node, Lnast_node(Lnast_ntype_phi, Token()));
        auto target_token = this->get_data(lnast_true_idx).token;

        auto itr = rename_table.find(target_token.get_text(buffer));
        if (itr != rename_table.end())
          itr->second += 1;
        auto target_subs = rename_table[target_token.get_text(buffer)];

        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  target_token, target_subs));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_cond, lnast_cidx_data.token));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  lnast_tidx_data.token, lnast_tidx_data.subs));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  lnast_fidx_data.token, lnast_fidx_data.subs));
      }

      for (auto const& [key, val] : phi_false_table){
        Lnast_index lnast_false_idx = val;
        Lnast_index lnast_true_idx  = get_complement_lnast_idx(key, phi_true_table, phi_tree, phi_psts_idx);
        Lnast_index lnast_cond_idx  = get_elder_sibling( this->get_parent(lnast_true_idx));


        auto& lnast_tidx_data = this->get_data(lnast_true_idx);  //tidx = true_idx
        auto& lnast_fidx_data = this->get_data(lnast_false_idx); //fidx = false_idx
        auto& lnast_cidx_data = this->get_data(lnast_cond_idx);
        I(this->get_data(lnast_cond_idx).type == Lnast_ntype_cond);

        Lnast_index phi_node = this->add_younger_sibling(if_node, Lnast_node(Lnast_ntype_phi, Token()));
        auto target_token = this->get_data(lnast_true_idx).token;

        auto itr = rename_table.find(target_token.get_text(buffer));
        if (itr != rename_table.end())
          itr->second += 1;
        auto target_subs = rename_table[target_token.get_text(buffer)];

        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  target_token, target_subs));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_cond, lnast_cidx_data.token));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  lnast_tidx_data.token, lnast_tidx_data.subs));
        this->add_child(phi_node, Lnast_node(Lnast_ntype_ref,  lnast_fidx_data.token, lnast_fidx_data.subs));
      }

    }
  }
}

Lnast_index Lnast::get_complement_lnast_idx (std::string_view lnast_var, const Phi_tree_table &phi_complement_table, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx) {
  //search direction: sibling table -> parent -> grand-parent -> ...
  for(auto const & [key, val] : phi_complement_table){
    if(lnast_var == key)
      return val;
  }
  return get_complement_lnast_idx_from_parent(lnast_var, phi_tree, phi_psts_idx);
}

Lnast_index Lnast::get_complement_lnast_idx_from_parent (std::string_view lnast_var, Phi_tree &phi_tree, const Phi_tree_index &phi_psts_idx) {
  auto &phi_psts_table = phi_tree.get_data(phi_psts_idx);
  for(auto const & [key, val] : phi_psts_table){
    if(lnast_var == key){
      return val;
    }
  }

  if(phi_tree.get_root() == phi_psts_idx)
    I(false, "variable is not defined in upper scopes");

  return get_complement_lnast_idx_from_parent(lnast_var, phi_tree, phi_tree.get_parent(phi_psts_idx));
}



bool Lnast::check_else_block_existence(const Lnast_index &if_node){
  const auto lnast_if_children = this->get_children(if_node);
  const auto last_child = lnast_if_children.back();
  const auto second_last_child = lnast_if_children.end()[-2];
  I(this->get_data(last_child).type == Lnast_ntype_statements);
  return this->get_data(last_child).type == this->get_data(second_last_child).type;
}

void Lnast::ssa_normal_subtree(const Lnast_index &opr_node, Rename_table &rename_table, Phi_tree &phi_tree, const Phi_tree_index &phi_sts_idx){
  auto& phi_table = phi_tree.get_data(phi_sts_idx);

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

bool Lnast::elder_sibling_is_label(const Lnast_index &opr_node) {
  const auto all_siblings = this->get_children(this->get_parent(opr_node));
  if(all_siblings.at(0) == opr_node) //itself is the eldest child
    return false;

  const auto elder_sibling = *std::prev( std::find(all_siblings.begin(), all_siblings.end(), opr_node));
  return this->get_data(elder_sibling).type == Lnast_ntype_label;
}

bool Lnast::elder_sibling_is_cond(const Lnast_index &sts_node) {
  I(this->get_data(sts_node).type == Lnast_ntype_statements);
  const auto all_siblings = this->get_children(this->get_parent(sts_node));
  if(all_siblings.at(0) == sts_node) //itself is the eldest child
    return false;

  const auto elder_sibling = *std::prev( std::find(all_siblings.begin(), all_siblings.end(), sts_node));
  return this->get_data(elder_sibling).type == Lnast_ntype_cond;
}

Lnast_index  Lnast::get_elder_sibling(const Lnast_index &self){
  const auto all_siblings = this->get_children(this->get_parent(self));
  return *std::prev( std::find(all_siblings.begin(), all_siblings.end(), self));
}
