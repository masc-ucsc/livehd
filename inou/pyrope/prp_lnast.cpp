//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_lnast.hpp"

#include "fmt/format.h"
#include "pass.hpp"

Prp_lnast::Prp_lnast() {
  in_lhs                = false;
  last_temp_var_counter = 1;
}

void Prp_lnast::dump(mmap_lib::Tree_index idx) const {
  for (const auto &index : ast->depth_preorder(idx)) {
    const auto &node = ast->get_data(index);
    std::string indent(index.level, ' ');
    fmt::print("{} l:{} p:{} rule = {}, token = {}\n",
               indent,
               index.level,
               index.pos,
               rule_id_to_string(node.rule_id),
               scan_text(node.token_entry));
  }
}

mmap_lib::str Prp_lnast::get_temp_string() {
  static mmap_lib::str current_temp_var("___t");

  return mmap_lib::str::concat(current_temp_var, last_temp_var_counter++);
}

Lnast_node Prp_lnast::get_lnast_temp_ref() {
  last_temp_var = get_temp_string();

  // Remember last_temp_var becuase chained expressions use it
  return Lnast_node::create_ref(last_temp_var);
}

/*
 * Translation functions
 */

Lnast_node Prp_lnast::eval_rule(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  auto node = ast->get_data(idx_start_ast);
  switch (node.rule_id) {
    case Prp_invalid: PRINT_DBG_LN("Prp_invalid\n"); break;
    case Prp_rule: PRINT_DBG_LN("Prp_rule\n"); break;
    case Prp_rule_start: PRINT_DBG_LN("Prp_start\n"); break;
    case Prp_rule_code_blocks: PRINT_DBG_LN("Prp_rule_code_blocks\n"); break;
    case Prp_rule_code_block_int: PRINT_DBG_LN("Prp_rule_code_block_int\n"); break;
    case Prp_rule_if_statement:
      PRINT_DBG_LN("Prp_rule_if_statement\n");
      eval_if_statement(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_else_statement: PRINT_DBG_LN("Prp_rule_else_statement\n"); break;
    case Prp_rule_for_statement:
      PRINT_DBG_LN("Prp_rule_for_statement\n");
      eval_for_statement(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_while_statement:
      PRINT_DBG_LN("Prp_rule_while_statement\n");
      eval_while_statement(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_try_statement: PRINT_DBG_LN("Prp_rule_try_statement\n"); break;
    case Prp_rule_punch_format: PRINT_DBG_LN("Prp_rule_punch_format\n"); break;
    case Prp_rule_function_pipe: PRINT_DBG_LN("Prp_rule_function_pipe\n"); break;
    case Prp_rule_fcall_explicit:
      PRINT_DBG_LN("Prp_rule_fcall_explicit\n");
      return eval_fcall_explicit(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_fcall_implicit:
      PRINT_DBG_LN("Prp_rule_fcall_implicit\n");
      return eval_fcall_implicit(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_for_index:
      PRINT_DBG_LN("Prp_rule_for_index\n");
      eval_for_index(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_assignment_expression:
      PRINT_DBG_LN("Prp_rule_assignment_expression\n");
      if (ast->get_data(ast->get_last_child(idx_start_ast)).rule_id == Prp_rule_scope_declaration) {
        return eval_scope_declaration(idx_start_ast, idx_start_ln);
      }
      eval_assignment_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_logical_expression:
      PRINT_DBG_LN("Prp_rule_logical_expression\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_relational_expression:
      PRINT_DBG_LN("Prp_rule_relational_expression\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_additive_expression:
      PRINT_DBG_LN("Prp_rule_additive_expression\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_bitwise_expression:
      PRINT_DBG_LN("Prp_rule_bitwise_expression\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_multiplicative_expression:
      PRINT_DBG_LN("Prp_rule_multiplicative_expression\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_unary_expression:
      PRINT_DBG_LN("Prp_rule_unary_expression\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_factor: PRINT_DBG_LN("Prp_rule_factor\n"); break;
    case Prp_rule_tuple_by_notation: PRINT_DBG_LN("Prp_rule_tuple_by_notation\n"); break;
    case Prp_rule_tuple_notation_no_bracket: PRINT_DBG_LN("Prp_rule_tuple_notation_no_bracket\n"); break;
    case Prp_rule_tuple_notation:
      PRINT_DBG_LN("Prp_rule_tuple_notation\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_tuple_notation_with_object: PRINT_DBG_LN("Prp_rule_tuple_notation_with_object\n"); break;
    case Prp_rule_range_notation:
      PRINT_DBG_LN("Prp_rule_range_notation\n");
      return eval_tuple(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_bit_selection_bracket: PRINT_DBG_LN("Prp_rule_bit_selection_bracket\n"); break;
    case Prp_rule_bit_selection_notation: {
      PRINT_DBG_LN("Prp_rule_bit_selection_notation\n");
      Lnast_node invalid;
      return eval_bit_selection_notation(idx_start_ast, invalid);
      break;
    }
    case Prp_rule_tuple_array_bracket: PRINT_DBG_LN("Prp_rule_tuple_array_bracket\n"); break;
    case Prp_rule_tuple_array_notation:
      PRINT_DBG_LN("Prp_rule_tuple_array_notation\n");
      return eval_tuple_array_notation(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_lhs_expression: PRINT_DBG_LN("Prp_rule_lhs_expression\n"); break;
    case Prp_rule_lhs_var_name: PRINT_DBG_LN("Prp_rule_var_name\n"); break;
    case Prp_rule_rhs_expression_property: PRINT_DBG_LN("Prp_rule_rhs_expression_property\n"); break;
    case Prp_rule_rhs_expression: PRINT_DBG_LN("Prp_rule_rhs_expression\n"); break;
    case Prp_rule_identifier:
      PRINT_DBG_LN("Prp_rule_identifier\n");
      return eval_expression(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_reference:
      if (node.token_entry == 0) {
        return eval_fluid_ref(idx_start_ast, idx_start_ln);
      }
      return Lnast_node::create_ref(get_token(node.token_entry));
      break;
    case Prp_rule_constant:
      PRINT_DBG_LN("Prp_rule_constant\n");
      return create_const_node(idx_start_ast);
      break;
    case Prp_rule_assignment_operator: PRINT_DBG_LN("Prp_rule_assignment_operator\n"); break;
    case Prp_rule_tuple_dot_notation:
      PRINT_DBG_LN("Prp_rule_tuple_dot_notation\n");
      return eval_tuple_dot_notation(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_tuple_dot_dot: PRINT_DBG_LN("Prp_rule_tuple_dot_dot\n"); break;
    case Prp_rule_overload_notation: PRINT_DBG_LN("Prp_rule_overload_notation\n"); break;
    case Prp_rule_scope_else: PRINT_DBG_LN("Prp_rule_scope_else\n"); break;
    case Prp_rule_scope_body: PRINT_DBG_LN("Prp_rule_scope_body\n"); break;
    case Prp_rule_scope_declaration:
      PRINT_DBG_LN("Prp_rule_scope_declaration\n");
      return eval_scope_declaration(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_scope: PRINT_DBG_LN("Prp_rule_scope\n"); break;
    case Prp_rule_scope_condition: PRINT_DBG_LN("Prp_rule_scope_condition\n"); break;
    case Prp_rule_scope_argument: PRINT_DBG_LN("Prp_rule_scope_argument\n"); break;
    case Prp_rule_punch_rhs: PRINT_DBG_LN("Prp_rule_punch_rhs\n"); break;
    case Prp_rule_fcall_arg_notation:
      PRINT_DBG_LN("Prp_rule_fcall_arg_notation\n");
      eval_fcall_arg_notation(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_return_statement: PRINT_DBG_LN("Prp_rule_return_statement\n"); break;
    case Prp_rule_compile_check_statement: PRINT_DBG_LN("Prp_rule_compile_check_statement\n"); break;
    case Prp_rule_block_body: PRINT_DBG_LN("Prp_rule_block_body\n"); break;
    case Prp_rule_empty_scope_colon: PRINT_DBG_LN("Prp_rule_empty_scope_colon\n"); break;
    case Prp_rule_assertion_statement:
      PRINT_DBG_LN("Prp_rule_assertion_statement\n");
      // eval_assertion_statement(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_negation_statement: PRINT_DBG_LN("Prp_rule_negation_statement\n"); break;
    case Prp_rule_scope_colon: PRINT_DBG_LN("Prp_rule_scope_colon\n"); break;
    case Prp_rule_numerical_constant:
      PRINT_DBG_LN("Prp_rule_numerical_constant\n");
      return create_const_node(idx_start_ast);
      break;
    case Prp_rule_string_constant:
      PRINT_DBG_LN("Prp_rule_string_constant\n");
      return create_const_node(idx_start_ast);
      break;
    case Prp_rule_overload_name: PRINT_DBG_LN("Prp_rule_overload_name\n"); break;
    case Prp_rule_overload_exception: PRINT_DBG_LN("Prp_rule_overload_exception\n"); break;
    case Prp_rule_for_in_notation:
      PRINT_DBG_LN("Prp_rule_for_in_notation\n");
      return eval_for_in_notation(idx_start_ast, idx_start_ln);
      break;
    case Prp_rule_not_in_implicit: PRINT_DBG_LN("Prp_rule_not_in_implicit\n"); break;
    case Prp_rule_keyword: PRINT_DBG_LN("Prp_rule_keyword\n"); break;
    default: break;
  }
  return Lnast_node();  // should be invalid
}

void Prp_lnast::translate_code_blocks(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Rule_id term_rule,
                                      bool check_return_stmt) {
  if (ast->get_data(idx_start_ast).token_entry != 0)
    return;

  auto nxt_idx_ast = ast->get_child(idx_start_ast);
  if (nxt_idx_ast.is_invalid())
    return;

  if (ast->get_data(nxt_idx_ast).rule_id == Prp_rule_code_blocks)
    nxt_idx_ast = ast->get_child(nxt_idx_ast);

  while (!nxt_idx_ast.is_invalid()) {
    if (check_return_stmt) {
      PRINT_DBG_LN("looking for return statements.\n");
      // look ahead by one rule
      auto lookahead_idx_ast = ast->get_sibling_next(nxt_idx_ast);
      if (lookahead_idx_ast.is_invalid()
          || (ast->get_data(lookahead_idx_ast).rule_id == term_rule
              && (ast->get_data(lookahead_idx_ast).token_entry != 0))) {  // if the next rule is valid, just continue on
        // we are the last rule. Check if it should be converted to a return statement
        auto cur_node = ast->get_data(nxt_idx_ast);
        if (cur_node.rule_id == Prp_rule_reference) {
          current_return_node = Lnast_node::create_ref(get_token(cur_node.token_entry));
          return;
        } else if (cur_node.rule_id == Prp_rule_constant || cur_node.rule_id == Prp_rule_numerical_constant
                   || cur_node.rule_id == Prp_rule_string_constant) {
          current_return_node = create_const_node(nxt_idx_ast);
          return;
        } else if (cur_node.rule_id == Prp_rule_fcall_implicit) {
          auto idx_fcall_imp_child = ast->get_child(nxt_idx_ast);
          // if it's just one node, just return it as a reference, otherwise continue
          if (ast->get_sibling_next(idx_fcall_imp_child).is_invalid()) {
            current_return_node = Lnast_node::create_ref(get_token(ast->get_data(idx_fcall_imp_child).token_entry));
            return;
          }
        }
        // otherwise, just continue
      }
    }
    if (ast->get_data(nxt_idx_ast).rule_id == term_rule && (ast->get_data(nxt_idx_ast).token_entry != 0)) {
      break;
    }
    // pass to eval rule the roots of second level subtrees
    if (cur_stmts.is_invalid()) {
      // create a statements node if we are the direct child of root
      idx_start_ln = lnast->add_child(idx_start_ln, Lnast_node::create_stmts());
      cur_stmts    = idx_start_ln;
      idx_start_ln = cur_stmts;
    }
    eval_rule(nxt_idx_ast, idx_start_ln);
    nxt_idx_ast = ast->get_sibling_next(nxt_idx_ast);
  }
}

void Prp_lnast::eval_fcall_arg_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  /*if (cur_stmts == lnast->invalid_index()) {
    // create a statements node if we are the direct child of root
    auto lnast_seq    = lnast->add_string("___SEQ" + std::to_string(current_seq++));
    auto subtree_root = Lnast_node::create_stmts(lnast_seq);
    idx_nxt_ln        = lnast->add_child(idx_nxt_ln, subtree_root);
    cur_stmts         = idx_nxt_ln;
  }*/

  // go down to the (
  auto idx_nxt_ast = ast->get_child(idx_start_ast);

  // skip the (
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

  // we are an empty function call
  if (scan_text(ast->get_data(idx_nxt_ast).token_entry) == ")") {
    return;
  }

  bool next = true;
  while (next) {
    auto node = eval_rule(idx_nxt_ast, idx_nxt_ln);
    lnast->add_child(idx_start_ln, node);
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    if (scan_text(ast->get_data(idx_nxt_ast).token_entry) == ")")
      return;
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
  }
}

void Prp_lnast::eval_for_index(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  auto idx_nxt_ln  = idx_start_ln;
  auto idx_nxt_ast = idx_start_ast;

  bool next = true;
  while (next) {
    // evaluate the first element
    eval_rule(idx_nxt_ln, idx_nxt_ast);
    // check if there are more elements
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    if (idx_nxt_ast.is_invalid()) {
      next = false;
    } else {
      idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);  // go to the next rule
    }
  }
}

// WARNING: can we always pass an assignment expression to this? nope!
Lnast_node Prp_lnast::eval_scope_declaration(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                             Lnast_node name_node) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  // check if the root is an assignment expression
  auto idx_nxt_ast = idx_start_ast;
  bool is_anon     = true;
  if (ast->get_data(idx_nxt_ast).rule_id == Prp_rule_assignment_expression) {
    idx_nxt_ast = ast->get_child(idx_start_ast);
    is_anon     = false;
  }

  // first, we need to check whether the condition is an expression
  auto idx_scope_dec     = !is_anon ? ast->get_last_child(idx_start_ast) : idx_start_ast;  // scope dec
  auto idx_cond_nxt_ast  = ast->get_child(idx_scope_dec);                                  // scope
  idx_cond_nxt_ast       = ast->get_child(idx_cond_nxt_ast);                               // :
  auto        idx_fcall  = ast->get_sibling_next(idx_cond_nxt_ast);  // scope cond or fcall_args or another colon (no arguments)
  const auto &fcall_node = ast->get_data(idx_fcall);                 // it's called fcall, but it could be any of those three

  mmap_lib::Tree_index idx_cond_ast;
  Lnast_node           cond_lhs;
  if (fcall_node.token_entry == 0) {
    idx_cond_ast = ast->get_last_child(idx_fcall);  // expression for fcall truth
    if (is_expr(idx_cond_ast)) {
      cond_lhs = eval_rule(idx_cond_ast, idx_start_ln);
    }
  }

  auto idx_func_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_func_def());

  // add the name of the function
  Lnast_node retnode;
  if (!is_anon) {
    retnode = Lnast_node::create_ref(get_token(ast->get_data(idx_nxt_ast).token_entry));
  } else {
    if (name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
      retnode = name_node;
    } else {
      retnode = get_lnast_temp_ref();
    }
  }
  lnast->add_child(idx_func_root, retnode);

  // add true to the condition
  lnast->add_child(idx_func_root, Lnast_node::create_const("true"));

  // move to the scope body
  idx_nxt_ast = ast->get_sibling_next(ast->get_child(idx_scope_dec));
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
  // create the new statements node
  auto idx_stmts = lnast->add_child(idx_func_root, Lnast_node::create_stmts());

  // translate the scope statements
  auto old_stmts = cur_stmts;
  cur_stmts      = idx_stmts;
  translate_code_blocks(idx_nxt_ast, idx_stmts, Prp_rule_scope_body, true);

  cur_stmts = old_stmts;

  // translate the scope argument (which can only be fcall arg notation for now)
  // evaluate the fcall arg notation, if present
  if (fcall_node.token_entry == 0)
    eval_rule(idx_fcall, idx_func_root);
  if (current_return_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
    lnast->add_child(idx_func_root, current_return_node);
    current_return_node = Lnast_node();
  }
  // is there a scope else?
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
  if (!idx_nxt_ast.is_invalid()) {
    PRINT_DBG_LN("Found a scope_else\n");
    // move down to the else code blocks
    idx_nxt_ast = ast->get_last_child(idx_nxt_ast);
    // create the new statements node
    auto idx_stmts_else = lnast->add_child(idx_func_root, Lnast_node::create_stmts());

    // translate the scope statements
    old_stmts = cur_stmts;
    cur_stmts = idx_stmts_else;
    translate_code_blocks(idx_nxt_ast, idx_stmts_else, Prp_rule_scope_body, true);
    if (current_return_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
      lnast->add_child(idx_func_root, current_return_node);
      current_return_node = Lnast_node();
    }

    cur_stmts = old_stmts;
  }

  return retnode;
}

void Prp_lnast::eval_while_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  auto idx_nxt_ast = ast->get_child(idx_start_ast);

  // evaluate the while condition
  auto while_cond = eval_rule(idx_nxt_ast, idx_nxt_ln);

  // create the while node
  auto idx_while_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_while());

  // add the condition
  if (while_cond.token.tok == Token_id_num)
    lnast->add_child(idx_while_root, Lnast_node::create_const(while_cond.token));
  else
    lnast->add_child(idx_while_root, Lnast_node::create_ref(while_cond.token));

  // skip the opening {
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

  // go to the block body
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

  // create statements node
  auto idx_stmts = lnast->add_child(idx_while_root, Lnast_node::create_stmts());

  // evaluate the block inside the while
  auto old_stmts = cur_stmts;
  cur_stmts      = idx_stmts;
  translate_code_blocks(idx_nxt_ast, idx_stmts, Prp_rule_block_body);
  cur_stmts = old_stmts;
}

void Prp_lnast::eval_for_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  auto idx_for_index = ast->get_child(idx_start_ast);
  auto idx_nxt_ast   = idx_for_index;

  // check if the current rule is a for_index; if so, we have multiple for ins
  if (ast->get_data(idx_nxt_ast).rule_id == Prp_rule_for_index) {
    // go down to the for... in condition
    PRINT_DBG_LN("Found a for index node.\n");
    idx_nxt_ast = ast->get_child(idx_nxt_ast);
  }

  bool                    next = true;
  std::vector<Lnast_node> iterators;
  std::vector<Lnast_node> iterator_range;

  while (next) {
    auto idx_for_in_root = idx_nxt_ast;

    auto idx_for_counter = ast->get_child(idx_nxt_ast);

    idx_nxt_ast = ast->get_sibling_next(idx_for_counter);  // go to the in token

    auto idx_tuple = ast->get_sibling_next(idx_nxt_ast);  // what is the value in?

    PRINT_DBG_LN("The for expression is rule {}.\n", rule_id_to_string(ast->get_data(idx_tuple).rule_id));

    iterators.push_back(Lnast_node::create_ref(get_token(ast->get_data(idx_for_counter).token_entry)));

    iterator_range.push_back(eval_rule(idx_tuple, idx_nxt_ln));
    PRINT_DBG_LN("Evaluated a for..in rule.\n");
    idx_nxt_ast = ast->get_sibling_next(idx_for_in_root);
    PRINT_DBG_LN("Moved past the the for in root\n");
    if (idx_nxt_ast.is_invalid()) {
      next = false;
    } else {
      PRINT_DBG_LN("The next index was not an invalid index.\n");
      if (ast->get_data(idx_nxt_ast).rule_id != Prp_rule_for_index) {
        PRINT_DBG_LN("The next index was not an invalid index.\n");
        next = false;
      } else {
        PRINT_DBG_LN("Moving to next for...in.\n");
        idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
      }
    }
  }
  // evaluate the for conditon
  // this node is the root of the value at x: for in x

  // skip the {
  idx_nxt_ast = ast->get_sibling_next(idx_for_index);

  // move to the block body
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

  auto idx_for_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_for());

  // add our new statements node
  auto idx_stmts = lnast->add_child(idx_for_root, Lnast_node::create_stmts());

  // add the name of our index tracking value
  for (auto i = 0u; i < iterators.size(); i++) {
    lnast->add_child(idx_for_root, iterators[i]);
    lnast->add_child(idx_for_root, iterator_range[i]);
  }

  auto old_stmts = cur_stmts;
  cur_stmts      = idx_stmts;
  translate_code_blocks(idx_nxt_ast, idx_stmts, Prp_rule_block_body);
  cur_stmts = old_stmts;
}

void Prp_lnast::eval_if_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  // first step: determine whether it is "if" or "unique if"
  auto idx_nxt_ast = ast->get_child(idx_start_ast);

  auto cur_ast = ast->get_data(idx_nxt_ast);

  Lnast_node root_if_node;

  if (cur_ast.rule_id == Prp_rule_if_statement) {  // conditioned if
    if (scan_text(cur_ast.token_entry) == "if") {  // if
      root_if_node = Lnast_node::create_if();
    } else if (scan_text(cur_ast.token_entry) == "unique") {  // unique ifs
      root_if_node = Lnast_node::create_uif();
      idx_nxt_ast  = ast->get_sibling_next(idx_nxt_ast);
    } else {  // just a block of code with no condition
      idx_nxt_ln = lnast->add_child(idx_nxt_ln, Lnast_node::create_if());
      lnast->add_child(idx_nxt_ln, Lnast_node::create_const("true"));
      idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

      auto idx_stmts = lnast->add_child(idx_nxt_ln, Lnast_node::create_stmts());
      auto old_stmts = cur_stmts;
      cur_stmts      = idx_stmts;

      translate_code_blocks(idx_nxt_ast, idx_stmts, Prp_rule_block_body);

      cur_stmts = old_stmts;
      return;
    }

    std::list<Lnast_node> cond_nodes;
    // loop over the if tree and evaluate conditions

    idx_nxt_ast            = ast->get_sibling_next(idx_nxt_ast);
    auto lookahead_idx_ast = idx_nxt_ast;

    // first step: evaluate all conditions
    while (!lookahead_idx_ast.is_invalid()) {
      cur_ast = ast->get_data(lookahead_idx_ast);
      if (cur_ast.rule_id
          == Prp_rule_else_statement) {  // we need to set the index to the same spot as it would be for the first if
        lookahead_idx_ast = ast->get_child(lookahead_idx_ast);
        lookahead_idx_ast
            = ast->get_sibling_next(lookahead_idx_ast);  // now at either condition or brace (for elif and else respectively)
        cur_ast = ast->get_data(lookahead_idx_ast);
      }
      PRINT_DBG_LN("checking condition expression in lookahead.\n");
      if (cur_ast.rule_id != Prp_rule_empty_scope_colon) {
        auto token = eval_rule(lookahead_idx_ast, idx_nxt_ln).token;
        if (token.tok == Token_id_num)
          cond_nodes.push_back(Lnast_node::create_const(token));
        else
          cond_nodes.push_back(Lnast_node::create_ref(token));

        lookahead_idx_ast = ast->get_sibling_next(lookahead_idx_ast);
      } else {
        break;
      }

      lookahead_idx_ast = ast->get_sibling_next(lookahead_idx_ast);
      lookahead_idx_ast = ast->get_sibling_next(lookahead_idx_ast);
    }

    idx_nxt_ln = lnast->add_child(idx_nxt_ln, root_if_node);

    // second step: evaluate the statements
    while (!idx_nxt_ast.is_invalid()) {
      cur_ast = ast->get_data(idx_nxt_ast);
      // check if we're looking at an else or an elif first
      if (cur_ast.rule_id
          == Prp_rule_else_statement) {  // we need to set the index to the same spot as it would be for the first if
        PRINT_DBG_LN("Before rule: {}.\n", rule_id_to_string(ast->get_data(idx_nxt_ast).rule_id));
        idx_nxt_ast = ast->get_child(idx_nxt_ast);
        idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);  // now at either condition or brace (for elif and else respectively)
        PRINT_DBG_LN("After rule: {}.\n", rule_id_to_string(ast->get_data(idx_nxt_ast).rule_id));
        cur_ast = ast->get_data(idx_nxt_ast);
      }

      if (cur_ast.rule_id != Prp_rule_empty_scope_colon) {
        lnast->add_child(idx_nxt_ln, cond_nodes.front());
        cond_nodes.pop_front();
        idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
      }

      // eval statements
      // add statements node
      auto idx_stmts = lnast->add_child(idx_nxt_ln, Lnast_node::create_stmts());
      auto old_stmts = cur_stmts;
      cur_stmts      = idx_stmts;
      // skip { token
      idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
      translate_code_blocks(idx_nxt_ast, idx_stmts, Prp_rule_block_body);
      idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
      cur_stmts   = old_stmts;
    }
  }
}

void Prp_lnast::eval_assignment_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  // Check if the assignment has an operator (+=, etc.)
  auto idx_lhs_ast   = ast->get_child(idx_start_ast);
  auto rhs_ast       = ast->get_last_child(idx_start_ast);
  auto idx_assign_op = ast->get_sibling_next(idx_lhs_ast);

  // if the assignment operator doesn't have a token, that means it has more than one child
  bool is_op_assign = ast->get_data(idx_assign_op).token_entry == 0;
  PRINT_DBG_LN("is_op_assign = {}\n", is_op_assign);
  if (!is_op_assign) {
    PRINT_DBG_LN("Our assignment operator does not have boolean/mathematical operator in it.\n");
    // next, check if we are a function declaration
    if (ast->get_data(rhs_ast).rule_id == Prp_rule_scope_declaration) {
      eval_scope_declaration(idx_start_ast, idx_nxt_ln);
      return;
    }

    // next, check if we are a function call
    if (ast->get_data(rhs_ast).rule_id == Prp_rule_fcall_explicit) {
      PRINT_DBG_LN("Found an assignment to a function call.\n");
      eval_fcall_explicit(idx_start_ast, idx_nxt_ln);
      return;
    }

    // or an implicit function call?
    if (ast->get_data(rhs_ast).rule_id == Prp_rule_fcall_implicit) {
      PRINT_DBG_LN("Found an assignment to a function call.\n");
      eval_fcall_implicit(idx_start_ast, idx_nxt_ln);
      return;
    }
  }

  Lnast_node rhs_node;
  // bool       rhs_is_leaf  = ast->get_data(rhs_ast).token_entry != 0 || ast->get_data(rhs_ast).rule_id ==
  // Prp_rule_string_constant;

  if (is_op_assign) {
    rhs_node = eval_expression(idx_start_ast, idx_nxt_ln);
  } else if (ast->get_data(rhs_ast).rule_id == Prp_rule_tuple_notation) {
    rhs_node = eval_tuple(rhs_ast, idx_nxt_ln);
  } else {
    rhs_node = eval_rule(rhs_ast, idx_nxt_ln);
  }

  // NOTE: (Mode of operation)
  //
  // in_lhs & in_lhs_dp_assign
  // __tmp = TupGet a.b.c.d    //
  // __tmp := rhs_node
  // TupAdd a.b.c.d __tmp
  //
  // in_lhs & !in_lhs_dp_assign
  // TupAdd a.b.c.d rhs_node
  //
  // !in_lhs
  // __tmp = TupGet a.b.c.d

  bool in_lhs_dp_assign = get_token(ast->get_data(idx_assign_op).token_entry).tok == Token_id_coloneq;
  I(!in_lhs);

  if (in_lhs_dp_assign) {
    // 1st: __tmp = TupGet/AttrGet a.b.c.d
    auto tmp_node = eval_rule(idx_lhs_ast, idx_start_ln);  // TupGet/AttrGet

    // 2nd: __tmp := rhs_node
    auto idx_assign = lnast->add_child(idx_nxt_ln, Lnast_node::create_dp_assign());
    lnast->add_child(idx_assign, tmp_node);
    lnast->add_child(idx_assign, rhs_node);

    // 3rd: TupAdd/AttSet a.b.c.d __tmp
    in_lhs_rhs_node = tmp_node;
    in_lhs_sel_root = idx_nxt_ln;
  } else {
    // Just TupAdd/AttrSet a.b.c.d rhs_node

    in_lhs_rhs_node = rhs_node;
    in_lhs_sel_root = idx_nxt_ln;
  }
  I(!in_lhs);
  in_lhs          = true;
  in_lhs_sel_root = mmap_lib::Tree_index();
  // first thing, create the lhs if it is an expression
  auto lhs_node = eval_rule(idx_lhs_ast, idx_start_ln);
  I(in_lhs);
  in_lhs = false;
  if (!lhs_node.is_invalid() && !in_lhs_sel_root.is_invalid()) {
    const auto &d = lnast->get_data(in_lhs_sel_root);
    if (d.type.is_set_mask())  // Why not the is_tup_add too??
      lnast->add_child(in_lhs_sel_root, in_lhs_rhs_node);
  } else if (!lhs_node.is_invalid() && lhs_node.token.get_text() != in_lhs_rhs_node.token.get_text()) {
    auto idx_assign = lnast->add_child(idx_nxt_ln, Lnast_node::create_assign());
    lnast->add_child(idx_assign, lhs_node);
    lnast->add_child(idx_assign, in_lhs_rhs_node);
  }
}

Lnast_node Prp_lnast::eval_tuple(const mmap_lib::Tree_index &idx_start_ast, const mmap_lib::Tree_index &idx_start_ln,
                                 mmap_lib::Tree_index idx_pre_tuple_vals, mmap_lib::Tree_index idx_post_tuple_vals) {
  auto idx_tuple_not_root = idx_start_ast;
  // first thing: find any expressions inside the tuple and save the rhs temporary variables
  // get the rhs of the tuple assignment
  I(!idx_start_ast.is_invalid());
  auto root_rid = ast->get_data(idx_start_ast).rule_id;
  if (root_rid == Prp_rule_assignment_expression) {
    idx_tuple_not_root = ast->get_last_child(idx_start_ast);
  }

  auto lnast_idx_main = evaluate_all_tuple_nodes(idx_tuple_not_root, idx_start_ln);

  Lnast_node lnast_idx_pre;
  if (!idx_pre_tuple_vals.is_invalid())
    lnast_idx_pre = evaluate_all_tuple_nodes(idx_pre_tuple_vals, idx_start_ln);

  Lnast_node lnast_idx_post;
  if (!idx_post_tuple_vals.is_invalid())
    lnast_idx_post = evaluate_all_tuple_nodes(idx_post_tuple_vals, idx_start_ln);

  if ((lnast_idx_main.type.is_ref() || lnast_idx_main.type.is_const()) && lnast_idx_pre.is_invalid()
      && lnast_idx_post.is_invalid()) {
    // Common case that avoid tuple concat
    return lnast_idx_main;
  }

  Lnast_node retnode;
  if (root_rid == Prp_rule_assignment_expression) {
    auto idx_assign_lhs = ast->get_child(idx_start_ast);
    retnode             = eval_rule(idx_assign_lhs, idx_start_ln);
  } else {
    retnode = get_lnast_temp_ref();
  }

  auto idx_tuple_root = lnast->add_child(idx_start_ln, Lnast_node::create_tuple_concat());

  lnast->add_child(idx_tuple_root, retnode);

  if (!lnast_idx_pre.is_invalid())
    lnast->add_child(idx_tuple_root, lnast_idx_pre);

  lnast->add_child(idx_tuple_root, lnast_idx_main);

  if (!lnast_idx_post.is_invalid())
    lnast->add_child(idx_tuple_root, lnast_idx_post);

  return retnode;
}

void Prp_lnast::add_tuple_nodes(mmap_lib::Tree_index idx_start_ln, std::vector<std::array<Lnast_node, 3>> &tuple_nodes) {
  for (const auto &node_subtrees : tuple_nodes) {
    if (node_subtrees[0].type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_invalid) {
      lnast->add_child(idx_start_ln, node_subtrees[2]);
    } else {
      auto idx_assign = lnast->add_child(idx_start_ln, node_subtrees[0]);
      lnast->add_child(idx_assign, node_subtrees[1]);
      lnast->add_child(idx_assign, node_subtrees[2]);
    }
  }
}

Lnast_node Prp_lnast::evaluate_all_tuple_nodes(const mmap_lib::Tree_index &idx_start_ast,
                                               const mmap_lib::Tree_index &idx_start_ln) {
  I(!idx_start_ast.is_invalid());

  std::vector<std::array<Lnast_node, 3>> tuple_nodes;

  auto retnode = get_lnast_temp_ref();

  auto cur_node = ast->get_data(idx_start_ast);
  if (cur_node.rule_id == Prp_rule_tuple_notation || cur_node.rule_id == Prp_rule_fcall_arg_notation) {
    auto idx_tup_el_next = ast->get_child(idx_start_ast);
    idx_tup_el_next      = ast->get_sibling_next(idx_tup_el_next);

    while (true) {
      auto tup_el_cur = ast->get_data(idx_tup_el_next);
      if (get_token(tup_el_cur.token_entry).tok == Token_id_cp) {
        break;
      }
      Lnast_node assign_root;
      Lnast_node assign_lhs;
      Lnast_node rhs;
      if (tup_el_cur.rule_id == Prp_rule_assignment_expression) {
        auto idx_assign_lhs      = ast->get_child(idx_tup_el_next);
        assign_lhs               = eval_rule(idx_assign_lhs, cur_stmts);
        auto idx_assign_operator = ast->get_sibling_next(idx_assign_lhs);
        if (ast->get_data(idx_assign_operator).token_entry == 0) {  // must have an expression operator
          rhs         = eval_expression(idx_tup_el_next, cur_stmts);
          assign_root = Lnast_node::create_assign();
        } else {
          auto idx_rhs = ast->get_sibling_next(idx_assign_operator);
          PRINT_DBG_LN("Evaluating the rhs of a tuple element.\n");
          rhs = eval_rule(idx_rhs, cur_stmts);
          if (get_token(ast->get_data(idx_assign_operator).token_entry).tok == Token_id_eq) {
            assign_root = Lnast_node::create_assign();
          } else {
            I(get_token(ast->get_data(idx_assign_operator).token_entry).tok == Token_id_coloneq);
            assign_root = Lnast_node::create_dp_assign();
          }
        }
      } else {
        assign_root = Lnast_node();
        assign_lhs  = Lnast_node();
        rhs         = eval_rule(idx_tup_el_next, cur_stmts);
      }
      std::array<Lnast_node, 3> nodes = {assign_root, assign_lhs, rhs};
      tuple_nodes.push_back(nodes);
      idx_tup_el_next         = ast->get_sibling_next(idx_tup_el_next);
      auto tup_el_comma_maybe = ast->get_data(idx_tup_el_next);
      if (get_token(tup_el_comma_maybe.token_entry).tok == Token_id_cp) {
        break;
      }
      idx_tup_el_next = ast->get_sibling_next(idx_tup_el_next);
    }
    idx_tup_el_next = ast->get_sibling_next(idx_tup_el_next);

    if (!idx_tup_el_next.is_invalid()) {
      const auto &tup_el_next_data = ast->get_data(idx_tup_el_next);
      I(tup_el_next_data.rule_id != Prp_rule_bit_selection_notation);  // Is notation used??
      if (tup_el_next_data.rule_id == Prp_rule_bit_selection_bracket) {
        dump(idx_tup_el_next);

        if (tuple_nodes.size() == 1) {
          return eval_bit_selection_notation(idx_tup_el_next, tuple_nodes[0][2]);
        }
        // auto idx_tuple_root = lnast->add_child(idx_start_ln, Lnast_node::create_tuple()); //DEBUG
        auto idx_tuple_root = lnast->add_child(idx_start_ln, Lnast_node::create_tuple_add());
        lnast->add_child(idx_tuple_root, retnode);
        add_tuple_nodes(idx_tuple_root, tuple_nodes);

        return eval_bit_selection_notation(idx_tup_el_next, Lnast_node::create_ref(retnode.token.get_text()));
      }
    }
  } else if (cur_node.rule_id == Prp_rule_range_notation) {
    auto idx_range_next = ast->get_child(idx_start_ast);
    auto range_el       = ast->get_data(idx_range_next);

    auto       assign_root          = Lnast_node::create_assign();
    auto       range_start_sentinel = Lnast_node::create_const("__range_begin");
    Lnast_node range_start;
    bool       range_start_is_null = false;
    if (range_el.token_entry != 0) {
      if (get_token(range_el.token_entry).tok == Token_id_colon) {
        range_start         = Lnast_node();
        range_start_is_null = true;
      }
    }

    if (!range_start_is_null) {
      range_start = eval_rule(idx_range_next, cur_stmts);
    }

    std::array<Lnast_node, 3> nodes_start = {assign_root, range_start_sentinel, range_start};

    idx_range_next = ast->get_sibling_next(idx_range_next);  // Skip :
    if (!range_start_is_null) {
      idx_range_next = ast->get_sibling_next(idx_range_next);
    }

    auto       range_end_sentinel = Lnast_node::create_const("__range_end");
    Lnast_node range_end;
    bool       range_end_is_null = false;
    if (idx_range_next.is_invalid()) {
      range_end         = Lnast_node::create_const("0b?");
      range_end_is_null = true;
    }

    if (!range_end_is_null) {
      range_end = eval_rule(idx_range_next, cur_stmts);
    }

    std::array<Lnast_node, 3> nodes_end = {assign_root, range_end_sentinel, range_end};

    tuple_nodes.emplace_back(nodes_start);
    tuple_nodes.emplace_back(nodes_end);

    // TODO: by notation
  } else {
    std::array<Lnast_node, 3> nodes = {Lnast_node(), Lnast_node(), eval_rule(idx_start_ast, cur_stmts)};
    tuple_nodes.emplace_back(nodes);
  }

  if (tuple_nodes.size() == 1 && tuple_nodes[0][1].is_invalid()) {
    return tuple_nodes[0][2];
  }

  // auto idx_tuple_root = lnast->add_child(idx_start_ln, Lnast_node::create_tuple()); //DEBUG
  auto idx_tuple_root = lnast->add_child(idx_start_ln, Lnast_node::create_tuple_add());
  lnast->add_child(idx_tuple_root, retnode);
  add_tuple_nodes(idx_tuple_root, tuple_nodes);

  return retnode;
}

Lnast_node Prp_lnast::eval_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  (void)idx_start_ln;
  mmap_lib::Tree_index idx_nxt_ln  = cur_stmts;
  auto                 idx_nxt_ast = idx_start_ast;

  std::list<Lnast_node> operand_stack;
  std::list<Lnast_node> operator_stack;
  Lnast_node            assign_operator, assign_operand;
  bool                  has_assign_op = false;
  // first, we need to see if we're an expression of the form (op)=, e.g. +=
  if (ast->get_data(idx_nxt_ast).rule_id == Prp_rule_assignment_expression) {
    has_assign_op        = true;
    idx_nxt_ast          = ast->get_child(idx_nxt_ast);
    auto operand_special = ast->get_data(idx_nxt_ast);

    if (operand_special.token_entry != 0)  // can only be an identifier
      assign_operand = Lnast_node::create_ref(get_token(operand_special.token_entry));
    else {
      assign_operand = eval_rule(idx_nxt_ast, idx_nxt_ln);
    }

    // move to the op= node
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

    auto    idx_op_ast = ast->get_child(idx_nxt_ast);
    uint8_t skip_sibs;
    assign_operator = gen_operator(idx_op_ast, &skip_sibs);

    // go to the root of the RHS expression, like normal
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
  }

  if (ast->get_data(idx_nxt_ast).rule_id == Prp_rule_tuple_notation && !has_assign_op) {
    return eval_tuple(idx_nxt_ast, idx_nxt_ln);
  }

  mmap_lib::Tree_index child_cur;
  if (is_expr_with_operators(idx_nxt_ast))
    child_cur = ast->get_child(idx_nxt_ast);
  else
    child_cur = idx_nxt_ast;
  auto expr_line = get_token(ast->get_data(child_cur).token_entry).line;

  Lnast_node  op_node_last;
  bool        last_op_valid = false;
  mmap_lib::str last_op_overload_name;
  while (!child_cur.is_invalid()) {
    const auto &child_cur_data = ast->get_data(child_cur);
    PRINT_DBG_LN("Rule name: {}, etoken text: {}\n",
                 rule_id_to_string(child_cur_data.rule_id),
                 scan_text(child_cur_data.token_entry));
    if (child_cur_data.token_entry != 0 || child_cur_data.rule_id == Prp_rule_string_constant
        || child_cur_data.rule_id == Prp_rule_overload_notation) {  // is a leaf
      if (child_cur_data.rule_id == Prp_rule_identifier) {          // identifier
        uint8_t skip_sibs;
        auto    op_node = gen_operator(child_cur, &skip_sibs);
        operator_stack.emplace_back(op_node);
      } else if (child_cur_data.rule_id == Prp_rule_reference) {
        operand_stack.emplace_back(Lnast_node::create_ref(get_token(child_cur_data.token_entry)));
      } else if (child_cur_data.rule_id == Prp_rule_numerical_constant || child_cur_data.rule_id == Prp_rule_string_constant) {
        operand_stack.emplace_back(create_const_node(child_cur));
      } else if (child_cur_data.rule_id == Prp_rule_sentinel) {
        // get the index of the operator that is not inside the parentheses
        auto sentinel_op_idx = ast->get_sibling_next(child_cur);
        child_cur            = sentinel_op_idx;
        // generate that operator and put it into the stack
        uint8_t skip_sibs;
        auto    op_node = gen_operator(sentinel_op_idx, &skip_sibs);
        for (int i = 0; i < skip_sibs; i++) child_cur = ast->get_sibling_next(child_cur);
        // multiplication and division operators don't have their priority tracked by the parser
        bool evaluated_sub_expr = false;
        if (last_op_valid) {
          if ((op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_mult
               || op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_div)
              && (op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus
                  || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus)) {
            auto mult_expr_start = ast->get_sibling_prev(child_cur);
            operand_stack.emplace_back(eval_sub_expression(mult_expr_start, op_node));
            child_cur          = ast->get_sibling_next(child_cur);  // skip the next operand
            evaluated_sub_expr = true;
          }
        }

        if (!evaluated_sub_expr) {
          operator_stack.emplace_back(op_node);
          // evaluate the rest of the expression as though it were inside parentheses
          child_cur = ast->get_sibling_next(child_cur);
          operand_stack.emplace_back(eval_expression(child_cur, idx_nxt_ln));
          break;
        }
      } else {  // operator
        bool    sub_expr = false;
        uint8_t skip_sibs;
        auto    op_node = gen_operator(child_cur, &skip_sibs);
        if (last_op_valid) {
          // first check if we are a mult, and the previous was as an add
          if ((op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_mult
               || op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_div)
              && (op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus
                  || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus)) {
            sub_expr             = true;
            auto mult_expr_start = ast->get_sibling_prev(child_cur);
            operand_stack.emplace_back(eval_sub_expression(mult_expr_start, op_node));
            child_cur = ast->get_sibling_next(child_cur);  // skip the next operand
          }
        }
        if (!sub_expr) {
          if (last_op_valid) {
            if (op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
              if (last_op_overload_name != op_node.token.get_text()) {
                fmt::print("Operator priority error in expression around line {}.\n", expr_line + 1);
                exit(1);
              }
            } else {
              auto pri_op_cur = priority_map[op_node.type.get_raw_ntype()];
              if (pri_op_cur == priority_map[op_node_last.type.get_raw_ntype()]) {
                if (op_node.type.get_raw_ntype() != op_node_last.type.get_raw_ntype()) {
                  bool op0_pm = op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus
                                || op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus;
                  bool op1_pm = op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus
                                || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus;

                  bool op1_md = (op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_mult
                                 || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_div);
                  if (!(op0_pm && op1_pm) && !(op0_pm && op1_md)) {
                    fmt::print("Operator priority error in expression around line {}.\n", expr_line + 1);
                    exit(1);
                  }
                }
              }
            }
          }
          for (int i = 0; i < skip_sibs; i++) child_cur = ast->get_sibling_next(child_cur);
          op_node_last = op_node;
          if (op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_ref) {
            last_op_overload_name = op_node.token.get_text();
          } else {
            last_op_overload_name = mmap_lib::str();
          }
          last_op_valid = true;
          operator_stack.emplace_back(op_node);
        }
      }
    } else {
      PRINT_DBG_LN("Evaluating an operand that is not a leaf.\n");
      operand_stack.emplace_back(eval_rule(child_cur, idx_nxt_ln));
    }
    child_cur = ast->get_sibling_next(child_cur);
  }

  if (has_assign_op) {
    operator_stack.emplace_back(assign_operator);
  }

  for (auto it = operator_stack.begin(); it != operator_stack.end(); ++it) {
    // add the operator node as a child of the starting node
    // case of normal (non overload) operator
    if ((*it).type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_ref) {
      // create our lhs variable
      auto lhs = get_lnast_temp_ref();

      auto idx_operator_ln = lnast->add_child(idx_nxt_ln, *it);
      // add both operands and the LHS as a child of the operator unless the operator is a unary operator
      lnast->add_child(idx_operator_ln, lhs);  // LHS node
      auto op1 = operand_stack.front();
      operand_stack.pop_front();
      bool added_assign_op = false;
      if (it == std::prev(operator_stack.end())) {
        if (assign_operand.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
          lnast->add_child(idx_operator_ln, assign_operand);
          added_assign_op = true;
        }
      }
      lnast->add_child(idx_operator_ln, op1);  // first operand
      if (!(added_assign_op || it->type.is_bit_not() || it->type.is_logical_not())) {
        auto op2 = operand_stack.front();  // optional second operand
        operand_stack.pop_front();
        lnast->add_child(idx_operator_ln, op2);
      }
      while (true) {
        if (std::next(it, 1) != operator_stack.end()) {
          if ((*(std::next(it, 1))).type.get_raw_ntype() == (*it).type.get_raw_ntype()) {
            it++;
            bool added_assign_op_tail = false;
            if (it == std::prev(operator_stack.end())) {
              if (assign_operand.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
                added_assign_op_tail = true;
                lnast->add_child(idx_operator_ln, assign_operand);
              }
            }
            if (!added_assign_op_tail) {
              lnast->add_child(idx_operator_ln, operand_stack.front());
              operand_stack.pop_front();
            }
          } else
            break;
        } else
          break;
      }
      // push the lhs onto the operand stack
      operand_stack.emplace_front(lhs);
    } else {
      // overloaded operator case
      // first, create the tuple for the function call
      // auto idx_args_tuple = lnast->add_child(cur_stmts, Lnast_node::create_tuple()); //DEBUG
      auto idx_args_tuple = lnast->add_child(cur_stmts, Lnast_node::create_tuple_add());
      // create the tuple lhs variable
      auto lhs_tuple = get_lnast_temp_ref();

      lnast->add_child(idx_args_tuple, lhs_tuple);

      lnast->add_child(idx_args_tuple, operand_stack.front());
      operand_stack.pop_front();

      lnast->add_child(idx_args_tuple, operand_stack.front());
      operand_stack.pop_front();

      auto lhs = get_lnast_temp_ref();

      auto idx_operator_ln = lnast->add_child(idx_nxt_ln, Lnast_node::create_func_call());
      lnast->add_child(idx_operator_ln, lhs);
      lnast->add_child(idx_operator_ln, *it);
      lnast->add_child(idx_operator_ln, lhs_tuple);

      // push the lhs onto the operand stack
      operand_stack.emplace_front(lhs);
    }
  }

  return operand_stack.front();
}

Lnast_node Prp_lnast::eval_sub_expression(mmap_lib::Tree_index idx_start_ast, Lnast_node operator_node) {
  // evaluate a single binary expression op0 op op1. Assume that if op0 is an expression, it has already
  // been added to the lnast

  auto                 idx_nxt_ast = idx_start_ast;  // this points to the first operand
  mmap_lib::Tree_index idx_nxt_ln  = cur_stmts;

  auto op0_idx      = idx_nxt_ast;
  auto operator_idx = ast->get_sibling_next(op0_idx);  // can only be + or -
  auto op1_idx      = ast->get_sibling_next(operator_idx);

  auto op0         = Lnast_node::create_ref(last_temp_var);

  auto op1 = eval_rule(op1_idx, idx_nxt_ln);

  auto expr_root = lnast->add_child(idx_nxt_ln, operator_node);

  auto lhs = get_lnast_temp_ref();

  lnast->add_child(expr_root, lhs);
  lnast->add_child(expr_root, op0);
  lnast->add_child(expr_root, op1);

  return lhs;
}

Lnast_node Prp_lnast::eval_for_in_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  (void)idx_start_ln;
  auto idx_nxt_ln = cur_stmts;

  // this node is the root of the value at x: for in x
  auto idx_tuple = ast->get_last_child(idx_start_ast);

  PRINT_DBG_LN("The for expression is rule {}.\n", rule_id_to_string(ast->get_data(idx_tuple).rule_id));

  return eval_rule(idx_tuple, idx_nxt_ln);
}

Lnast_node Prp_lnast::eval_tuple_array_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = cur_stmts;

  // go down to the select expression on the AST
  auto tuple_arr_idx = ast->get_last_child(idx_start_ast);

  // this goes down to the opening bracket in the tuple array bracket node
  auto ast_nxt_idx = ast->get_child(tuple_arr_idx);

  // go to the actual select expression in that node
  ast_nxt_idx = ast->get_sibling_next(ast_nxt_idx);

  std::vector<Lnast_node> index_nodes;
  while (1) {
    // assume we're starting on the first actual node
    index_nodes.push_back(eval_rule(ast_nxt_idx, idx_start_ln));
    ast_nxt_idx = ast->get_sibling_next(ast_nxt_idx);  // goes to the close bracket
    ast_nxt_idx = ast->get_sibling_next(ast_nxt_idx);  // goes to the open bracket
    if (ast_nxt_idx.is_invalid())
      break;
    ast_nxt_idx = ast->get_sibling_next(ast_nxt_idx);  // go to the next element
  }

  // create the LHS temporary variable
  auto retnode = get_lnast_temp_ref();

#if 1
  mmap_lib::Tree_index idx_sel_root;
  if (in_lhs) {
    idx_sel_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_tuple_set());
  } else {
    idx_sel_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_tuple_get());
    lnast->add_child(idx_sel_root, retnode);
  }
#else
  auto idx_sel_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_select());
#endif

  // add the identifier of the tuple being selected
  lnast->add_child(idx_sel_root, Lnast_node::create_ref(get_token(ast->get_data(ast->get_child(idx_start_ast)).token_entry)));

  // add all the select indexes
  for (auto node : index_nodes) {
    lnast->add_child(idx_sel_root, node);
  }
  if (in_lhs) {
    lnast->add_child(idx_sel_root, in_lhs_rhs_node);
    in_lhs_rhs_node = retnode;
    in_lhs_sel_root = idx_sel_root;
  }

  return retnode;
}

Lnast_node Prp_lnast::eval_fcall_explicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                          mmap_lib::Tree_index idx_piped_val, Lnast_node piped_node, Lnast_node name_node) {
  // check if the root is an assignment expression
  const auto &root_rid    = ast->get_data(idx_start_ast).rule_id;
  auto        idx_nxt_ast = idx_start_ast;
  if (root_rid == Prp_rule_assignment_expression) {
    idx_nxt_ast = ast->get_last_child(idx_nxt_ast);
  }

  // whether we are an assignment expression or not, idx_nxt_ast will equal fcall_explicit

  // evaluate the rhs of the function call (the fcall_arg_notation), but it has to be a tuple

  auto idx_func_lhs           = ast->get_child(idx_nxt_ast);
  auto idx_func_name          = idx_func_lhs;
  bool uniform_call_tuple     = ast->get_data(idx_func_lhs).rule_id == Prp_rule_tuple_notation;
  bool uniform_call_tuple_dot = ast->get_data(idx_func_lhs).rule_id == Prp_rule_tuple_dot_notation;

  mmap_lib::Tree_index idx_uniform_call_els;
  I(idx_uniform_call_els.is_invalid());

  if (uniform_call_tuple) {
    PRINT_DBG_LN("Our function call uses UCS rooted at a tuple.\n");
    // go down to the (
    idx_uniform_call_els = idx_func_lhs;
    // point idx_func_lhs to the sibling of fcall_arg_notation
    idx_func_lhs  = ast->get_sibling_next(idx_func_lhs);  // sees the dot
    idx_func_lhs  = ast->get_sibling_next(idx_func_lhs);  // is now where it should be
    idx_func_name = idx_func_lhs;

  } else if (uniform_call_tuple_dot) {
    PRINT_DBG_LN("Our function call uses UCS rooted at a tuple dot notation.");
    idx_uniform_call_els = ast->get_child(idx_func_lhs);
    idx_func_name        = ast->get_sibling_next(idx_uniform_call_els);
    idx_func_name        = ast->get_last_child(idx_func_name);
  }

  Lnast_node arg_lhs;
  auto       idx_fcall_args = idx_nxt_ast;
  if (ast->get_data(ast->get_last_child(idx_nxt_ast)).rule_id == Prp_rule_fcall_arg_notation) {
    idx_fcall_args = ast->get_last_child(idx_nxt_ast);
    if (!idx_piped_val.is_invalid()) {
      if (ast->get_data(idx_piped_val).rule_id == Prp_rule_tuple_notation) {
        arg_lhs = eval_tuple(idx_fcall_args, idx_start_ln, idx_uniform_call_els, idx_piped_val);
      } else {
        arg_lhs               = eval_tuple(idx_fcall_args, idx_start_ln, idx_uniform_call_els);
        auto tuple_concat_lhs = get_lnast_temp_ref();

        auto concat_el        = eval_rule(idx_piped_val, idx_start_ln);
        auto idx_tuple_concat = lnast->add_child(cur_stmts, Lnast_node::create_tuple_concat());
        lnast->add_child(idx_tuple_concat, tuple_concat_lhs);
        lnast->add_child(idx_tuple_concat, arg_lhs);
        lnast->add_child(idx_tuple_concat, concat_el);
        arg_lhs = tuple_concat_lhs;
      }
    } else {
      arg_lhs = eval_tuple(idx_fcall_args, idx_start_ln, idx_uniform_call_els);
    }
    if (piped_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
      auto tuple_concat_lhs = get_lnast_temp_ref();

      auto idx_tuple_concat = lnast->add_child(cur_stmts, Lnast_node::create_tuple_concat());
      lnast->add_child(idx_tuple_concat, tuple_concat_lhs);
      lnast->add_child(idx_tuple_concat, arg_lhs);
      lnast->add_child(idx_tuple_concat, piped_node);

      arg_lhs = tuple_concat_lhs;
    }

  } else {
    idx_fcall_args = ast->get_sibling_next(idx_func_lhs);
    arg_lhs        = eval_tuple(idx_fcall_args, idx_start_ln, idx_uniform_call_els, idx_piped_val);
  }

  // check for scope declaration
  Lnast_node func_name     = Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry));
  auto       idx_scope_dec = ast->get_sibling_next(idx_fcall_args);
  if (!idx_scope_dec.is_invalid()) {
    if (ast->get_data(idx_scope_dec).rule_id == Prp_rule_scope_declaration) {
      PRINT_DBG_LN("Found a scope declaration to go with the explicit fcall.\n");
      eval_scope_declaration(idx_scope_dec, idx_start_ln, func_name);
      idx_fcall_args = idx_scope_dec;
    }
  }

  Lnast_node lhs_node;
  if (root_rid == Prp_rule_assignment_expression) {
    lhs_node = eval_rule(ast->get_child(idx_start_ast), idx_start_ln);
  } else if (name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
    lhs_node = name_node;
  } else {
    lhs_node = get_lnast_temp_ref();
  }

  // create the function call root
  auto idx_fcall_root = lnast->add_child(cur_stmts, Lnast_node::create_func_call());

  auto idx_pipe_maybe = ast->get_sibling_next(idx_fcall_args);

  if (!idx_pipe_maybe.is_invalid()) {
    auto rid = ast->get_data(idx_pipe_maybe).rule_id;
    if (rid == Prp_rule_function_pipe) {
      PRINT_DBG_LN("We found a function pipe.\n");
      // we have a pipe
      auto idx_next_fcall = ast->get_last_child(idx_pipe_maybe);

      // add the LHS of the function call
      Lnast_node intermediate_lhs;
      bool       is_assign_expr = root_rid == Prp_rule_assignment_expression;
      if (is_assign_expr) {
        // need to add a temporary node anyway
        intermediate_lhs = get_lnast_temp_ref();

        lnast->add_child(idx_fcall_root, intermediate_lhs);
      } else {
        lnast->add_child(idx_fcall_root, lhs_node);
      }

      // add the name of the function
      lnast->add_child(idx_fcall_root, func_name);

      // add the argument tuple
      lnast->add_child(idx_fcall_root, arg_lhs);

      if (idx_next_fcall.is_invalid()) {
        idx_next_fcall = ast->get_sibling_next(idx_pipe_maybe);
        if (idx_next_fcall.is_invalid()) {
          return lhs_node;
        }
        I(ast->get_data(idx_next_fcall).rule_id == Prp_rule_fcall_explicit);
      }

      if (is_assign_expr) {
        return eval_fcall_explicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), intermediate_lhs, lhs_node);
      } else if (name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
        return eval_fcall_explicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), lhs_node, name_node);
      } else {
        return eval_fcall_explicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), lhs_node);
      }
    }
    if (rid == Prp_rule_fcall_explicit) {
      auto        idx_after_fcall = ast->get_sibling_next(idx_pipe_maybe);
      const auto &data            = ast->get_data(idx_after_fcall);
      I(data.rule_id == Prp_rule_reference);  // fcall(x,y).foo  "foo"

      auto fcall_ret = get_lnast_temp_ref();
      lnast->add_child(idx_fcall_root, fcall_ret);
      lnast->add_child(idx_fcall_root, func_name);
      lnast->add_child(idx_fcall_root, arg_lhs);

      auto idx_tup_get = lnast->add_child(cur_stmts, Lnast_node::create_tuple_get());
      lnast->add_child(idx_tup_get, lhs_node);
      lnast->add_child(idx_tup_get, fcall_ret);
      lnast->add_child(idx_tup_get, Lnast_node::create_const(get_token(data.token_entry)));

      return lhs_node;
    } else {
      I(false);  // FIXME
    }
  }

  lnast->add_child(idx_fcall_root, lhs_node);   // add the LHS of the function call
  lnast->add_child(idx_fcall_root, func_name);  // add the name of the function
  lnast->add_child(idx_fcall_root, arg_lhs);    // add the argument tuple

  return lhs_node;
}

