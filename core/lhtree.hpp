//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// TODO: Decide order and fields. Best order:
// 01: ?? | 1
// 02: ?? -── 1.1
// 03: ?? ├── 1.2
// 04: ?? │   -── 1.2.1
// 05: ?? │       -── 1.2.1.1
// 06: ?? │       ├── 1.2.1.2
// 07: ?? ├── 1.3
// 08: ?? │   -── 1.3.1
// 09: ?? │   ├── 1.3.2
// 10: ?? │   ├── 1.3.3
// 11: ?? │       -── 1.3.1.1
// 12: ?? │       |── 1.3.1.2
// 13: ?? │       |── 1.2.1.3
// 14: ?? │           -── 1.2.1.3.1
// 15: ?? │           -── 1.2.1.3.2
// 16: ?? ├── 1.4
// 17: ?? │   -── 1.4.1
// 18: ?? │   ├── 1.4.2
// 19: ?? │   │   -── 1.4.2.1
// 20: ?? │   ├── 1.4.3
// 21: ?? │   │   -── 1.4.3.1
//
// Vector n-ary tree implementation:
//
// 2 arrays: main and overflow
//
// mmap: overflow (under 32K entries continuous) : main array
//
// main array:
//
//  * A int16_t per entry (<0 points to overflow), >0 is the tree level (0 deleted/unused)
//  * Nodes in array using post-order, Entry is the level
//
// Capacity to print a bit like "tree" (but in post-order)
// Index: NextSibling: Level : Tree
// 00: +7, 2 -── 1.1
// 01: +1, 4 │       -── 1.2.1.1
// 02: +3, 4 │       ├── 1.2.1.2
// 03: +1, 5 │       |   -── 1.2.1.3.1
// 04: +0, 5 │       |   |── 1.2.1.3.2
// 05: +0, 4 │       |── 1.2.1.3
// 06: +0, 3 │   -── 1.2.1
// 07: +6, 2 ├── 1.2
// 08: +1, 4 │       -── 1.3.1.1
// 09: +1, 4 │       |── 1.3.1.2
// 0a: +1, 3 │   -── 1.3.1
// 0b: +1, 3 │   ├── 1.3.2
// 0c: +0, 3 │   ├── 1.3.3
// 0d: +6, 2 ├── 1.3
// 0e: +3, 3 │   -── 1.4.1
// 0f: +1, 4 │   │   -── 1.4.2.1
// 10: +0, 4 │   │   |── 1.4.2.2
// 11: +1, 3 │   ├── 1.4.2
// 12: +0, 3 │   ├── 1.4.3
// 13: +0, 2 ├── 1.4
// 14: +0, 1 | 1
//
// API: find_siblings
//  while(l[pos]<self_level) {
//    if (l[pos] == self_level)
//      add_to_sibling list
//    ++pos
//  }
//
// API: find_parent
//  while(l[pos]!=self_level+1) {
//    ++pos
//  }
//  pos is parent (unless root which has no parent)
//
// API: find_children
//  while(l[pos]<self_level) {
//    if (l[pos] == self_level-1)
//      add_to_children list
//    --pos
//  }
//
// ------------------------------------------------------
// ALTERNATIVE ORDER: (children_first traversal) -- Better (more spatial locality, less search)
// Visit tree node once children and niblings are visited
// (post-order does visit tree node once the children are visited)
// visit:
//   visit children(...)
//   print children()
// Index: first Child, parent
// 01: 00, 05 │           -── 1.2.1.3.1
// 02: 00, 05 │           -── 1.2.1.3.2
// 03: 00, 06 │       -── 1.2.1.1
// 04: 00, 06 │       ├── 1.2.1.2
// 05: 01, 06 │       |── 1.2.1.3
// 06: 03, 12 │   -── 1.2.1
// 07: 00, 09 │       -── 1.3.1.1
// 08: 00, 09 │       |── 1.3.1.2
// 09: 08, 13 │   -── 1.3.1
// 0a: 00, 13 │   ├── 1.3.2
// 0b: 00, 13 │   ├── 1.3.3
// 0c: 00, 0e │   │   -── 1.4.2.1
// 0d: 00, 0e │   │   -── 1.4.3.1
// 0e: 00, 14 │   -── 1.4.1
// 0f: 0c, 14 │   ├── 1.4.2
// 10: 0d, 14 │   ├── 1.4.3
// 11: 00, 15 -── 1.1
// 12: 06, 15 ├── 1.2
// 13: 09, 15 ├── 1.3
// 14: 0e, 15 ├── 1.4
// 15: 11, 00 | 1
//
// API: find_next_siblings
//  while(parent[++pos]==parent[self]) {
//    add_to_sibling list
//  }
//
// API: find_prev_siblings
//  while(parent[--pos]==parent[self]) {
//    add_to_sibling list
//  }
//
// API: find_parent
//  parent[pos]
//
// API: find_children
//  from = fc[pos]
//  while(parent[from++] == pos) {
//    add_to_children list
//  }
//
//  API is_first_child()
//    Check prev parent (if parent_id is diff, is first child)
//
//  API is_last_child()
//    Check next parent (if parent_id is diff, is last child)
//
//  API: find_last_child()
//    check older sibling that has children (fc[pos+1] if exist)
//    sibling_fc-1 should be the last child
//
// API: insert_child_next_to
//    insert after, keep same parent
//
// API: insert_first_child
//    insert before fc[pos], update fc[pos]--
//
// API: insert_last_child
//    find last_child, insert after
//
// API: add_child (first child or insert_last_child)
//
// ------------------------------------------------------
// BEST potential locality on typical LNAST traversal
//
//   Get children & get parent calls
// 02: 02 -── 1.1
// 05: 04 │       -── 1.2.1.1
// 06: 04 │       ├── 1.2.1.2
// 14: 05 │           -── 1.2.1.3.1
// 15: 05 │           -── 1.2.1.3.2
// 13: 04 │       |── 1.2.1.3
// 04: 03 │   -── 1.2.1
// 03: 02 ├── 1.2
// 11: 04 │       -── 1.3.1.1
// 12: 04 │       |── 1.3.1.2
// 08: 03 │   -── 1.3.1
// 09: 03 │   ├── 1.3.2
// 10: 03 │   ├── 1.3.3
// 07: 02 ├── 1.3
// 17: 03 │   -── 1.4.1
// 19: 04 │   │   -── 1.4.2.1
// 18: 03 │   ├── 1.4.2
// 21: 04 │   │   -── 1.4.3.1
// 20: 03 │   ├── 1.4.3
// 16: 02 ├── 1.4
// 01: 01 | 1
//
// API: find_next_siblings
//  ++pos;
//  while(level[pos]<=level[self]) {
//    if(level[pos]==level[self]) {
//      return pos
//    }
//    ++pos;
//  }
//
// API: find_prev_siblings
//   Same traverse --pos
//
// API: find_parent
//   ++pos until level[pos] == level[self]+1
//
//  API is_first_child()
//   go --pos while (level[pos]>level[self])
//   true when level[pos] == level[self] || level[pos] == level[self-1]
//
//  API is_last_child()
//   true when level[pos+1] == level[self] + 1
//
// ------------------------------------------------------
// BEST Possible order for LNAST "typical" traversal is post-order (first-case)
//   Get children & get parent calls
//
// Index: level (2bytes)
//
// 01: 01 | 1
// 02: 02 -── 1.1
// 03: 02 ├── 1.2
// 04: 03 │   -── 1.2.1
// 05: 04 │       -── 1.2.1.1
// 06: 04 │       ├── 1.2.1.2
// 07: 02 ├── 1.3
// 08: 03 │   -── 1.3.1
// 09: 03 │   ├── 1.3.2
// 10: 03 │   ├── 1.3.3
// 11: 04 │       -── 1.3.1.1
// 12: 04 │       |── 1.3.1.2
// 13: 04 │       |── 1.2.1.3
// 14: 05 │           -── 1.2.1.3.1
// 15: 05 │           -── 1.2.1.3.2
// 16: 02 ├── 1.4
// 17: 03 │   -── 1.4.1
// 18: 03 │   ├── 1.4.2
// 19: 04 │   │   -── 1.4.2.1
// 20: 03 │   ├── 1.4.3
// 21: 04 │   │   -── 1.4.3.1
//
// API: find_next_siblings
//  ++pos;
//  while(level[pos]<=level[self]) {
//    if(level[pos]==level[self]) {
//      return pos
//    }
//    ++pos;
//  }
//
// API: find_prev_siblings
//   Same traverse --pos
//
// API: find_parent
//  Build to ID when traversing, but could be recomputed
//  go back until level+1 is found
//
// API: find_children
//  Similar find_next_siblings but continue adding
//
//  API is_first_child()
//   return level[self-1] == level[self]-1
//
//  API is_last_child()
//   return level[self+1] >= level[self]-1
//
//  API: find_last_child()
//    check older sibling that has children (fc[pos+1] if exist)
//    sibling_fc-1 should be the last child
//
// API: insert_child_next_to
//    insert after, keep same parent
//
// API: insert_first_child
//    insert before fc[pos], update fc[pos]--
//
// API: insert_last_child
//    find last_child, insert after
//
// API: add_child (first child or insert_last_child)

