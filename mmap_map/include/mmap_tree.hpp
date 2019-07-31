//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdint.h>
#include <functional>
#include <vector>

#include "iassert.hpp"

namespace mmap_map {
using Tree_level = int32_t;
using Tree_pos   = int32_t;

class Tree_index {
public:
  Tree_level level;
  Tree_pos   pos;

  Tree_index() = delete;
  Tree_index(Tree_level l, Tree_pos i) : level(l), pos(i) {}

  bool operator==(const Tree_index &i) const { return level == i.level && pos == i.pos; }
  bool operator!=(const Tree_index &i) const { return level != i.level || pos != i.pos; }
  Tree_index operator=(const Tree_index &i) {
    level = i.level;
    pos = i.pos;
    return *this;
  }

  bool is_invalid() const { return level == -1 || pos == -1; }
  void invalidate() { level == -1; pos = -1; }
};

template <typename X>
class tree {
protected:
  struct Tree_pointers {
    Tree_pointers(Tree_pos p) : parent(p), next_sibling(0) {
      for (int i = 0; i < 4; ++i) {
        first_child[i] = -1;
        last_child[i] = -1;
      }
    }
    Tree_pointers() : parent(0), next_sibling(0) {
      for (int i = 0; i < 4; ++i) {
        first_child[i] = -1;
        last_child[i] = -1;
      }
    }

    const Tree_pos  parent; // Parent can never change
    Tree_pos        next_sibling;   // 0 -> 1 child[0], 1 -> 2 child[0..1], 2 -> 3 child[0..2], 3 -> 4 child[0..3] (no external child)

    Tree_pos        first_child[4];
    Tree_pos        last_child[4];
  };
  std::vector<std::vector<X>>             data_stack;
  std::vector<std::vector<Tree_pointers>> pointers_stack;
  int                                     pending_parent;  // Must be signed
  int                                     pending_child;

  void adjust_to_level(Tree_level level);

  Tree_pos create_space(const Tree_index &parent, const X &data) {
    auto pos = data_stack[parent.level+1].size();

    data_stack[parent.level+1].emplace_back(data);
    data_stack[parent.level+1].emplace_back();
    data_stack[parent.level+1].emplace_back();
    data_stack[parent.level+1].emplace_back();
    pointers_stack[parent.level+1].emplace_back(parent.pos);

    return pos;
  }

  const Tree_pos *ref_last_child_pos(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].last_child[index.pos & 3];
  }

  Tree_pos *ref_last_child_pos(const Tree_index &index) {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].last_child[index.pos & 3];
  }

  const Tree_pos *ref_first_child_pos(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].first_child[index.pos & 3];
  }

  Tree_pos *ref_first_child_pos(const Tree_index &index) {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].first_child[index.pos & 3];
  }

  bool is_valid(const Tree_index &index) const {
    if (index.level >= (int)pointers_stack.size())
      return false;

    if((index.pos >> 2) >= (int)pointers_stack[index.level].size())
      return false;

    auto pos = pointers_stack[index.level][index.pos >> 2].next_sibling;
    if ((pos>>2)==0 && (pos <= index.pos & 3))
      return false;

    return true;
  }

  const Tree_pos *ref_next_sibling_pos(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].next_sibling;
  }

  Tree_pos *ref_next_sibling_pos(const Tree_index &index) {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].next_sibling;
  }

  bool is_last_next_sibling_chunk(const Tree_index &sibling) const {
    const auto *next_sibling = ref_next_sibling_pos(sibling);

    return ((*next_sibling)>>2) == 0;
  }

  bool has_next_sibling_space(const Tree_index &sibling) const {
    I(data_stack.size() > (size_t)sibling.level);
    I(data_stack[sibling.level].size() > (size_t)sibling.pos);
    I(pointers_stack[sibling.level].size() > (size_t)sibling.pos>>2);

    const auto &next_sibling = pointers_stack[sibling.level][sibling.pos>>2].next_sibling;
    if ((next_sibling>>2) != 0)
      return false;

    return next_sibling < 3; // next_sibling points to the last inserted (not next)
  }