Lnast_node Prp_lnast::eval_fcall_implicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                          mmap_lib::Tree_index idx_piped_val, Lnast_node piped_node, Lnast_node name_node) {
  PRINT_DBG_LN("Evaluating an implicit function call.\n");
  auto        idx_root    = idx_start_ast;
  const auto &root_rid    = ast->get_data(idx_start_ast).rule_id;
  auto        idx_nxt_ast = idx_root;

  if (piped_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
    fmt::print("(implicit) The piped lnast node's text is {}\n", piped_node.token.get_text());
  }
  if (!idx_piped_val.is_invalid()) {
    fmt::print("(implicit) The piped index's token text is {}\n", scan_text(ast->get_data(idx_piped_val).token_entry));
  }

  if (root_rid == Prp_rule_assignment_expression) {
    PRINT_DBG_LN("Found an assignment expression in the fcall implicit.\n");
    idx_nxt_ast = ast->get_last_child(idx_root);
  }

  auto idx_func_name = ast->get_child(idx_nxt_ast);

  bool has_piped_value = !idx_piped_val.is_invalid() || piped_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid;
  bool is_plain_value  = false;

  // no name, anonymous function
  Lnast_node decl_func_name_node;
  if (ast->get_data(idx_func_name).rule_id == Prp_rule_scope_declaration) {
    // create function declaration node
    PRINT_DBG_LN("Found an inline anonymous function.\n");
    decl_func_name_node = eval_scope_declaration(idx_func_name, idx_start_ln);
  } else {
    if (!has_piped_value) {
      if (ast->get_data(ast->get_sibling_next(idx_func_name)).rule_id != Prp_rule_scope_declaration) {
        is_plain_value = true;
      }
    }
  }

  mmap_lib::Tree_index idx_scope_dec_maybe;
  idx_scope_dec_maybe = ast->get_sibling_next(idx_func_name);
  if (!idx_scope_dec_maybe.is_invalid()) {
    if (ast->get_data(idx_scope_dec_maybe).rule_id == Prp_rule_scope_declaration) {
      auto idx_scope_dec  = ast->get_sibling_next(idx_func_name);
      decl_func_name_node = eval_scope_declaration(idx_scope_dec,
                                                   idx_start_ln,
                                                   Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry)));
      idx_func_name       = idx_scope_dec;
    }
  }

  auto idx_pipe_maybe = ast->get_sibling_next(idx_func_name);

  mmap_lib::Tree_index idx_fcall_root = idx_start_ln;

  if (!idx_pipe_maybe.is_invalid()) {
    Lnast_node assign_lhs;
    if (root_rid == Prp_rule_assignment_expression) {
      auto idx_assign_lhs = ast->get_child(idx_start_ast);
      assign_lhs          = eval_rule(idx_assign_lhs, idx_start_ln);
    } else if (name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
      assign_lhs = name_node;
    }
    Lnast_node piped_node_new;
    if (!is_plain_value) {
      // create a temporary name
      piped_node_new = get_lnast_temp_ref();

      Lnast_node arg_tuple;
      if (piped_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
        arg_tuple = piped_node;
      } else if (!idx_piped_val.is_invalid()) {
        arg_tuple = eval_rule(idx_piped_val, idx_start_ln);
      }
      idx_fcall_root = lnast->add_child(cur_stmts, Lnast_node::create_func_call());
      lnast->add_child(idx_fcall_root, piped_node_new);
      if (decl_func_name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
        lnast->add_child(idx_fcall_root, decl_func_name_node);
      } else {
        lnast->add_child(idx_fcall_root, Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry)));
      }
      lnast->add_child(idx_fcall_root, arg_tuple);
    }
    auto idx_piped_fcall = ast->get_last_child(idx_pipe_maybe);
    if (ast->get_data(idx_piped_fcall).rule_id == Prp_rule_fcall_explicit) {
      if (is_plain_value) {
        return eval_fcall_explicit(idx_piped_fcall, idx_start_ln, idx_func_name, piped_node_new, assign_lhs);
      } else {
        return eval_fcall_explicit(idx_piped_fcall, idx_start_ln, ast->invalid_index(), piped_node_new, assign_lhs);
      }
    } else {
      if (is_plain_value) {
        return eval_fcall_implicit(idx_piped_fcall, idx_start_ln, idx_func_name, piped_node_new, assign_lhs);
      } else {
        return eval_fcall_implicit(idx_piped_fcall, idx_start_ln, ast->invalid_index(), piped_node_new, assign_lhs);
      }
    }
  }

  // we're done?
  Lnast_node retnode;
  if (root_rid == Prp_rule_assignment_expression) {
    auto idx_assign_lhs = ast->get_child(idx_start_ast);
    retnode             = eval_rule(idx_assign_lhs, idx_start_ln);
  } else if (name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
    retnode = name_node;
  } else {
    retnode = get_lnast_temp_ref();
  }

  // evaluate any piped values
  Lnast_node arg_tuple;
  if (piped_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
    arg_tuple = piped_node;
  } else if (!idx_piped_val.is_invalid()) {
    arg_tuple = eval_rule(idx_piped_val, idx_start_ln);
  }

  // create the function call root
  idx_fcall_root = lnast->add_child(cur_stmts, Lnast_node::create_func_call());

  // add the function LHS
  lnast->add_child(idx_fcall_root, retnode);

  // add the function name
  if (decl_func_name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid) {
    lnast->add_child(idx_fcall_root, decl_func_name_node);
  } else {
    lnast->add_child(idx_fcall_root, Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry)));
  }

  // add any piped arguments
  lnast->add_child(idx_fcall_root, arg_tuple);

  return retnode;
}

