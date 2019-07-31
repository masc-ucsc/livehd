//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ast.hpp"

Ast_parser::Ast_parser(std::string_view _buffer, Rule_id top_rule) : buffer(_buffer) {
  I(!buffer.empty());

  set_root(Ast_parser_node(top_rule,0));
  add_track_parent(get_root());

  level      = 0;
}

void Ast_parser::add_track_parent(const mmap_map::Tree_index &index) {
  for (int i = last_added.size(); i < index.level + 1; ++i) {
    last_added.emplace_back(-1, -1);
  }
  I(last_added.size()>index.level);

  down_added = index.level;
  last_added[down_added] = index;
}

void Ast_parser::up(Rule_id rule_id) {
  I(level > 0);

  if (down_added == level) {
    // Child was added, nothing to do now
    last_added[down_added].invalidate(); // Not needed, but helps asserts
  } else if (down_added > level) {
    I((down_added - 1) == level);
    // It has descendents but not current node (add one now)
    I(!last_added[level].is_invalid());
    I(get_data(last_added[level]).rule_id == 0); // rule_id should be zero create OoO

    Ast_parser_node a(rule_id, 0);
    set_data(last_added[down_added], a);
    down_added = level;
  } else {
    I(down_added < level);
    // nothing to do, no descendents
  }

  level = level - 1;
}

void Ast_parser::add(Rule_id rule_id, Token_entry te) {
  I(level > 0, "no rule add for ast root");

  I(last_added.size()>=down_added);

  // Populate parents if needed
  for (int i = down_added; i < level; ++i) {
    I(!last_added[i].is_invalid());
    auto child_index = add_child(last_added[i], Ast_parser_node(0,0));
    add_track_parent(child_index);
  }
  I(down_added+1>=level);
  I(last_added.size() == level);

  auto child_index = add_child(last_added.back(), Ast_parser_node(rule_id, te));
  GI(!last_added.back().is_invalid(), get_parent(last_added.back()) == get_parent(child_index));
  add_track_parent(child_index);
}

