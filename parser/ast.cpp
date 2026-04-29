//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ast.hpp"

#include <print>

namespace {
struct Ast_attr_init {
  Ast_attr_init() { hhds::register_attr_tag<ast_parser::attrs::token_entry_t>("ast.token_entry"); }
};
[[maybe_unused]] const Ast_attr_init ast_attr_init_{};
}  // namespace

Ast_parser::Ast_parser(std::string_view _buffer, Rule_id top_rule) : tree_(hhds::Tree::create()), buffer(_buffer) {
  auto root = set_root(Ast_parser_node(top_rule, 0));
  add_track_parent(root, 0);

  level = 0;
}

void Ast_parser::add_track_parent(const Tree_index& index, int idx_level) {
  for (int i = static_cast<int>(last_added.size()); i < idx_level + 1; ++i) {
    last_added.emplace_back();  // default-constructed = invalid
  }
  I(static_cast<int>(last_added.size()) > idx_level);

  down_added             = idx_level;
  last_added[down_added] = index;
}

void Ast_parser::down() { level = level + 1; }

void Ast_parser::up(Rule_id rid_up) {
  I(level > 0);

  if (down_added == level) {
    // Child was added at this level — fix its rule_id if still 0.
    if (get_rule_id(last_added[level]) == 0) {
      set_rule_id(last_added[level], rid_up);
    }
    last_added[down_added] = Tree_index();  // invalidate (helps asserts)
  } else if (down_added > level) {
    I((down_added - 1) == level);
    I(!last_added[level].is_invalid());
    set_data(last_added[down_added], Ast_parser_node(rid_up, 0));
    down_added = level;
  } else {
    I(down_added < level);
    I(static_cast<int>(last_added.size()) <= level);
    level = level - 1;
    return;
  }

  level = level - 1;
  if (static_cast<int>(last_added.size()) > level && level > 0) {
    last_added.pop_back();
    I(static_cast<int>(last_added.size()) == level + 1);
  }
  if (down_added > level) {
    down_added--;
    I(down_added == level);
  }
}

void Ast_parser::add(Rule_id rule_id, Token_entry te) {
  I(level > 0, "no rule add for ast root");

  I(static_cast<int>(last_added.size()) >= down_added);

  // Populate intermediate parents if the parsing level jumped down.
  for (int i = down_added; i < level - 1; ++i) {
    I(!last_added[i].is_invalid());
    auto child_index = add_child(last_added[i], Ast_parser_node());
    add_track_parent(child_index, i + 1);
  }
  I(down_added + 1 >= level);

  if (down_added == level) {
    auto child_index = append_sibling(last_added.back(), Ast_parser_node(rule_id, te));
    add_track_parent(child_index, level);
  } else {
    auto child_index = add_child(last_added.back(), Ast_parser_node(rule_id, te));
    add_track_parent(child_index, level);
  }
}

void Ast_parser::dump() const {
  // Walk preorder. Depth is recovered via parent walks (cheap for diagnostic
  // dumps; the parser doesn't carry an explicit depth on each node anymore).
  for (const auto& index : depth_preorder()) {
    int         depth = 0;
    auto        p     = index.parent();
    while (p.is_valid()) {
      ++depth;
      p = p.parent();
    }
    std::string indent(depth, ' ');
    std::print("{} l:{} p:{} rule_id:{}\n", indent, depth, index.get_class_index().value, get_rule_id(index));
  }
}

std::string_view Ast_parser::get_memblock() const { return buffer; }

// ─── Mutation forwarders ───────────────────────────────────────────────

Ast_parser::Tree_index Ast_parser::set_root(const Ast_parser_node& n) {
  auto root = tree_->add_root_node();
  set_data(root, n);
  return root;
}

Ast_parser::Tree_index Ast_parser::add_child(const Tree_index& parent, const Ast_parser_node& n) {
  auto child = parent.add_child();
  set_data(child, n);
  return child;
}

Ast_parser::Tree_index Ast_parser::append_sibling(const Tree_index& sibling, const Ast_parser_node& n) {
  auto next = sibling.append_sibling();
  set_data(next, n);
  return next;
}

// ─── Payload accessors ────────────────────────────────────────────────

Ast_parser_node Ast_parser::get_data(const Tree_index& nid) const {
  return Ast_parser_node(get_rule_id(nid), get_token_entry(nid));
}

void Ast_parser::set_data(const Tree_index& nid, const Ast_parser_node& n) {
  set_rule_id(nid, n.rule_id);
  nid.attr(ast_parser::attrs::token_entry).set(static_cast<uint32_t>(n.token_entry.value));
}

Rule_id Ast_parser::get_rule_id(const Tree_index& nid) const { return static_cast<Rule_id>(nid.get_type()); }

Token_entry Ast_parser::get_token_entry(const Tree_index& nid) const {
  auto ref = nid.attr(ast_parser::attrs::token_entry);
  return ref.has() ? Token_entry(ref.get()) : Token_entry(0);
}

void Ast_parser::set_rule_id(const Tree_index& nid, Rule_id rid) { nid.set_type(static_cast<hhds::Type>(rid)); }