Lnast_node Prp_lnast::eval_tuple_dot_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  // root is a tuple_dot_notation node

  // move to the first element whose attribute is being accessed
  auto idx_nxt_ast = ast->get_child(idx_start_ast);

  // evaluate that first element on its own
  auto accessed_el = eval_rule(idx_nxt_ast, idx_start_ln);

  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
  idx_nxt_ast = ast->get_child(idx_nxt_ast);

  std::vector<Lnast_node> select_fields;

  while (!idx_nxt_ast.is_invalid()) {
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);  // move past the dot

    const auto &attribute_node = ast->get_data(idx_nxt_ast);

    Lnast_node accessed_attribute;
    if (attribute_node.rule_id == Prp_rule_tuple_array_notation) {
      // need to select the resolved attribute, not the name of the attribute
      auto idx_attribute      = ast->get_child(idx_nxt_ast);
      auto idx_attribute_node = ast->get_data(idx_attribute);

      I(idx_attribute_node.rule_id == Prp_rule_reference || idx_attribute_node.rule_id == Prp_rule_numerical_constant);
      accessed_attribute = Lnast_node::create_const(get_token(idx_attribute_node.token_entry));
      // if assertion fails: else accessed_attribute = eval_rule(idx_attribute, idx_start_ln);
      select_fields.emplace_back(accessed_attribute);

      auto idx_tuple_array_brack = ast->get_sibling_next(idx_attribute);
      auto idx_first_brack       = ast->get_child(idx_tuple_array_brack);
      auto idx_sel_idx_ast       = ast->get_sibling_next(idx_first_brack);
      while (!idx_sel_idx_ast.is_invalid()) {
        auto sel_index = eval_rule(idx_sel_idx_ast, idx_start_ln);

        select_fields.emplace_back(sel_index);

        idx_sel_idx_ast = ast->get_sibling_next(idx_sel_idx_ast);  // Skip sel_index
        idx_sel_idx_ast = ast->get_sibling_next(idx_sel_idx_ast);  // Skip ]
        if (idx_sel_idx_ast.is_invalid())
          break;

        idx_sel_idx_ast = ast->get_sibling_next(idx_sel_idx_ast);  // Skip [
      }
    } else {
      if (attribute_node.rule_id == Prp_rule_reference) {
        accessed_attribute = Lnast_node::create_const(get_token(attribute_node.token_entry));
      } else {
        accessed_attribute = eval_rule(idx_nxt_ast, idx_start_ln);
      }
      select_fields.emplace_back(accessed_attribute);
    }

    // go to the next dot, or to invalid index
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
  }

  // create the dot and all of its children
  mmap_lib::Tree_index idx_dot_root;

  bool is_attr = false;
  for (auto i = 0u; i < select_fields.size(); ++i) {
    auto txt = select_fields[i].token.get_text();
    if (txt.substr(0, 2) == "__" && txt[3] != '_') {
      if (is_attr) {
        mmap_lib::str v_all;
        for (const auto &v : select_fields) {
          v_all = mmap_lib::str::concat(v_all, ".", v.token.get_text());
        }
        Pass::error("Illegal to have attribute {} in the middle {}\n", txt, v_all);
      }
      is_attr = true;
    }
  }

  if (in_lhs) {
    if (in_lhs_sel_root.is_invalid()) {
      idx_dot_root = lnast->add_child(cur_stmts, Lnast_node::create_tuple_set());
    }
  } else if (is_attr) {  // rhs

    auto field = select_fields.back().token.get_text();
    if (field == "__create_flop" || field == "__last_value") {
      idx_dot_root = lnast->add_child(cur_stmts, Lnast_node::create_attr_get());
    } else {
      idx_dot_root = lnast->add_child(cur_stmts, Lnast_node::create_tuple_get());
    }
  } else {
    I(!is_attr);  // rhs
    idx_dot_root = lnast->add_child(cur_stmts, Lnast_node::create_tuple_get());
  }

  if (in_lhs) {
    if (in_lhs_sel_root.is_invalid()) {
      // TupAdd a.b.c.d rhs_node
      lnast->add_child(idx_dot_root, accessed_el);
      for (auto &lnode : select_fields) {
        lnast->add_child(idx_dot_root, lnode);
      }
      lnast->add_child(idx_dot_root, in_lhs_rhs_node);
    } else {
      // Last child is the value to copy. Must be preserved as last child
      auto last_child_idx  = lnast->get_last_child(in_lhs_sel_root);
      auto last_child_node = lnast->get_data(last_child_idx);

      lnast->set_data(last_child_idx, select_fields[0]);
      select_fields.emplace_back(last_child_node);

      for (auto i = 1u; i < select_fields.size(); ++i) {
        lnast->add_child(in_lhs_sel_root, select_fields[i]);
      }
    }

    Lnast_node invalid;
    return invalid;
  } else {
    // TupGet __tmp a.b.c.d
    Lnast_node dot_lhs = get_lnast_temp_ref();
    lnast->add_child(idx_dot_root, dot_lhs);

    lnast->add_child(idx_dot_root, accessed_el);
    for (auto &lnode : select_fields) {
      lnast->add_child(idx_dot_root, lnode);
    }

    return dot_lhs;
  }
}

