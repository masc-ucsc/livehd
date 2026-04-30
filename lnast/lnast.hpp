//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "hhds/attrs/name.hpp"
#include "hhds/tree.hpp"
#include "lnast_attrs.hpp"
#include "lnast_ntype.hpp"

// Local replacements for the legacy `(level, pos)` accessors. `level_of`
// walks the parent chain, so it's diagnostic-only — algorithmic users
// (pass/locator) carry depth along their pre-order walk instead.
inline int32_t level_of(const hhds::Tree::Node_class& nid) {
  int32_t d = 0;
  auto    p = nid.parent();
  while (p.is_valid()) {
    ++d;
    p = p.parent();
  }
  return d;
}
inline int64_t pos_of(const hhds::Tree::Node_class& nid) { return nid.get_class_index().value; }

using Lnast_nid = hhds::Tree::Node_class;

// Lightweight transient bundle: tree slot is the source of truth for `type`
// after `Lnast::set_data` runs; this struct is only used to carry
// (type, name) into add_child / set_root / append_sibling. LoC fields
// (line/pos/fname) are NOT carried here — set them via Lnast::set_loc on
// the resulting nid when needed.
//
// `name` is owned (std::string) so the int-form `create_const(int64_t)` can
// stringify safely. SSO covers the common case (short var names, "nil",
// "true", small constants).
#define CREATE_LNAST_NODE(type)                                                                                  \
  static Lnast_node create_##type() { return Lnast_node(Lnast_ntype::create_##type(), std::string{}); }          \
  static Lnast_node create_##type(std::string_view str) { return Lnast_node(Lnast_ntype::create_##type(), std::string{str}); }

struct Lnast_node {
  Lnast_ntype type;
  std::string name;

  Lnast_node() : type(Lnast_ntype::create_invalid()) {}
  Lnast_node(Lnast_ntype _type) : type(_type) {}
  Lnast_node(Lnast_ntype _type, std::string _name) : type(_type), name(std::move(_name)) { I(!type.is_invalid()); }

  constexpr bool is_invalid() const { return type.is_invalid(); }
  void           dump() const;

#define LNAST_NODE(NAME, VERBAL) CREATE_LNAST_NODE(NAME)
#include "lnast_nodes.def"

  static Lnast_node create_const(int64_t v) {
    return Lnast_node(Lnast_ntype::create_const(), std::to_string(v));
  }
};

class Lnast {
private:
  // Forest must outlive the tree — HHDS Tree::forest_ptr is a raw pointer
  // and TreeIO::forest_owner_ is weak, so dropping our shared_ptr would
  // leave forest_ptr dangling.
  std::shared_ptr<hhds::Forest> forest_;
  std::shared_ptr<hhds::TreeIO> treeio_;
  std::shared_ptr<hhds::Tree>   tree_;
  std::string                   top_module_name;
  std::string                   source_filename;
  Lnast_nid                     undefined_var_nid;

public:
  static constexpr char version[] = "0.1.0";

  explicit Lnast() : Lnast("noname", "") {}
  explicit Lnast(std::string_view _module_name) : Lnast(_module_name, "") {}
  Lnast(std::string_view _module_name, std::string_view _file_name);
  // Wrap an already-built unattached tree (no Forest, no TreeIO). Used by
  // the upass runner to wrap a `Forest::create_tree_temp` body so the
  // existing add_child / set_data API drives staging emission. The wrapper
  // cannot be replaced (no TreeIO); replace_body() asserts on this Lnast.
  Lnast(std::shared_ptr<hhds::Tree> body, std::string_view _module_name);
  ~Lnast();

  // ── tree access ─────────────────────────────────────────────────────────
  hhds::Tree&                          tree() noexcept { return *tree_; }
  const hhds::Tree&                    tree() const noexcept { return *tree_; }
  std::shared_ptr<hhds::Tree>          tree_ptr() const noexcept { return tree_; }
  const std::shared_ptr<hhds::Forest>& forest() const noexcept { return forest_; }
  const std::shared_ptr<hhds::TreeIO>& treeio() const noexcept { return treeio_; }

  // Atomically swap the tree body backing this Lnast. `new_body` must be an
  // unattached tree (e.g. from `forest()->create_tree_temp(...)` or
  // `tree_ptr()->clone()`). Slot identity (Tid + name) is preserved; any
  // other holder of this Lnast picks up the new body on next access.
  // Asserts when called on a Lnast constructed without a TreeIO.
  void replace_body(std::shared_ptr<hhds::Tree> new_body);

  Lnast_nid get_root() const { return tree_->get_root_node(); }

