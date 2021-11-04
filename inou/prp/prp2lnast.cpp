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

  process_root();
}

Prp2lnast::~Prp2lnast() {

  ts_parser_delete(parser);
}

bool Prp2lnast::is_lhs_fcall_or_variable(TSTreeCursor *tc) const {
  auto tc2 = ts_tree_cursor_copy(tc);

  while(ts_tree_cursor_goto_next_sibling(&tc2)) {
    const TSNode node = ts_tree_cursor_current_node(&tc2);
    mmap_lib::str node_type(ts_node_type(node));
    if (node_type == "assignment_cont2")
      return true;
  }

  return false;
}

mmap_lib::str Prp2lnast::get_text(const TSNode &node) const {
  auto start = ts_node_start_byte(node);
  auto end = ts_node_end_byte(node);
  auto length = end - start;

  I(end<=prp_file.size());
  return mmap_lib::str(prp_file.substr(start, length));
}

mmap_lib::str Prp2lnast::get_complex_identifier(const TSNode &node) const {
  auto start = ts_node_start_byte(node);
  auto end = ts_node_end_byte(node);
  auto length = end - start;

  if (length>2) {
    // create a canonical name: $foo -> $.foo
    auto ch1 = prp_file[start];
    auto ch2 = prp_file[start+1];
    if ((ch1=='$' || ch1=='#' || ch1 == '%') && ch2 != '.')
      return mmap_lib::str::concat(mmap_lib::str(1,ch1), ".", prp_file.substr(start+1, length-1));
  }

  I(end<=prp_file.size());
  return mmap_lib::str(prp_file.substr(start, length));
}

void Prp2lnast::process_fcall_or_variable(TSTreeCursor *tc) {

  const TSNode node = ts_tree_cursor_current_node(tc);

  mmap_lib::str node_type(ts_node_type(node));
  if (node_type == "trivial_identifier") {
    auto var = get_trivial_identifier(node);

    fmt::print("trivial_identifier[{}]\n", var);
  }else if (node_type == "complex_identifier") {
    auto var = get_complex_identifier(node);

    fmt::print("complex_identifier[{}]\n", var);
  }else{
    fmt::print("FIXME: add {} fcall_or_variable\n", node_type);
  }
}

void Prp2lnast::process_multiple_stmt(TSTreeCursor *tc) {

  const TSNode node = ts_tree_cursor_current_node(tc);

  mmap_lib::str node_type(ts_node_type(node));
  if (node_type == "fcall_or_variable") {
    in_lhs = is_lhs_fcall_or_variable(tc);
    ts_tree_cursor_goto_first_child(tc);
    process_fcall_or_variable(tc);
    ts_tree_cursor_goto_parent(tc);
  }else{
    fmt::print("FIXME: add start {} to process_multiple_stmt\n", node_type);
  }

  auto has_cont = ts_tree_cursor_goto_next_sibling(tc);
  if (has_cont) {
    const TSNode cont_node = ts_tree_cursor_current_node(tc);
    mmap_lib::str cont_type(ts_node_type(cont_node));
    if (cont_type == "assignment_cont2") {
      in_lhs = false;
      // TODO: HERE
    }else{
      fmt::print("FIXME: add cont {} to process_multiple_stmt\n", cont_type);
    }
  }
}

void Prp2lnast::process_stmt_seq(TSTreeCursor *tc) {

  const TSNode node = ts_tree_cursor_current_node(tc);

  mmap_lib::str node_type(ts_node_type(node));
  if (node_type != "stmt_seq") {
    return Pass::error("invalid tree-sitter stmt_seq node");
  }

  auto has_stmt = ts_tree_cursor_goto_first_child(tc);
  while (has_stmt) {
    const TSNode stmt = ts_tree_cursor_current_node(tc);
    mmap_lib::str stmt_type(ts_node_type(stmt));

    if (stmt_type == "multiple_stmt") {
      ts_tree_cursor_goto_first_child(tc);
      process_multiple_stmt(tc);
      ts_tree_cursor_goto_parent(tc);
    }else{
      fmt::print("FIXME: add {} to process_stmt_seq\n", stmt_type);
    }

    has_stmt = ts_tree_cursor_goto_next_sibling(tc);
  }

  ts_tree_cursor_goto_parent(tc);
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

void Prp2lnast::process_root() {

  auto tc = ts_tree_cursor_new(ts_root_node);

  const TSNode node = ts_tree_cursor_current_node(&tc);
  mmap_lib::str node_type(ts_node_type(node));
  if (node_type != "start") {
    return Pass::error("invalid tree-sitter root node");
  }

  auto has_child = ts_tree_cursor_goto_first_child(&tc);
  if (has_child) {
    process_stmt_seq(&tc);
  }

  ts_tree_cursor_delete(&tc);
}

