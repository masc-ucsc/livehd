//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <functional>
#include <vector>

#include "iassert.hpp"
#include "mmap_map.hpp"  // To add the hash for trees

namespace mmap_lib {

using Tree2_index   = uint64_t;

template <typename X>
class tree2 {
protected:
  const std::string         mmap_name;
  const std::string         mmap_path;

  std::vector<X>            data_stack;
  std::vector<int16_t>      level_stack;

  std::vector<X>            overflow_data_stack;
  std::vector<int16_t>      overflow_level_stack;

public:
  static constexpr Tree2_index invalid_index() { return Tree2_index(0); }
  static constexpr Tree2_index root_index()    { return Tree2_index(1); }

  const Tree2_index get_last_child(const Tree2_index parent_index) const;
  const Tree2_index get_first_child(const Tree2_index parent_index) const;

  const Tree2_index get_sibling_next(const Tree2_index sibling) const;

  const Tree2_index get_sibling_prev(const Tree2_index &sibling) const;

  class Tree2_depth_preorder_iterator {
  public:
    class CTree2_depth_preorder_iterator {
    public:
      CTree2_depth_preorder_iterator(const Tree2_index _ti, const tree2<X> *_t) : ti(_ti), t(_t) { start_ti = _ti; }
      CTree2_depth_preorder_iterator operator++() {
        CTree2_depth_preorder_iterator i(ti, t);

        ti = t->get_depth_preorder_next(ti);
        if (ti.level == start_ti.level && ti.pos > start_ti.pos) {
          ti = invalid_index();
        }
        return i;
      };
      bool operator!=(const CTree2_depth_preorder_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }
      const Tree2_index &operator*() const { return ti; }

    private:
      Tree2_index     ti;
      Tree2_index     start_ti;
      const tree2<X> *t;
    };

  private:
  protected:
    Tree2_index     ti;
    const tree2<X> *t;

  public:
    Tree2_depth_preorder_iterator() = delete;
    explicit Tree2_depth_preorder_iterator(const Tree2_index _b, const tree2<X> *_t) : ti(_b), t(_t) {}

    CTree2_depth_preorder_iterator begin() const { return CTree2_depth_preorder_iterator(ti, t); }
    CTree2_depth_preorder_iterator end() const {
      return CTree2_depth_preorder_iterator(get_root_index(), t);
    }  // 0 is end index for iterator
  };

  class Tree2_sibling_iterator {
  public:
    class CTree2_sibling_iterator {
    public:
      CTree2_sibling_iterator(const Tree2_index &_ti, const tree2<X> *_t) : ti(_ti), t(_t) {}
      CTree2_sibling_iterator operator++() {
        CTree2_sibling_iterator i(ti, t);

        ti = t->get_sibling_next(ti);

        return i;
      };
      bool operator!=(const CTree2_sibling_iterator &other) {
        I(t == other.t);
        return ti != other.ti;
      }

      bool operator==(const CTree2_sibling_iterator &other) {
        I(t == other.t);
        return ti == other.ti;
      }
      const Tree2_index &operator*() const { return ti; }

    private:
      Tree2_index     ti;
      const tree2<X> *t;
    };

  private:
  protected:
    Tree2_index     ti;
    const tree2<X> *t;

  public:
    Tree2_sibling_iterator() = delete;
    explicit Tree2_sibling_iterator(const Tree2_index &_b, const tree2<X> *_t) : ti(_b), t(_t) {}

    CTree2_sibling_iterator begin() const { return CTree2_sibling_iterator(ti, t); }
    CTree2_sibling_iterator end() const { return CTree2_sibling_iterator(invalid_index(), t); }
  };

  tree2() {
  };

  tree2(std::string_view _path, std::string_view _map_name)
  : mmap_path(_path.empty() ? "." : _path), mmap_name{std::string(_path) + std::string("/") + std::string(_map_name)} {
    if (mmap_path != ".") {
      struct stat sb;
      if (stat(mmap_path.c_str(), &sb) != 0 || !S_ISDIR(sb.st_mode)) {
        int e = mkdir(mmap_path.c_str(), 0755);
        I(e >= 0);
      }
    }
  }

  [[nodiscard]] inline std::string_view get_name() const { return mmap_name; }
  [[nodiscard]] inline std::string_view get_path() const { return mmap_path; }

  void clear() {
    data_stack.clear();
    pointers_stack.clear();

    overflow_data_stack.clear();
    overflow_pointers_stack.clear();
  }

  [[nodiscard]] bool empty() const { return data_stack.empty(); }

  // WARNING: can not return Tree2_index & because future additions can move the pointer (vector realloc)
  const Tree2_index add_child(const Tree2_index parent, const X &data);
  const bool        delete_leaf(const Tree2_index child);
  const bool        delete_subtree(const Tree2_index child);
  const Tree2_index append_sibling(const Tree2_index sibling, const X &data);
  const Tree2_index insert_next_sibling(const Tree2_index sibling, const X &data);
  size_t            get_tree_width(const Tree2_level level) const;

  void set_data(const Tree2_index index, const X &data) {
    I((int)data_stack.size() > index);

    data_stack[index] = data;
  }

  X *ref_data(const Tree2_index leaf) {
    I((int)data_stack.size() > leaf);

    return &data_stack[leaf];
  }

  const X &get_data(const Tree2_index leaf) const {
    I(data_stack.size() > leaf);

    return data_stack[leaf];
  }

  const Tree2_index get_depth_preorder_next(const Tree2_index child) const;

  const Tree2_index get_parent(const Tree2_index index) const;

  void                        set_root(const X &data);

  const Tree2_index get_child(const Tree2_index start_index) const;

  // tree traversals: https://en.wikipedia.org/wiki/Tree2_traversal
  Tree2_depth_preorder_iterator depth_preorder(const Tree2_index start_index) const {
    return Tree2_depth_preorder_iterator(start_index, this);
  }

  Tree2_depth_preorder_iterator  depth_preorder() const { return Tree2_depth_preorder_iterator(Tree2_index::root(), this); }

  Tree2_sibling_iterator siblings(const Tree2_index &start_index) const { return Tree2_sibling_iterator(start_index, this); }
  Tree2_sibling_iterator children(const Tree2_index &start_index) const {
    if (is_leaf(start_index))
      return Tree2_sibling_iterator(invalid_index(), this);

    return Tree2_sibling_iterator(get_first_child(start_index), this);
  }

  bool is_leaf(const Tree2_index index) const;
  bool is_root(const Tree2_index index) constexpr { return index == 1; }
  bool is_invalid(const Tree2_index index) constexpr { return index == 0; }

  bool has_single_child(const Tree2_index index) const;

  bool is_child_of(const Tree2_index child, const Tree2_index potential_parent) const;

  /* LCOV_EXCL_START */
  void dump() const {
    for (const auto &index : depth_preorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\n", indent.c_str(), level_stack[indexi], index);
    }
  }

  void dump_data() const {
    for (const auto &index : depth_preorder()) {
      std::string indent(index.level, ' ');
      printf("%s l:%d p:%d\t", indent.c_str(), level_stack[index], index);
      std::cout << get_data(index) << std::endl;
    }
  }
  /* LCOV_EXCL_STOP */
};

}  // namespace mmap_lib
