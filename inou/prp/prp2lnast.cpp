//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp2lnast.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include "fmt/format.h"
#include "pass.hpp"
#include "str_tools.hpp"

extern "C" TSLanguage *tree_sitter_pyrope();

Prp2lnast::Prp2lnast(std::string_view filename, std::string_view module_name) {
  lnast = std::make_unique<Lnast>(module_name);

  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  {
    std::string   fname(filename);
    auto          ss = std::ostringstream{};
    std::ifstream file(fname);
    ss << file.rdbuf();
    prp_file = ss.str();
  }

  parser = ts_parser_new();
  ts_parser_set_language(parser, tree_sitter_pyrope());

  TSTree *tst_tree = ts_parser_parse_string(parser, NULL, prp_file.data(), prp_file.size());
  ts_root_node     = ts_tree_root_node(tst_tree);

  dump();

  tmp_ref_count = 0;
  is_function_input = false;
  is_function_output = false;

  process_description();
}

Prp2lnast::~Prp2lnast() { ts_parser_delete(parser); }

std::string_view Prp2lnast::get_text(const TSNode &node) const {
  auto start  = ts_node_start_byte(node);
  auto end    = ts_node_end_byte(node);
  auto length = end - start;

  I(end <= prp_file.size());
  return std::string_view(prp_file).substr(start, length);
}

void Prp2lnast::dump_tree_sitter() const {
  auto tc = ts_tree_cursor_new(ts_root_node);

  dump_tree_sitter(&tc, 1);

  ts_tree_cursor_delete(&tc);
}