public:
  Tree_index get_last_child(const Tree_index &parent_index) const {
    Tree_index child_index(parent_index.level+1, *ref_last_child_pos(parent_index));

    GI(!child_index.is_invalid(), get_next_sibling(child_index).is_invalid());

    return child_index;
  }

  Tree_index get_first_child(const Tree_index &parent_index) const {

    Tree_index child_index(parent_index.level+1, *ref_first_child_pos(parent_index));

    GI(!child_index.is_invalid(), get_next_sibling(child_index).is_invalid());

    return child_index;
  }

  Tree_index get_next_sibling(const Tree_index &sibling) const {
    const auto *next_sibling = ref_next_sibling_pos(sibling);

    bool all_used = ((*next_sibling) >> 2) != 0 || *next_sibling == 3;
    if ((sibling.pos&3)+1 > *next_sibling) {
      return Tree_index(-1,-1); // No more siblings
    }

    auto pos = sibling.pos + 1;
    if (all_used && pos >= 4) {
      return Tree_index(sibling.level, *next_sibling);
    }

    return Tree_index(sibling.level, pos);
  }

  class Tree_depth_preorder_iterator {
  public:
    class CTree_depth_preorder_iterator {
    public:
      CTree_depth_preorder_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) {}
      CTree_depth_preorder_iterator operator++() {
        CTree_depth_preorder_iterator i(ti, t);

        ti = t->get_depth_preorder_next(ti);

        return i;
      };
      bool operator!=(const CTree_depth_preorder_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      const tree<X> *t;
    };

  private:
  protected:
    Tree_index     ti;
    const tree<X> *t;

  public:
    Tree_depth_preorder_iterator() = delete;
    explicit Tree_depth_preorder_iterator(const Tree_index &_b, const tree<X> *_t) : ti(_b), t(_t) {}

    CTree_depth_preorder_iterator begin() const { return CTree_depth_preorder_iterator(ti, t); }
    CTree_depth_preorder_iterator end()   const { return CTree_depth_preorder_iterator(Tree_index(-1,-1), t); }  // 0 is end index for iterator
  };

  tree();

  void clear() {
    pending_parent = -1;  // Nobody pending
    pending_child  = -1;  // Nobody pending

    data_stack.clear();
    pointers_stack.clear();
  }

  // WARNING: can not return Tree_index & because future additions can move the pointer (vector realloc)
  const Tree_index add_child          (const Tree_index &parent, const X &data);
  const Tree_index add_next_sibling   (const Tree_index &sibling, const X &data);
  size_t get_tree_width(const Tree_level &level) const {
    if (level>=data_stack.size())
      return 0;

    return data_stack[level].size();
  }

  void     set_data(const Tree_index &index, const X &data) {
    I((int)data_stack.size() > index.level);
    I((int)data_stack[index.level].size() > index.pos);

    data_stack[index.level][index.pos] = data;
  }

  X &      get_data(const Tree_index &leaf) {
    I((int)data_stack.size() > leaf.level);
    I((int)data_stack[leaf.level].size() > leaf.pos);

    return data_stack[leaf.level][leaf.pos];
  }
  const X &get_data(const Tree_index &leaf) const {
    I(data_stack.size() > leaf.level);
    I(data_stack[leaf.level].size() > leaf.pos);

    return data_stack[leaf.level][leaf.pos];
  }

  const Tree_index get_depth_preorder_next(const Tree_index &child) const;

  const Tree_index get_parent(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    auto pos   = pointers_stack[index.level][index.pos >> 2].parent;
    auto level = index.level - 1;
    if (level < 0) level = 0;

    GI(index.level == 0, level == 0 && pos == 0);  // parent of root is root

    return Tree_index(level, pos);
  }
  const Tree_index get_root() const;
  void             set_root(const X &data);

  // NOTE: not a typical depth first traversal, goes to bottom without touching
  // parents first. It is also not a bottom-up traversal because it touches all
  // the tree level before going to next level
  void each_bottom_up_fast(std::function<void(const Tree_index &self, const X &)> fn) const;

  void each_top_down_fast(std::function<void(const Tree_index &self, const X &)> fn) const;

  // void each_bottom_up(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;
  // void each_depth_first(const Tree_index &start_index, std::function<void(const Tree_index &parent, const Tree_index &self, const
  // X &)> fn) const
  const std::vector<Tree_index> get_children       (const Tree_index &start_index) const;
  const Tree_index              get_child          (const Tree_index &start_index) const;

  // tree traversals: https://en.wikipedia.org/wiki/Tree_traversal
  Tree_depth_preorder_iterator  depth_preorder(const Tree_index &start_index) const {
    return Tree_depth_preorder_iterator(start_index, this);
  }

  Tree_depth_preorder_iterator  depth_preorder() const { return Tree_depth_preorder_iterator(get_root(), this); }

  bool is_leaf(const Tree_index &index) const {

    return (*ref_first_child_pos(index)) == -1;
  }

  bool is_root(const Tree_index &index) const {

    GI(index.level == 0, index.pos == 0);

    return index.level == 0;
  }

  bool has_single_child(const Tree_index &index) const {
    auto *fc_pos = ref_first_child_pos(index);

    if (*fc_pos==-1)
      return false;

    auto lc_index = get_last_child(index);

    return *fc_pos == lc_index.pos;
  }

  bool is_child_of(const Tree_index &child, const Tree_index &potential_parent) const {

    auto level_stop_at = potential_parent.level;
    auto level = child.level-1;
    auto child_parent_chain = get_parent(child);

    while(child_parent_chain.level>level_stop_at) {
      child_parent_chain = get_parent(child_parent_chain);
    }

    return potential_parent == child_parent_chain;
  }

  void dump() const {
    for(const auto &index:depth_preorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\n", indent.c_str(), index.level, index.pos);
    }
  }

};

//--------------------- Template Implementation ----