Lnast_node Prp_lnast::eval_bit_selection_notation(mmap_lib::Tree_index idx_start_ast, const Lnast_node &lhs_node) {
  mmap_lib::Tree_index idx_nxt_ln = cur_stmts;

  mmap_lib::Tree_index lhs_var_idx;
  mmap_lib::Tree_index child_at_idx;
  if (lhs_node.is_invalid()) {
    lhs_var_idx          = ast->get_child(idx_start_ast);
    auto select_expr_idx = ast->get_sibling_next(lhs_var_idx);
    child_at_idx         = ast->get_child(select_expr_idx);  // @
  } else {
    child_at_idx = ast->get_child(idx_start_ast);
  }

  Lnast_node src_var;  // tmp or lhs_var

  if (lhs_node.is_invalid()) {
    auto data_var = ast->get_data(lhs_var_idx);
    if (data_var.rule_id == Prp_rule_reference) {
      src_var = Lnast_node::create_ref(get_token(data_var.token_entry));
    }else{
      // There was a foo.bar.xxx@(...) sequence
      src_var = eval_tuple(lhs_var_idx, idx_nxt_ln);
    }
  } else {
    src_var = lhs_node;
  }

  Lnast_node sel_rhs;

  bool sel_exists = false;
  I(get_token(ast->get_data(child_at_idx).token_entry).tok == Token_id_at);
  auto select_expr_idx = ast->get_sibling_next(child_at_idx);  // skip the @

  if (!select_expr_idx.is_invalid()) {
    bool close_parenthesis = get_token(ast->get_data(select_expr_idx).token_entry).tok == Token_id_cp;
    if (!close_parenthesis) {
      auto tmp = in_lhs;
      in_lhs   = false;  // foo@(x.b)  the x.b is not a tup_add

      sel_rhs = eval_tuple(select_expr_idx, idx_nxt_ln);

      in_lhs = tmp;

      sel_exists = true;
    }
  }

  Lnast_node retnode;

  // create bit select node
  mmap_lib::Tree_index idx_sel_root;
  Lnast_node           lr_bits_node;
  if (in_lhs) {
    if (!in_lhs_sel_root.is_invalid()) {
      // FIXME: current pyrope parser does not translate well BUT it is valid
      // foo@(1:3)@(1) = 1 // same as foo@(2) = 1
      Pass::error("FIXME: pyrope parser does not handle nested bit set in lhs");
    }

    mmap_lib::Tree_index idx_shl_root;
    Lnast_node           shl_node;
    if (sel_exists) {
      idx_shl_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_shl());
      shl_node     = get_lnast_temp_ref();
    }
    idx_sel_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_set_mask());

    retnode = src_var;

    in_lhs_sel_root = idx_sel_root;

    lnast->add_child(idx_sel_root, retnode);
    lnast->add_child(idx_sel_root, src_var);

    // add the rhs value of the select expression
    if (sel_exists) {
      lnast->add_child(idx_shl_root, shl_node);
      lnast->add_child(idx_shl_root, Lnast_node::create_const("1"));
      lnast->add_child(idx_shl_root, sel_rhs);

      lnast->add_child(idx_sel_root, shl_node);
    }
  } else {
    lr_bits_node = Lnast_node::create_get_mask();
    idx_sel_root = lnast->add_child(idx_nxt_ln, lr_bits_node);
    retnode      = get_lnast_temp_ref();

    lnast->add_child(idx_sel_root, retnode);
    lnast->add_child(idx_sel_root, src_var);

    // add the rhs value of the select expression
    if (sel_exists) {
      lnast->add_child(idx_sel_root, sel_rhs);
    }
  }

  auto another_child = ast->get_child(select_expr_idx);
  while (!another_child.is_invalid()) {
    auto rule_id = ast->get_data(another_child).rule_id;
    if (rule_id == Prp_rule_bit_selection_bracket) {
      retnode = eval_bit_selection_notation(another_child, retnode);
      break;
    }
    another_child = ast->get_sibling_next(another_child);
  }

  return retnode;
}

