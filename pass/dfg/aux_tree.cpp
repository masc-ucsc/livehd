#include "aux_tree.hpp"

void Aux_node::set_alias(std::string_view v, Node_pin n) {
  auxtab[v] = n;
  fmt::print("set alias {} <-> node_pin:{}\n", v, n.get_name());
}

void Aux_node::set_pending(std::string_view v, Node_pin n) {
  pendtab[v] = n;
  //SH:FIXME: wait for pin.get_name() bug resolve
  //fmt::print("set pending {} <-> node_pin:{}\n", v, n.get_name());
}

Aux_node* Aux_tree::get_cur_auxnd() const {
  return auxes_stack.back();
}

bool Aux_tree::is_root_aux(const Aux_node *auxnd) const {
  assert(auxnd != nullptr);
  return (auxnd->parent == nullptr);
}
Aux_node *Aux_tree::get_root() {
  return root_auxnd;
}

void Aux_tree::set_parent_child(Aux_node *parent, Aux_node *new_child, bool branch) {
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

void Aux_tree::set_parent(Aux_node *parent, Aux_node *child) {
  assert(parent != nullptr && child != nullptr);
  child->parent = parent;
}

const Aux_node *Aux_tree::get_parent(const Aux_node *child) const {
  if(child->parent != nullptr)
    return child->parent;
  else
    return child;
}

void Aux_tree::disconnect_child(Aux_node *parent, Aux_node *child, bool branch) {
  // assert(child->get_auxtab().empty()); //put back later when resolving phi
  if(branch) {
    // delete child; //buggy to have a function delete a pointer that was passed to it!
    parent->lchild = nullptr;
    fmt::print("delete branch true auxtab\n");
  } else {
    // delete child;
    parent->rchild = nullptr;
    fmt::print("delete branch false auxtab\n");
  }
}

void Aux_tree::set_pending(std::string_view v, Node_pin n) {
  Aux_node *cur_auxnd = get_cur_auxnd();
  cur_auxnd->set_pending(v, n);
}

void Aux_tree::set_alias(std::string_view v, Node_pin n) {
  Aux_node *cur_auxnd = get_cur_auxnd();
  cur_auxnd->set_alias(v, n);
}

bool Aux_tree::has_alias(std::string_view v) const {
  const Aux_node *cur_auxnd = get_cur_auxnd();
  // recursive check on parents
  return check_global_alias(cur_auxnd, v);
}

// check_global_alias() only checks "chain of patent auxtabs" and don't check sibling auxtab
bool Aux_tree::check_global_alias(const Aux_node *auxnd, std::string_view v) const {
  if(auxnd == nullptr)
    return false;

  if(auxnd->get_auxtab().find(v) != auxnd->get_auxtab().end())
    return true;

  return check_global_alias(auxnd->parent, v);
};

Node_pin Aux_tree::get_alias(std::string_view v) const {
  const Aux_node *cur_auxnd = get_cur_auxnd();
  // recursive search through parents
  return get_global_alias(cur_auxnd, v);
}

// get_global_alias() only gets "chain of patent auxtabs" and don't get sibling auxtab
Node_pin Aux_tree::get_global_alias(const Aux_node *auxnd, std::string_view v) const {
  if(auxnd->get_auxtab().find(v) != auxnd->get_auxtab().end())
    return auxnd->get_auxtab().at(v);

  if(is_root_aux(auxnd))
    assert(false); // must has a alias in chain of parents since it has passed has_alias()

  return get_global_alias(get_parent(auxnd), v);
};

bool Aux_tree::has_pending(std::string_view v) const {
  const Aux_node *cur_auxnd = get_cur_auxnd()->parent;
  return check_global_pending(cur_auxnd, v); // recursive check on parents
}

// check_global_pending() only checks "chain of patent pendtabs" and don't check sibling pendtab
bool Aux_tree::check_global_pending(const Aux_node *auxnd, std::string_view v) const {
  if(auxnd == nullptr)
    return false;

  if(auxnd->get_pendtab().find(v) != auxnd->get_pendtab().end())
    return true;

  return check_global_pending(auxnd->parent, v);
};

Node_pin Aux_tree::get_pending(std::string_view v) const {
  const Aux_node *cur_auxnd = get_cur_auxnd();
  return get_global_pending(cur_auxnd, v); // recursive search through parents
}

// get_global_pending() only gets "chain of patent pendtabs" and don't get sibling pendtab
Node_pin Aux_tree::get_global_pending(const Aux_node *auxnd, std::string_view v) const {
  if(auxnd->get_pendtab().find(v) != auxnd->get_pendtab().end())
    return auxnd->get_pendtab().at(v);

  if(is_root_aux(auxnd))
    assert(false); // must has a pending in chain of parents since it has passed has_pending()

  return get_global_pending(get_parent(auxnd), v);
};

void Aux_tree::print_cur_auxnd() {
  for(const auto &iter : get_cur_auxnd()->get_auxtab()) {
    fmt::print("auxtab:{:>10} -> {}\n", iter.first, iter.second.get_compact());
  }
}