#include <sys/stat.h>
#include <sys/types.h>

#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "iassert.hpp"

namespace lh {
using Tree_level = int32_t;
using Tree_pos   = int32_t;

class __attribute__((packed)) Tree_index {
public:
  Tree_level level;
  Tree_pos   pos;

  constexpr Tree_index() : level(-1), pos(-1) {};
  constexpr Tree_index(Tree_level l, Tree_pos i) : level(l), pos(i) {}
  constexpr Tree_index(const Tree_index &ti) : level(ti.level), pos(ti.pos) {}

  static constexpr Tree_index root() { return Tree_index(0, 0); }

  constexpr Tree_index &operator=(const Tree_index &o) {
    level = o.level;
    pos   = o.pos;
    return *this;
  }

  constexpr bool operator==(const Tree_index &i) const { return level == i.level && pos == i.pos; }
  constexpr bool operator!=(const Tree_index &i) const { return level != i.level || pos != i.pos; }

  constexpr bool is_invalid() const { return level == -1 || pos == -1; }
  void           invalidate() {
    level = -1;
    pos   = -1;
  }
  constexpr bool is_root() const { return level == 0; }

  template <typename H>
  friend H AbslHashValue(H h, const Tree_index &s) {
    return H::combine(std::move(h), s.level, s.pos);
  };