Lnast_node Prp_lnast::eval_fluid_ref(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  (void)idx_start_ast;
  (void)idx_start_ln;

  fmt::print("WARNING. The select syntax for fluid does not seem right beyond trivial cases like foo?\n");

#if 0
  auto idx_dot_root = lnast->add_child(cur_stmts, Lnast_node::create_select());

  // create the temporary variable for the LHS
  auto retnode = get_lnast_temp_ref();

  lnast->add_child(idx_dot_root, retnode);

  auto idx_ref = ast->get_child(idx_start_ast);

  // add the name of the variable
  lnast->add_child(idx_dot_root, Lnast_node::create_ref(get_token(ast->get_data(idx_ref).token_entry)));

  auto idx_first_fluid    = ast->get_sibling_next(idx_ref);
  auto first_command_text = scan_text(ast->get_data(idx_first_fluid).token_entry);
  if (first_command_text == "?") {
    lnast->add_child(idx_dot_root, Lnast_node::create_const("__valid"));
  } else {  // must be either ! or !!
    auto second_fluid = ast->get_sibling_next(idx_first_fluid);
    if (second_fluid.is_invalid()) {
      lnast->add_child(idx_dot_root, Lnast_node::create_const("__retry"));
    } else {
      lnast->add_child(idx_dot_root, Lnast_node::create_const("__fluid_reset"));
    }
  }
#else
  auto retnode = get_lnast_temp_ref();
#endif

  return retnode;
}

