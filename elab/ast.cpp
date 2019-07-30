//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ast.hpp"

Ast_parser::Ast_parser(std::string_view _buffer, Rule_id top_rule) : buffer(_buffer) {
  I(!buffer.empty());

  set_root(Ast_parser_node(top_rule,0));
  add_track_parent(get_root());

  level      = 0;
}

void Ast_parser::add_track_parent(const Tree_index &index) {
  for (int i = last_added.size(); i < index.level + 1; ++i) {
    last_added.emplace_back(-, 1, -1);
  }
  I(last_added.size()>index.level);

  down_added = index.level;
  last_added[down_added] = index
}

void Ast_parser::up(Rule_id rid) {
  I(level > 0);

  if (down_added == level) {
    // Child was added, nothing to do now
    last_added[down_added].invalidate(); // Not needed, but helps asserts
  } else if (down_added > level) {
    I((down_added - 1) == level);
    // It has descendents but not current node (add one now)
    I(!last_added[level].is_invalid());
    I(last_added[level].get_data().rid == 0); // rid should be zero create OoO

    last_added[down_added].set_data(Ast_parser_node(rid, 0));
    down_added = level;
  } else {
    I(down_added < level);
    // nothing to do, no descendents
  }

  level = level - 1;
}

void Ast_parser::add(Rule_id rid, Token_entry te) {
  I(level > 0, "no rule add for ast root");

  I(last_added.size()>=down_added);

  // Populate parents if needed
  for (int i = down_added; i < level; ++i) {
    I(!last_added[i].is_invalid());
    auto child_index = add_child(last_added[i], Ast_parser_node(0,0));
    add_track_parent(child_index);
  }
  I(down_added+1>=level);

  auto child_index = add_child(last_added[i], Ast_parser_node(rid, te));
  GI(!last_added[child.level].is_invalid(), last_added[child.level].get_parent() == child_index.get_parent());
  add_track_parent(child_index);
}