  // ── navigation forwarders (operate on Node_class internally) ────────────
  bool      is_root(const Lnast_nid& nid) const { return nid == get_root(); }
  bool      is_leaf(const Lnast_nid& nid) const { return nid.is_leaf(); }
  bool      is_first_child(const Lnast_nid& nid) const { return nid.is_first_child(); }
  bool      is_last_child(const Lnast_nid& nid) const { return nid.is_last_child(); }
  Lnast_nid get_parent(const Lnast_nid& nid) const { return nid.parent(); }
  Lnast_nid get_first_child(const Lnast_nid& nid) const { return nid.first_child(); }
  Lnast_nid get_last_child(const Lnast_nid& nid) const { return nid.last_child(); }
  Lnast_nid get_sibling_next(const Lnast_nid& nid) const { return nid.next_sibling(); }
  Lnast_nid get_sibling_prev(const Lnast_nid& nid) const { return nid.prev_sibling(); }
  Lnast_nid get_child(const Lnast_nid& nid) const { return nid.first_child(); }

  // True iff the node has exactly one child.
  bool has_single_child(const Lnast_nid& nid) const {
    auto fc = nid.first_child();
    return fc.is_valid() && fc.is_last_child();
  }

  // ── iteration ───────────────────────────────────────────────────────────
  // children(parent): visit each direct child of parent.
  auto children(const Lnast_nid& parent) const { return tree_->sibling_order(parent.first_child()); }
  // depth_preorder(start): walk subtree in pre-order. Yields Node_class.
  auto depth_preorder(const Lnast_nid& start) const { return tree_->pre_order(start); }
  auto depth_preorder() const { return tree_->pre_order(); }
  // depth_postorder uses HHDS's non-const post_order range; callers needing
  // post-order traversal should reach into the Node_class API directly.

  // ── mutation ────────────────────────────────────────────────────────────
  // set_root(node): create the root and stamp `node`'s payload onto it.
  Lnast_nid set_root(const Lnast_node& n);
  // add_child(parent, node): append `node` as a new last-child of `parent`.
  Lnast_nid add_child(const Lnast_nid& parent, const Lnast_node& n);
  // append_sibling(sibling, node): insert `node` right after `sibling`.
  Lnast_nid append_sibling(const Lnast_nid& sibling, const Lnast_node& n);

  // ── payload accessors ───────────────────────────────────────────────────
  Lnast_ntype      get_type(const Lnast_nid& nid) const;
  void             set_type(const Lnast_nid& nid, Lnast_ntype t);
  std::string_view get_name(const Lnast_nid& nid) const;
  std::string_view get_vname(const Lnast_nid& nid) const { return get_name(nid); }
  void             set_name(const Lnast_nid& nid, std::string_view name);

  // LoC payload — read/write the per-node source-position attributes
  // independently of the name/type bundle. Most callers don't need this;
  // diagnostic prints and the parser do.
  struct Loc {
    uint64_t pos1{0};
    uint64_t pos2{0};
    uint32_t line{0};
    uint8_t  tok{0};
  };
  Loc              get_loc(const Lnast_nid& nid) const;
  void             set_loc(const Lnast_nid& nid, const Loc& loc);
  std::string_view get_fname(const Lnast_nid& nid) const;
  void             set_fname(const Lnast_nid& nid, std::string_view fname);

  // get_data / set_data: bundle accessors for code that wants the legacy
  // Lnast_node value at once. Performs multiple attribute lookups; prefer the
  // narrow accessors above on hot paths.
  Lnast_node get_data(const Lnast_nid& nid) const;
  void       set_data(const Lnast_nid& nid, const Lnast_node& n);

  // ── module / source metadata ────────────────────────────────────────────
  std::string_view get_top_module_name() const { return top_module_name; }
  std::string_view get_source() const { return source_filename; }
  void             set_top_module_name(std::string_view name) { top_module_name = name; }

  // ── name predicates (work off the textual name only) ────────────────────
  static bool is_register(std::string_view name) { return !name.empty() && name.front() == '#'; }
  static bool is_output(std::string_view name) { return !name.empty() && name.front() == '%'; }
  static bool is_input(std::string_view name) { return !name.empty() && name.front() == '$'; }
  static bool is_tmp(std::string_view name) { return name.size() >= 3 && name.substr(0, 3) == "___"; }

  // ── dump ────────────────────────────────────────────────────────────────
  void dump(const Lnast_nid& root_nid) const;
  void dump() const { dump(get_root()); }

  template <typename... Args>
  static void info(std::format_string<Args...> format, Args&&... args) {
    auto txt = std::format(format, std::forward<Args>(args)...);
    std::print("info:{}\n", txt);
  }
  static void info(std::string_view txt) { std::print("info:{}\n", txt); }

  class error : public std::runtime_error {
  public:
    template <typename... Args>
    error(std::format_string<Args...> format, Args&&... args)
        : std::runtime_error(std::format(format, std::forward<Args>(args)...)) {
      std::print("error:lnast {}\n", what());
      throw std::runtime_error(std::string(what()));
    }
    error(std::string_view txt) : std::runtime_error(std::string(txt)) {
      std::print("error:lnast {}\n", what());
      throw std::runtime_error(std::string(what()));
    }
  };
};