/*
 * Main function
 */
std::unique_ptr<Lnast> Prp_lnast::prp_ast_to_lnast(const mmap_lib::str &module_name) {
  lnast = std::make_unique<Lnast>(module_name);

  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  // generate_op_map();
  generate_priority_map();
  generate_expr_rules();

  translate_code_blocks(mmap_lib::Tree_index::root(), mmap_lib::Tree_index::root());

  return std::move(lnast);
}

/*
 * Translation helper functions
 */

Lnast_node Prp_lnast::gen_operator(mmap_lib::Tree_index idx, uint8_t *skip_sibs) {
  const auto &node = ast->get_data(idx);
  if (node.token_entry != 0) {  // normal operator, not overload
    auto tid   = scan_text(node.token_entry);
    *skip_sibs = 0;
    switch (tid[0]) {
      case '|': return Lnast_node::create_bit_or();
      case '&': return Lnast_node::create_bit_and();
      case '^': return Lnast_node::create_bit_xor();
      case '/': return Lnast_node::create_div();
      case '*': return Lnast_node::create_mult();
      case '=': return Lnast_node::create_eq();
      case '!':
        if (tid.size() > 1)
          return Lnast_node::create_ne();
        else
          return Lnast_node::create_logical_not();
      case '>':
        if (tid.size() > 1)
          return Lnast_node::create_ge();
        else {
          if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == ">") {
            idx = ast->get_sibling_next(idx);
            *skip_sibs = 1;
            return Lnast_node::create_sra();
          }
          return Lnast_node::create_gt();
        }
      case '<':
        if (tid.size() > 1)
          return Lnast_node::create_le();
        else {
          if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "<") {
            idx = ast->get_sibling_next(idx);
            if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "<") {
              *skip_sibs = 2;
              return Lnast_node::create_shl();
            }
            *skip_sibs = 1;
            return Lnast_node::create_shl();
          }
          return Lnast_node::create_lt();
        }
      case 'o': return Lnast_node::create_logical_or();
      case 'a': return Lnast_node::create_logical_and();
      case 'i': return Lnast_node::create_is();
      case '~': return Lnast_node::create_bit_not();
      case '+':
        if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "+") {
          *skip_sibs = 1;
          return Lnast_node::create_tuple_concat();
        }
        return Lnast_node::create_plus();
      case '-':
        return Lnast_node::create_minus();
      default:  // unimplemented operator
        Pass::error("Operator {} is not yet supported.", tid);
        return Lnast_node::create_phi();  // not sure what phi is
    }
  } else {  // overloaded operator
    *skip_sibs = 0;
    // return the function name node
    idx = ast->get_child(idx);         // first dot
    idx = ast->get_sibling_next(idx);  // second dot
    idx = ast->get_sibling_next(idx);  // name
    return Lnast_node::create_ref(get_token(ast->get_data(idx).token_entry));
  }
}

