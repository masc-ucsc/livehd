//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ast.hpp"

Ast_parser::Ast_parser(std::string_view _buffer, Rule_id top_rule) : buffer(_buffer) {
  I(!buffer.empty());

  set_root(Ast_parser_node(top_rule,0));

  level      = 0;
  down_added = 0;
}

void Ast_parser::up(Rule_id rid) {
  I(level > 0);

  if (down_added == level) {
    // Child was added, nothing to do now
  } else if (down_added > level) {
    I((down_added - 1) == level);
    // It has descendents but not child (add one now)
    down_added = level;
    add_lazy_child(level, Ast_parser_node(rid, 0));
  } else {
    I(down_added < level);
    // nothing to do, no descendents
  }

  level = level - 1;
}

void Ast_parser::add(Rule_id rid, Token_entry te) {
  I(level > 0, "no rule add for ast root");

  GI(down_added > level, down_added == (level + 1));

  down_added = level;

  add_lazy_child(level, Ast_parser_node(rid, te));
}
