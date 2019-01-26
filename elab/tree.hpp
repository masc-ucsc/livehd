//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include "explicit_type.hpp"
#include "iassert.hpp"

using Tree_level = Explicit_type<int32_t, struct Tree_level_struct>;  // Must be signed
using Tree_pos   = Explicit_type<int32_t, struct Tree_pos_struct>;    // Must be unsigned, <0 not connected

class Tree_index {
public:
  Tree_level level;
  Tree_pos   pos;

  Tree_index() = delete;
  Tree_index(Tree_level l, Tree_pos i) : level(l), pos(i) {}

  bool operator==(const Tree_index &i) const { return level == i.level && pos == i.pos; }
  bool operator!=(const Tree_index &i) const { return level != i.level || pos != i.pos; }
};


template <typename X>
class Tree {
  struct Tree_pointers {
    Tree_pointers(Tree_pos p) : parent(p), eldest_child(-1), younger_child(-1), younger_sibling(-1) {  }

    const Tree_pos  parent; // Parent can never change
    Tree_pos        eldest_child;
    Tree_pos        younger_child;
    Tree_pos        younger_sibling;
  };
  std::vector<std::vector<X>>             data_stack;
  std::vector<std::vector<Tree_pointers>> pointers_stack;
  int                                     pending_parent;  // Must be signed

  void adjust_to_level(Tree_level level);

public:
class Tree_depth_preorder_iterator {
public:
  class CTree_depth_preorder_iterator {
  public:
    CTree_depth_preorder_iterator(const Tree_index &_ti, const Tree<X> *_t) : ti(_ti), t(_t) {}
    CTree_depth_preorder_iterator operator++() {
      CTree_depth_preorder_iterator i(ti, t);

      ti = t->get_depth_preorder_next(ti);

      return i;
    };
    bool operator!=(const CTree_depth_preorder_iterator &other) {
      assert(t == other.t);
      return ti != other.ti;
    }
    const Tree_index &operator*() const { return ti; }

  private:
    Tree_index     ti;
    const Tree<X> *t;
  };

private:
protected:
  Tree_index                     ti;
  const Tree<X> *t;

public:
  Tree_depth_preorder_iterator() = delete;
  explicit Tree_depth_preorder_iterator(const Tree_index &_b, const Tree<X> *_t) : ti(_b), t(_t) {}

  CTree_depth_preorder_iterator begin() const { return CTree_depth_preorder_iterator(ti, t); }
  CTree_depth_preorder_iterator end() const { return CTree_depth_preorder_iterator(t->get_depth_preorder_next(ti), t); }  // 0 is end index for iterator
};
  Tree();

  // WARNING: can not return Tree_index & because future additions can move the pointer (vector realloc)
  const Tree_index add_child(const Tree_index &parent, const X &data);
  const Tree_index add_sibling(const Tree_index &sibling, const X &data);

  X &      get_data(const Tree_index &leaf);
  const X &get_data(const Tree_index &leaf) const;

  const Tree_index get_depth_preorder_next(const Tree_index &child) const;

  const Tree_index get_parent(const Tree_index &child) const;
  const Tree_index get_root() const;
  void             set_root(const X &data);

  void add_lazy_child(const Tree_level &child_level, const X &data);

  // const Tree_index add_child(Tree_index parent, const Tree &t);
  // const Tree_index add_sibling(Tree_index brother, const Tree &t);
  // const Tree_index add_parent(Tree_index brother, X data);

  // NOTE: not a typical depth first traversal, goes to bottom without touching
  // parents first. It is also not a bottom-up traversal because it touches all
  // the tree level before going to next level
  void each_bottom_first_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;

  void each_breadth_first_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;

  // void each_bottom_up(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;
  // void each_depth_first(const Tree_index &start_index, std::function<void(const Tree_index &parent, const Tree_index &self, const
  // X &)> fn) const
  const std::vector<Tree_index> get_children(const Tree_index &start_index) const;

