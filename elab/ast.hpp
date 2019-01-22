//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "tree.hpp"
#include "elab_scanner.hpp"

using Rule_id = int; // FIXME explicit_type

struct Ast_parser_node {
  const Rule_id     rule_id;
  const Token_entry token_entry;
  Ast_parser_node(Rule_id rid, Token_entry te)
    :rule_id(rid),
    token_entry(te) {
      I(rid);
    }
};

class Ast_parser : public Tree<Ast_parser_node> {
private:
protected:
  Tree_level level;
  Tree_level down_added;
  const std::string_view buffer; // const because it can not change at runtime
public:
  Ast_parser(std::string_view buffer);

  void down() {
    level = level + 1;
  }
  void up(Rule_id rid);
  void add(Rule_id rid, Token_entry te);
};

