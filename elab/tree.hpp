//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include "explicit_type.hpp"
#include "iassert.hpp"

using Tree_level = Explicit_type<int32_t, struct Tree_level_struct>;  // Must be signed
using Tree_pos   = Explicit_type<uint32_t, struct Tree_pos_struct>;

class Tree_index {
  const Tree_level level;
  const Tree_pos   pos;

public:
  Tree_index() = delete;
  Tree_index(Tree_level l, Tree_pos i) : level(l), pos(i) {}

  inline Tree_level get_level() const { return level; }
  inline Tree_pos   get_pos() const { return pos; }

  bool operator==(const Tree_index &i) const { return level == i.level && pos == i.pos; }
};

template <typename X>
class Tree {
  std::vector<std::vector<X>>          data_stack;
  std::vector<std::vector<Tree_index>> parent_stack;
  int                                  pending_parent;  // Must be signed

  void adjust_to_level(Tree_level level);

public:
  Tree();

  // WARNING: can not return Tree_index & because future additions can move the pointer (vector realloc)
  const Tree_index add_child(const Tree_index &parent, const X &data);
  const Tree_index add_sibling(const Tree_index &sibling, const X &data);

  X &      get_data(const Tree_index &leaf);
  const X &get_data(const Tree_index &leaf) const;

  const Tree_index &get_parent(const Tree_index &child) const;
  const Tree_index &get_root() const;
  void              set_root(const X &data);

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
};

//--------------------- Template Implementation ----

template <typename X>
void Tree<X>::adjust_to_level(Tree_level level) {
  if (data_stack.size() > level) return;

  while (data_stack.size() <= level) {
    data_stack.emplace_back();
    parent_stack.emplace_back();
  }
};

template <typename X>
Tree<X>::Tree() {
  pending_parent = -1;  // Nobody pending
};

template <typename X>
const Tree_index Tree<X>::add_child(const Tree_index &parent, const X &data) {
  const auto parent_level = parent.get_level();
  const auto parent_pos   = parent.get_pos();

  I(data_stack.size() > parent_level);
  I(data_stack[parent_level].size() > parent_pos);
  I(data_stack.size() == parent_stack.size());

  auto child_level = parent_level + 1;

  adjust_to_level(child_level);

  auto child_pos = data_stack[child_level].size();

  I(pending_parent != child_level);
  data_stack[child_level].emplace_back(data);
  parent_stack[child_level].emplace_back(Tree_index(parent_level, parent_pos));

  return Tree_index(child_level, child_pos);
};

template <typename X>
const Tree_index Tree<X>::add_sibling(const Tree_index &sibling, const X &data) {
  const auto sibling_level = sibling.get_level();
  const auto sibling_pos   = sibling.get_pos();

  I(sibling_level > 0);  // No siblings to root

  I(data_stack.size() > sibling_level);
  I(data_stack[sibling_level].size() > sibling_pos);

  auto child_level = sibling_level;
  auto child_pos   = data_stack[child_level].size();

  I(pending_parent != child_level);
  data_stack[child_level].emplace_back(data);
  parent_stack[child_level].emplace_back(parent_stack[sibling_level][sibling_pos]);

  return Tree_index(child_level, child_pos);
}

template <typename X>
X &Tree<X>::get_data(const Tree_index &leaf) {
  I(data_stack.size() > leaf.get_level());
  I(data_stack[leaf.get_level()].size() > leaf.get_pos());

  return data_stack[leaf.get_level()][leaf.get_pos()];
}

template <typename X>
const X &Tree<X>::get_data(const Tree_index &leaf) const {
  I(data_stack.size() > leaf.get_level());
  I(data_stack[leaf.get_level()].size() > leaf.get_pos());

  return data_stack[leaf.get_level()][leaf.get_pos()];
}

template <typename X>
void Tree<X>::add_lazy_child(const Tree_level &child_level, const X &data) {
  // -If child_level==1, add to root, do not mark as pending

  I(child_level != 0);

  adjust_to_level(child_level);

  Tree_level parent_level = child_level - 1;
  Tree_pos   parent_pos   = parent_stack[parent_level].size();
  if (parent_level == 0) {
    parent_pos = parent_pos - 1;  // No lazy root
  } else {
    I(pending_parent <= child_level);
    pending_parent = parent_level;
  }

  data_stack[child_level].emplace_back(data);
  parent_stack[child_level].emplace_back(Tree_index(parent_level, parent_pos));
}

template <typename X>
const Tree_index &Tree<X>::get_parent(const Tree_index &child) const {
  I(child.get_level() < parent_stack.size());
  I(child.get_pos() < parent_stack[child.get_level()].size());

  return parent_stack[child.get_level()][child.get_pos()];
}

template <typename X>
const Tree_index &Tree<X>::get_root() const {
  return parent_stack[0][0];
}

template <typename X>
void Tree<X>::set_root(const X &data) {
  adjust_to_level(0);

  if (!data_stack[0].empty()) {
    data_stack[0].clear();
    parent_stack[0].clear();
  }

  data_stack[0].emplace_back(data);
  parent_stack[0].emplace_back(0, 0);
}

template <typename X>
void Tree<X>::each_bottom_first_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const {
  auto sz = data_stack.size();
  for (int i = sz - 1; i >= 0; --i) {  // WARNING: must be signed to handle -1 for loop exit
    auto sz2 = data_stack[i].size();
    for (size_t j = 0; j < sz2; j++) {
      fn(parent_stack[i][j], Tree_index(i, j), data_stack[i][j]);
    }
  }
}

template <typename X>
void Tree<X>::each_breadth_first_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const {
  auto sz = data_stack.size();
  for (size_t i = 0; i < sz; i++) {
    auto sz2 = data_stack[i].size();
    for (size_t j = 0; j < sz2; j++) {
      fn(parent_stack[i][j], Tree_index(i, j), data_stack[i][j]);
    }
  }
}

template <typename X>
const std::vector<Tree_index> Tree<X>::get_children(const Tree_index &top) const {
  std::vector<Tree_index> children;

  I(top.get_level() < parent_stack.size());
  if (top.get_level() == (parent_stack.size())) return children;

  for (size_t j = 0; j < parent_stack[top.get_level() + 1].size(); j++) {
    if (parent_stack[top.get_level() + 1][j].get_pos() == top.get_pos()) children.emplace_back(top.get_level() + 1, j);
  }

  return children;
}
