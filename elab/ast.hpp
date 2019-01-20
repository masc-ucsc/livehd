//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdint.h>
#include <vector>
#include <functional>

#include "iassert.hpp"

using Ast_level = uint16_t;
using Ast_pos   = uint16_t;

class Ast_index {
  const Ast_level level;
  const Ast_pos   pos;

public:
  Ast_index() = delete;
  Ast_index(Ast_level l, Ast_pos i) : level(l), pos(i) {}

  inline Ast_level get_level() const { return level; }
  inline Ast_pos   get_pos() const { return pos; }

  bool operator==(const Ast_index &i) const {
    return level == i.level && pos == i.pos;
  }
};

template<typename X>
class Ast {

  std::vector<std::vector<X>>         data_stack;
  std::vector<std::vector<Ast_index>> parent_stack;
  int                                 pending_parent;

  void adjust_to_level(Ast_level level);
public:

  Ast();

  // WARNING: can not return Ast_index & because future additions can move the pointer (vector realloc)
  const Ast_index add_child(const Ast_index &parent, const X data);
  const Ast_index add_sibling(const Ast_index &sibling, const X data);

        X &get_data(const Ast_index &leaf);
  const X &get_data(const Ast_index &leaf) const;

  const Ast_index &get_parent(const Ast_index &child) const;
  const Ast_index &get_root() const;
  void  set_root(const X data);

  void add_lazy_child(const Ast_level &child_level, const X data);

  //const Ast_index add_child(Ast_index parent, const Tree &t);
  //const Ast_index add_sibling(Ast_index brother, const Tree &t);
  //const Ast_index add_parent(Ast_index brother, X data);

  // NOTE: not a typical depth first traversal, goes to bottom without touching
  // parents first. It is also not a bottom-up traversal because it touches all
  // the tree level before going to next level
  void each_bottom_first_fast(std::function<void(const Ast_index &parent, const Ast_index &self, const X &)> fn) const;

  void each_breadth_first_fast(std::function<void(const Ast_index &parent, const Ast_index &self, const X &)> fn) const;

  // void each_bottom_up(std::function<void(const Ast_index &parent, const Ast_index &self, const X &)> fn) const;
  // void each_depth_first(const Ast_index &start_index, std::function<void(const Ast_index &parent, const Ast_index &self, const X &)> fn) const 
};

//--------------------- Template Implementation ----

template<typename X>
void Ast<X>::adjust_to_level(Ast_level level) {
  if (data_stack.size()>level)
    return;

  while(data_stack.size()<=level) {
    data_stack.emplace_back();
    parent_stack.emplace_back();
  }
};

template<typename X>
Ast<X>::Ast() {
  pending_parent = -1; // Nobody pending
};

template<typename X>
const Ast_index Ast<X>::add_child(const Ast_index &parent, const X data) {
  const auto parent_level = parent.get_level();
  const auto parent_pos = parent.get_pos();

  I(data_stack.size()>parent_level);
  I(data_stack[parent_level].size()>parent_pos);
  I(data_stack.size() == parent_stack.size());

  auto child_level = parent_level + 1;

  adjust_to_level(child_level);

  auto child_pos = data_stack[child_level].size();

  I(pending_parent != child_level);
  data_stack[child_level].emplace_back(data);
  parent_stack[child_level].emplace_back(Ast_index(parent_level, parent_pos));

  return Ast_index(child_level, child_pos);
};

template<typename X>
const Ast_index Ast<X>::add_sibling(const Ast_index &sibling, const X data) {
  const auto sibling_level = sibling.get_level();
  const auto sibling_pos = sibling.get_pos();

  I(sibling_level>0); // No siblings to root

  I(data_stack.size()>sibling_level);
  I(data_stack[sibling_level].size()>sibling_pos);

  auto child_level = sibling_level;
  auto child_pos = data_stack[child_level].size();

  I(pending_parent != child_level);
  data_stack[child_level].emplace_back(data);
  parent_stack[child_level].emplace_back(parent_stack[sibling_level][sibling_pos]);

  return Ast_index(child_level, child_pos);
}

template<typename X>
X &Ast<X>::get_data(const Ast_index &leaf) {
  I(data_stack.size() > leaf.get_level());
  I(data_stack[leaf.get_level()].size() > leaf.get_pos());

  return data_stack[leaf.get_level()][leaf.get_pos()];
}

template<typename X>
const X &Ast<X>::get_data(const Ast_index &leaf) const {
  I(data_stack.size() > leaf.get_level());
  I(data_stack[leaf.get_level()].size() > leaf.get_pos());

  return data_stack[leaf.get_level()][leaf.get_pos()];
}

template<typename X>
void Ast<X>::add_lazy_child(const Ast_level &child_level, const X data) {
  // -If child_level==1, add to root, do not mark as pending

  I(child_level!=0);

  adjust_to_level(child_level);

  Ast_level parent_level = child_level-1;
  Ast_pos   parent_pos = parent_stack[parent_level].size();
  if (parent_level==0) {
    parent_pos = parent_pos-1; // No lazy root
  }else{
    I(pending_parent <= child_level);
    pending_parent = parent_level;
  }

  data_stack[child_level].emplace_back(data);
  parent_stack[child_level].emplace_back(Ast_index(parent_level, parent_pos));
}

template<typename X>
const Ast_index &Ast<X>::get_parent(const Ast_index &child) const {
  I(child.get_level()<parent_stack.size());
  I(child.get_pos()<parent_stack[child.get_level()].size());

  return parent_stack[child.get_level()][child.get_pos()];
}

template<typename X>
const Ast_index &Ast<X>::get_root() const {
  return parent_stack[0][0];
}

template<typename X>
void Ast<X>::set_root(const X data) {
  adjust_to_level(0);

  if (!data_stack[0].empty()) {
    data_stack[0].clear();
    parent_stack[0].clear();
  }

  data_stack[0].emplace_back(data);
  parent_stack[0].emplace_back(0,0);
}

template<typename X>
void Ast<X>::each_bottom_first_fast(std::function<void(const Ast_index &parent, const Ast_index &self, const X &)> fn) const {
  auto sz=data_stack.size();
  for(int i=sz-1;i>=0;--i) { // WARNING: must be signed to handle -1 for loop exit
    auto sz2=data_stack[i].size();
    for(size_t j=0;j<sz2;j++) {
      fn(parent_stack[i][j], Ast_index(i,j), data_stack[i][j]);
    }
  }
}

template<typename X>
void Ast<X>::each_breadth_first_fast(std::function<void(const Ast_index &parent, const Ast_index &self, const X &)> fn) const {
  auto sz=data_stack.size();
  for(size_t i=0;i<sz;i++) {
    auto sz2=data_stack[i].size();
    for(size_t j=0;j<sz2;j++) {
      fn(parent_stack[i][j], Ast_index(i,j), data_stack[i][j]);
    }
  }
}

