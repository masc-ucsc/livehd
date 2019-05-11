#include "aux_tree.hpp"

void Aux::set_alias(std::string_view v, Node_pin n) {
  auxtab[v] = n;
  fmt::print("set alias {} <-> node_pin:{}\n", v, n.get_name());
}

void Aux::set_pending(std::string_view v, Node_pin n) {
  pendtab[v] = n;
  fmt::print("set pending {} <-> node_pin:{}\n", v, n.get_name());
}

Aux* Aux_tree::get_cur_aux() const {
  return auxes_stack.back();
}

bool Aux_tree::is_root_aux(const Aux *aux) const {
  assert(aux != nullptr);
  return (aux->parent == nullptr);
}
Aux *Aux_tree::get_root() {
  return root_aux;
}

void Aux_tree::set_parent_child(Aux *parent, Aux *new_child, bool branch){
  I(parent != nullptr && new_child != nullptr);
  if(branch) {
    I(parent->lchild == nullptr);
    auxes_stack.push_back(new_child);
    parent->lchild    = new_child;
    new_child->parent = parent;
    new_child->lchild = nullptr;
    new_child->rchild = nullptr;
  } else {
    I(parent->rchild == nullptr);
    auxes_stack.push_back(new_child);
    parent->rchild    = new_child;
    new_child->parent = parent;
    new_child->lchild = nullptr;
    new_child->rchild = nullptr;
  }
}

void Aux_tree::set_parent(Aux *parent, Aux *child) {
  assert(parent != nullptr && child != nullptr);
  child->parent = parent;
}

const Aux *Aux_tree::get_parent(const Aux *child) const {
  if(child->parent != nullptr)
    return child->parent;
  else
    return child;
}

void Aux_tree::disconnect_child(Aux *parent, Aux *child, bool branch) {
  I(child->get_pendtab().empty());
  if(branch) {
    // delete child; //buggy to have a function delete a pointer that was passed to it!
    parent->lchild = nullptr;
    fmt::print("delete branch true auxtab\n");
    I(parent->lchild == nullptr);
  } else {
    // delete child;
    parent->rchild = nullptr;
    fmt::print("delete branch false auxtab\n");
    I(parent->rchild == nullptr);
  }
}

void Aux_tree::set_pending(std::string_view v, Node_pin n) {
  get_cur_aux()->set_pending(v, n);
}

void Aux_tree::set_alias(std::string_view v, Node_pin n) {
  get_cur_aux()->set_alias(v, n);
}

bool Aux_tree::has_alias(std::string_view v) const {
  const Aux *cur_aux = get_cur_aux();
  // recursive check on parents
  return check_global_alias(cur_aux, v);
}

// check_global_alias() only checks "chain of patent auxtabs" and don't check sibling auxtab
bool Aux_tree::check_global_alias(const Aux *aux, std::string_view v) const {
  if(aux == nullptr)
    return false;

  if(aux->get_auxtab().find(v) != aux->get_auxtab().end())
    return true;

  return check_global_alias(aux->parent, v);
};

Node_pin Aux_tree::get_alias(std::string_view v) const {
  I(this->has_alias(v));
  const Aux *cur_aux = get_cur_aux();
  // recursive search through parents
  return get_global_alias(cur_aux, v);
}


// get_global_alias() only gets "chain of patent auxtabs" and don't get sibling auxtab
Node_pin Aux_tree::get_global_alias(const Aux *aux, std::string_view v) const {
  if(aux->get_auxtab().find(v) != aux->get_auxtab().end())
    return aux->get_auxtab().at(v);

  if(is_root_aux(aux))
    assert(false); // must has a alias in chain of parents since it has passed has_alias()

  return get_global_alias(get_parent(aux), v);
};


bool Aux_tree::has_pending(std::string_view v) const {
  const Aux *cur_aux = get_cur_aux()->parent;
  return check_global_pending(cur_aux, v); // recursive check on parents
}

// check_global_pending() only checks "chain of patent pendtabs" and don't check sibling pendtab
bool Aux_tree::check_global_pending(const Aux *aux, std::string_view v) const {
  if(aux == nullptr)
    return false;

  if(aux->get_pendtab().find(v) != aux->get_pendtab().end())
    return true;

  return check_global_pending(aux->parent, v);
};

Node_pin Aux_tree::get_pending(std::string_view v) const {
  const Aux *cur_aux = get_cur_aux();
  return get_global_pending(cur_aux, v); // recursive search through parents
}

// get_global_pending() only gets "chain of patent pendtabs" and don't get sibling pendtab
Node_pin Aux_tree::get_global_pending(const Aux *aux, std::string_view v) const {
  if(aux->get_pendtab().find(v) != aux->get_pendtab().end())
    return aux->get_pendtab().at(v);

  if(is_root_aux(aux))
    assert(false); // must has a pending in chain of parents since it has passed has_pending()

  return get_global_pending(get_parent(aux), v);
};

void Aux_tree::print_cur_aux() {
  for(const auto &iter : get_cur_aux()->get_auxtab()) {
    fmt::print("auxtab:{:>10} -> {}\n", iter.first, iter.second.get_name());
  }
}

//SH:FIXME: only update local alias seems reasonalbe
//SH:FIXME: but for graph io, it's legal be created in if-else branch
//replace all keys which point to old Node_pin with the new Node_pin
void Aux_tree::update_alias(std::string_view target, Node_pin new_pin) {
  Aux *cur_aux = get_cur_aux();
  I(cur_aux->has_alias(target));

  Node_pin old_pin = cur_aux->get_alias(target);
  for(const auto& iter : cur_aux->get_auxtab()){
    if(iter.second == old_pin){
      cur_aux->set_alias(iter.first, new_pin);
    }
  }
}

