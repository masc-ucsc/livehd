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
#include "absl/container/flat_hash_set.h"
#include "absl/container/node_hash_map.h"
#include "absl/strings/str_cat.h"
#include "elab_scanner.hpp"
#include "hhds/attrs/name.hpp"
#include "hhds/tree.hpp"
#include "lnast_attrs.hpp"
#include "lnast_ntype.hpp"
#include "tree_compat.hpp"  // level_of / pos_of helpers

using Lnast_nid = hhds::Tree::Node_class;

using Phi_rtable      = absl::flat_hash_map<std::string, Lnast_nid>;  // rtable = resolve_table
using Cnt_rtable      = absl::flat_hash_map<std::string, int16_t>;
using Selc_lrhs_table = absl::flat_hash_map<Lnast_nid, Lnast_nid>;  // sel -> paired opr node

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
  int16_t     subs;  // ssa subscript

  Lnast_node() : type(Lnast_ntype::create_invalid()), subs(0) {}
  Lnast_node(Lnast_ntype _type) : type(_type), subs(0) {}
  Lnast_node(Lnast_ntype _type, const State_token& _token) : type(_type), token(_token), subs(0) {}
  Lnast_node(Lnast_ntype _type, const State_token& _token, int16_t _subs) : type(_type), token(_token), subs(_subs) {
    I(!type.is_invalid());
  }

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
  static inline int             trace_module_cnt = 0;

  void      do_ssa_trans(const Lnast_nid& top_nid);
  void      ssa_lhs_handle_a_statement(const Lnast_nid& psts_nid, const Lnast_nid& opr_nid);
  void      ssa_rhs_handle_a_statement(const Lnast_nid& psts_nid, const Lnast_nid& opr_nid);
  void      ssa_lhs_if_subtree(const Lnast_nid& if_nid);
  void      ssa_rhs_if_subtree(const Lnast_nid& if_nid);
  void      opr_lhs_merge_if_subtree(const Lnast_nid& if_nid);
  void      opr_lhs_merge_handle_a_statement(const Lnast_nid& opr_nid);
  void      ssa_handle_phi_nodes(const Lnast_nid& if_nid);
  void      resolve_phi_nodes(const Lnast_nid& cond_nid, Phi_rtable& true_table, Phi_rtable& false_table);
  void      update_phi_resolve_table(const Lnast_nid& psts_nid, const Lnast_nid& target_nid);
  bool      has_else_stmts(const Lnast_nid& if_nid);
  void      add_phi_node(const Lnast_nid& cond_nid, const Lnast_nid& t_nid, const Lnast_nid& f_nid);
  Lnast_nid get_complement_nid(std::string_view brother_name, const Lnast_nid& psts_nid, bool false_path);
  Lnast_nid check_phi_table_parents_chain(std::string_view brother_name, const Lnast_nid& psts_nid);
  void      resolve_ssa_lhs_subs(const Lnast_nid& psts_nid);
  void      resolve_ssa_rhs_subs(const Lnast_nid& psts_nid);
  void      opr_lhs_merge(const Lnast_nid& psts_nid);
  void      update_global_lhs_ssa_cnt_table(const Lnast_nid& target_nid);
  void      respect_latest_global_lhs_ssa(const Lnast_nid& target_nid);
  int8_t    check_rhs_cnt_table_parents_chain(const Lnast_nid& psts_nid, const Lnast_nid& target_key);
  void      update_rhs_ssa_cnt_table(const Lnast_nid& psts_nid, const Lnast_nid& target_key);
  void      analyze_selc_lrhs(const Lnast_nid& psts_nid);
  void      analyze_selc_lrhs_if_subtree(const Lnast_nid& if_nid);
  void      analyze_selc_lrhs_handle_a_statement(const Lnast_nid& psts_nid, const Lnast_nid& opr_nid);
  void      insert_implicit_dp_parent(const Lnast_nid& opr_nid);

  void ssa_rhs_handle_a_operand(const Lnast_nid& gpsts_nid, const Lnast_nid& opd_nid);

  // tuple operator process
  void trans_tuple_opr(const Lnast_nid& pats_nid);
  void trans_tuple_opr_if_subtree(const Lnast_nid& if_nid);
  void merge_tconcat_paired_assign(const Lnast_nid& psts_nid, const Lnast_nid& concat_nid);

  // hierarchical statements node -> symbol table
  absl::node_hash_map<Lnast_nid, Phi_rtable>      phi_resolve_tables;
  absl::node_hash_map<Lnast_nid, Cnt_rtable>      ssa_rhs_cnt_tables;
  absl::node_hash_map<Lnast_nid, Selc_lrhs_table> selc_lrhs_tables;
  absl::node_hash_map<Lnast_nid, Phi_rtable>      new_added_phi_node_tables;
  absl::flat_hash_set<std::string>                tuplized_table;
  absl::flat_hash_map<std::string, Lnast_nid>     candidates_update_phi_resolve_table;
  absl::flat_hash_map<std::string, int16_t>       global_ssa_lhs_cnt_table;

  // populated during LG->LN pass, maps name -> bitwidth
  absl::flat_hash_map<std::string, uint32_t> from_lgraph_bw_table;

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
  Lnast_ntype     get_type(const Lnast_nid& nid) const;
  void            set_type(const Lnast_nid& nid, Lnast_ntype t);
  State_token     get_token(const Lnast_nid& nid) const;
  void            set_token(const Lnast_nid& nid, const State_token& tok);
  int16_t         get_subs(const Lnast_nid& nid) const;
  void            set_subs(const Lnast_nid& nid, int16_t v);
  std::string_view get_name(const Lnast_nid& nid) const;
  std::string_view get_vname(const Lnast_nid& nid) const { return get_name(nid); }
  std::string      get_sname(const Lnast_nid& nid) const;

  // get_data / set_data: bundle accessors for code that wants the legacy
  // Lnast_node value at once. Performs multiple attribute lookups; prefer the
  // narrow accessors above on hot paths.
  Lnast_node get_data(const Lnast_nid& nid) const;
  void       set_data(const Lnast_nid& nid, const Lnast_node& n);

  // ── ssa transform ───────────────────────────────────────────────────────
  void ssa_trans() { do_ssa_trans(get_root()); }

  // ── module / source metadata ────────────────────────────────────────────
  std::string_view get_top_module_name() const { return top_module_name; }
  std::string_view get_source() const { return source_filename; }
  void             set_top_module_name(std::string_view name) { top_module_name = name; }

  // ── name predicates (work off the textual name only) ────────────────────
  static bool is_register(std::string_view name) { return !name.empty() && name.front() == '#'; }
  static bool is_output(std::string_view name) { return !name.empty() && name.front() == '%'; }
  static bool is_input(std::string_view name) { return !name.empty() && name.front() == '$'; }
  static bool is_tmp(std::string_view name) { return name.size() >= 3 && name.substr(0, 3) == "___"; }

  // ── bitwidth side-table (populated during LG->LN) ───────────────────────
  bool     is_in_bw_table(std::string_view name) const;
  uint32_t get_bitwidth(std::string_view name) const;
  void     set_bitwidth(std::string_view name, uint32_t bitwidth);

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
