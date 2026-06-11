//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <vector>

#include "ast_attrs.hpp"
#include "elab_scanner.hpp"
#include "hhds/tree.hpp"

using Rule_id = int;  // FIXME explicit_type

class Ast_parser_node {
public:
  Rule_id     rule_id;
  Token_entry token_entry;
  constexpr Ast_parser_node() : rule_id(0), token_entry(0) {}
  constexpr Ast_parser_node(const Rule_id rid, const Token_entry te) : rule_id(rid), token_entry(te) { I(rid); }
};

class Ast_parser {
public:
  using Tree_index = hhds::Tree::Node_class;

  Ast_parser(std::string_view buffer, Rule_id top_rule);

  void down();
  void up(Rule_id rid);
  void add(Rule_id rid, Token_entry te);

  void dump() const;

  std::string_view get_memblock() const;

  // Tree access (mirrors Lnast::tree() / get_root()).
  hhds::Tree&       tree() noexcept { return *tree_; }
  const hhds::Tree& tree() const noexcept { return *tree_; }
  Tree_index        get_root() const { return tree_->get_root_node(); }

  bool       is_root(const Tree_index& nid) const { return nid == get_root(); }
  Tree_index get_parent(const Tree_index& nid) const { return nid.parent(); }
  Tree_index get_first_child(const Tree_index& nid) const { return nid.first_child(); }
  Tree_index get_last_child(const Tree_index& nid) const { return nid.last_child(); }
  Tree_index get_sibling_next(const Tree_index& nid) const { return nid.next_sibling(); }
  Tree_index get_sibling_prev(const Tree_index& nid) const { return nid.prev_sibling(); }
  Tree_index get_child(const Tree_index& nid) const { return nid.first_child(); }

  auto children(const Tree_index& parent) const { return tree_->sibling_order(parent.first_child()); }
  auto depth_preorder() const { return tree_->pre_order(); }
  auto depth_preorder(const Tree_index& start) const { return tree_->pre_order(start); }

  // Mutation: same semantics as the legacy lhtree-based API.
  Tree_index set_root(const Ast_parser_node& n);
  Tree_index add_child(const Tree_index& parent, const Ast_parser_node& n);
  Tree_index append_sibling(const Tree_index& sibling, const Ast_parser_node& n);

  Ast_parser_node get_data(const Tree_index& nid) const;
  void            set_data(const Tree_index& nid, const Ast_parser_node& n);

  Rule_id     get_rule_id(const Tree_index& nid) const;
  Token_entry get_token_entry(const Tree_index& nid) const;
  void        set_rule_id(const Tree_index& nid, Rule_id rid);

protected:
  std::shared_ptr<hhds::Tree> tree_;
  int                         level;
  int                         down_added;
  const std::string_view      buffer;

  std::vector<Tree_index> last_added;

  void add_track_parent(const Tree_index& index, int idx_level);
};