  constexpr inline uint64_t get_hash() const {
    uint64_t at = level;
    at <<= 32;
    at |= pos;

    return at;
  }
};

template <typename X>
class tree {
protected:
  struct Tree_pointers {
    Tree_pointers(Tree_pos p) : parent(p), next_sibling(1) {
      for (int i = 0; i < 4; ++i) {
        first_child[i] = -1;
        last_child[i]  = -1;
      }
    }
    Tree_pointers() : parent(0), next_sibling(0) {
      for (int i = 0; i < 4; ++i) {
        first_child[i] = -1;
        last_child[i]  = -1;
      }
    }

    Tree_pos parent;        // Parent can change when shifting at insert
    Tree_pos next_sibling;  // 0 -> 4 children, 1 -> 1 children, 2 -> 2 children, 3 -> 3 children

    Tree_pos first_child[4];
    Tree_pos last_child[4];
  };

  const std::string                       mmap_name;
  const std::string                       mmap_path;
  std::vector<std::vector<X>>             data_stack;
  std::vector<std::vector<Tree_pointers>> pointers_stack;
  int                                     pending_parent;  // Must be signed
  int                                     pending_child;

  void adjust_to_level(Tree_level level);

  Tree_pos create_space(const Tree_index &parent, const X &data) {
    auto &dsp = data_stack[parent.level + 1];

    dsp.emplace_back(data);
    dsp.emplace_back();
    dsp.emplace_back();
    dsp.emplace_back();

    I(static_cast<size_t>(parent.pos >> 2) < pointers_stack[parent.level].size());
    pointers_stack[parent.level + 1].emplace_back(parent.pos);

    I(pointers_stack[parent.level + 1].back().next_sibling == 1);

    return dsp.size() - 4;
  }

  void adjust_parent_pointer(const Tree_index &child, Tree_pos current_pos, Tree_pos new_pos) {
    auto &l         = pointers_stack[child.level];
    auto  shift_pos = child.pos >> 2;

    while (true) {
      I(l[shift_pos].parent == current_pos);
      l[shift_pos].parent = new_pos;
      shift_pos           = l[shift_pos].next_sibling >> 2;
      if (shift_pos == 0) {
        return;
      }
    }
  }

  void make_space_after(const Tree_index &sibling, bool after = true) {
    I((sibling.pos & 3) != 3);  // no space to make if it is the last already

    auto &ptrs = pointers_stack[sibling.level][sibling.pos >> 2];

    auto first_child = get_first_child(get_parent(sibling));
    I((first_child.pos & 3) == 0);
    I(first_child.level == sibling.level);

    auto last_pos = ptrs.next_sibling & 3;
    if (last_pos == 0) {
      last_pos = 3;
    }

    I((sibling.pos & 3) <= last_pos);

    auto first_pos = sibling.pos & 3;
    if (after) {
      first_pos++;
    }

    for (int i = last_pos; i > first_pos; --i) {
      data_stack[sibling.level][((sibling.pos >> 2) << 2) + i] = data_stack[sibling.level][((sibling.pos >> 2) << 2) + i - 1];

      ptrs.first_child[i] = ptrs.first_child[i - 1];
      ptrs.last_child[i]  = ptrs.last_child[i - 1];
      if (i != first_pos && ptrs.first_child[i] != -1) {
        I(ptrs.last_child[i] != -1);
        auto base = ((sibling.pos >> 2) << 2) + i - 1;
        adjust_parent_pointer(Tree_index(sibling.level + 1, ptrs.first_child[i]), base, base + 1);
      }
    }

    ptrs.first_child[first_pos] = -1;
    ptrs.last_child[first_pos]  = -1;
  }

  Tree_pos increase_size(const Tree_index &sibling) {
    auto *next_sibling = ref_next_sibling_pos(sibling);
    if (*next_sibling == 3) {
      (*next_sibling) = ((*next_sibling) >> 2) << 2;
      return ((sibling.pos >> 2) << 2) | 3;
    }
    unsigned char pos = *next_sibling;
    (*next_sibling)++;

    return ((sibling.pos >> 2) << 2) | (pos & 3);
  }

  void make_space_after(const Tree_index &younger, const Tree_index &older) {
    I(older.level == younger.level);
    I((older.pos & 3) == 0);  // Insert beginning
    I(get_parent(younger) == get_parent(older));

    make_space_after(older, false);

    data_stack[older.level][older.pos]                         = data_stack[younger.level][younger.pos | 3];  // oldest there
    pointers_stack[older.level][older.pos >> 2].first_child[0] = pointers_stack[younger.level][younger.pos >> 2].first_child[3];
    pointers_stack[older.level][older.pos >> 2].last_child[0]  = pointers_stack[younger.level][younger.pos >> 2].last_child[3];

    auto fc = pointers_stack[older.level][older.pos >> 2].first_child[0];
    if (fc != -1) {
      adjust_parent_pointer(Tree_index(older.level + 1, fc), younger.pos | 3, older.pos);
    }
    make_space_after(younger);
  }

  Tree_pos get_last_child_pos(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return pointers_stack[index.level][index.pos >> 2].last_child[index.pos & 3];
  }