template <typename X>
void tree<X>::adjust_to_level(Tree_level level) {
  if (data_stack.size() > static_cast<size_t>(level)) return;

  while (data_stack.size() <= static_cast<size_t>(level)) {
    data_stack.emplace_back();
    pointers_stack.emplace_back();
  }
};

template <typename X>
tree<X>::tree() {
  pending_parent = -1;  // Nobody pending
  pending_child  = -1;  // Nobody pending
};

template <typename X>
const Tree_index tree<X>::add_child(const Tree_index &parent, const X &data) {
  const auto parent_level = parent.level;
  const auto parent_pos   = parent.pos;

  I((int)data_stack.size() > parent_level);
  I((int)data_stack[parent_level].size() > parent_pos);

  auto child_level = parent_level + 1;

  adjust_to_level(child_level);

  auto *parent_lc_pos = ref_last_child_pos(parent);
  if (*parent_lc_pos != -1) {
    Tree_index sibling(child_level, *parent_lc_pos);
    return add_next_sibling(sibling, data);
  }

  auto child_pos      = create_space(parent, data);
  auto *parent_fc_pos = ref_first_child_pos(parent);
  *parent_fc_pos      = child_pos;
  *parent_lc_pos      = child_pos;

  I(pending_parent != child_level);
  I(pending_child  != child_level);

  Tree_index child(child_level, child_pos);

  I(get_last_child(parent) == child);
  I(is_leaf(child));
  I(has_single_child(parent));
  I(get_parent(child) == parent);

  return child;
};

template <typename X>
const Tree_index tree<X>::add_next_sibling(const Tree_index &sibling, const X &data) {
  const auto sibling_level = sibling.level;
  const auto sibling_pos   = sibling.pos;

  I(sibling_level > 0);  // No siblings to root

  auto parent         = get_parent(sibling);
  auto *parent_lc_pos = ref_last_child_pos(parent);

  Tree_index child(sibling_level, -1);

  if (has_next_sibling_space(sibling)) {
    I(is_last_next_sibling_chunk(sibling));
    auto *next_sibling  = ref_next_sibling_pos(sibling);
    (*next_sibling)++;

    child.pos   = ((sibling.pos>>2)<<2) + *next_sibling;

    set_data(child, data);
  }else{
    child.pos     = create_space(parent, data);
    auto *next_sibling  = ref_next_sibling_pos(sibling); // Warning. Get it after create_space (potential dangling pointer otherwise)
    *next_sibling = child.pos;

    I(!is_leaf(parent));     // Add sibling, so already has child
  }

  I(!child.is_invalid());
  I(is_leaf(child));
  if (*parent_lc_pos == sibling_pos) { // new child is the youngest now
    *parent_lc_pos = child.pos;
  }

  return child;
}

template <typename X>
const Tree_index tree<X>::get_depth_preorder_next(const Tree_index &child) const {
  I(child.level    < (int)pointers_stack.size());
  I((child.pos>>2) < (int)pointers_stack[child.level].size());

  auto *fc = ref_first_child_pos(child);
  if (*fc != -1) {
    return Tree_index(child.level+1, *fc);
  }

  I(is_leaf(child));

  auto next = get_next_sibling(child);
  if (!next.is_invalid()) {
    return next;
  }

  // It was leaf, without more siblings. Go to parent with sibling
  auto parent = get_parent(child);
  while (!is_root(parent)) {
    auto parent_next = get_next_sibling(parent);
    if (!parent_next.is_invalid())
      return parent_next;

    parent = get_parent(parent);
  }

  return Tree_index(-1,-1);
}


template <typename X>
const Tree_index tree<X>::get_root() const {
  return Tree_index(0,0);
}

template <typename X>
void tree<X>::set_root(const X &data) {
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
void tree<X>::each_bottom_up_fast(std::function<void(const Tree_index &self, const X &)> fn) const {
  auto sz = data_stack.size();
  for (int i = sz - 1; i >= 0; --i) {  // WARNING: must be signed to handle -1 for loop exit
    auto sz2 = data_stack[i].size();

    for (size_t j = 0; j < sz2; j++) {
      Tree_index ti(i, j);
      if (!is_valid(ti))
        continue;

      fn(ti, data_stack[i][j]);
    }
  }
}

template <typename X>
void tree<X>::each_top_down_fast(std::function<void(const Tree_index &self, const X &)> fn) const {
  auto sz = data_stack.size();
  for (size_t i = 0; i < sz; i++) {
    auto sz2 = data_stack[i].size();

    for (size_t j = 0; j < sz2; j++) {
      Tree_index ti(i, j);
      if (!is_valid(ti))
        continue;

      fn(ti, data_stack[i][j]);
    }
  }
}

template <typename X>
const Tree_index tree<X>::get_child(const Tree_index &top) const {
  auto *fc = ref_first_child_pos(top);
  if (*fc == -1) return Tree_index(-1, -1);

  return Tree_index(top.level + 1, *fc);
}

} // namespace mmap_map

