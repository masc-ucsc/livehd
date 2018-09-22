#include "aux_tree.hpp"

void Aux_node::set_alias(const std::string &v, Index_ID n) {
  auxtab[v] = n;
  fmt::print("set alias {} <-> {}\n", v,n );
}

void Aux_node::print_aux(){
  for(const auto &iter:auxtab)
    fmt::print("auxtab:{:>10} -> {}\n",iter.first, iter.second);
}

void Aux_tree::set_child(Aux_node *parent, Aux_node *child, bool branch) {
  if(branch){
    assert(parent->lchild == nullptr);
    parent->lchild = child;
    child ->lchild = nullptr;
    child ->rchild = nullptr;
  }
  else{
    assert(parent->lchild == nullptr);
    parent->rchild = child;
    child ->lchild = nullptr;
    child ->rchild = nullptr;
  }
}

void Aux_tree::set_parent(Aux_node *parent, Aux_node *child){
  assert(parent!=nullptr && child!= nullptr);
  child -> parent = parent;
}

Aux_node* Aux_tree:: get_parent(Aux_node *child){
  assert(child->parent != nullptr);
  return child->parent;
}

void Aux_tree::delete_child(Aux_node *parent, bool branch){
  if(branch){
    assert(parent->lchild->get_auxtab().empty());
    delete parent->lchild;
    parent->lchild = nullptr;
  }
  else{
    assert(!parent->rchild->get_auxtab().empty());
    delete parent->rchild;
    parent->rchild = nullptr;
  }
}