void Prp_lnast::generate_priority_map() {
  priority_map[Lnast_ntype::Lnast_ntype_logical_and]  = 3;
  priority_map[Lnast_ntype::Lnast_ntype_logical_or]   = 3;
  priority_map[Lnast_ntype::Lnast_ntype_gt]           = 2;
  priority_map[Lnast_ntype::Lnast_ntype_lt]           = 2;
  priority_map[Lnast_ntype::Lnast_ntype_ge]           = 2;
  priority_map[Lnast_ntype::Lnast_ntype_le]           = 2;
  priority_map[Lnast_ntype::Lnast_ntype_eq]           = 2;
  priority_map[Lnast_ntype::Lnast_ntype_ne]           = 2;
  priority_map[Lnast_ntype::Lnast_ntype_tuple_concat] = 1;
  priority_map[Lnast_ntype::Lnast_ntype_shl]          = 1;
  priority_map[Lnast_ntype::Lnast_ntype_sra]          = 1;
  priority_map[Lnast_ntype::Lnast_ntype_minus]        = 1;
  priority_map[Lnast_ntype::Lnast_ntype_plus]         = 1;
  priority_map[Lnast_ntype::Lnast_ntype_bit_xor]      = 1;
  priority_map[Lnast_ntype::Lnast_ntype_bit_or]       = 1;
  priority_map[Lnast_ntype::Lnast_ntype_bit_and]      = 1;
  priority_map[Lnast_ntype::Lnast_ntype_div]          = 1;
  priority_map[Lnast_ntype::Lnast_ntype_mult]         = 1;
  priority_map[Lnast_ntype::Lnast_ntype_ref]          = 1;  // overload
}

