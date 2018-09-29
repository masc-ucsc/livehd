#include "aux_tree.hpp"

void Aux_node::set_alias(const std::string &v, Index_ID n) {
  auxtab[v] = n;
  fmt::print("set alias {} <-> {}\n", v,n );
}

void Aux_node::print_aux(){
  for(const auto &iter:auxtab)
    fmt::print("auxtab:{:>10} -> {}\n",iter.first, iter.second);
}


//Aux_tree member_functions
//std::vector<Aux_node *>  Aux_tree::pre_order_trans(Aux_node* node, std::vector<Aux_node*> &auxes_stack){
//  if (node == nullptr)
//    return auxes_stack;
//
//  auxes_stack.push_back(node);
//  pre_order_trans(node->lchild, auxes_stack);
//  pre_order_trans(node->rchild, auxes_stack);
//
//  return auxes_stack;
//}

Aux_node* Aux_tree::get_cur_auxnd() const{
  return auxes_stack.back();
}

//Aux_node * Aux_tree::get_cur_auxnd_const() const{
//  return auxes_stack.back();
//}

bool Aux_tree::is_root_aux(const Aux_node *auxnd) const{
  assert(auxnd != nullptr);
  return (auxnd->parent == nullptr);
}
Aux_node* Aux_tree::get_root(){
  return root_auxnd;
}

void Aux_tree::set_parent_child(Aux_node *parent, Aux_node *new_child, bool branch) {
  assert(parent!=nullptr && new_child!= nullptr);
  if(branch){
    assert(parent->lchild == nullptr);
    auxes_stack.push_back(new_child);
    parent->lchild     = new_child;
    new_child ->parent = parent;
    new_child ->lchild = nullptr;
    new_child ->rchild = nullptr;
  }
  else{
    assert(parent->lchild == nullptr);
    auxes_stack.push_back(new_child);
    parent->rchild     = new_child;
    new_child ->parent = parent;
    new_child ->lchild = nullptr;
    new_child ->rchild = nullptr;
  }
}

void Aux_tree::set_parent(Aux_node *parent, Aux_node *child){
  assert(parent!=nullptr && child!= nullptr);
  child -> parent = parent;
}

const Aux_node* Aux_tree::get_parent(const Aux_node *child) const{
  if(child->parent != nullptr)
    return child->parent;
  else
    return child;
}

void Aux_tree::delete_child(Aux_node *parent, Aux_node *child, bool branch){
  //assert(child->get_auxtab().empty()); //put back later when resolving phi
  if(branch){
    delete child;
    parent->lchild = nullptr;
    fmt::print("delete branch true auxtab\n");
  }
  else{
    delete child;
    parent->rchild = nullptr;
    fmt::print("delete branch false auxtab\n");
  }
}



//get_global_alias() only gets "chain of patent auxtabs" and don't get sibling auxtab
Index_ID Aux_tree::get_global_alias(const Aux_node *auxnd, const std::string &v) const{
  if(auxnd->get_auxtab().find(v) != auxnd->get_auxtab().end())
    return auxnd->get_auxtab().at(v);

  if(is_root_aux(auxnd))
    assert(false);//must has a alias in chain of parents since it has passed has_alias()

  return get_global_alias(get_parent(auxnd),v);
};

void Aux_tree::set_alias(const std::string &v, Index_ID n){
  Aux_node* cur_auxnd = get_cur_auxnd();
  cur_auxnd->set_alias(v,n);
}

bool Aux_tree::has_alias(const std::string &v) const {
  const Aux_node* cur_auxnd = get_cur_auxnd();
  //recursive check on parents
  return check_global_alias(cur_auxnd,v);
}

//check_global_alias() only checks "chain of patent auxtabs" and don't check sibling auxtab
bool Aux_tree::check_global_alias(const Aux_node *auxnd, const std::string &v) const{
  if (auxnd == nullptr)
    return false;

  if(auxnd->get_auxtab().find(v) != auxnd->get_auxtab().end())
    return true;

  return check_global_alias(auxnd->parent,v);
};

Index_ID Aux_tree::get_alias(const std::string &v) const {
  const Aux_node* cur_auxnd = get_cur_auxnd();
  //recursive check on parents
  return get_global_alias(cur_auxnd,v);
}

void Aux_tree::print_cur_aux() {
  const Aux_node* cur_auxnd = get_cur_auxnd();
  for(const auto &iter : get_cur_auxnd()->get_auxtab()){
    fmt::print("auxtab:{:>10} -> {}\n", iter.first, iter.second);
  }
}
