//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <fstream>
#include <sstream>
#include <string>

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

  tmp_ref_count = 0;

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

  stmts_index = lnast->add_child(mmap_lib::Tree_index::root(), Lnast_node::create_stmts());

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

  fmt::print("-> {}\n", node_type);

  if      (node_type == "binary_expression") process_binary_expression(node);
  else if (node_type == "declaration"      ) process_declaration(node);
  else if (node_type == "tuple"            ) process_tuple(node);
  else if (node_type == "tuple_list"       ) process_tuple_or_expression_list(node);
  else if (node_type == "expression_list"  ) process_tuple_or_expression_list(node);
  else if (node_type == "identifier"       ) process_identifier(node);
  else if (node_type == "simple_number"    ) process_simple_number(node);
  else                                       process_node(get_child(node));
}

void Prp2lnast::process_each_list_item(TSNode node, std::function<void()> pre, std::function<void()> post) {
  node = get_child(node);
  while (!ts_node_is_null(node)) {
    if (mmap_lib::str(ts_node_type(node)) != ",") {
      if (pre) pre();
      process_node(node);
      if (post) post();
    }
    node = get_sibling(node);
  }
}

void Prp2lnast::process_declaration(TSNode node) {

  // if (expr_state.top() == Expression_state::Lvalue) {
  //   // TODO: Emit error
  // }

  auto lnode = get_child(node, "lvalue");
  auto onode = get_child(node, "operator");
  auto rnode = get_child(node, "rvalue");

  fmt::print("`{}` {} `{}`\n", get_text(lnode), get_text(onode), get_text(rnode));

  // TODO: Handle different assign operators
  id_state_stack.push(Identifier_state::Get);
  expr_state_stack.push(Expression_state::Rvalue);
  process_node(rnode);
  rvalue_node = primary_node_stack.top(); primary_node_stack.pop();
  id_state_stack.pop();
  expr_state_stack.pop();

  id_state_stack.push(Identifier_state::Set);
  expr_state_stack.push(Expression_state::Lvalue);
  process_node(lnode);
  id_state_stack.pop();
  expr_state_stack.pop();
}

void Prp2lnast::process_tuple(TSNode node) {
  // TODO: Handle empty tuple
  process_node(ts_node_child(node, 1));
}

void Prp2lnast::process_tuple_or_expression_list(TSNode node) {
  auto add_to_tuple = [this]() {
    tuple_rvalue_stack.top().push_back(primary_node_stack.top());
    primary_node_stack.pop();
  };
  switch (expr_state_stack.top()) {
    case Expression_state::Lvalue:
      if (ts_node_named_child_count(node) > 1) {
        tuple_lvalue_positions.push_back(0);
        process_each_list_item(node, nullptr,
        [this]() {
          // NOTE:
          // Example: (a, b, c, (d, (e, f))) = tup
          //                         ^
          //   stmts -> tuple_get -> lhs        = ref("___t0")
          //                      -> tuple_name = ref("tup")
          //                      -> 1st index  = const("3")
          //                      -> 2nd index  = const("1")
          //                      -> last index = const("0")
          //         -> assign    -> lhs        = ref("e")
          //                      -> rhs        = ref("___t0")
          if (primary_node_stack.size() != 0) {
            fmt::print("{} = {}.{}\n", primary_node_stack.top().token.get_text(), rvalue_node.token.get_text(), fmt::join(tuple_lvalue_positions, "."));
            auto tuple_item_name = get_tmp_name();
            auto tuple_get_index = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
            lnast->add_child(tuple_get_index, Lnast_node::create_ref(tuple_item_name));
            lnast->add_child(tuple_get_index, rvalue_node);
            for (auto pos : tuple_lvalue_positions) {
              lnast->add_child(tuple_get_index, Lnast_node::create_const(pos));
            }
            auto assign_index = lnast->add_child(stmts_index, Lnast_node::create_assign());
            lnast->add_child(assign_index, primary_node_stack.top());
            primary_node_stack.pop();
            lnast->add_child(assign_index, Lnast_node::create_ref(tuple_item_name));
          }
          ++tuple_lvalue_positions.back();
        });
        tuple_lvalue_positions.pop_back();
      } else {
        process_each_list_item(node, nullptr, nullptr);
      }
      break;
    case Expression_state::Rvalue:
      if (ts_node_named_child_count(node) > 1) {
        tuple_rvalue_stack.push(std::vector<Lnast_node>());
        
        process_each_list_item(node, nullptr, add_to_tuple);
        auto tuple_name = get_tmp_name();
        auto tuple_set_index = lnast->add_child(stmts_index, Lnast_node::create_tuple_add());
        lnast->add_child(tuple_set_index, Lnast_node::create_ref(tuple_name));
        for (const auto rvalue : tuple_rvalue_stack.top()) {
          lnast->add_child(tuple_set_index, rvalue);
        }
        
        // 
        fmt::print("Tuple: {} = ( ", tuple_name);
        for (auto n : tuple_rvalue_stack.top()) {
          fmt::print("{} ", n.token.get_text());
        }
        fmt::print(")\n");
        
        tuple_rvalue_stack.pop();
        primary_node_stack.push(Lnast_node::create_ref(tuple_name));
      } else {
        process_each_list_item(node, nullptr, add_to_tuple);
      }
      break;
    case Expression_state::Type:
      // TODO: Handle type tuple
      break;
  }
}

