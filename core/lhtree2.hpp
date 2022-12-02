//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

// Not same, but similar idea and different representation:
//
// Meyerovich, Leo A., Todd Mytkowicz, and Wolfram Schulte. "Data parallel programming for irregular tree computations." (2011).

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

unsing Tree_index = uint32_t;

class Tree2 {
protected:
public:
  Tree_index get_last_child(const Tree_index &parent_index) const;

  size_t max_size() const;

  bool is_last_child(const Tree_index &self_index) const;

  bool is_first_child(const Tree_index &self_index) const;

  Tree_index get_first_child(const Tree_index &parent_index) const;

  Tree_index get_sibling_next(const Tree_index &sibling) const;

  Tree_index get_sibling_prev(const Tree_index &sibling) const;

#if 0
  class Tree_depth_preorder_iterator {
  public:
    class CTree_depth_preorder_iterator {
    public:
      CTree_depth_preorder_iterator(const Tree_index &_ti, const tree<X> *_t) : ti(_ti), t(_t) { start_ti = _ti; }
      CTree_depth_preorder_iterator operator++();
      bool operator!=(const CTree_depth_preorder_iterator &other);
      const Tree_index &operator*() const { return ti; }

    private:
      Tree_index     ti;
      Tree_index     start_ti;
      const Tree2 *t;
    };
  public:
    Tree_depth_preorder_iterator() = delete;
    explicit Tree_depth_preorder_iterator(const Tree_index &_b, const tree<X> *_t) : ti(_b), t(_t) {}

    CTree_depth_preorder_iterator begin() const;
    CTree_depth_preorder_iterator end() const;
  };
#endif

#if 0
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
      bool operator!=(const CTree_sibling_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }

      bool operator==(const CTree_sibling_iterator &other) {
        I(t == other.t);
        return ti == other.ti;
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
#endif

  Tree2();
  Tree2(std::string_view _path, std::string_view _map_name);

  [[nodiscard]] inline std::string_view get_name() const { return mmap_name; }
  [[nodiscard]] inline std::string_view get_path() const { return mmap_path; }

  void clear();

  [[nodiscard]] bool empty() const;

  // WARNING: can not return Tree_index & because future additions can move the pointer (vector realloc)
  Tree_index add_child(const Tree_index &parent, const X &data);
  Tree_index append_sibling(const Tree_index &sibling, const X &data);
  Tree_index insert_next_sibling(const Tree_index &sibling, const X &data);

  Tree_index get_depth_preorder_next(const Tree_index &child) const;
  Tree_index get_depth_postorder_next(const Tree_index &child) const;

  Tree_index get_parent(const Tree_index &index) const;

  static constexpr Tree_index invalid_index() { return Tree_index(-1, -1); }

  Tree_index get_child(const Tree_index &start_index) const;

#if 0
  void each_bottom_up_fast(std::function<void(const Tree_index &self, const X &)> fn) const;
  void each_top_down_fast(std::function<void(const Tree_index &self, const X &)> fn) const;

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
    if (is_leaf(start_index))
      return Tree_sibling_iterator(invalid_index(), this);

    return Tree_sibling_iterator(get_first_child(start_index), this);
  }
#endif

  bool is_leaf(const Tree_index &index) const;
  bool is_root(const Tree_index &index) const;
  bool has_single_child(const Tree_index &index) const;

  /* LCOV_EXCL_START */
  void dump() const;
  /* LCOV_EXCL_STOP */
};