  Tree_depth_preorder_iterator depth_preorder(const Tree_index &start_index) const {
    return Tree_depth_preorder_iterator(start_index, this);
  }
};

//--------------------- Template Implementation ----

template <typename X>
void Tree<X>::adjust_to_level(Tree_level level) {
  if (data_stack.size() > level) return;

  while (data_stack.size() <= level) {
    data_stack.emplace_back();
    pointers_stack.emplace_back();
  }
};

template <typename X>
Tree<X>::Tree() {
  pending_parent = -1;  // Nobody pending
};

template <typename X>
const Tree_index Tree<X>::add_child(const Tree_index &parent, const X &data) {
  const auto parent_level = parent.level;
  const auto parent_pos   = parent.pos;

  I(data_stack.size() > parent_level);
  I(data_stack[parent_level].size() > parent_pos);
  I(data_stack.size() == pointers_stack.size());

  auto child_level = parent_level + 1;

  adjust_to_level(child_level);

  auto child_pos = data_stack[child_level].size();

  I(pending_parent != child_level);
  data_stack[child_level].emplace_back(data);
  pointers_stack[child_level].emplace_back(parent_pos);

  if (pointers_stack[parent_level][parent_pos].eldest_child < 0) {
    I(pointers_stack[parent_level][parent_pos].younger_child < 0);
    pointers_stack[parent_level][parent_pos].younger_child = child_pos;
  }else{
    auto older_sibling = pointers_stack[parent_level][parent_pos].eldest_child;
    I(pointers_stack[child_level][older_sibling].younger_sibling<0);
    pointers_stack[child_level][older_sibling].younger_sibling = child_pos;
  }
  pointers_stack[parent_level][parent_pos].eldest_child  = child_pos;

  return Tree_index(child_level, child_pos);
};

template <typename X>
const Tree_index Tree<X>::add_sibling(const Tree_index &sibling, const X &data) {
  const auto sibling_level = sibling.level;
  const auto sibling_pos   = sibling.pos;

  I(sibling_level > 0);  // No siblings to root

  I(data_stack.size() > sibling_level);
  I(data_stack[sibling_level].size() > sibling_pos);

  auto child_level = sibling_level;
  auto child_pos   = data_stack[child_level].size();

  I(pending_parent != child_level);
  data_stack[child_level].emplace_back(data);

  auto parent_level = child_level - 1;
  auto parent_pos   = pointers_stack[sibling_level][sibling_pos].parent;

  pointers_stack[child_level].emplace_back(parent_pos);

  I(pointers_stack[parent_level][parent_pos].eldest_child >= 0);
  auto older_sibling = pointers_stack[parent_level][parent_pos].eldest_child;
  pointers_stack[parent_level][parent_pos].eldest_child  = child_pos;
  pointers_stack[child_level][older_sibling].younger_sibling = child_pos;

  return Tree_index(child_level, child_pos);
}

template <typename X>
X &Tree<X>::get_data(const Tree_index &leaf) {
  I(data_stack.size() > leaf.level);
  I(data_stack[leaf.level].size() > leaf.pos);

  return data_stack[leaf.level][leaf.pos];
}

template <typename X>
const X &Tree<X>::get_data(const Tree_index &leaf) const {
  I(data_stack.size() > leaf.level);
  I(data_stack[leaf.level].size() > leaf.pos);

  return data_stack[leaf.level][leaf.pos];
}

