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
#include "elab_scanner.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/tree.hpp"
#include "lnast_attrs.hpp"
#include "lnast_ntype.hpp"
#include "tree_compat.hpp"  // level_of / pos_of helpers

using Lnast_nid = hhds::Tree::Node_class;

#define CREATE_LNAST_NODE(type)                                                                                                 \
  static Lnast_node create_##type() { return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, 0, "")); }           \
  static Lnast_node create_##type(std::string_view str) {                                                                       \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, 0, str));                                              \
  }                                                                                                                             \
  static Lnast_node create_##type(std::string_view str, uint32_t line_num) {                                                    \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, 0, 0, line_num, str));                                       \
  }                                                                                                                             \
  static Lnast_node create_##type(std::string_view str, uint32_t line_num, uint64_t pos1, uint64_t pos2) {                      \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, pos1, pos2, line_num, str));                                 \
  }                                                                                                                             \
  static Lnast_node create_##type(const State_token& new_token) { return Lnast_node(Lnast_ntype::create_##type(), new_token); } \
  static Lnast_node create_##type(std::string_view str, uint32_t line_num, uint64_t pos1, uint64_t pos2, std::string fname) {   \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, pos1, pos2, line_num, str, fname));                          \
  }                                                                                                                             \
  static Lnast_node create_##type(std::string_view str, uint64_t pos1, uint64_t pos2, std::string fname) {                      \
    return Lnast_node(Lnast_ntype::create_##type(), State_token(0, pos1, pos2, 0, str, fname));                                 \
  }

struct Lnast_node {
  Lnast_ntype type;
  State_token token;

  Lnast_node() : type(Lnast_ntype::create_invalid()) {}
  Lnast_node(Lnast_ntype _type) : type(_type) {}
  Lnast_node(Lnast_ntype _type, const State_token& _token) : type(_type), token(_token) { I(!type.is_invalid()); }

  constexpr bool is_invalid() const { return type.is_invalid(); }
  void           dump() const;

#define LNAST_NODE(NAME, VERBAL) CREATE_LNAST_NODE(NAME)
#include "lnast_nodes.def"

  static Lnast_node create_const(int64_t v) {
    return Lnast_node(Lnast_ntype::create_const(), State_token(0, 0, 0, 0, std::to_string(v)));
  }
};

class Lnast {
private:
  std::shared_ptr<hhds::Forest> forest_;
  std::shared_ptr<hhds::Tree>   tree_;
  std::string                   top_module_name;
  std::string                   source_filename;
  Lnast_nid                     undefined_var_nid;

public:
  static constexpr char version[] = "0.1.0";

  explicit Lnast() : Lnast("noname", "") {}
  explicit Lnast(std::string_view _module_name) : Lnast(_module_name, "") {}
  Lnast(std::string_view _module_name, std::string_view _file_name);
  ~Lnast();

  // ── tree access ─────────────────────────────────────────────────────────
  hhds::Tree&                          tree() noexcept { return *tree_; }
  const hhds::Tree&                    tree() const noexcept { return *tree_; }
  std::shared_ptr<hhds::Tree>          tree_ptr() const noexcept { return tree_; }
  const std::shared_ptr<hhds::Forest>& forest() const noexcept { return forest_; }

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
  State_token      get_token(const Lnast_nid& nid) const;
  void             set_token(const Lnast_nid& nid, const State_token& tok);
  std::string_view get_name(const Lnast_nid& nid) const;
  std::string_view get_vname(const Lnast_nid& nid) const { return get_name(nid); }

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
