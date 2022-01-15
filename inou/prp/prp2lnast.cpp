//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <sstream>

#include "prp2lnast.hpp"

#include "fmt/format.h"
#include "pass.hpp"

extern "C" TSLanguage *tree_sitter_pyrope();

Prp2lnast::Prp2lnast(const mmap_lib::str filename, const mmap_lib::str module_name) {
  lnast = std::make_unique<Lnast>(module_name);

  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  {
    auto ss = std::ostringstream{};
    std::ifstream file(filename.to_s());
    ss << file.rdbuf();
    prp_file = ss.str();
  }

  parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  TSTree *tst_tree = ts_parser_parse_string(parser, NULL, prp_file.data(), prp_file.size());
  ts_root_node = ts_tree_root_node(tst_tree);

  dump();

  process_description();
}

Prp2lnast::~Prp2lnast() {

  ts_parser_delete(parser);
}

mmap_lib::str Prp2lnast::get_text(const TSNode &node) const {
  auto start = ts_node_start_byte(node);
  auto end = ts_node_end_byte(node);
  auto length = end - start;

  I(end<=prp_file.size());
  return mmap_lib::str(prp_file.substr(start, length));
}

void Prp2lnast::dump_tree_sitter() const {
  auto tc = ts_tree_cursor_new(ts_root_node);

  dump_tree_sitter(&tc, 1);

  ts_tree_cursor_delete(&tc);
}

void Prp2lnast::dump_tree_sitter(TSTreeCursor *tc, int level) const {

  auto indent = mmap_lib::str(level*2,' ');

  bool go_next = true;
  while (go_next) {
    auto node         = ts_tree_cursor_current_node(tc);
    auto num_children = ts_node_child_count(node);
    auto node_type    = mmap_lib::str(ts_node_type(node));

    fmt::print("{}{} {}\n", indent, node_type, num_children);

    if (num_children) {
      ts_tree_cursor_goto_first_child(tc);
      dump_tree_sitter(tc, level+1);
      ts_tree_cursor_goto_parent(tc);
    }

    go_next = ts_tree_cursor_goto_next_sibling(tc);
  }
}

void Prp2lnast::dump() const {
  fmt::print("tree-sitter-dump\n");

  dump_tree_sitter();
}

void Prp2lnast::process_description() {
  auto tc = ts_tree_cursor_new(ts_root_node);

  auto ti = lnast->add_child(mmap_lib::Tree_index::root(), Lnast_node::create_stmts());
  tree_index.push(ti);
  
  bool go_next = ts_tree_cursor_goto_first_child(&tc);
  while (go_next) {
    process_statement(&tc);
    go_next = ts_tree_cursor_goto_next_sibling(&tc);
  }
  ts_tree_cursor_goto_parent(&tc);

  ts_tree_cursor_delete(&tc);
}

void Prp2lnast::process_statement(TSTreeCursor* tc) {
  ts_tree_cursor_goto_first_child(tc);
  process_node(ts_tree_cursor_current_node(tc));
  ts_tree_cursor_goto_parent(tc);
}

void Prp2lnast::process_node(TSNode node) {
	if (ts_node_is_null(node)) return;
  mmap_lib::str node_type(ts_node_type(node));

  if      (node_type == "binary_expression") process_binary_expression(node);
  else if (node_type == "simple_number"    ) process_simple_number(node);
  else                                       process_node(ts_node_child(node, 0));
}

void Prp2lnast::process_binary_expression(TSNode node) {
  auto lnode = ts_node_child_by_field_id(node, ts_language_field_id_for_name(tree_sitter_pyrope(), "left", 4));
  auto onode = ts_node_child_by_field_id(node, ts_language_field_id_for_name(tree_sitter_pyrope(), "operator", 8));
  auto rnode = ts_node_child_by_field_id(node, ts_language_field_id_for_name(tree_sitter_pyrope(), "right", 5));
  
  fmt::print("{} <{}> {}\n", get_text(lnode), get_text(onode), get_text(rnode));

  auto op = get_text(onode);
  Lnast_node lnast_node;
  if      (op == "+" ) lnast_node = Lnast_node::create_plus();
  else if (op == "-" ) lnast_node = Lnast_node::create_minus();
  else if (op == "*" ) lnast_node = Lnast_node::create_mult();
  else if (op == "/" ) lnast_node = Lnast_node::create_div();
  else if (op == "&" ) lnast_node = Lnast_node::create_bit_and();
  else if (op == "^" ) lnast_node = Lnast_node::create_bit_xor();
  else if (op == "|" ) lnast_node = Lnast_node::create_bit_or();
  else if (op == "<" ) lnast_node = Lnast_node::create_lt();
  else if (op == "<=") lnast_node = Lnast_node::create_le();
  else if (op == ">" ) lnast_node = Lnast_node::create_gt();
  else if (op == ">=") lnast_node = Lnast_node::create_ge();
  else if (op == "==") lnast_node = Lnast_node::create_eq();
  else if (op == "!=") lnast_node = Lnast_node::create_ne();
  else                 lnast_node = Lnast_node::create_invalid();

  tree_index.push(lnast->add_child(tree_index.top(), lnast_node));
  process_node(lnode);
  process_node(rnode);
  tree_index.pop();
}

void Prp2lnast::process_simple_number(TSNode node) {
  auto text = get_text(node);
  lnast->add_child(tree_index.top(), Lnast_node::create_const(text));
}