template <typename X>
void Tree<X>::add_lazy_child(const Tree_level &child_level, const X &data) {
  // -If child_level==1, add to root, do not mark as pending

  I(child_level != 0);

  adjust_to_level(child_level);

  Tree_level parent_level = child_level - 1;
  Tree_pos   parent_pos   = pointers_stack[parent_level].size();
  if (parent_level == 0) {
    parent_pos = parent_pos - 1;  // No lazy root
  } else {
    I(pending_parent <= child_level);
    pending_parent = parent_level;
  }

  auto child_pos = data_stack[child_level].size();
  if (child_pos>0 && pointers_stack[child_level][child_pos-1].parent == parent_pos) {
    pointers_stack[child_level][child_pos-1].younger_sibling = child_pos;
  }

  data_stack[child_level].emplace_back(data);
  pointers_stack[child_level].emplace_back(parent_pos);

  if (pointers_stack.size() <= (child_level+1))
    return;

  if (pointers_stack[child_level+1].empty())
    return;

  if (pointers_stack[child_level+1].back().parent != child_pos)
    return;
  pointers_stack[child_level].back().younger_child = child_pos;

  auto eldest_child = child_pos;
  for(int i = pointers_stack[child_level+1].size()-1; i>=0 ; --i) {
    if (pointers_stack[child_level+1][i].parent != child_pos)
      break;
    eldest_child = i;
  }

  pointers_stack[child_level].back().eldest_child = eldest_child;
}

template <typename X>
const Tree_index Tree<X>::get_depth_preorder_next(const Tree_index &child) const {
  I(child.level < pointers_stack.size());
  I(child.pos   < pointers_stack[child.level].size());

  I(0); // HERE

  if (child.level>0)
    return Tree_index(child.level-1, pointers_stack[child.level][child.pos].parent);

  I(pointers_stack[0].size() == 1); // One single root

  return Tree_index(0,0);
}

template <typename X>
const Tree_index Tree<X>::get_parent(const Tree_index &child) const {
  I(child.level < pointers_stack.size());
  I(child.pos   < pointers_stack[child.level].size());

  if (child.level>0)
    return Tree_index(child.level-1, pointers_stack[child.level][child.pos].parent);

  I(pointers_stack[0].size() == 1); // One single root

  return Tree_index(0,0);
}

template <typename X>
const Tree_index Tree<X>::get_root() const {
  return Tree_index(0,0);
}

template <typename X>
void Tree<X>::set_root(const X &data) {
  adjust_to_level(0);

  if (data_stack[0].empty()) {
    pointers_stack[0].emplace_back(0);
  }else{
    I(pointers_stack[0][0].parent == 0);
    data_stack[0].clear();
  }

  data_stack[0].emplace_back(data);
}

template <typename X>
void Tree<X>::each_bottom_first_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const {
  auto sz = data_stack.size();
  for (int i = sz - 1; i >= 0; --i) {  // WARNING: must be signed to handle -1 for loop exit
    auto sz2 = data_stack[i].size();
    Tree_level parent_level;
    if (i>0)
      parent_level = i - 1;
    else
      parent_level = 0;

    for (size_t j = 0; j < sz2; j++) {
      fn(Tree_index(parent_level, pointers_stack[i][j].parent), Tree_index(i, j), data_stack[i][j]);
    }
  }
}

template <typename X>
void Tree<X>::each_breadth_first_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const {
  auto sz = data_stack.size();
  for (size_t i = 0; i < sz; i++) {
    auto sz2 = data_stack[i].size();
    Tree_level parent_level;
    if (i>0)
      parent_level = i - 1;
    else
      parent_level = 0;

    for (size_t j = 0; j < sz2; j++) {
      fn(Tree_index(parent_level, pointers_stack[i][j].parent), Tree_index(i, j), data_stack[i][j]);
    }
  }
}

template <typename X>
const std::vector<Tree_index> Tree<X>::get_children(const Tree_index &top) const {
  std::vector<Tree_index> children;

  // FIXME: build iterator

  I(top.level < pointers_stack.size());
  if (top.level == (pointers_stack.size())) return children;

  for (size_t j = 0; j < pointers_stack[top.level + 1].size(); j++) {
    if (pointers_stack[top.level + 1][j].parent == top.pos)
      children.emplace_back(top.level + 1, j);
  }

  return children;
}