  Tree_pos *ref_last_child_pos(const Tree_index &index) {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].last_child[index.pos & 3];
  }

  Tree_pos get_first_child_pos(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return pointers_stack[index.level][index.pos >> 2].first_child[index.pos & 3];
  }

  Tree_pos *ref_first_child_pos(const Tree_index &index) {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].first_child[index.pos & 3];
  }

  bool is_valid(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    auto pos = pointers_stack[index.level][index.pos >> 2].next_sibling;
    if ((pos & 3) > (index.pos & 3) || (pos & 3) == 0) {
      return true;  // Either the index is smaller than end or all the entries are used (pos==0)
    }

    return false;
  }

  Tree_pos get_next_sibling_pos(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return pointers_stack[index.level][index.pos >> 2].next_sibling;
  }

  Tree_pos *ref_next_sibling_pos(const Tree_index &index) {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    return &pointers_stack[index.level][index.pos >> 2].next_sibling;
  }

  bool is_last_next_sibling_chunk(const Tree_index &sibling) const {
    const auto next_sibling = get_next_sibling_pos(sibling);

    return (next_sibling >> 2) == 0;
  }

  bool has_next_sibling_space(const Tree_index &sibling) const {
    I(data_stack.size() > (size_t)sibling.level);
    I(data_stack[sibling.level].size() > (size_t)sibling.pos);
    I(pointers_stack[sibling.level].size() > (size_t)sibling.pos >> 2);

    const auto next_sibling = get_next_sibling_pos(sibling);

    return (next_sibling & 3) != 0;
  }

