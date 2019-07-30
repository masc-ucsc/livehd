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
  Tree_index operator=(const Tree_index &i) {
    level = i.level;
    pos = i.pos;
    return *this;
  }
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
        I(t == other.t);
        return ti != other.ti;
      }
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      const Tree<X> *t;
    };

  private:
  protected:
    Tree_index     ti;
    const Tree<X> *t;

  public:
    Tree_depth_preorder_iterator() = delete;
    explicit Tree_depth_preorder_iterator(const Tree_index &_b, const Tree<X> *_t) : ti(_b), t(_t) {}

    CTree_depth_preorder_iterator begin() const { return CTree_depth_preorder_iterator(ti, t); }
    CTree_depth_preorder_iterator end()   const { return CTree_depth_preorder_iterator(Tree_index(-1,-1), t); }  // 0 is end index for iterator
  };

  class Tree_breadth_first_iterator {
  public:
    class CTree_breadth_first_iterator {
    public:
      CTree_breadth_first_iterator(const Tree_index &_ti, const Tree<X> *_t) : ti(_ti), t(_t) {}
      CTree_breadth_first_iterator operator++() {
        CTree_breadth_first_iterator i(ti, t);

        ti = t->get_breadth_first_next(ti);

        return i;
      };
      bool operator!=(const CTree_breadth_first_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      const Tree<X> *t;
    };

  private:
  protected:
    Tree_index     ti;
    const Tree<X> *t;

  public:
    Tree_breadth_first_iterator() = delete;
    explicit Tree_breadth_first_iterator(const Tree_index &_b, const Tree<X> *_t) : ti(_b), t(_t) {}

    CTree_breadth_first_iterator begin() const { return CTree_breadth_first_iterator(ti, t); }
    CTree_breadth_first_iterator end()   const { return CTree_breadth_first_iterator(Tree_index(-1,-1), t); }  // 0 is end index for iterator
  };
  Tree();

  void clear() {
    pending_parent = -1;  // Nobody pending
    data_stack.clear();
    pointers_stack.clear();
  }

  // WARNING: can not return Tree_index & because future additions can move the pointer (vector realloc)
  const Tree_index add_child             (const Tree_index &parent, const X &data);
  const Tree_index add_younger_sibling   (const Tree_index &sibling, const X &data);
  size_t get_tree_width(const Tree_level &level) const {
    if (level>=data_stack.size())
      return 0;

    return data_stack[level].size();
  }

  X &      get_data(const Tree_index &leaf);
  const X &get_data(const Tree_index &leaf) const;

  const Tree_index get_depth_preorder_next(const Tree_index &child) const;
  const Tree_index get_breadth_first_next(const Tree_index &child) const;

  const Tree_index get_parent     (const Tree_index &child) const;
  const Tree_index get_grandparent(const Tree_index &grandson) const;
  const Tree_index get_root() const;
  void             set_root(const X &data);

  void add_lazy_child(const Tree_level &child_level, const X &data); // FIXME: same as bimap/map, use emplace_back style (avoid extra copy)

  // const Tree_index add_child(Tree_index parent, const Tree &t);
  // const Tree_index add_sibling(Tree_index brother, const Tree &t);
  // const Tree_index add_parent(Tree_index brother, X data);

  // NOTE: not a typical depth first traversal, goes to bottom without touching
  // parents first. It is also not a bottom-up traversal because it touches all
  // the tree level before going to next level
  void each_bottom_up_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;

  void each_top_down_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;

  // void each_bottom_up(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;
  // void each_depth_first(const Tree_index &start_index, std::function<void(const Tree_index &parent, const Tree_index &self, const
  // X &)> fn) const
  const std::vector<Tree_index> get_children       (const Tree_index &start_index) const;

  // Tree traversals: https://en.wikipedia.org/wiki/Tree_traversal
  Tree_depth_preorder_iterator  depth_preorder(const Tree_index &start_index) const {
    return Tree_depth_preorder_iterator(start_index, this);
  }

  Tree_breadth_first_iterator  breadth_first(const Tree_index &start_index) const {
    return Tree_breadth_first_iterator(start_index, this);
  }

  Tree_depth_preorder_iterator  depth_preorder() const { return Tree_depth_preorder_iterator(get_root(), this); }
  Tree_breadth_first_iterator   breadth_first()  const { return Tree_breadth_first_iterator(get_root(), this); }

  bool is_leaf(const Tree_index &index) const {
    I(index.level< (int)pointers_stack.size());
    I(index.pos  < (int)pointers_stack[index.level].size());

    return pointers_stack[index.level][index.pos].younger_child < 0;
  }

  bool has_single_child(const Tree_index &index) const {
    I(index.level< (int)pointers_stack.size());
    I(index.pos  < (int)pointers_stack[index.level].size());

    bool valid  = pointers_stack[index.level][index.pos].younger_child >=0 ;
    bool single = pointers_stack[index.level][index.pos].younger_child == pointers_stack[index.level][index.pos].eldest_child;

    return valid && single;
  }

  bool is_child_of(const Tree_index &child, const Tree_index &parent) const {
    I(child.level< (int)pointers_stack.size());
    I(child.pos  < (int)pointers_stack[child.level].size());
    I(parent.level< (int)pointers_stack.size());
    I(parent.pos  < (int)pointers_stack[parent.level].size());

    auto level_stop_at = parent.level;
    auto level = child.level-1;
    auto pos = pointers_stack[child.level][child.pos].parent;

    while(level>level_stop_at) {
      pos = pointers_stack[level][pos].parent;
      level--;
    }

    return parent.level == level && parent.pos == pos;
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
void Tree<X>::adjust_to_level(Tree_level level) {
  if (data_stack.size() > static_cast<size_t>(level)) return;

  while (data_stack.size() <= static_cast<size_t>(level)) {
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

  I((int)data_stack.size() > parent_level);
  I((int)data_stack[parent_level].size() > parent_pos);
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
    pointers_stack[parent_level][parent_pos].eldest_child  = child_pos;
  }else{
    //auto older_sibling = pointers_stack[parent_level][parent_pos].eldest_child;
    auto older_sibling = pointers_stack[parent_level][parent_pos].younger_child;
    pointers_stack[parent_level][parent_pos].younger_child = child_pos;
    //I(pointers_stack[child_level][older_sibling].younger_sibling<0);
    pointers_stack[child_level][older_sibling].younger_sibling = child_pos;
  }

  //pointers_stack[parent_level][parent_pos].eldest_child  = child_pos;
  return Tree_index(child_level, child_pos);
};

template <typename X>
const Tree_index Tree<X>::add_younger_sibling(const Tree_index &sibling, const X &data) {
  const auto sibling_level = sibling.level;
  const auto sibling_pos   = sibling.pos;

  I(sibling_level > 0);  // No siblings to root

  I(data_stack.size() > (size_t)sibling_level);
  I(data_stack[sibling_level].size() > (size_t)sibling_pos);
  I(pointers_stack[sibling_level].size() > (size_t)sibling_pos);

  auto child_level = sibling_level;
  auto child_pos   = data_stack[child_level].size();

  I(pending_parent != child_level);
  data_stack[child_level].emplace_back(data);

  auto parent_level = child_level - 1;
  auto parent_pos   = pointers_stack[sibling_level][sibling_pos].parent;
  auto append_to_pos = pointers_stack[parent_level][parent_pos].younger_child;

  pointers_stack[child_level].emplace_back(parent_pos);
  I(append_to_pos>=0); // It has to have a child
  I(pointers_stack[parent_level][parent_pos].eldest_child >= 0);

  if (append_to_pos == sibling_pos) { // new child is the youngest now
    I(pointers_stack[sibling_level][sibling_pos].younger_sibling == -1); // It is the youngest
    I(pointers_stack[child_level][child_pos].younger_sibling == -1);

    pointers_stack[parent_level][parent_pos].younger_child = child_pos;
  } else {
    auto ori_younger_sibling = pointers_stack[sibling_level][sibling_pos].younger_sibling;

    pointers_stack[sibling_level][child_pos  ].younger_sibling = ori_younger_sibling;
  }
  pointers_stack[sibling_level][sibling_pos].younger_sibling = child_pos;

  return Tree_index(child_level, child_pos);
}

template <typename X>
X &Tree<X>::get_data(const Tree_index &leaf) {
  I((int)data_stack.size() > leaf.level);
  I((int)data_stack[leaf.level].size() > leaf.pos);

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
  if (parent_level.value == 0) {
    parent_pos = parent_pos - 1;  // No lazy root
  } else {
    I(pending_parent <= child_level);
    pending_parent = parent_level;
  }

  Tree_pos child_pos = data_stack[child_level].size();
  if (child_pos>0 && pointers_stack[child_level][child_pos-1].parent == parent_pos) {
    pointers_stack[child_level][child_pos-1].younger_sibling = child_pos;
  }

  data_stack[child_level].emplace_back(data);
  pointers_stack[child_level].emplace_back(parent_pos);

  if (pointers_stack.size() <= static_cast<size_t>(child_level+1))
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
  I(child.level < (int)pointers_stack.size());
  I(child.pos   < (int)pointers_stack[child.level].size());

  if(pointers_stack[child.level][child.pos].eldest_child != -1) {
    return Tree_index(child.level+1, pointers_stack[child.level][child.pos].eldest_child);
  }

  if (pointers_stack[child.level][child.pos].younger_sibling != -1) {
    return Tree_index(child.level, pointers_stack[child.level][child.pos].younger_sibling);
  }

  auto this_node  = pointers_stack[child.level][child.pos];
  auto prev_level = child.level - 1;
  auto parent_pos = this_node.parent;

  while(prev_level > 0){
    auto next_node = pointers_stack[prev_level][parent_pos];
    if(next_node.younger_sibling != -1) {
      return Tree_index(prev_level, next_node.younger_sibling);
    }
    prev_level = prev_level - 1;
    parent_pos = next_node.parent;
  }

  return Tree_index(-1,-1);
}

template <typename X>
const Tree_index Tree<X>::get_breadth_first_next(const Tree_index &child) const {
  I(child.level < (int)pointers_stack.size());
  I(child.pos   < (int)pointers_stack[child.level].size());

  // Go for sibling
  if (pointers_stack[child.level][child.pos].younger_sibling != -1) {
    return Tree_index(child.level, pointers_stack[child.level][child.pos].younger_sibling);
  }

  // No siblings left, go for another uncle/aunt
  if (child.level) { // Not if root already
    auto parent_pos   = pointers_stack[child.level][child.pos].parent;
    auto parent_level = child.level -1;
    parent_pos.value++;
    while (parent_pos < pointers_stack[parent_level].size()) {
      auto eldest_child = pointers_stack[parent_level][parent_pos].eldest_child;
      if (eldest_child != -1) {
        return Tree_index(child.level, eldest_child);
      }
      parent_pos.value++;
    }
  }

  // No siblings or uncles/aunts left, go for first descendent in tree.
  auto descendent_level = child.level+1;
  if (pointers_stack.size()<= descendent_level)
    return Tree_index(-1,-1); // DONE

  size_t pos = 0;
  while (pos < pointers_stack[child.level].size()) {
    auto eldest_child = pointers_stack[child.level][pos].eldest_child;
    if (eldest_child != -1) {
      return Tree_index(descendent_level, eldest_child);
    }
    pos++;
  }

  I(false); // There was a descendent. WHere is it?
  return Tree_index(-1,-1);
}

template <typename X>
const Tree_index Tree<X>::get_parent(const Tree_index &child) const {
  I(child.level < (int)pointers_stack.size());
  I(child.pos   < (int)pointers_stack[child.level].size());

  if (child.level>0)
    return Tree_index(child.level-1, pointers_stack[child.level][child.pos].parent);

  I(pointers_stack[0].size() == 1); // One single root

  return Tree_index(0,0);
}

template <typename X>
const Tree_index Tree<X>::get_grandparent(const Tree_index &grandson) const {
  return get_parent(get_parent(grandson));
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
    I(pointers_stack[0][0].parent.value == 0);
    data_stack[0].clear();
  }

  data_stack[0].emplace_back(data);
}

template <typename X>
void Tree<X>::each_bottom_up_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const {
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
void Tree<X>::each_top_down_fast(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const {
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

  I((size_t)top.level < pointers_stack.size());
  if ((size_t)top.level.value == pointers_stack.size()) return children;

  for (size_t j = 0; j < pointers_stack[top.level + 1].size(); j++) {
    if (pointers_stack[top.level + 1][j].parent == top.pos)
      children.emplace_back(top.level + 1, j);
  }

  return children;
}