void Prp2lnast::dump_tree_sitter(TSTreeCursor *tc, int level) const {
  auto indent = std::string(level * 2, ' ');

  bool go_next = true;
  while (go_next) {
    auto        node         = ts_tree_cursor_current_node(tc);
    auto        num_children = ts_node_child_count(node);
    std::string node_type(ts_node_type(node));

    fmt::print("{}{} {}\n", indent, node_type, num_children);

    if (num_children) {
      ts_tree_cursor_goto_first_child(tc);
      dump_tree_sitter(tc, level + 1);
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

  stmts_index = lnast->add_child(lh::Tree_index::root(), Lnast_node::create_stmts());
  type_index = stmts_index;

  bool go_next = ts_tree_cursor_goto_first_child(&tc);
  while (go_next) {
    enter_scope(Expression_state::Rvalue);
    select_stack.push(std::vector<node_attr_t>());
    process_statement(ts_tree_cursor_current_node(&tc));
    leave_scope();
    select_stack.pop();
    I(primary_node_stack.size() == 0);
    I(select_stack.size() == 0);
    go_next = ts_tree_cursor_goto_next_sibling(&tc);
  }
  ts_tree_cursor_goto_parent(&tc);

  ts_tree_cursor_delete(&tc);
}

void Prp2lnast::process_statement(TSNode node) {
  node = get_child(node);
  while (!ts_node_is_null(node)) {
    process_node(node);
    node = get_sibling(node);
  }
}

void Prp2lnast::process_node(TSNode node) {
  if (ts_node_is_null(node))
    return;
  std::string node_type(ts_node_type(node));

  fmt::print("-> {}\n", node_type);

  if (node_type == "statement")
    process_statement(node);
  else if (node_type == "binary_expression")
    process_binary_expression(node);
  else if (node_type == "unary_expression")
    process_unary_expression(node);
  else if (node_type == "dot_expression")
    process_dot_expression(node);
  else if (node_type == "member_selection")
    process_member_selection(node);
  else if (node_type == "member_select")
    process_member_select(node);
  else if (node_type == "select")
    process_select(node);
  else if (node_type == "assignment_or_declaration_statement")
    process_assignment_or_declaration(node);
  else if (node_type == "simple_assignment")
    process_simple_assignment(node);
  else if (node_type == "simple_declaration")
    process_simple_declaration(node);
  else if (node_type == "scope_statement")
    process_scope_statement(node);
  else if (node_type == "expression_statement")
    process_expression_statement(node);
  else if (node_type == "if_statement")
    process_if_statement(node);
  else if (node_type == "for_statement")
    process_for_statement(node);
  else if (node_type == "while_statement")
    process_while_statement(node);
  else if (node_type == "function_call_statement")
    process_function_call_statement(node);
  else if (node_type == "simple_function_call")
    process_simple_function_call(node);
  else if (node_type == "function_definition")
    process_function_definition(node);
  else if (node_type == "type_specification")
    process_type_specification(node);
  else if (node_type == "unsized_integer_type")
    process_unsized_integer_type(node);
  else if (node_type == "sized_integer_type")
    process_sized_integer_type(node);
  else if (node_type == "range_type")
    process_range_type();
  else if (node_type == "string_type")
    process_string_type();
  else if (node_type == "boolean_type")
    process_boolean_type();
  else if (node_type == "type_type")
    process_type_type();
  else if (node_type == "array_type")
    process_array_type(node);
  else if (node_type == "tuple")
    process_tuple(node);
  else if (node_type == "tuple_list")
    process_tuple_or_expression_list(node);
  else if (node_type == "expression_list")
    process_tuple_or_expression_list(node);
  else if (node_type == "identifier")
    process_identifier(node);
  else if (node_type == "constant")
    process_constant(node);
  else if (node_type == "attributes")
    process_attributes(node);
  else
    process_node(get_child(node));
}

void Prp2lnast::process_scope_statement(TSNode node) {
  node = get_child(node);
  while (!ts_node_is_null(node)) {
    std::string node_type(ts_node_type(node));
    if (node_type == "statement") {
      process_node(node);
    }
    node = get_sibling(node);
  }
}

void Prp2lnast::process_expression_statement(TSNode node) {
  node = get_child(node);
  process_node(node);
  // TODO: Consider expression statement as function return statement
  add_ref_child_conditional(stmts_index);
  primary_node_stack.pop();
}

void Prp2lnast::process_if_statement(TSNode node) {
  node = get_named_child(node);

  // TODO: Handle unique if
  std::vector<Lnast_node> cond_refs;
  std::vector<TSNode>     code_blocks;
  
  enter_scope(Expression_state::Rvalue);
  process_node(node);
  add_ref_child_conditional(stmts_index);
  cond_refs.push_back(primary_node_stack.top().first);
  primary_node_stack.pop();
  node = get_sibling(node);
  code_blocks.push_back(node);

  node = get_sibling(node);
  while (!ts_node_is_null(node)) {
    if (get_text(node) == "elif") {
      node = get_sibling(node);
      process_node(node);
      add_ref_child_conditional(stmts_index);
      cond_refs.push_back(primary_node_stack.top().first);
      primary_node_stack.pop();
      node = get_sibling(node);
      code_blocks.push_back(node);
    } else if (get_text(node) == "else") {
      node = get_sibling(node);
      code_blocks.push_back(node);
    }
    node = get_sibling(node);
  }
  leave_scope();

  auto if_index = lnast->add_child(stmts_index, Lnast_node::create_if());
  for (size_t i = 0; i < cond_refs.size(); ++i) {
    enter_scope(Expression_state::Rvalue);
    lnast->add_child(if_index, cond_refs[i]);
    auto original_stmts_index = stmts_index;
    stmts_index               = lnast->add_child(if_index, Lnast_node::create_stmts());
    process_node(code_blocks[i]);
    stmts_index = original_stmts_index;
    leave_scope();
  }
  if (code_blocks.size() > cond_refs.size()) {
    enter_scope(Expression_state::Rvalue);
    auto original_stmts_index = stmts_index;
    stmts_index               = lnast->add_child(if_index, Lnast_node::create_stmts());
    process_node(code_blocks.back());
    stmts_index = original_stmts_index;
    leave_scope();
  }
}

void Prp2lnast::process_for_statement(TSNode node) {
  auto inode = get_child(node, "index");
  auto dnode = get_child(node, "data");
  auto cnode = get_child(node, "code");

  enter_scope(Expression_state::Rvalue);
  process_node(inode);
  add_ref_child_conditional(stmts_index);
  auto index = primary_node_stack.top().first;
  primary_node_stack.pop();
  process_node(dnode);
  add_ref_child_conditional(stmts_index);
  auto data = primary_node_stack.top().first;
  primary_node_stack.pop();
  auto for_index = lnast->add_child(stmts_index, Lnast_node::create_for());
  lnast->add_child(for_index, index);
  lnast->add_child(for_index, data);
  auto original_stmts_index = stmts_index;
  stmts_index = lnast->add_child(for_index, Lnast_node::create_stmts());
  process_node(cnode);
  stmts_index = original_stmts_index;
  leave_scope();
}

void Prp2lnast::process_while_statement(TSNode node) {
  auto cond = get_child(node, "condition");
  auto code = get_child(node, "code");

  auto while_index          = lnast->add_child(stmts_index, Lnast_node::create_while());
  auto original_stmts_index = stmts_index;
  stmts_index               = lnast->add_child(while_index, Lnast_node::create_stmts());
  process_node(cond);
  add_ref_child(while_index, primary_node_stack.top());
  primary_node_stack.pop();
  stmts_index = lnast->add_child(while_index, Lnast_node::create_stmts());
  process_node(code);
  stmts_index = original_stmts_index;
}

void Prp2lnast::process_function_call_statement(TSNode node) {
  auto fcall = get_child(node);
  process_node(fcall);
  primary_node_stack.pop();
}

void Prp2lnast::process_simple_function_call(TSNode node) {
  auto func_node = get_child(node, "function");
  auto args_node= get_child(node, "argument");

  enter_scope(Expression_state::Rvalue);
  process_node(func_node);
  auto func = primary_node_stack.top();
  primary_node_stack.pop();
  process_node(args_node);
  auto args = primary_node_stack.top();
  primary_node_stack.pop();
  leave_scope();

  auto ref = get_tmp_ref();
  auto fcall_index = lnast->add_child(stmts_index, Lnast_node::create_func_call());
  lnast->add_child(fcall_index, ref);
  add_ref_child(fcall_index, func);
  add_ref_child(fcall_index, args);
  primary_node_stack.push({ref, {}});
}

void Prp2lnast::process_function_definition(TSNode node) {
  // TODO: Handle func_type/capture/generic/condition

  auto inode = get_child(node, "input");
  auto onode = get_child(node, "output");
  auto cnode = get_child(node, "code");

  enter_scope(Expression_state::Type);
  if (!ts_node_is_null(inode)) {
    is_function_input = true;
    process_node(inode);
    is_function_input = false;
  }
  if (!ts_node_is_null(onode)) {
    is_function_output = true;
    process_node(get_sibling(onode));
    is_function_output = false;
  }
  leave_scope();

  process_node(cnode);

  // FIXME: Should return function definition
  primary_node_stack.push({Lnast_node::create_invalid(), {}});
}

void Prp2lnast::process_assignment_or_declaration(TSNode node) {
  auto lnode = get_child(node, "lvalue");
  auto onode = get_child(node, "operator");
  auto rnode = get_child(node, "rvalue");

  fmt::print("-> assignment/declaration `{}` {} `{}`\n", get_text(lnode), get_text(onode), get_text(rnode));

  // TODO: Handle different assign operators
  if (!ts_node_is_null(rnode)) {
    enter_scope(Expression_state::Rvalue);
    process_node(rnode);
    leave_scope();
    rvalue_node_stack.push(primary_node_stack.top());
    primary_node_stack.pop();

    enter_scope(Expression_state::Lvalue);
    process_node(lnode);
    leave_scope();

    rvalue_node_stack.pop();
  } else {
    // TODO: Handle empty rvalue (type spec ...)
    // FIXME: Temporary fix for generating function input/output refs
    enter_scope(Expression_state::Decl);
    process_node(lnode);
    leave_scope();
    primary_node_stack.pop();
  }
}

void Prp2lnast::process_simple_assignment(TSNode node) {
  auto lnode = get_child(node, "lvalue");
  auto rnode = get_child(node, "rvalue");
  
  enter_scope(Expression_state::Rvalue);
  process_node(rnode);
  auto rvalue = primary_node_stack.top();
  primary_node_stack.pop();
  leave_scope();

  enter_scope(Expression_state::Const);
  process_node(lnode);
  auto lvalue = primary_node_stack.top();
  primary_node_stack.pop();
  leave_scope();

  switch (expr_state_stack.top()) {
    case Expression_state::Attr:
      primary_node_stack.top().second.push_back({Lnast_node::create_attr(lvalue.first.token.get_text()), rvalue.first});
      break;
    default:
      primary_node_stack.push(rvalue);
      lvalue_node_stack.push(lvalue);
      break;
  }
}

void Prp2lnast::process_simple_declaration(TSNode node) {
  auto lnode = get_child(node, "lvalue");
 
  enter_scope(Expression_state::Const);
  process_node(lnode);
  auto lvalue = primary_node_stack.top();
  primary_node_stack.pop();
  leave_scope();
  
  switch (expr_state_stack.top()) {
    case Expression_state::Attr:
      primary_node_stack.top().second.push_back({lvalue.first, Lnast_node::create_invalid()});
      break;
    default:
      lvalue_node_stack.push(lvalue);
      break;
  }
}

void Prp2lnast::process_type_specification(TSNode node) {
  auto arg_node = get_child(node, "argument");
  auto type_node = get_child(node, "type");
  auto attr_node = get_child(node, "attribute");

  process_node(arg_node);
  if (!ts_node_is_null(attr_node)) {
    process_node(attr_node);
  }
  auto arg = primary_node_stack.top();
  primary_node_stack.pop();

  if (!ts_node_is_null(type_node)) {
    auto type_spec_index = lnast->add_child(stmts_index, Lnast_node::create_type_spec());
    lnast->add_child(type_spec_index, arg.first);
    enter_scope(Expression_state::Type);
    type_index = type_spec_index;
    process_node(type_node);
    leave_scope();
  }
  
  switch (expr_state_stack.top()) {
    case Expression_state::Rvalue: 
    case Expression_state::Type:
    case Expression_state::Lvalue:
    case Expression_state::Decl:
      primary_node_stack.push(arg);
      break;
    case Expression_state::Const:
    default:
      break;
  }
}

void Prp2lnast::process_unsized_integer_type(TSNode node) {
  auto text = get_text(node);
  lnast->add_child(type_index,
      (text[0] == 'u') ? Lnast_node::create_prim_type_uint() : Lnast_node::create_prim_type_sint());
}

void Prp2lnast::process_sized_integer_type(TSNode node) {
  auto text = get_text(node);
  auto prim_type_int_index = lnast->add_child(type_index,
      (text[0] == 'u') ? Lnast_node::create_prim_type_uint() : Lnast_node::create_prim_type_sint());
  lnast->add_child(prim_type_int_index, Lnast_node::create_const(text.substr(1)));
}

void Prp2lnast::process_array_type(TSNode node) {
  auto base_node = get_child(node);
  auto length_node = get_sibling(base_node);
  
  auto comp_type_array_index = lnast->add_child(type_index, Lnast_node::create_comp_type_array());
  
  auto prev_type_index = type_index;
  type_index = comp_type_array_index;
  
  process_node(base_node);
  enter_scope(Expression_state::Rvalue);
  process_node(length_node);
  leave_scope();
  add_ref_child(comp_type_array_index);
  primary_node_stack.pop();
  type_index = prev_type_index;
}

void Prp2lnast::process_range_type() {
  lnast->add_child(type_index, Lnast_node::create_prim_type_range());
}

void Prp2lnast::process_string_type() {
  lnast->add_child(type_index, Lnast_node::create_prim_type_string());
}

void Prp2lnast::process_boolean_type() {
  lnast->add_child(type_index, Lnast_node::create_prim_type_boolean());
}

void Prp2lnast::process_type_type() {
  lnast->add_child(type_index, Lnast_node::create_prim_type_type());
}

void Prp2lnast::process_tuple(TSNode node) {
  // TODO: Handle empty tuple
  process_node(ts_node_child(node, 1));
}

void Prp2lnast::process_tuple_or_expression_list(TSNode node) {
  switch (expr_state_stack.top()) {
    case Expression_state::Lvalue:
      process_lvalue_list(node);
      break;
    case Expression_state::Rvalue:
      process_rvalue_list(node);
      break;
    case Expression_state::Type:
      // TODO: Handle type tuple
      process_tuple_type_list(node);
      break;
    case Expression_state::Const:
      // process_select_list(node);
      break;
    case Expression_state::Decl:
      process_declaration_list(node);
      break;
    case Expression_state::Attr:
      process_attribute_list(node);
      break;
  }
}

void Prp2lnast::process_lvalue_list(TSNode node) {
  bool has_multiple_items = (ts_node_named_child_count(node) > 1);
  if (has_multiple_items)
    tuple_lvalue_positions.push_back(0);
  node = get_named_child(node);
  while (!ts_node_is_null(node)) {
    process_node(node);
    std::string node_type(ts_node_type(node));
    fmt::print("Lvalue type - {}\n", node_type);
    if (node_type != "tuple") {
      // RHS
      node_attr_t rvalue;
      if (has_multiple_items) {
        auto tuple_item_name = get_tmp_name();
        auto tuple_get_index = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
        lnast->add_child(tuple_get_index, Lnast_node::create_ref(tuple_item_name));
        add_ref_child(tuple_get_index, rvalue_node_stack.top());
        for (auto pos : tuple_lvalue_positions) {
          lnast->add_child(tuple_get_index, Lnast_node::create_const(pos));
        }
        rvalue.first = Lnast_node::create_ref(tuple_item_name);
      } else {
        rvalue = rvalue_node_stack.top();
      }
      // LHS
      // FIXME: Temporary fix for generating function input/output refs
      if (rvalue.first.is_invalid()) {
        primary_node_stack.pop();
        break;
      }
      if (select_stack.top().empty()) {
        fmt::print("Rvalue node stack : {}\n", rvalue_node_stack.size());
        fmt::print("Primary node stack : {}\n", primary_node_stack.size());
        auto assign_index = lnast->add_child(stmts_index, Lnast_node::create_assign());
        add_ref_child(assign_index);
        add_ref_child(assign_index, rvalue);
        primary_node_stack.pop();
      } else {
        auto tuple_set_index = lnast->add_child(stmts_index, Lnast_node::create_tuple_set());
        add_ref_child(tuple_set_index);
        primary_node_stack.pop();
        for (auto n : select_stack.top()) {
          add_ref_child(tuple_set_index, n);
        }
        add_ref_child(tuple_set_index, rvalue);
        select_stack.top().clear();
      }
    }
    if (has_multiple_items)
      ++tuple_lvalue_positions.back();
    node = get_named_sibling(node);
  }
  if (has_multiple_items)
    tuple_lvalue_positions.pop_back();
}

void Prp2lnast::process_rvalue_list(TSNode node) {
  auto named_child_count = ts_node_named_child_count(node);
  node = get_named_child(node);
  std::string child_node_type(ts_node_type(node));

  if (named_child_count > 1 || child_node_type == "simple_assignment"
      || child_node_type == "simple_declaration") {
    tuple_rvalue_stack.push(std::vector<std::pair<node_attr_t, node_attr_t>>());

    while (!ts_node_is_null(node)) {
      std::string node_type(ts_node_type(node));
      process_node(node);
      if (node_type == "simple_assignment") {
        tuple_rvalue_stack.top().push_back({lvalue_node_stack.top(), primary_node_stack.top()});
        lvalue_node_stack.pop();
        primary_node_stack.pop();
      } else if (node_type == "simple_declaration") {
        tuple_rvalue_stack.top().push_back({lvalue_node_stack.top(), {Lnast_node::create_const("_"), {}}});
        lvalue_node_stack.pop();
      } else {
        tuple_rvalue_stack.top().push_back({{Lnast_node::create_invalid(), {}}, primary_node_stack.top()});
        primary_node_stack.pop();
      }
      node = get_named_sibling(node);
    }

    auto tuple_name      = get_tmp_name();
    auto tuple_add_index = lnast->add_child(stmts_index, Lnast_node::create_tuple_add());
    lnast->add_child(tuple_add_index, Lnast_node::create_ref(tuple_name));
    for (const auto &p : tuple_rvalue_stack.top()) {
      if (p.first.first.is_invalid()) {
        add_ref_child(tuple_add_index, p.second);
      } else {
        auto assign_index = lnast->add_child(tuple_add_index, Lnast_node::create_assign());
        add_ref_child(assign_index, p.first);
        add_ref_child(assign_index, p.second);
      }
    }

    fmt::print("Tuple: {} = (", tuple_name);
    for (const auto &p : tuple_rvalue_stack.top()) {
      if (p.first.first.is_invalid()) {
        fmt::print(" {}", p.second.first.token.get_text());
      } else {
        fmt::print(" {} = {}", p.first.first.token.get_text(), p.second.first.token.get_text());
      }
    }
    fmt::print(" )\n");

    tuple_rvalue_stack.pop();
    primary_node_stack.push({Lnast_node::create_ref(tuple_name), {}});
  } else {
    process_node(node);
  }
}

void Prp2lnast::process_tuple_type_list(TSNode node) {
  // FIXME: Temporary fix for generating input/output refs
  // WARNING: Will fail if the list contains assignment `xx = xx`
  node = get_named_child(node);
  while (!ts_node_is_null(node)) {
    process_node(node);
    primary_node_stack.pop();
    node = get_named_sibling(node);
  }
}

void Prp2lnast::process_declaration_list(TSNode node) {
  // FIXME: Temporary fix for generating input/output refs
  node = get_named_child(node);
  while (!ts_node_is_null(node)) {
    // TODO: `node` must be an identifier
    process_node(node);
    primary_node_stack.pop();
    node = get_named_sibling(node);
  }
}

void Prp2lnast::process_attribute_list(TSNode node) {
  node = get_named_child(node);
  while (!ts_node_is_null(node)) {
    process_node(node);
    node = get_named_sibling(node);
  }
}

void Prp2lnast::process_binary_expression(TSNode node) {
  auto lnode = get_child(node, "left");
  auto onode = get_child(node, "operator");
  auto rnode = get_child(node, "right");

  fmt::print("`{}` {} `{}`\n", get_text(lnode), get_text(onode), get_text(rnode));

  auto op = get_text(onode);
  Lnast_node lnast_node;

  // Regular math operators
  if (op == "+")
    lnast_node = Lnast_node::create_plus();
  else if (op == "-")
    lnast_node = Lnast_node::create_minus();
  else if (op == "*")
    lnast_node = Lnast_node::create_mult();
  else if (op == "/")
    lnast_node = Lnast_node::create_div();
  else if (op == "&")
    lnast_node = Lnast_node::create_bit_and();
  else if (op == "^")
    lnast_node = Lnast_node::create_bit_xor();
  else if (op == "|")
    lnast_node = Lnast_node::create_bit_or();
  else if (op == "<")
    lnast_node = Lnast_node::create_lt();
  else if (op == "<=")
    lnast_node = Lnast_node::create_le();
  else if (op == ">")
    lnast_node = Lnast_node::create_gt();
  else if (op == ">=")
    lnast_node = Lnast_node::create_ge();
  else if (op == "==")
    lnast_node = Lnast_node::create_eq();
  else if (op == "!=")
    lnast_node = Lnast_node::create_ne();
  else
    lnast_node = Lnast_node::create_invalid();

  // TODO: Merge to child node if
  //  (1) same op as the child node
  //  (2) op takes n arguments
  //
  if (!lnast_node.is_invalid()) {
    process_node(lnode);
    auto lhs = primary_node_stack.top();
    primary_node_stack.pop();
    process_node(rnode);
    auto rhs = primary_node_stack.top();
    primary_node_stack.pop();

    auto expr_index = lnast->add_child(stmts_index, lnast_node);
    auto ref = get_tmp_ref();
    
    lnast->add_child(expr_index, ref);
    add_ref_child(expr_index, lhs);
    add_ref_child(expr_index, rhs);

    primary_node_stack.push({ref, {}});
    return;
  }

  // Special operators
  // Range
  if (op == "..=" || op == "..<" || op == "..+") {
    process_node(lnode);
    auto lhs = primary_node_stack.top();
    primary_node_stack.pop();
    process_node(rnode);
    auto rhs = primary_node_stack.top();
    primary_node_stack.pop();

    if (op == "..=") {
      auto minus_index = lnast->add_child(stmts_index, Lnast_node::create_minus());
      auto ref = get_tmp_ref();
      lnast->add_child(minus_index, ref);
      add_ref_child(minus_index, rhs);
      lnast->add_child(minus_index, Lnast_node::create_const("1"));
      rhs = {ref, {}};
    } else if (op == "..+") {
      auto plus_index = lnast->add_child(stmts_index, Lnast_node::create_plus());
      auto ref = get_tmp_ref();
      lnast->add_child(plus_index, ref);
      add_ref_child(plus_index, lhs);
      add_ref_child(plus_index, rhs);
      rhs = {ref, {}};
    }
    auto range_index = lnast->add_child(stmts_index, Lnast_node::create_range());
    auto ref = get_tmp_ref();
    lnast->add_child(range_index, ref);
    add_ref_child(range_index, lhs);
    add_ref_child(range_index, rhs);

    primary_node_stack.push({ref, {}});
    return;
  }
}

void Prp2lnast::process_unary_expression(TSNode node) {
  auto op_node = get_child(node, "operator");
  auto arg_node = get_child(node, "argument");

  fmt::print("{} `{}`\n", get_text(op_node), get_text(arg_node));

  auto op = get_text(op_node);
  Lnast_node lnast_node;

  // Regular math operators
  if (op == "!" || op == "not")
    lnast_node = Lnast_node::create_logical_not();
  else if (op == "~")
    lnast_node = Lnast_node::create_bit_not();
  else if (op == "-")
    lnast_node = Lnast_node::create_minus();
  else
    lnast_node = Lnast_node::create_invalid();

  // TODO: Merge to child node if
  //  (1) same op as the child node
  //  (2) op takes n arguments
  //
  if (!lnast_node.is_invalid()) {
    process_node(arg_node);
    auto arg = primary_node_stack.top();
    primary_node_stack.pop();

    auto expr_index = lnast->add_child(stmts_index, lnast_node);
    auto ref = get_tmp_ref();
    
    lnast->add_child(expr_index, ref);
    add_ref_child(expr_index, arg);

    primary_node_stack.push({ref, {}});
    return;
  }
}

void Prp2lnast::process_dot_expression(TSNode node) {
  fmt::print("-> dot_expression `{}`\n", get_text(node));

  node = get_named_child(node);

  // First node (ref)
  process_node(node);
  node     = get_named_sibling(node);
  auto lhs = primary_node_stack.top();
  primary_node_stack.pop();

  expr_state_stack.push(Expression_state::Const);
  while (!ts_node_is_null(node)) {
    fmt::print("-> item `{}`\n", get_text(node));
    process_node(node);
    fmt::print("{} {}\n", select_stack.size(), primary_node_stack.top().first.token.get_text());
    select_stack.top().emplace_back(primary_node_stack.top());
    primary_node_stack.pop();
    node = get_named_sibling(node);
  }
  expr_state_stack.pop();

  switch (expr_state_stack.top()) {
    case Expression_state::Rvalue: {
      auto tuple_get_index = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
      auto ref = get_tmp_ref();
      lnast->add_child(tuple_get_index, ref);
      add_ref_child(tuple_get_index, lhs);
      for (auto i : select_stack.top()) {
        add_ref_child(tuple_get_index, i);
      }
      primary_node_stack.push({ref, {}});
      select_stack.top().clear();
    } break;
    case Expression_state::Lvalue: primary_node_stack.push(lhs);
    default: break;
  }
}

void Prp2lnast::process_member_selection(TSNode node) {
  auto lnode = get_child(node, "argument");
  auto rnode = get_child(node, "select");

  fmt::print("-> member_selection `{}` `{}`\n", get_text(lnode), get_text(rnode));

  process_node(lnode);
  auto lhs = primary_node_stack.top();
  primary_node_stack.pop();

  expr_state_stack.push(Expression_state::Rvalue);
  process_node(rnode);
  expr_state_stack.pop();

  switch (expr_state_stack.top()) {
    case Expression_state::Rvalue: {
      auto tuple_get_index = lnast->add_child(stmts_index, Lnast_node::create_tuple_get());
      auto ref = get_tmp_ref();
      lnast->add_child(tuple_get_index, ref);
      add_ref_child(tuple_get_index, lhs);
      for (auto i : select_stack.top()) {
        add_ref_child(tuple_get_index, i);
      }
      primary_node_stack.push({ref, {}});
      select_stack.top().clear();
    } break;
    case Expression_state::Lvalue: primary_node_stack.push(lhs); break;
    default: break;
  }
}

void Prp2lnast::process_member_select(TSNode node) {
  node = get_child(node);
  while (!ts_node_is_null(node)) {
    process_node(node);
    select_stack.top().emplace_back(primary_node_stack.top());
    primary_node_stack.pop();
    node = get_sibling(node);
  }
}

void Prp2lnast::process_select(TSNode node) {
  fmt::print("-> select `{}`\n", get_text(node));

  select_stack.push(std::vector<node_attr_t>());
  if (!ts_node_is_null(get_child(node, "list"))) {
    process_node(get_child(node, "list"));
  } else if (!ts_node_is_null(get_child(node, "open_range"))) {
    // TODO: select -> open_range
  } else if (!ts_node_is_null(get_child(node, "from_zero"))) {
    // TODO: select -> from_zero
  } else {
    // TODO: select -> empty
  }
  select_stack.pop();
}

void Prp2lnast::process_identifier(TSNode node) {
  auto name = get_text(node);
  fmt::print("-> identifier `{}`\n", name);
  switch (expr_state_stack.top()) {
    case Expression_state::Decl:
    case Expression_state::Type:
    case Expression_state::Lvalue:
    case Expression_state::Rvalue:
      if (ref_name_map.count(name) == 0) {
        std::string directed_name(name);
        if (is_function_input) {
          directed_name = absl::StrCat("$.", name);
        } else if (is_function_output) {
          directed_name = absl::StrCat("%.", name);
        }
        ref_name_map[name] = directed_name;
      }
      fmt::print("ref = {}\n", ref_name_map[name]);
      primary_node_stack.push({Lnast_node::create_ref(ref_name_map[name]), {}});
      // FIXME: Temporary fix to avoid default initialize-to-zero strategy
      /*
        if (expr_state_stack.top() == Expression_state::Type) {
        auto assign_index = lnast->add_child(stmts_index, Lnast_node::create_assign());
        lnast->add_child(assign_index, Lnast_node::create_ref(ref_name_map[name]));
        lnast->add_child(assign_index, Lnast_node::create_const("0b?"));
      }
      */
      break;
    case Expression_state::Attr:
      fmt::print("attr = {}\n", name);
      primary_node_stack.top().second.push_back({Lnast_node::create_attr(name), Lnast_node::create_invalid()});
      break;
    case Expression_state::Const:
      primary_node_stack.push({Lnast_node::create_const(name), {}}); break;
    default: break;
  }
}

void Prp2lnast::process_constant(TSNode node) {
  auto text = get_text(node);
  primary_node_stack.push({Lnast_node::create_const(text), {}});
}

void Prp2lnast::process_attributes(TSNode node) {
  enter_scope(Expression_state::Attr);
  process_node(get_named_child(node));
  leave_scope();
}

void Prp2lnast::add_ref_child(lh::Tree_index parent, node_attr_t p) {
  auto ref_index = lnast->add_child(parent, p.first);
  for (auto &[key, value] : p.second) {
    auto attr_index = lnast->add_child(ref_index, key);
    if (!value.is_invalid()) lnast->add_child(attr_index, value);
  }
  p.second.clear();
}

void Prp2lnast::add_ref_child(lh::Tree_index parent) {
  add_ref_child(parent, primary_node_stack.top());
}

void Prp2lnast::add_ref_child_conditional(lh::Tree_index parent) {
  if (primary_node_stack.top().second.size() > 0) add_ref_child(parent);
}

inline std::string Prp2lnast::get_tmp_name() { return absl::StrCat("___t", tmp_ref_count++); }

inline Lnast_node Prp2lnast::get_tmp_ref() { return Lnast_node::create_ref(get_tmp_name()); }

inline TSNode Prp2lnast::get_child(const TSNode &node, const char *field) const {
  return ts_node_child_by_field_name(node, field, std::char_traits<char>::length(field));
}

inline void Prp2lnast::enter_scope(Expression_state expr_state) {
  fmt::print("enter_scope({})\n", str(expr_state));
  expr_state_stack.push(expr_state);
}

inline void Prp2lnast::leave_scope() {
  fmt::print("leave_scope({})\n", str(expr_state_stack.top()));
  expr_state_stack.pop();
}

inline std::string Prp2lnast::str(Expression_state state) {
  switch (state) {
    case Expression_state::Type:
      return "type";
    case Expression_state::Lvalue:
      return "lvalue";
    case Expression_state::Rvalue:
      return "rvalue";
    case Expression_state::Const:
      return "const";
    case Expression_state::Decl:
      return "decl";
    case Expression_state::Attr:
      return "attr";
  }
  return "invalid";
}

inline TSNode Prp2lnast::get_child(const TSNode &node, unsigned int index) const { return ts_node_child(node, index); }

inline TSNode Prp2lnast::get_child(const TSNode &node) const { return ts_node_child(node, 0); }

inline TSNode Prp2lnast::get_sibling(const TSNode &node) const { return ts_node_next_sibling(node); }

inline TSNode Prp2lnast::get_named_child(const TSNode &node) const { return ts_node_named_child(node, 0); }

inline TSNode Prp2lnast::get_named_sibling(const TSNode &node) const { return ts_node_next_named_sibling(node); }
