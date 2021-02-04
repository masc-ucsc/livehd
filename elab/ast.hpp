//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "elab_scanner.hpp"
#include "mmap_tree.hpp"

using Rule_id = int;  // FIXME explicit_type

class Ast_parser_node {
public:
  Rule_id     rule_id;
  Token_entry token_entry;
  constexpr Ast_parser_node() : rule_id(0), token_entry(0) {}
  constexpr Ast_parser_node(const Rule_id rid, const Token_entry te) : rule_id(rid), token_entry(te) { I(rid); }
};

class Ast_parser : public mmap_lib::tree<Ast_parser_node> {
private:
protected:
  mmap_lib::Tree_level             level;
  mmap_lib::Tree_level             down_added;
  const std::string_view buffer;  // const because it can not change at runtime

  std::vector<mmap_lib::Tree_index> last_added;

  void add_track_parent(const mmap_lib::Tree_index &index);
public:
  Ast_parser(std::string_view buffer, Rule_id top_rule);

  void down();
  void up(Rule_id rid);
  void add(Rule_id rid, Token_entry te);

  void dump() const;

  std::string_view get_memblock() const; // FIXME: memblock has to go. Support user provided strings (memblock should be inside lnast)
};

