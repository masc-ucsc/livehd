//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "ast.hpp"

#include <print>

Ast_parser::Ast_parser(std::string_view _buffer, Rule_id top_rule) : buffer(_buffer) {
  set_root(Ast_parser_node(top_rule, 0));
  add_track_parent(lh::Tree_index::root());

  level = 0;
}

void Ast_parser::add_track_parent(const lh::Tree_index &index) {
  for (int i = last_added.size(); i < index.level + 1; ++i) {
    last_added.emplace_back(-1, -1);
  }
  I(last_added.size() > (size_t)index.level);

  down_added             = index.level;
  last_added[down_added] = index;
}

void Ast_parser::down() { level = level + 1; }

void Ast_parser::up(Rule_id rid_up) {
  I(level > 0);

  if (down_added == level) {
    // Child was added, nothing to do now
    auto *data = ref_data(last_added[level]);
    if (!data->rule_id) {
      data->rule_id = rid_up;
    }
    last_added[down_added].invalidate();  // Not needed, but helps asserts
  } else if (down_added > level) {
    I((down_added - 1) == level);
    // It has descendents but not current node (add one now)
    I(!last_added[level].is_invalid());

    Ast_parser_node a(rid_up, 0);
    set_data(last_added[down_added], a);  // Allow to fix the rule when going up
    down_added = level;
  } else {
    I(down_added < level);
    // nothing to do, no descendents
    I((int)last_added.size() <= level);
    level = level - 1;
    return;
  }

  level = level - 1;
  if ((int)last_added.size() > level && level > 0) {
    last_added.pop_back();
    I((int)last_added.size() == level + 1);
  }
  if (down_added > level) {
    down_added--;
    I(down_added == level);
  }
}

void Ast_parser::add(Rule_id rule_id, Token_entry te) {
  I(level > 0, "no rule add for ast root");

  I(last_added.size() >= (size_t)down_added);

  // Populate parents if needed
  for (int i = down_added; i < level - 1; ++i) {
    I(!last_added[i].is_invalid());
    auto child_index = add_child(last_added[i], Ast_parser_node());
    add_track_parent(child_index);
  }
  I(down_added + 1 >= level);

  if (down_added == level) {
    auto child_index = append_sibling(last_added.back(), Ast_parser_node(rule_id, te));
    add_track_parent(child_index);
  } else {
    auto child_index = add_child(last_added.back(), Ast_parser_node(rule_id, te));
    add_track_parent(child_index);
  }
}

void Ast_parser::dump() const {
  for (const auto &index : depth_preorder()) {
    std::string indent(index.level, ' ');
    const auto &d = get_data(index);
    std::print("{} l:{} p:{} rule_id:{}\n", indent.c_str(), index.level, index.pos, d.rule_id);
  }
}