/*
inline void Prp_lnast::generate_priority_matrix() {
  auto op_list[] = {Lnast_ntype::Lnast_ntype_logical_and, Lnast_ntype::Lnast_ntype_logical_or, Lnast_ntype::Lnast_ntype_gt,
Lnast_ntype::Lnast_ntype_lt, Lnast_ntype::Lnast_ntype_ge, Lnast_ntype::Lnast_ntype_le, Lnast_ntype::Lnast_ntype_same,
Lnast_ntype::Lnast_ntype_eq, Lnast_ntype::Lnast_ntype_tuple_concat, Lnast_ntype::Lnast_ntype_shift_left,
Lnast_ntype::Lnast_ntype_shift_right, Lnast_ntype::Lnast_ntype_rotate_shift_left, Lnast_ntype::Lnast_ntype_rotate_shift_right,
Lnast_ntype::Lnast_ntype_minus, Lnast_ntype::Lnast_ntype_plus, Lnast_ntype::Lnast_ntype_xor, Lnast_ntype::Lnast_ntype_or,
Lnast_ntype::Lnast_ntype_and, Lnast_ntype::Lnast_ntype_div,Lnast_ntype::Lnast_ntype_mult};

  for(auto op0 : op_list){
    for(auto op1 : op_list){
      if(priority_map[op0] == priority_map)
    }
  }
}
*/

void Prp_lnast::generate_expr_rules() {
  expr_rules.insert(Prp_rule_logical_expression);
  expr_rules.insert(Prp_rule_relational_expression);
  expr_rules.insert(Prp_rule_additive_expression);
  expr_rules.insert(Prp_rule_bitwise_expression);
  expr_rules.insert(Prp_rule_multiplicative_expression);
  expr_rules.insert(Prp_rule_unary_expression);
  expr_rules.insert(Prp_rule_tuple_notation);
  expr_rules.insert(Prp_rule_tuple_array_notation);
  expr_rules.insert(Prp_rule_identifier);
  expr_rules.insert(Prp_rule_tuple_dot_notation);
  expr_rules.insert(Prp_rule_fcall_explicit);
  expr_rules.insert(Prp_rule_bit_selection_notation);
  expr_rules.insert(Prp_rule_scope_declaration);
}

bool Prp_lnast::is_expr(mmap_lib::Tree_index idx) {
  const auto &node = ast->get_data(idx);
  if (node.token_entry != 0)
    return false;
  if (expr_rules.find(node.rule_id) != expr_rules.end()) {
    PRINT_DBG_LN("Found an expression node.\n");
    return true;
  }
  return false;
}

inline bool Prp_lnast::is_expr_with_operators(mmap_lib::Tree_index idx) {
  const auto &node = ast->get_data(idx);
  if (node.token_entry != 0)
    return false;
  if (node.rule_id == Prp_rule_logical_expression || node.rule_id == Prp_rule_relational_expression
      || node.rule_id == Prp_rule_additive_expression || node.rule_id == Prp_rule_identifier
      || node.rule_id == Prp_rule_unary_expression) {
    return true;
  }
  return false;
}

inline uint8_t Prp_lnast::maybe_child_expr(mmap_lib::Tree_index idx) {
  const auto &rule_id = ast->get_data(idx).rule_id;
  if (rule_id == Prp_rule_assignment_expression) {  // TODO: add more true cases, if applicable
    if (ast->get_data(ast->get_last_child(idx)).rule_id == Prp_rule_tuple_notation
        || ast->get_data(ast->get_last_child(idx)).rule_id == Prp_rule_scope_declaration) {
      return 2;
    }
    return true;
  }
  return false;
}

inline void Prp_lnast::create_simple_lhs_expr(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                              Lnast_node rhs_node) {
  const auto &base_rule_node = ast->get_data(idx_start_ast);
  if (base_rule_node.rule_id == Prp_rule_assignment_expression) {
    auto expr_root_idx = lnast->add_child(idx_start_ln, Lnast_node::create_assign());
    auto expr_lhs_idx  = ast->get_child(idx_start_ast);
    lnast->add_child(expr_root_idx, Lnast_node::create_ref(get_token(ast->get_data(expr_lhs_idx).token_entry)));
    lnast->add_child(expr_root_idx, rhs_node);
  } else {
    PRINT_LN("ERROR: simple lhs expression was passed an invalid AST node.\n");
  }
}

inline Lnast_node Prp_lnast::create_const_node(mmap_lib::Tree_index idx) {
  const auto &node             = ast->get_data(idx);
  bool        is_string        = node.rule_id == Prp_rule_string_constant;
  auto        node_token_entry = node.token_entry;
  if (is_string) {
    if (node_token_entry) {  // this must be a double (") string
      return Lnast_node::create_const(get_token(node_token_entry));
    } else {  // merge the single ' string into one token
      auto idx_cur_string = ast->get_child(idx);
      // skip the starting '
      idx_cur_string              = ast->get_sibling_next(idx_cur_string);
      uint64_t    string_length   = 0;
      auto        cur_token_entry = ast->get_data(idx_cur_string).token_entry;
      auto        cur_token       = get_token(cur_token_entry);
      mmap_lib::str new_token_text;
      auto        string_start    = cur_token.pos1;
      while (scan_text(cur_token_entry) != "\'") {
        if (string_length != 0) {
          // insert space(s)
          auto spaces_needed = cur_token.pos1 - (string_start + string_length);
          new_token_text = new_token_text.append(spaces_needed, ' ');
        }
        string_length += cur_token.get_text().size();
        new_token_text = mmap_lib::str::concat(new_token_text, cur_token.get_text());
        idx_cur_string  = ast->get_sibling_next(idx_cur_string);
        cur_token_entry = ast->get_data(idx_cur_string).token_entry;
        cur_token       = get_token(cur_token_entry);
      }
      auto new_token_view = new_token_text;
      return Lnast_node::create_const(new_token_view, cur_token.line, string_start, string_start + string_length + 1);
    }
  } else {  // we need to add the datatype to the token string if it's implicit (decimal only)
    bool negative = false;
    if (node_token_entry == 0) {  // negative sign
      negative         = true;
      auto idx_num     = ast->get_last_child(idx);
      node_token_entry = ast->get_data(idx_num).token_entry;
    }
    auto token = get_token(node_token_entry);
    if (negative) {
      auto ln_decimal_view = token.get_text().prepend('-');
      return Lnast_node::create_const(ln_decimal_view, token.line, token.pos1, token.pos2);
    } else {
      return Lnast_node::create_const(token);
    }
  }
}

inline bool Prp_lnast::is_decimal(const mmap_lib::str &number) {
  if (number.size() < 3)  // must have at least 0x/n/b(something), otherwise it must be decimal
    return true;
  if (number[0] == 't' || number[2] == 'l') {
    return false;  // must be "true" or "false"
  }

  if (number[0] != '0')
    return true;  // must start with 0;

  if (number[1] != 'x' || number[1] != 'X' || number[1] == 'b' || number[1] == 'B')
    return false;

  return true;
}