void Prp2lnast::process_binary_expression(TSNode node) {
  auto lnode = get_child(node, "left");
  auto onode = get_child(node, "operator");
  auto rnode = get_child(node, "right");

  fmt::print("`{}` {} `{}`\n", get_text(lnode), get_text(onode), get_text(rnode));

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

  // TODO: Merge to child node if
  //  (1) same op as the child node
  //  (2) op takes n arguments
  process_node(lnode);
  process_node(rnode);

  auto expr_index = lnast->add_child(stmts_index, lnast_node);
  auto ref = get_tmp_ref();
  auto rhs = primary_node_stack.top(); primary_node_stack.pop();
  auto lhs = primary_node_stack.top(); primary_node_stack.pop();

  lnast->add_child(expr_index, ref);
  lnast->add_child(expr_index, lhs);
  lnast->add_child(expr_index, rhs);

  primary_node_stack.push(ref);
}

void Prp2lnast::process_identifier(TSNode node) {
  auto text = get_text(node);
  fmt::print("ID `{}`\n", text);
  switch (id_state_stack.top()) {
    case Identifier_state::Set:
    case Identifier_state::Get:
      primary_node_stack.push(Lnast_node::create_ref(text));
      break;
    default:
      break;
  }
}

void Prp2lnast::process_simple_number(TSNode node) {
  auto text = get_text(node);
  primary_node_stack.push(Lnast_node::create_const(text));
}

inline mmap_lib::str Prp2lnast::get_tmp_name() {
  return mmap_lib::str::concat("___t", tmp_ref_count++);
}

inline Lnast_node Prp2lnast::get_tmp_ref() {
  return Lnast_node::create_ref(get_tmp_name());
}

inline TSNode Prp2lnast::get_child(const TSNode &node, const char *field) const {
  return ts_node_child_by_field_name(node, field, std::char_traits<char>::length(field));
}

inline TSNode Prp2lnast::get_child(const TSNode &node) const {
  return ts_node_child(node, 0);
}

inline TSNode Prp2lnast::get_sibling(const TSNode &node) const {
  return ts_node_next_sibling(node);
}

inline TSNode Prp2lnast::get_named_child(const TSNode &node) const {
  return ts_node_named_child(node, 0);
}

inline TSNode Prp2lnast::get_named_sibling(const TSNode &node) const {
  return ts_node_next_named_sibling(node);
}