public:
  Tree_index get_last_child(const Tree_index &parent_index) const {
    Tree_index child_index(parent_index.level + 1, get_last_child_pos(parent_index));

    // GI(!child_index.is_invalid(), get_sibling_next(child_index).is_invalid());
    // Commented out by Abdullah

    return child_index;
  }

  size_t max_size() const {
    size_t sz = 0;
    for (const auto &ent : pointers_stack) {
      sz += ent.size();
    }

    return sz * 4;  // WARNING: 4x because pointers stack has 4x pointer
  }

  bool is_last_child(const Tree_index &self_index) const { return get_sibling_next(self_index) == invalid_index(); }

  bool is_first_child(const Tree_index &self_index) const { return get_first_child(get_parent(self_index)) == self_index; }

  Tree_index get_first_child(const Tree_index &parent_index) const {
    Tree_index child_index(parent_index.level + 1, get_first_child_pos(parent_index));

    return child_index;
  }

  Tree_index get_sibling_next(const Tree_index &sibling) const {
    const auto next_bucket_pos = get_next_sibling_pos(sibling);
    const auto next_offset     = next_bucket_pos & 3;
    const auto sibling_offset  = sibling.pos & 3;

    if (next_offset == 0 && sibling_offset != 3) {
      return Tree_index(sibling.level, sibling.pos + 1);
    }

    if (next_offset != 0 && next_offset > (sibling_offset + 1)) {
      return Tree_index(sibling.level, sibling.pos + 1);
    }

    if ((next_bucket_pos >> 2) == 0) {
      return invalid_index();  // No more siblings
    }

    return Tree_index(sibling.level, (next_bucket_pos >> 2) << 2);
  }

  Tree_index get_sibling_prev(const Tree_index &sibling) const {
    int pos = sibling.pos - 1;
    if (pos >= 0) {
      return Tree_index(sibling.level, pos);
    }

    auto       index = get_first_child(get_parent(sibling));
    Tree_index prev;
    while (true) {
      if (index == sibling) {
        return prev;  // Returns invalid if first chipd ast for prev
      }

      prev = index;

      index.pos = get_next_sibling_pos(index);
    }

    I(false);  // Previous traversal should always hit

    return invalid_index();
  }

  class Tree_depth_preorder_iterator {
  public:
    class CTree_depth_preorder_iterator {
    public:
      CTree_depth_preorder_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) { start_ti = _ti; }
      CTree_depth_preorder_iterator operator++() {
        CTree_depth_preorder_iterator i(ti, t);

        ti = t->get_depth_preorder_next(ti);
        if (ti.level == start_ti.level && ti.pos > start_ti.pos) {
          ti = invalid_index();
        }
        return i;
      };
      bool operator!=(const CTree_depth_preorder_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      Tree_index     start_ti;
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
    CTree_depth_preorder_iterator end() const {
      return CTree_depth_preorder_iterator(Tree_index(-1, -1), t);
    }  // 0 is end index for iterator
  };

  class Tree_depth_postorder_iterator {
  public:
    class CTree_depth_postorder_iterator {
    public:
      CTree_depth_postorder_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) {}
      CTree_depth_postorder_iterator operator++() {
        CTree_depth_postorder_iterator i(ti, t);

        ti = t->get_depth_postorder_next(ti);

        return i;
      };
      bool operator!=(const CTree_depth_postorder_iterator &other) {
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
    Tree_depth_postorder_iterator() = delete;
    explicit Tree_depth_postorder_iterator(const Tree_index &_b, const tree<X> *_t) : ti(_b), t(_t) {}

    CTree_depth_postorder_iterator begin() const { return CTree_depth_postorder_iterator(ti, t); }
    CTree_depth_postorder_iterator end() const {
      return CTree_depth_postorder_iterator(Tree_index(-1, -1), t);
    }  // 0 is end index for iterator
  };

  class Tree_sibling_iterator {
  public:
    class CTree_sibling_iterator {
    public:
      CTree_sibling_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) {}
      CTree_sibling_iterator operator++() {
        CTree_sibling_iterator i(ti, t);

        ti = t->get_sibling_next(ti);

        return i;
      };
      bool operator==(const CTree_sibling_iterator &other) const { return !(ti != other.ti); }

      bool operator!=(const CTree_sibling_iterator &other) const {
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
    Tree_sibling_iterator() = delete;
    explicit Tree_sibling_iterator(const Tree_index &_b, const tree<X> *_t) : ti(_b), t(_t) {}

    CTree_sibling_iterator begin() const { return CTree_sibling_iterator(ti, t); }
    CTree_sibling_iterator end() const { return CTree_sibling_iterator(invalid_index(), t); }
  };

  tree();
  tree(std::string_view _path, std::string_view _map_name);

  [[nodiscard]] inline std::string_view get_name() const { return mmap_name; }
  [[nodiscard]] inline std::string_view get_path() const { return mmap_path; }

  void clear() {
    pending_parent = -1;  // Nobody pending
    pending_child  = -1;  // Nobody pending

    data_stack.clear();
    pointers_stack.clear();
  }

  [[nodiscard]] bool empty() const { return data_stack.empty(); }

  // WARNING: can not return Tree_index & because future additions can move the pointer (vector realloc)
  Tree_index add_child(const Tree_index &parent, const X &data);
  bool       delete_leaf(const Tree_index &child);
  bool       delete_subtree(const Tree_index &child);
  Tree_index append_sibling(const Tree_index &sibling, const X &data);
  Tree_index insert_next_sibling(const Tree_index &sibling, const X &data);
  size_t     get_tree_width(const Tree_level &level) const {
    if (level >= data_stack.size()) {
      return 0;
    }

    return data_stack[level].size();
  }

  void set_data(const Tree_index &index, const X &data) {
    I((int)data_stack.size() > index.level);
    I((int)data_stack[index.level].size() > index.pos);

    data_stack[index.level][index.pos] = data;
  }

  X *ref_data(const Tree_index &leaf) {
    I((int)data_stack.size() > leaf.level);
    I((int)data_stack[leaf.level].size() > leaf.pos);

    return &data_stack[leaf.level][leaf.pos];
  }
  const X &get_data(const Tree_index &leaf) const {
    I(data_stack.size() > leaf.level);
    // I(data_stack[leaf.level].size() > leaf.pos);
    // Commented out by Abdullah
    // When deleting nodes, it became possible to have a higher pos value than
    // the number of nodes at that level

    return data_stack[leaf.level][leaf.pos];
  }

  Tree_index get_depth_preorder_next(const Tree_index &child) const;
  Tree_index get_depth_postorder_next(const Tree_index &child) const;

  Tree_index get_parent(const Tree_index &index) const {
    I(index.level < (int)pointers_stack.size());
    I((index.pos >> 2) < (int)pointers_stack[index.level].size());

    auto pos   = pointers_stack[index.level][index.pos >> 2].parent;
    auto level = index.level - 1;
    if (level < 0) {
      level = 0;  // TODO: cleaner to return invalid_index()
    }

    GI(index.level == 0, level == 0 && pos == 0);  // parent of root is root

    return Tree_index(level, pos);
  }

  static constexpr Tree_index invalid_index() { return Tree_index(-1, -1); }
  void                        set_root(const X &data);

  // NOTE: not a typical depth first traversal, goes to bottom without touching
  // parents first. It is also not a bottom-up traversal because it touches all
  // the tree level before going to next level
  void each_bottom_up_fast(std::function<void(const Tree_index &self, const X &)> fn) const;

  void each_top_down_fast(std::function<void(const Tree_index &self, const X &)> fn) const;

  // void each_bottom_up(std::function<void(const Tree_index &parent, const Tree_index &self, const X &)> fn) const;
  // void each_depth_first(const Tree_index &start_index, std::function<void(const Tree_index &parent, const Tree_index &self, const
  // X &)> fn) const
  Tree_index get_child(const Tree_index &start_index) const;

  // tree traversals: https://en.wikipedia.org/wiki/Tree_traversal
  Tree_depth_preorder_iterator depth_preorder(const Tree_index &start_index) const {
    return Tree_depth_preorder_iterator(start_index, this);
  }

  Tree_depth_preorder_iterator  depth_preorder() const { return Tree_depth_preorder_iterator(Tree_index::root(), this); }
  Tree_depth_postorder_iterator depth_postorder() const {
    auto last_child = Tree_index::root();
    while (!is_leaf(last_child)) {
      last_child = get_first_child(last_child);
    }
    return Tree_depth_postorder_iterator(last_child, this);
  }

  Tree_sibling_iterator siblings(const Tree_index &start_index) const { return Tree_sibling_iterator(start_index, this); }
  Tree_sibling_iterator children(const Tree_index &start_index) const {
    if (is_leaf(start_index)) {
      return Tree_sibling_iterator(invalid_index(), this);
    }

    return Tree_sibling_iterator(get_first_child(start_index), this);
  }

  bool is_leaf(const Tree_index &index) const { return (get_first_child_pos(index)) == -1; }

  bool is_root(const Tree_index &index) const { return index.is_root(); }

  bool has_single_child(const Tree_index &index) const {
    auto fc_pos = get_first_child_pos(index);

    if (fc_pos == -1) {
      return false;
    }

    auto lc_index = get_last_child(index);

    return fc_pos == lc_index.pos;
  }

  bool is_child_of(const Tree_index &child, const Tree_index &potential_parent) const {
    auto level_stop_at = potential_parent.level;
    // auto level              = child.level - 1;
    auto child_parent_chain = get_parent(child);

    while (child_parent_chain.level > level_stop_at) {
      child_parent_chain = get_parent(child_parent_chain);
    }

    return potential_parent == child_parent_chain;
  }

  /* LCOV_EXCL_START */
  void dump() const {
    for (const auto &index : depth_preorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\n", indent.c_str(), index.level, index.pos);
    }
  }

  void dump_data() const {
    for (const auto &index : depth_preorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\t", indent.c_str(), index.level, index.pos);
      std::cout << get_data(index) << std::endl;
    }
  }

  void check() const {
    for (const auto &index : depth_preorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\n", indent.c_str(), index.level, index.pos);
      if (index.level == 0) {  // skip root
        continue;
      }
      auto parent = get_parent(index);
      auto pos    = index.pos;
      while (true) {
        I(pointers_stack[index.level][pos >> 2].parent == parent.pos);

        pos = pointers_stack[index.level][pos >> 2].next_sibling;
        if ((pos >> 2) == 0) {
          if (pos == 0) {
            I(pointers_stack[parent.level][parent.pos >> 2].last_child[parent.pos & 3] == 3);
          } else {
            I(pointers_stack[parent.level][parent.pos >> 2].last_child[parent.pos & 3] == pos - 1);
          }
          return;
        }
      }
    }
  }

  void dump_postorder() const {
    for (const auto &index : depth_postorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\n", indent.c_str(), index.level, index.pos);
    }
  }

  void check_postorder() const {
    for (const auto &index : depth_postorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\n", indent.c_str(), index.level, index.pos);
      if (index.level == 0) {  // skip root
        continue;
      }
      auto parent = get_parent(index);
      auto pos    = index.pos;
      while (true) {
        I(pointers_stack[index.level][pos >> 2].parent == parent.pos);

        pos = pointers_stack[index.level][pos >> 2].next_sibling;
        if ((pos >> 2) == 0) {
          if (pos == 0) {
            I(pointers_stack[parent.level][parent.pos >> 2].last_child[parent.pos & 3] == 3);
          } else {
            I(pointers_stack[parent.level][parent.pos >> 2].last_child[parent.pos & 3] == pos - 1);
          }
          return;
        }
      }
    }
  }
  /* LCOV_EXCL_STOP */
};

//--------------------- Template Implementation ----

template <typename X>
void tree<X>::adjust_to_level(Tree_level level) {
  if (data_stack.size() > static_cast<size_t>(level)) {
    return;
  }

  while (data_stack.size() <= static_cast<size_t>(level)) {
    data_stack.emplace_back();
    pointers_stack.emplace_back();
  }
};

template <typename X>
tree<X>::tree(std::string_view _path, std::string_view _map_name)
    : mmap_path(_path.empty() ? "." : _path), mmap_name{std::string(_path) + std::string("/") + std::string(_map_name)} {
  if (mmap_path != ".") {
    struct stat sb;
    if (stat(mmap_path.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) {
      int e = mkdir(mmap_path.c_str(), 0755);
      I(e >= 0);
    }
  }

  pending_parent = -1;  // Nobody pending
  pending_child  = -1;  // Nobody pending
}

template <typename X>
tree<X>::tree() {
  pending_parent = -1;  // Nobody pending
  pending_child  = -1;  // Nobody pending
};

template <typename X>
bool tree<X>::delete_leaf(const Tree_index &child) {
  if (!is_leaf(child)) {
    // std::cout << get_data(child) << " is not a leaf" << std::endl;
    return false;
  }

  auto  parent        = get_parent(child);
  auto *parent_fc_pos = ref_first_child_pos(parent);
  auto *parent_lc_pos = ref_last_child_pos(parent);
  auto  prev_sibling  = get_sibling_prev(child);

  // std::cout << "next sibling of old: " << get_sibling_next(child).pos << std::endl;

  if (has_single_child(parent)) {
    // std::cout << get_data(child) << " has a single child parent" << std::endl;
    *parent_fc_pos = -1;
    *parent_lc_pos = -1;
  } else {
    // std::cout << get_data(child) << " does not have a single child parent" << std::endl;
    if (*parent_fc_pos == child.pos) {
      // std::cout << "fc" << std::endl;
      *parent_fc_pos = get_sibling_next(child).pos;
    }

    if (*parent_lc_pos == child.pos) {
      // std::cout << "lc" << std::endl;
      // std::cout << "child pos: " << child.pos << std::endl;
      // std::cout << "parent_lc_pos pointer: " << *parent_lc_pos << std::endl;
      auto prev_sibling_pos = prev_sibling.pos;

      for (const auto &index : siblings(get_first_child(parent))) {
        if (index.pos == prev_sibling_pos) {
          *parent_lc_pos = index.pos;
          break;
        }
      }
      // std::cout << "parent_lc_pos pointer: " << *parent_lc_pos << std::endl;
    }
  }

  // std::cout << "DELETED: level " << child.level << ", pos " << child.pos << "\n----------" << std::endl;

  // std::cout << "DATA STACK: " << data_stack[child.level][child.pos] << std::endl;
  // std::cout << "PTR STACK.parent: " << pointers_stack[child.level][child.pos >> 2].parent << std::endl;
  // std::cout << "PTR STACK.next_sibling: " << pointers_stack[child.level][child.pos >> 2].next_sibling << std::endl;

  // data_stack[child.level][child.pos] = "";
  // data_stack[child.level].erase(data_stack[child.level].begin() + child.pos);

  pointers_stack[child.level][child.pos >> 2].first_child[child.pos & 3] = -1;
  pointers_stack[child.level][child.pos >> 2].last_child[child.pos & 3]  = -1;
  // pointers_stack[child.level].erase(pointers_stack[child.level].begin() + (child.pos >> 2));

  return true;
}

template <typename X>
bool tree<X>::delete_subtree(const Tree_index &child) {
  auto  parent        = get_parent(child);
  auto *parent_lc_pos = ref_last_child_pos(parent);

  if (child.is_root()) {
    // std::cout << "DELETED: subtree at level " << child.level << ", pos " << child.pos << std::endl;
    clear();
    return true;
  }

  Tree_index x    = child;
  bool       done = false;
  while (!done) {
    if (is_leaf(child)) {
      break;
    }

    done    = true;
    auto fc = get_first_child(x).pos;

    while (fc != -1) {
      x  = get_first_child(x);
      fc = get_first_child(x).pos;
    }

    // std::cout << "Found next target: level " << x.level << ", pos " << x.pos << std::endl;
    x = get_parent(x);
    // std::cout << "Returned to parent: level " << x.level << ", pos " << x.pos << std::endl;

    done = !(delete_leaf(get_first_child(x)));
  }

  delete_leaf(child);

  // std::cout << "DELETED: subtree at level " << child.level << ", pos " << child.pos << std::endl;
  return true;
}

template <typename X>
Tree_index tree<X>::add_child(const Tree_index &parent, const X &data) {
  I((int)data_stack.size() > parent.level);
  I((int)data_stack[parent.level].size() > parent.pos);

  const auto child_level = parent.level + 1;

  adjust_to_level(child_level);

  auto *parent_lc_pos = ref_last_child_pos(parent);
  if (*parent_lc_pos != -1) {
    Tree_index sibling(child_level, *parent_lc_pos);
    return append_sibling(sibling, data);
  }

  auto  child_pos     = create_space(parent, data);
  auto *parent_fc_pos = ref_first_child_pos(parent);
  *parent_fc_pos      = child_pos;
  *parent_lc_pos      = child_pos;

  I(pending_parent != child_level);
  I(pending_child != child_level);

  Tree_index child(child_level, child_pos);
  I(get_next_sibling_pos(child) == 1);

  I(get_last_child(parent) == child);
  I(is_leaf(child));
  I(has_single_child(parent));
  I(get_parent(child) == parent);

  return child;
};

template <typename X>
Tree_index tree<X>::append_sibling(const Tree_index &sibling, const X &data) {
  I(sibling.level > 0);  // No siblings to root

  auto  parent        = get_parent(sibling);
  auto *parent_lc_pos = ref_last_child_pos(parent);

  Tree_index child(sibling.level, -1);

  if (has_next_sibling_space(sibling)) {
    I(is_last_next_sibling_chunk(sibling));
    auto *next_sibling = ref_next_sibling_pos(sibling);

    assert(((*next_sibling) >> 2) == 0);
    child.pos = ((sibling.pos >> 2) << 2) + *next_sibling;

    auto last_child_pos = increase_size(sibling);
    I(last_child_pos == child.pos);

    set_data(child, data);
  } else {
    child.pos          = create_space(parent, data);
    auto *next_sibling = ref_next_sibling_pos(sibling);
    *next_sibling      = child.pos;
    I(((*next_sibling) & 3) == 0);

    I(!is_leaf(parent));  // Add sibling, so already has child
  }

  I(!child.is_invalid());
  I(is_leaf(child));
  if (*parent_lc_pos == sibling.pos) {  // new child is the youngest now
    *parent_lc_pos = child.pos;
  }

  return child;
}

template <typename X>
Tree_index tree<X>::insert_next_sibling(const Tree_index &sibling, const X &data) {
  I(sibling.level > 0);  // No siblings to root

  auto  parent        = get_parent(sibling);
  auto *parent_lc_pos = ref_last_child_pos(parent);

  GI(get_first_child(parent).pos != -1, get_parent(get_first_child(parent)).pos == parent.pos);

  Tree_index child(sibling.level, -1);

  if (has_next_sibling_space(sibling)) {
    I((sibling.pos & 3) != 3);  // sibling can not be the last if there is space
    I(is_last_next_sibling_chunk(sibling));

    make_space_after(sibling);

    auto last_child_pos = increase_size(sibling);
    I(data_stack[sibling.level].size() > sibling.pos);

    child.pos = sibling.pos + 1;

    set_data(child, data);
    I(is_leaf(child));
    if (*parent_lc_pos >> 2 == sibling.pos >> 2) {  // new child is the youngest now
      *parent_lc_pos = last_child_pos;
    }

  } else {
    // WARNING: can not get ref, because the create can increse size and pointer is bogus
    auto npos = get_next_sibling_pos(sibling);

    if ((npos >> 2) != 0) {
      Tree_index next_bucket_child(sibling.level, (npos >> 2) << 2);
      I(pointers_stack[sibling.level][sibling.pos >> 2].parent == pointers_stack[sibling.level][next_bucket_child.pos >> 2].parent);
      if (has_next_sibling_space(next_bucket_child)) {  // No need to re-alloc.

        if ((sibling.pos & 3) == 3) {  // insert next bucket
          make_space_after(next_bucket_child, false);
          child.pos = next_bucket_child.pos;
        } else {
          make_space_after(sibling, next_bucket_child);
          child.pos = sibling.pos + 1;
        }
        auto last_child_pos = increase_size(next_bucket_child);
        set_data(child, data);
        I(is_leaf(child));

        if ((*parent_lc_pos) >> 2 == next_bucket_child.pos >> 2) {
          *parent_lc_pos = last_child_pos;
        }

        return child;
      }
    }

    Tree_index created_next_sibling(sibling.level, -1);

    if ((sibling.pos & 3) == 3) {  // last sibling in bucket
      created_next_sibling.pos = create_space(parent, data);

      child.pos = created_next_sibling.pos;
    } else {
      created_next_sibling.pos                        = create_space(parent, data_stack[sibling.level][sibling.pos | 3]);
      auto &l                                         = pointers_stack[created_next_sibling.level];
      l[created_next_sibling.pos >> 2].first_child[0] = l[sibling.pos >> 2].first_child[3];
      l[created_next_sibling.pos >> 2].last_child[0]  = l[sibling.pos >> 2].last_child[3];
      auto fc                                         = l[sibling.pos >> 2].first_child[3];
      if (fc != -1) {
        adjust_parent_pointer(Tree_index(sibling.level + 1, fc), sibling.pos | 3, created_next_sibling.pos);
      }

      make_space_after(sibling);

      child.pos = sibling.pos + 1;

      set_data(child, data);
      I(is_leaf(child));
    }
    I(pointers_stack[sibling.level][sibling.pos >> 2].parent
      == pointers_stack[sibling.level][created_next_sibling.pos >> 2].parent);

    // insert created in the next chain (if last *next_sibling>>2 is zero)
    auto *next_sibling = ref_next_sibling_pos(sibling);
    I(((*next_sibling) & 3) == 0);
    auto *next_next_sibling = ref_next_sibling_pos(created_next_sibling);
    *next_next_sibling |= *next_sibling;
    *next_sibling = created_next_sibling.pos;

    if ((*parent_lc_pos) >> 2 == sibling.pos >> 2) {  // new child is the youngest now
      *parent_lc_pos = created_next_sibling.pos;
    }

    I(!is_leaf(parent));  // Add sibling, so already has child
  }

  I(!child.is_invalid());
  I(is_leaf(child));

  return child;
}

template <typename X>
Tree_index tree<X>::get_depth_preorder_next(const Tree_index &child) const {
  I(child.level < (int)pointers_stack.size());
  I((child.pos >> 2) < (int)pointers_stack[child.level].size());

  auto fc = get_first_child_pos(child);
  if (fc != -1) {
    return Tree_index(child.level + 1, fc);
  }

  I(is_leaf(child));

  auto next = get_sibling_next(child);
  if (!next.is_invalid()) {
    return next;
  }

  // It was leaf, without more siblings. Go to parent with sibling
  auto parent = get_parent(child);
  while (!parent.is_root()) {
    auto parent_next = get_sibling_next(parent);
    if (!parent_next.is_invalid()) {
      return parent_next;
    }

    parent = get_parent(parent);
  }

  return invalid_index();
}

template <typename X>
Tree_index tree<X>::get_depth_postorder_next(const Tree_index &child) const {
  auto next_child = get_sibling_next(child);
  if (!next_child.is_invalid()) {
    while (!is_leaf(next_child)) {
      next_child = get_first_child(next_child);
    }
    return next_child;
  }
  if (is_root(child)) {
    return invalid_index();
  }

  auto parent = get_parent(child);
  return parent;
}

template <typename X>
void tree<X>::set_root(const X &data) {
  adjust_to_level(0);

  if (data_stack[0].empty()) {
    pointers_stack[0].emplace_back(0);
  } else {
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
      if (!is_valid(ti)) {
        continue;
      }

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
      if (!is_valid(ti)) {
        continue;
      }

      fn(ti, data_stack[i][j]);
    }
  }
}

template <typename X>
Tree_index tree<X>::get_child(const Tree_index &top) const {
  auto fc = get_first_child_pos(top);
  if (fc == -1) {
    return invalid_index();
  }

  return Tree_index(top.level + 1, fc);
}

}  // namespace lh
