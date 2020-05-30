//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "prp_lnast.hpp"

#include "fmt/format.h"

/*
 * Utility functions
 */
void print_tree_index(mmap_lib::Tree_index idx) { PRINT_DBG_LN("level = {}, pos = {}\n", (int)idx.level, (int)idx.pos); }

void Prp_lnast::print_ast_node(mmap_lib::Tree_index idx) {
  auto node = ast->get_data(idx);
  PRINT_DBG_LN("rule = {}, token = {}\n", rule_id_to_string(node.rule_id), scan_text(node.token_entry));
}

void Prp_lnast::get_next_temp_var() {
  int carry = 1;
  int pos   = current_temp_var.size() - 1;
  last_temp_var = current_temp_var;

  while (current_temp_var[pos] != '_') {
    int headroom = 'z' - current_temp_var[pos];
    carry -= headroom;
    if (carry > 0) {
      current_temp_var[pos] = 'a';
    } else {
      current_temp_var[pos] += carry + headroom;
      return;
    }
    pos--;
  }

  if (carry > 0) {
    current_temp_var.insert(pos + 1, "a");
  }
}

/*
 * Translation functions
 */

Lnast_node Prp_lnast::eval_rule(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, bool add_singles) {
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
      if(ast->get_data(ast->get_last_child(idx_start_ast)).rule_id == Prp_rule_scope_declaration){
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
    case Prp_rule_bit_selection_notation:
      PRINT_DBG_LN("Prp_rule_bit_selection_notation\n");
      return eval_bit_selection_notation(idx_start_ast, idx_start_ln);
      break;
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
      if(node.token_entry == 0){
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
      //eval_assertion_statement(idx_start_ast, idx_start_ln);
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

void Prp_lnast::translate_code_blocks(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Rule_id term_rule) {
  if (ast->get_data(idx_start_ast).token_entry != 0) return;

  auto nxt_idx_ast = ast->get_child(idx_start_ast);

  if (ast->get_data(nxt_idx_ast).rule_id == Prp_rule_code_blocks) nxt_idx_ast = ast->get_child(nxt_idx_ast);

  while (nxt_idx_ast != ast->invalid_index()) {
    if (ast->get_data(nxt_idx_ast).rule_id == term_rule && (ast->get_data(nxt_idx_ast).token_entry != 0)) {
      break;
    }
    // pass to eval rule the roots of second level subtrees
    if (cur_stmts == lnast->invalid_index()){
      // create a statements node if we are the direct child of root
      auto lnast_seq = lnast->add_string("___SEQ" + std::to_string(current_seq++));
      idx_start_ln   = lnast->add_child(idx_start_ln, Lnast_node::create_stmts(lnast_seq));
      cur_stmts      = idx_start_ln;
      idx_start_ln   = cur_stmts;
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
  if(scan_text(ast->get_data(idx_nxt_ast).token_entry) == ")"){
    return;
  }

  bool next = true;
  while(next){
    auto node = eval_rule(idx_nxt_ast, idx_nxt_ln);
    lnast->add_child(idx_start_ln, node);
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    if (scan_text(ast->get_data(idx_nxt_ast).token_entry) == ")") return;
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
  }
}

void Prp_lnast::eval_for_index(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln){
  auto idx_nxt_ln = idx_start_ln;
  auto idx_nxt_ast = idx_start_ast;
  
  bool next = true;
  while(next){
    // evaluate the first element
    eval_rule(idx_nxt_ln, idx_nxt_ast);
    // check if there are more elements
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    if(idx_nxt_ast != ast->invalid_index()){ // are we on the ;?
      idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast); // go to the next rule
    }
    else{
      next = false;
    }
  }
}

// WARNING: can we always pass an assignment expression to this? nope!
Lnast_node Prp_lnast::eval_scope_declaration(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Lnast_node name_node) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  /*if (cur_stmts == lnast->invalid_index()) {
    // create a statements node if we are the direct child of root
    auto lnast_seq    = lnast->add_string("___SEQ" + std::to_string(current_seq++));
    auto subtree_root = Lnast_node::create_stmts(lnast_seq);
    idx_nxt_ln        = lnast->add_child(idx_nxt_ln, subtree_root);
    cur_stmts         = idx_nxt_ln;
  }*/

  // check if the root is an assignment expression
  auto idx_nxt_ast = idx_start_ast;
  bool is_anon = true;
  if(ast->get_data(idx_nxt_ast).rule_id == Prp_rule_assignment_expression){
    idx_nxt_ast = ast->get_child(idx_start_ast);
    is_anon = false;
  }

  // first, we need to check whether the condition is an expression
  auto idx_scope_dec = !is_anon ? ast->get_last_child(idx_start_ast) : idx_start_ast;  // scope dec
  // print_tree_index(idx_scope_dec);
  auto idx_cond_nxt_ast = ast->get_child(idx_scope_dec);  // scope
  // print_tree_index(idx_cond_nxt_ast);
  idx_cond_nxt_ast = ast->get_child(idx_cond_nxt_ast);  // :
  // print_tree_index(idx_cond_nxt_ast);
  auto idx_fcall  = ast->get_sibling_next(idx_cond_nxt_ast);  // scope cond or fcall_args or another colon (no arguments)
  auto fcall_node = ast->get_data(idx_fcall);                 // it's called fcall, but it could be any of those three

  mmap_lib::Tree_index idx_cond_ast;
  bool                 cond_is_expr;
  Lnast_node           cond_lhs;
  if (fcall_node.token_entry == 0) {
    idx_cond_ast = ast->get_last_child(idx_fcall);  // expression for fcall truth
    cond_is_expr = false;
    if (is_expr(idx_cond_ast)) {
      cond_lhs     = eval_rule(idx_cond_ast, idx_start_ln);
      cond_is_expr = true;
    }
  }

  auto idx_func_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_func_def(""));

  // add the name of the function
  Lnast_node retnode;
  if(!is_anon){
    retnode = Lnast_node::create_ref(get_token(ast->get_data(idx_nxt_ast).token_entry));
  }
  else{
    if(name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
      retnode = name_node;
    }
    else{
      auto lnast_temp = lnast->add_string(current_temp_var);
      retnode    = Lnast_node::create_ref(lnast_temp);
      get_next_temp_var();
    }
  }
  lnast->add_child(idx_func_root, retnode);

  // move to either fcall_arg_notation or scope_cond or just another colon
  if (ast->get_data(idx_fcall).rule_id == Prp_rule_scope_condition) {
    // add the condition to the LNAST
    if (cond_is_expr)
      lnast->add_child(idx_func_root, Lnast_node::create_cond(cond_lhs.token));
    else
      lnast->add_child(idx_func_root, Lnast_node::create_cond(get_token(ast->get_data(idx_cond_ast).token_entry)));
    idx_fcall = ast->get_child(idx_fcall);
  } else {
    // add true to the condition
    lnast->add_child(idx_func_root, Lnast_node::create_cond("true"));
  }

  // move down to the scope body
  idx_nxt_ast = ast->get_last_child(idx_scope_dec);

  // create the new statements node
  auto lnast_seq = lnast->add_string("___SEQ" + std::to_string(current_seq++));
  auto idx_stmts = lnast->add_child(idx_func_root, Lnast_node::create_stmts(lnast_seq));

  // translate the scope statements
  auto old_stmts = cur_stmts;
  cur_stmts      = idx_stmts;
  translate_code_blocks(idx_nxt_ast, idx_stmts, Prp_rule_scope_body);
  
  cur_stmts = old_stmts;

  // print_tree_index(idx_fcall);
  // translate the scope argument (which can only be fcall arg notation for now)
  // evaluate the fcall arg notation, if present
  if (fcall_node.token_entry == 0) eval_rule(idx_fcall, idx_func_root);
  
  return retnode;
}

void Prp_lnast::eval_while_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  /*if (cur_stmts == lnast->invalid_index()) {
    // create a statements node if we are the direct child of root
    auto lnast_seq    = lnast->add_string("___SEQ" + std::to_string(current_seq++));
    auto subtree_root = Lnast_node::create_stmts(lnast_seq);
    idx_nxt_ln        = lnast->add_child(idx_nxt_ln, subtree_root);
    cur_stmts         = idx_nxt_ln;
  }*/

  auto idx_nxt_ast = ast->get_child(idx_start_ast);

  // evaluate the while condition
  auto while_cond = eval_rule(idx_nxt_ast, idx_start_ln);

  // create the while node
  auto idx_while_root = lnast->add_child(idx_start_ln, Lnast_node::create_while(""));

  // add the condition
  lnast->add_child(idx_while_root, Lnast_node::create_cond(while_cond.token));

  // skip the opening {
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

  // go to the block body
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

  // create statements node
  auto lnast_seq = lnast->add_string("___SEQ" + std::to_string(current_seq++));
  auto idx_stmts = lnast->add_child(idx_start_ln, Lnast_node::create_stmts(lnast_seq));

  // evaluate the block inside the while
  auto old_stmts = cur_stmts;
  cur_stmts      = idx_stmts;
  translate_code_blocks(idx_nxt_ast, idx_stmts, Prp_rule_block_body);
  cur_stmts = old_stmts;
}

void Prp_lnast::eval_for_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = idx_start_ln;

  auto idx_for_index = ast->get_child(idx_start_ast);
  auto idx_nxt_ast = idx_for_index;

  // check if the current rule is a for_index; if so, we have multiple for ins
  if(ast->get_data(idx_nxt_ast).rule_id == Prp_rule_for_index){
    // go down to the for... in condition
    PRINT_DBG_LN("Found a for index node.\n");
    idx_nxt_ast = ast->get_child(idx_nxt_ast);
  }
  
  bool next = true;
  std::vector<Lnast_node> iterators;
  std::vector<Lnast_node> iterator_range;
    
  while(next){
    auto idx_for_in_root = idx_nxt_ast;
    
    auto idx_for_counter = ast->get_child(idx_nxt_ast);
    
    idx_nxt_ast = ast->get_sibling_next(idx_for_counter); // go to the in token
    
    auto idx_tuple = ast->get_sibling_next(idx_nxt_ast); // what is the value in?
    
    PRINT_DBG_LN("The for expression is rule {}.\n", rule_id_to_string(ast->get_data(idx_tuple).rule_id));
    
    iterators.push_back(Lnast_node::create_ref(get_token(ast->get_data(idx_for_counter).token_entry)));
    
    iterator_range.push_back(eval_rule(idx_tuple, idx_nxt_ln));
    PRINT_DBG_LN("Evaluated a for..in rule.\n");
    print_tree_index(idx_for_in_root);
    idx_nxt_ast = ast->get_sibling_next(idx_for_in_root);
    PRINT_DBG_LN("Moved past the the for in root\n");
    if(idx_nxt_ast != ast->invalid_index()){
      PRINT_DBG_LN("The next index was not an invalid index.\n");
      if(ast->get_data(idx_nxt_ast).rule_id != Prp_rule_for_index){
        PRINT_DBG_LN("The next index was not an invalid index.\n");
        next = false;
      }
      else{
        PRINT_DBG_LN("Moving to next for...in.\n");
        idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
      }
    }
    else{
      next = false;
    }
  }
  // evaluate the for conditon
  // this node is the root of the value at x: for in x

  // skip the {
  idx_nxt_ast = ast->get_sibling_next(idx_for_index);

  // move to the block body
  idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);

  auto idx_for_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_for(""));

  // add our new statements node
  auto lnast_seq = lnast->add_string("___SEQ" + std::to_string(current_seq++));
  auto idx_stmts = lnast->add_child(idx_for_root, Lnast_node::create_stmts(lnast_seq));

  // add the name of our index tracking value
  for(uint16_t i = 0; i<iterators.size(); i++){
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

  /*if (cur_stmts == lnast->invalid_index()) {
    // create a statements node if we are the direct child of root
    auto lnast_seq    = lnast->add_string("___SEQ" + std::to_string(current_seq++));
    auto subtree_root = Lnast_node::create_stmts(lnast_seq);
    idx_nxt_ln        = lnast->add_child(idx_nxt_ln, subtree_root);
    cur_stmts         = idx_nxt_ln;
  }*/

  // first step: determine whether it is "if" or "unique if"
  auto idx_nxt_ast = ast->get_child(idx_start_ast);

  auto cur_ast = ast->get_data(idx_nxt_ast);

  if (cur_ast.rule_id == Prp_rule_if_statement) {  // conditioned if
    if (scan_text(cur_ast.token_entry) == "if") {  // if
      idx_nxt_ln = lnast->add_child(idx_nxt_ln, Lnast_node::create_if(get_token(cur_ast.token_entry)));
    } else {  // unique ifs
      idx_nxt_ln = lnast->add_child(idx_nxt_ln, Lnast_node::create_uif(get_token(cur_ast.token_entry)));
      idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    }
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    // second step: condition statement pairs
    while (idx_nxt_ast != ast->invalid_index()) {
      cur_ast = ast->get_data(idx_nxt_ast);
      // check if we're looking at an else or an elif first
      if (cur_ast.rule_id ==
          Prp_rule_else_statement) {  // we need to set the index to the same spot as it would be for the first if
        PRINT_DBG_LN("Before rule: {}.\n", rule_id_to_string(ast->get_data(idx_nxt_ast).rule_id));
        idx_nxt_ast = ast->get_child(idx_nxt_ast);
        idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);  // now at either condition or brace (for elif and else respectively)
        PRINT_DBG_LN("After rule: {}.\n", rule_id_to_string(ast->get_data(idx_nxt_ast).rule_id));
        cur_ast = ast->get_data(idx_nxt_ast);
      }
      // find condition if it's present
      if (is_expr(idx_nxt_ast) || cur_ast.rule_id == Prp_rule_reference || cur_ast.rule_id == Prp_rule_numerical_constant) {
        auto idx_cond_ln = lnast->add_child(idx_nxt_ln, Lnast_node::create_cstmts(""));
        auto old_stmts   = cur_stmts;
        cur_stmts        = idx_cond_ln;
        auto cond_lhs = eval_rule(idx_nxt_ast, idx_cond_ln);
        lnast->add_child(idx_nxt_ln, Lnast_node::create_cond(cond_lhs.token));
        cur_stmts   = old_stmts;
        // go past expression token
        idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
      }
      // eval statements
      // add statements node
      auto lnast_seq = lnast->add_string("___SEQ" + std::to_string(current_seq++));
      auto idx_stmts = lnast->add_child(idx_nxt_ln, Lnast_node::create_stmts(lnast_seq));
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

  /*if (cur_stmts == lnast->invalid_index()) {
    // create a statements node if we are the direct child of root
    auto lnast_seq    = lnast->add_string("___SEQ" + std::to_string(current_seq++));
    auto subtree_root = Lnast_node::create_stmts(lnast_seq);
    idx_nxt_ln        = lnast->add_child(idx_nxt_ln, subtree_root);
    cur_stmts         = idx_nxt_ln;
  }*/

  // Check if the assignment has an operator (+=, etc.)
  auto idx_lhs_ast = ast->get_child(idx_start_ast);
  auto rhs_ast = ast->get_last_child(idx_start_ast);
  auto idx_assign_op = ast->get_sibling_next(idx_lhs_ast);

  // if the assignment operator doesn't have a token, that means it has more than one child
  bool is_op_assign = ast->get_data(idx_assign_op).token_entry == 0;
  PRINT_DBG_LN("is_op_assign = {}\n", is_op_assign);
  if(!is_op_assign){
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
    if(ast->get_data(rhs_ast).rule_id == Prp_rule_fcall_implicit){
      PRINT_DBG_LN("Found an assignment to a function call.\n");
      eval_fcall_implicit(idx_start_ast, idx_nxt_ln);
      return;
    }
    
  }
  
  // first thing, create the lhs if it is an expression
  auto lhs_node = eval_rule(idx_lhs_ast, idx_start_ln);

  bool is_as = scan_text(ast->get_data(ast->get_sibling_next(idx_lhs_ast)).token_entry) == "as";

  mmap_lib::Tree_index idx_assign;

  Lnast_node rhs_node;
  bool       rhs_is_leaf  = ast->get_data(rhs_ast).token_entry != 0 || ast->get_data(rhs_ast).rule_id == Prp_rule_string_constant;
  bool       rhs_is_tuple = false;

  if (is_op_assign) {
    PRINT_DBG_LN("Evaluating an op= expression.\n");
    rhs_node = eval_expression(idx_start_ast, idx_nxt_ln);
  } else {
    // if the tuple is assigned with equals, there is no assign or as node
    if (!is_as) {
      if (ast->get_data(rhs_ast).rule_id == Prp_rule_tuple_notation) {
        rhs_node = eval_tuple(rhs_ast, idx_nxt_ln);
      }
      else if (!rhs_is_leaf) {
        rhs_node = eval_rule(rhs_ast, idx_nxt_ln);
      }
    }
    // a tuple (or anything) assigned with as will be an RHS expression
    else {
      if (!rhs_is_leaf) rhs_node = eval_rule(rhs_ast, idx_nxt_ln);
    }
  }

  // finally, create the assign node itself, which is also a child of subtree root
  if (!rhs_is_tuple) {
    mmap_lib::Tree_index idx_assign;
    if (is_as)
      idx_assign = lnast->add_child(idx_nxt_ln, Lnast_node::create_as(""));
    else{
      PRINT_DBG_LN("The assignment operator we are checking is {}.\n", scan_text(ast->get_data(idx_assign_op).token_entry));
      if(scan_text(ast->get_data(idx_assign_op).token_entry) == ":="){
        idx_assign = lnast->add_child(idx_nxt_ln, Lnast_node::create_dp_assign(""));
      }
      else{
        idx_assign = lnast->add_child(idx_nxt_ln, Lnast_node::create_assign(""));
      }
    }
    lnast->add_child(idx_assign, lhs_node);

    if (!rhs_is_leaf || is_op_assign)
      auto idx_rhs = lnast->add_child(idx_assign, rhs_node);
    else
      lnast->add_child(idx_assign, eval_rule(rhs_ast, idx_assign));
  } else
    auto tuple_node = eval_tuple(idx_start_ast, idx_nxt_ln);
}

Lnast_node Prp_lnast::eval_tuple(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, mmap_lib::Tree_index idx_pre_tuple_vals, mmap_lib::Tree_index idx_post_tuple_vals, Lnast_node extra_node) {
  auto idx_tuple_not_root = idx_start_ast;
  // first thing: find any expressions inside the tuple and save the rhs temporary variables
  // get the rhs of the tuple assignment
  Rule_id root_rid;
  if(idx_start_ast != ast->invalid_index()){
    Rule_id root_rid        = ast->get_data(idx_start_ast).rule_id;
    if (root_rid == Prp_rule_assignment_expression) {
      idx_tuple_not_root = ast->get_last_child(idx_start_ast);
    }
  }
  
  std::list<std::array<Lnast_node,3>> tuple_nodes;
  evaluate_all_tuple_nodes(idx_tuple_not_root, idx_pre_tuple_vals, idx_post_tuple_vals, tuple_nodes);
  
  // the list has every element in that we want to add to the tuple, and all of them have been evaluated if they needed to be
  
  // add the tuple root
  auto idx_tuple_root = lnast->add_child(idx_start_ln, Lnast_node::create_tuple(""));
  // add the tuple LHS
  Lnast_node retnode;
  if(root_rid == Prp_rule_assignment_expression){
    auto idx_assign_lhs = ast->get_child(idx_start_ast);
    retnode = eval_rule(idx_assign_lhs, idx_start_ln);
  }
  else{
    auto lnast_temp = lnast->add_string(current_temp_var);
    get_next_temp_var();
    retnode = Lnast_node::create_ref(lnast_temp);
  }
  lnast->add_child(idx_tuple_root, retnode);
  
  if(extra_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
    std::array<Lnast_node, 3> extra_node_subtree = {Lnast_node::create_assign(""), Lnast_node::create_ref("null"), extra_node};
    tuple_nodes.push_back(extra_node_subtree);
  }
  
  add_tuple_nodes(idx_tuple_root, tuple_nodes);
  
  // FIXME: anonymous function used as implicit argument?
  
  return retnode;
}

void Prp_lnast::add_tuple_nodes(mmap_lib::Tree_index idx_start_ln, std::list<std::array<Lnast_node,3>> &tuple_nodes){
  for(const auto& node_subtrees : tuple_nodes){
    auto idx_assign = lnast->add_child(idx_start_ln, node_subtrees[0]);
    lnast->add_child(idx_assign, node_subtrees[1]);
    lnast->add_child(idx_assign, node_subtrees[2]);
  }
}

void Prp_lnast::evaluate_all_tuple_nodes(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_pre_tuple_vals, mmap_lib::Tree_index idx_post_tuple_vals, std::list<std::array<Lnast_node,3>> &tuple_nodes){
  mmap_lib::Tree_index tuple_idxs[] = {idx_pre_tuple_vals, idx_start_ast, idx_post_tuple_vals};
  
  for(const auto& idx : tuple_idxs){
    if(idx != ast->invalid_index()){
      auto cur_node = ast->get_data(idx);
      if(cur_node.rule_id == Prp_rule_tuple_notation || cur_node.rule_id == Prp_rule_fcall_arg_notation){
        auto idx_tup_el_next = ast->get_child(idx);
        idx_tup_el_next = ast->get_sibling_next(idx_tup_el_next);
        bool next = true;
        while(next){
          auto tup_el_cur = ast->get_data(idx_tup_el_next);
          if(scan_text(tup_el_cur.token_entry) == ")")
            next = false;
          else{
            Lnast_node assign_root;
            Lnast_node assign_lhs;
            Lnast_node rhs;
            if(tup_el_cur.rule_id == Prp_rule_assignment_expression){
              auto idx_assign_lhs = ast->get_child(idx_tup_el_next);
              assign_lhs = eval_rule(idx_assign_lhs, cur_stmts);
              auto idx_assign_operator = ast->get_sibling_next(idx_assign_lhs);
              if(ast->get_data(idx_assign_operator).token_entry == 0){ // must have an expression operator
                rhs = eval_expression(idx_tup_el_next, cur_stmts);
                assign_root = Lnast_node::create_assign("");
              }
              else{
                auto idx_rhs = ast->get_sibling_next(idx_assign_operator);
                rhs = eval_rule(idx_rhs, cur_stmts);
                if(scan_text(ast->get_data(idx_assign_operator).token_entry) == "="){
                 assign_root = Lnast_node::create_assign("");
                }
                else if(scan_text(ast->get_data(idx_assign_operator).token_entry) == ":="){
                  assign_root = Lnast_node::create_dp_assign("");
                }
                else{
                  assign_root = Lnast_node::create_as("");
                }
              }
            }
            else{
              assign_root = Lnast_node::create_assign("");
              assign_lhs = Lnast_node::create_ref("null");
              rhs = eval_rule(idx_tup_el_next, cur_stmts);
            }
            std::array<Lnast_node,3> nodes = {assign_root, assign_lhs, rhs};
            tuple_nodes.push_back(nodes);
            idx_tup_el_next = ast->get_sibling_next(idx_tup_el_next);
            auto tup_el_comma_maybe = ast->get_data(idx_tup_el_next);
            if(scan_text(tup_el_comma_maybe.token_entry) == ")"){
              next = false;
            }
            else{
              idx_tup_el_next = ast->get_sibling_next(idx_tup_el_next);
            }
          }
        }
      }
      else if(cur_node.rule_id == Prp_rule_range_notation){
        auto idx_range_next = ast->get_child(idx);
        auto range_el = ast->get_data(idx_range_next);
        
        auto assign_root = Lnast_node::create_assign("");
        auto range_start_sentinel = Lnast_node::create_ref("__range_begin");
        Lnast_node range_start;
        bool range_start_is_null = false;
        if(range_el.token_entry != 0){
          if(scan_text(range_el.token_entry) == "."){
            range_start = Lnast_node::create_ref("null");
            range_start_is_null = true;
          }
        }
        
        if(!range_start_is_null){
          range_start = eval_rule(idx_range_next, cur_stmts);
        }
        
        std::array<Lnast_node,3> nodes_start = {assign_root, range_start_sentinel, range_start};
        
        idx_range_next = ast->get_sibling_next(idx_range_next);
        idx_range_next = ast->get_sibling_next(idx_range_next);
        if(!range_start_is_null){
          idx_range_next = ast->get_sibling_next(idx_range_next);
        }
        
        auto range_end_sentinel = Lnast_node::create_ref("__range_end");
        Lnast_node range_end;
        bool range_end_is_null = false;
        if(idx_range_next == ast->invalid_index()){
          range_end = Lnast_node::create_ref("null");
          range_end_is_null = true;
        }
        
        if(!range_end_is_null){
          range_end = eval_rule(idx_range_next, cur_stmts);
        }
        
        std::array<Lnast_node,3> nodes_end = {assign_root, range_end_sentinel, range_end};
        
        tuple_nodes.emplace_back(nodes_start);
        tuple_nodes.emplace_back(nodes_end);
        
        // TODO: by notation
      }
      else{
        std::array<Lnast_node,3> nodes = {Lnast_node::create_assign(""), Lnast_node::create_ref("null"), eval_rule(idx, cur_stmts)};
        tuple_nodes.emplace_back(nodes);
      }
    }
  }
}

Lnast_node Prp_lnast::eval_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = cur_stmts;
  auto idx_nxt_ast = idx_start_ast;
  // print_tree_index(idx_nxt_ast);

  // if we're a tuple, we need to evaluate ourselves as one
  /*if (ast->get_data(idx_nxt_ast).rule_id == Prp_rule_tuple_notation) {
    return eval_tuple(idx_nxt_ast, idx_nxt_ln);
    auto idx_tup_tst_nxt = ast->get_child(idx_nxt_ast);
    // skip the open parenth
    idx_tup_tst_nxt = ast->get_sibling_next(idx_tup_tst_nxt);

    if (idx_tup_tst_nxt == ast->invalid_index()) {
      return Lnast_node::create_ref("null");
    } else if (ast->get_data(idx_tup_tst_nxt).rule_id == Prp_rule_range_notation ||
               ast->get_data(idx_tup_tst_nxt).rule_id == Prp_rule_assignment_expression) {
      return eval_tuple(idx_nxt_ast, idx_nxt_ln);
    } else {
      // go to the comma, if it exists
      idx_tup_tst_nxt = ast->get_sibling_next(idx_tup_tst_nxt);
      if (scan_text(ast->get_data(idx_tup_tst_nxt).token_entry) == ",") return eval_tuple(idx_nxt_ast, idx_nxt_ln);
    }
  }*/

  std::list<Lnast_node> operand_stack;
  std::list<Lnast_node> operator_stack;
  Lnast_node assign_operator, assign_operand;
  bool has_assign_op = false;
  // first, we need to see if we're an expression of the form (op)=, e.g. +=
  if (ast->get_data(idx_nxt_ast).rule_id == Prp_rule_assignment_expression) {
    has_assign_op = true;
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
  
  Lnast_node op_node_last;
  bool last_op_valid = false;
  while (child_cur != ast->invalid_index()) {
    auto child_cur_data = ast->get_data(child_cur);
    PRINT_DBG_LN("Rule name: {}, Token text: {}\n", rule_id_to_string(child_cur_data.rule_id),
                 scan_text(child_cur_data.token_entry));
    if (child_cur_data.token_entry != 0 || child_cur_data.rule_id == Prp_rule_string_constant) {  // is a leaf
      if (child_cur_data.rule_id == Prp_rule_identifier) {                                        // identifier
          uint8_t skip_sibs;
          auto    op_node = gen_operator(child_cur, &skip_sibs);
          operator_stack.emplace_back(op_node);
      } else if (child_cur_data.rule_id == Prp_rule_reference){
        operand_stack.emplace_back(Lnast_node::create_ref(get_token(child_cur_data.token_entry)));
      } else if (child_cur_data.rule_id == Prp_rule_numerical_constant || child_cur_data.rule_id == Prp_rule_string_constant) {
        operand_stack.emplace_back(create_const_node(child_cur));
      } else if (child_cur_data.rule_id == Prp_rule_sentinel){
        // get the index of the operator that is not inside the parentheses
        auto sentinel_op_idx = ast->get_sibling_next(child_cur);
        child_cur = sentinel_op_idx;
        // generate that operator and put it into the stack
        uint8_t skip_sibs;
        auto    op_node = gen_operator(sentinel_op_idx, &skip_sibs);
        for (int i = 0; i < skip_sibs; i++) child_cur = ast->get_sibling_next(child_cur);
        // multiplication and division operators don't have their priority tracked by the parser
        bool evaluated_sub_expr = false;
        if(last_op_valid){
          if((op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_mult || op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_div) && (op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus)){
            auto mult_expr_start = ast->get_sibling_prev(child_cur);
            operand_stack.emplace_back(eval_sub_expression(mult_expr_start, op_node));
            child_cur = ast->get_sibling_next(child_cur); // skip the next operand
            evaluated_sub_expr = true;
          }
        }
        
        if(!evaluated_sub_expr){
          operator_stack.emplace_back(op_node);
          // evaluate the rest of the expression as though it were inside parentheses
          child_cur = ast->get_sibling_next(child_cur);
          operand_stack.emplace_back(eval_expression(child_cur, idx_nxt_ln));
        break;
        }
      } else {  // operator
        bool sub_expr = false;
        uint8_t skip_sibs;
        auto    op_node = gen_operator(child_cur, &skip_sibs);
        if(last_op_valid){
          // first check if we are a mult, and the previous was as an add
          if((op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_mult || op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_div) && (op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus)){
            sub_expr = true;
            auto mult_expr_start = ast->get_sibling_prev(child_cur);
            operand_stack.emplace_back(eval_sub_expression(mult_expr_start, op_node));
            child_cur = ast->get_sibling_next(child_cur); // skip the next operand
          }
        }
        if(!sub_expr){
          if(last_op_valid){
            auto pri_op_cur = priority_map[op_node.type.get_raw_ntype()];
            if(pri_op_cur == priority_map[op_node_last.type.get_raw_ntype()]){
              if(op_node.type.get_raw_ntype() != op_node_last.type.get_raw_ntype()){
                bool op0_pm = op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus || op_node.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus;
                bool op1_pm = op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_plus || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_minus;
                
                bool op1_md = (op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_mult || op_node_last.type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_div);
                if(!(op0_pm && op1_pm) && !(op0_pm && op1_md)){
                  fmt::print("Operator priority error in expression around line {}.\n", expr_line+1);
                  exit(1);
                }
              }
            }
          }
          for (int i = 0; i < skip_sibs; i++) child_cur = ast->get_sibling_next(child_cur);
          op_node_last = op_node;
          last_op_valid=true;
          operator_stack.emplace_back(op_node);
        }
      }
    } else {
      PRINT_DBG_LN("Evaluating an operand that is not a leaf.\n");
      operand_stack.emplace_back(eval_rule(child_cur, idx_nxt_ln));
    }
    child_cur = ast->get_sibling_next(child_cur);
  }
  
  if(has_assign_op){
    operator_stack.emplace_back(assign_operator);
  }

  for (auto it = operator_stack.begin(); it != operator_stack.end(); ++it) {
    // add as a child of the starting node
    auto idx_operator_ln = lnast->add_child(idx_nxt_ln, *it);

    // create our lhs variable
    auto lnast_temp = lnast->add_string(current_temp_var);
    auto lhs        = Lnast_node::create_ref(lnast_temp);
    get_next_temp_var();

    // add both operands and the LHS as a child of the operator unless the operator is a unary operator
    lnast->add_child(idx_operator_ln, lhs);  // LHS node
    auto op1 = operand_stack.front();
    operand_stack.pop_front();
    bool added_assign_op = false;
    if(it == std::prev(operator_stack.end())){
      if(assign_operand.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
        lnast->add_child(idx_operator_ln, assign_operand);
        added_assign_op = true;
      }
    }
    lnast->add_child(idx_operator_ln, op1);  // first operand
    if (!(added_assign_op || (*it).type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_not ||
          (*it).type.get_raw_ntype() == Lnast_ntype::Lnast_ntype_logical_not)) {
      auto op2 = operand_stack.front();  // optional second operand
      operand_stack.pop_front();
      lnast->add_child(idx_operator_ln, op2);
    }
    while (1) {
      if (std::next(it, 1) != operator_stack.end()) {
        if ((*(std::next(it, 1))).type.get_raw_ntype() == (*it).type.get_raw_ntype()) {
          it++;
          if(it == std::prev(operator_stack.end())){
            lnast->add_child(idx_operator_ln, assign_operand);
          }
          else{
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
  }
  // hopefully after this, the expression is correct
  return operand_stack.front();
}

Lnast_node Prp_lnast::eval_sub_expression(mmap_lib::Tree_index idx_start_ast, Lnast_node operator_node){
  // evaluate a single binary expression op0 op op1. Assume that if op0 is an expression, it has already
  // been added to the lnast
  
  auto idx_nxt_ast = idx_start_ast; // this points to the first operand
  mmap_lib::Tree_index idx_nxt_ln = cur_stmts;
  
  auto op0_idx = idx_nxt_ast;
  auto op0_data = ast->get_data(op0_idx);
  
  auto operator_idx = ast->get_sibling_next(op0_idx); // can only be + or -
  
  auto op1_idx = ast->get_sibling_next(operator_idx);
  auto op1_data = ast->get_data(op1_idx);
  
  Lnast_node op0, op1;
  bool op0_is_expr = false;
  bool op1_is_expr = false;
  
  auto lnast_temp0 = lnast->add_string(last_temp_var);
  op0 = Lnast_node::create_ref(lnast_temp0);
  
  op1 = eval_rule(op1_idx, idx_nxt_ln);
  
  auto expr_root = lnast->add_child(idx_nxt_ln, operator_node);
  auto lnast_temp1 = lnast->add_string(current_temp_var);
  auto lhs        = Lnast_node::create_ref(lnast_temp1);
  get_next_temp_var();
  
  lnast->add_child(expr_root, lhs);
  lnast->add_child(expr_root, op0);
  lnast->add_child(expr_root, op1);
  
  return lhs;
}

Lnast_node Prp_lnast::eval_for_in_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
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

  // first evaluate the select expression
  Lnast_node sel_rhs = eval_rule(ast_nxt_idx, idx_nxt_ln);;

  // create sel node
  auto idx_sel_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_select(""));

  // create the LHS temporary variable
  auto lnast_temp = lnast->add_string(current_temp_var);
  auto retnode    = Lnast_node::create_ref(lnast_temp);
  lnast->add_child(idx_sel_root, retnode);
  get_next_temp_var();

  // add the identifier of the tuple being selected
  lnast->add_child(idx_sel_root, Lnast_node::create_ref(get_token(ast->get_data(ast->get_child(idx_start_ast)).token_entry)));

  // add the rhs value of the select expression
  lnast->add_child(idx_sel_root, sel_rhs);

  return retnode;
}

Lnast_node Prp_lnast::eval_fcall_explicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, mmap_lib::Tree_index idx_piped_val, Lnast_node piped_node, Lnast_node name_node){
  // check if the root is an assignment expression
  auto root_rid    = ast->get_data(idx_start_ast).rule_id;
  auto idx_nxt_ast = idx_start_ast;
  if (root_rid == Prp_rule_assignment_expression) {
    idx_nxt_ast = ast->get_last_child(idx_nxt_ast);
  }

  // whether we are an assignment expression or not, idx_nxt_ast will equal fcall_explicit

  // evaluate the rhs of the function call (the fcall_arg_notation), but it has to be a tuple
  Lnast_node arg_lhs;
  Lnast_node func_def_lhs;
  
  auto idx_func_lhs = ast->get_child(idx_nxt_ast);
  auto idx_func_name = idx_func_lhs;
  bool uniform_call_tuple = ast->get_data(idx_func_lhs).rule_id == Prp_rule_tuple_notation;
  bool uniform_call_tuple_dot = ast->get_data(idx_func_lhs).rule_id == Prp_rule_tuple_dot_notation;
  
  mmap_lib::Tree_index idx_uniform_call_els = ast->invalid_index();
  
  if(uniform_call_tuple){
    PRINT_DBG_LN("Our function call uses UCS rooted at a tuple.\n");
    // go down to the (
    idx_uniform_call_els = idx_func_lhs;
    // point idx_func_lhs to the sibling of fcall_arg_notation
    idx_func_lhs = ast->get_sibling_next(idx_func_lhs); // sees the dot
    idx_func_lhs = ast->get_sibling_next(idx_func_lhs); // is now where it should be
    idx_func_name = idx_func_lhs;
    
  }
  else if(uniform_call_tuple_dot){
    PRINT_DBG_LN("Our function call uses UCS rooted at a tuple dot notation.");
    idx_uniform_call_els = ast->get_child(idx_func_lhs);
    idx_func_name = ast->get_sibling_next(idx_uniform_call_els);
    idx_func_name = ast->get_last_child(idx_func_name);
  }
  
  auto idx_fcall_args = idx_nxt_ast;
  if(ast->get_data(ast->get_last_child(idx_nxt_ast)).rule_id == Prp_rule_fcall_arg_notation){
    idx_fcall_args = ast->get_last_child(idx_nxt_ast);
    arg_lhs = eval_tuple(idx_fcall_args, idx_start_ln, idx_uniform_call_els, idx_piped_val, piped_node);
  }
  else{
    idx_fcall_args = ast->get_sibling_next(idx_func_lhs);
    arg_lhs = eval_tuple(idx_fcall_args, idx_start_ln, idx_uniform_call_els, idx_piped_val, piped_node);
  }
  
  // check for scope declaration
  Lnast_node func_name = Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry));
  auto idx_scope_dec = ast->get_sibling_next(idx_fcall_args);
  if(idx_scope_dec != ast->invalid_index()){
    if(ast->get_data(idx_scope_dec).rule_id == Prp_rule_scope_declaration){
      PRINT_DBG_LN("Found a scope declaration to go with the explicit fcall.\n");
      eval_scope_declaration(idx_scope_dec, idx_start_ln, func_name);
      idx_fcall_args = idx_scope_dec;
    }
  }

  Lnast_node lhs_node;
  if (root_rid == Prp_rule_assignment_expression) {
    lhs_node = eval_rule(ast->get_child(idx_start_ast), idx_start_ln);
  } 
  else if(name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
    lhs_node = name_node;
  } 
  else {
    auto lnast_temp = lnast->add_string(current_temp_var);
    lhs_node        = Lnast_node::create_ref(lnast_temp);
    get_next_temp_var();
  }

  // create the function call root
  auto idx_fcall_root = lnast->add_child(cur_stmts, Lnast_node::create_func_call(""));
  
  auto idx_pipe_maybe = ast->get_sibling_next(idx_fcall_args);
  
  if(idx_pipe_maybe != ast->invalid_index()){
    PRINT_DBG_LN("We found a function pipe.\n");
    // we have a pipe
    auto idx_next_fcall = ast->get_last_child(idx_pipe_maybe);
    
    // add the LHS of the function call
    Lnast_node intermediate_lhs;
    bool is_assign_expr = root_rid == Prp_rule_assignment_expression;
    if(is_assign_expr){
      // need to add a temporary node anyway
      auto lnast_temp  = lnast->add_string(current_temp_var);
      intermediate_lhs = Lnast_node::create_ref(lnast_temp);
      get_next_temp_var();
      lnast->add_child(idx_fcall_root, intermediate_lhs);
    }
    else{
      lnast->add_child(idx_fcall_root, lhs_node);
    }

    // add the name of the function
    lnast->add_child(idx_fcall_root, func_name);

    // add the argument tuple
    lnast->add_child(idx_fcall_root, arg_lhs);
    
    if(ast->get_data(idx_next_fcall).rule_id == Prp_rule_fcall_explicit){
      if(is_assign_expr){
        return eval_fcall_explicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), intermediate_lhs, lhs_node);
      }
      else if(name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
        return eval_fcall_explicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), lhs_node, name_node);
      }
      else{
        return eval_fcall_explicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), lhs_node);
      }
    }
    else{ // hopefully can't be anything else than implicit
      if(is_assign_expr){
        return eval_fcall_implicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), intermediate_lhs, lhs_node);
      }
      else if(name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
        return eval_fcall_implicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), lhs_node, name_node);
      }
      else{
        return eval_fcall_implicit(idx_next_fcall, idx_start_ln, ast->invalid_index(), lhs_node);
      }
    }
  }
  
  // add the LHS of the function call
  lnast->add_child(idx_fcall_root, lhs_node);

  // add the name of the function
  lnast->add_child(idx_fcall_root, func_name);

  // add the argument tuple
  lnast->add_child(idx_fcall_root, arg_lhs);
  
  return lhs_node;
}

Lnast_node Prp_lnast::eval_fcall_implicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, mmap_lib::Tree_index idx_piped_val, Lnast_node piped_node, Lnast_node name_node) {
  PRINT_DBG_LN("Evaluating an implicit function call.\n");
  auto idx_root = idx_start_ast;
  auto root_rid = ast->get_data(idx_start_ast).rule_id;
  auto idx_nxt_ast = idx_root;
  
  if(root_rid == Prp_rule_assignment_expression){
    PRINT_DBG_LN("Found an assignment expression in the fcall implicit.\n");
    idx_nxt_ast = ast->get_last_child(idx_root);
  }
  
  auto idx_func_name = ast->get_child(idx_nxt_ast);
  
  bool has_piped_value = idx_piped_val != ast->invalid_index() || piped_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid;
  bool is_plain_value = false;
  
  // no name, anonymous function
  Lnast_node decl_func_name_node;
  if(ast->get_data(idx_func_name).rule_id == Prp_rule_scope_declaration){
    // create function declaration node
    PRINT_DBG_LN("Found an inline anonymous function.\n");
    decl_func_name_node = eval_scope_declaration(idx_func_name, idx_start_ln);
  }
  else{
    if(!has_piped_value){
      if(ast->get_data(ast->get_sibling_next(idx_func_name)).rule_id != Prp_rule_scope_declaration){
        is_plain_value = true;
      }
      else{
        auto idx_scope_dec = ast->get_sibling_next(idx_func_name);
        decl_func_name_node = eval_scope_declaration(idx_scope_dec, idx_start_ln, Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry)));
      }
    }
  }
  
  auto idx_pipe_maybe = ast->get_sibling_next(idx_func_name);
  
  mmap_lib::Tree_index idx_fcall_root = idx_start_ln;
  
  if(idx_pipe_maybe != ast->invalid_index()){
    Lnast_node assign_lhs;
    if(root_rid == Prp_rule_assignment_expression){
      auto idx_assign_lhs = ast->get_child(idx_start_ast);
      assign_lhs = eval_rule(idx_assign_lhs, idx_start_ln);
    }
    else if(name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
      assign_lhs = name_node;
    }
    Lnast_node piped_node_new;
    if(!is_plain_value){
      // create a temporary name
      auto lnast_temp = lnast->add_string(current_temp_var);
      piped_node_new = Lnast_node::create_ref(lnast_temp);
      get_next_temp_var();
      auto arg_tuple  = eval_tuple(idx_piped_val, idx_start_ln, ast->invalid_index(), ast->invalid_index(), piped_node);
      idx_fcall_root = lnast->add_child(cur_stmts, Lnast_node::create_func_call(""));
      lnast->add_child(idx_fcall_root, piped_node_new);
      if(decl_func_name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
        lnast->add_child(idx_fcall_root, decl_func_name_node);
      }
      else{
        lnast->add_child(idx_fcall_root, Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry)));
      }
      lnast->add_child(idx_fcall_root, arg_tuple);
    }
    auto idx_piped_fcall = ast->get_last_child(idx_pipe_maybe);
    if(ast->get_data(idx_piped_fcall).rule_id == Prp_rule_fcall_explicit){
      if(is_plain_value){
        return eval_fcall_explicit(idx_piped_fcall, idx_start_ln, idx_func_name, piped_node_new, assign_lhs);
      }
      else{
        return eval_fcall_explicit(idx_piped_fcall, idx_start_ln, ast->invalid_index(), piped_node_new, assign_lhs);
      }
    }
    else{
      if(is_plain_value){
        return eval_fcall_implicit(idx_piped_fcall, idx_start_ln, idx_func_name, piped_node_new, assign_lhs);
      }
      else{
        return eval_fcall_implicit(idx_piped_fcall, idx_start_ln, ast->invalid_index(), piped_node_new, assign_lhs);
      }
    }
  }
  
  // we're done?
  Lnast_node retnode;
  if(root_rid == Prp_rule_assignment_expression){
    auto idx_assign_lhs = ast->get_child(idx_start_ast);
    retnode = eval_rule(idx_assign_lhs, idx_start_ln);
  }
  else if(name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
    retnode = name_node;
  }
  else{
    auto lnast_temp = lnast->add_string(current_temp_var);
    retnode = Lnast_node::create_ref(lnast_temp);
    get_next_temp_var();
  }
  
  // evaluate any piped values
  auto arg_tuple = eval_tuple(idx_piped_val, idx_start_ln, ast->invalid_index(), ast->invalid_index(), piped_node);
  
  // create the function call root
  idx_fcall_root = lnast->add_child(cur_stmts, Lnast_node::create_func_call(""));
  
  // add the function LHS
  lnast->add_child(idx_fcall_root, retnode);
  
  // add the function name
  if(decl_func_name_node.type.get_raw_ntype() != Lnast_ntype::Lnast_ntype_invalid){
    lnast->add_child(idx_fcall_root, decl_func_name_node);
  }
  else{
    lnast->add_child(idx_fcall_root, Lnast_node::create_ref(get_token(ast->get_data(idx_func_name).token_entry)));
  }
  
  // add any piped arguments
  lnast->add_child(idx_fcall_root, arg_tuple);
  
  return retnode;
}

Lnast_node Prp_lnast::eval_tuple_dot_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = cur_stmts;
  // root is a tuple_dot_notation node
  
  // create the lhs temp variable
  auto lnast_temp = lnast->add_string(current_temp_var);
  auto lhs_node   = Lnast_node::create_ref(lnast_temp);
  get_next_temp_var();

  auto idx_dot_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_dot(""));

  // add the LHS node
  lnast->add_child(idx_dot_root, lhs_node);

  // add the top level identifier
  lnast->add_child(idx_dot_root, Lnast_node::create_ref(get_token(ast->get_data(ast->get_child(idx_start_ast)).token_entry)));

  // go to the tuple_dot_dot root
  auto idx_nxt_ast = ast->get_last_child(idx_start_ast);

  // go down to the first dot
  idx_nxt_ast = ast->get_child(idx_nxt_ast);

  while (scan_text(ast->get_data(idx_nxt_ast).token_entry) == ".") {
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    lnast->add_child(idx_dot_root, eval_rule(idx_nxt_ast, idx_dot_root));
    idx_nxt_ast = ast->get_sibling_next(idx_nxt_ast);
    if (idx_nxt_ast == ast->invalid_index()) break;
  }

  return lhs_node;
}

Lnast_node Prp_lnast::eval_bit_selection_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln) {
  mmap_lib::Tree_index idx_nxt_ln = cur_stmts;

  // go down to the identifier being selected
  auto select_expr_idx = ast->get_child(idx_start_ast);
  select_expr_idx      = ast->get_sibling_next(select_expr_idx);
  select_expr_idx      = ast->get_child(select_expr_idx);
  select_expr_idx      = ast->get_sibling_next(select_expr_idx);  // skip the [
  print_ast_node(select_expr_idx);
  // first evaluate the select expression if it exists
  bool       sel_exists  = scan_text(ast->get_data(select_expr_idx).token_entry) != "]";
  Lnast_node sel_rhs;
  if (sel_exists) {
    sel_rhs = eval_rule(select_expr_idx, idx_nxt_ln);
  }
  // create bit select node
  auto idx_sel_root = lnast->add_child(idx_nxt_ln, Lnast_node::create_bit_select(""));

  // create the LHS temporary variable
  auto lnast_temp = lnast->add_string(current_temp_var);
  auto retnode    = Lnast_node::create_ref(lnast_temp);
  lnast->add_child(idx_sel_root, retnode);
  get_next_temp_var();

  // add the identifier of the register being selected
  lnast->add_child(idx_sel_root, Lnast_node::create_ref(get_token(ast->get_data(ast->get_child(idx_start_ast)).token_entry)));

  // add the rhs value of the select expression
  if (sel_exists) {
    lnast->add_child(idx_sel_root, sel_rhs);
  }

  return retnode;
}

Lnast_node Prp_lnast::eval_fluid_ref(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln){
  mmap_lib::Tree_index idx_nxt_ln = cur_stmts;
  
  auto idx_dot_root = lnast->add_child(cur_stmts, Lnast_node::create_dot(""));
  
  // create the temporary variable for the LHS
  auto lnast_temp = lnast->add_string(current_temp_var);
  auto retnode         = Lnast_node::create_ref(lnast_temp);
  lnast->add_child(idx_dot_root, retnode);
  get_next_temp_var();
  
  auto idx_ref = ast->get_child(idx_start_ast);
  
  // add the name of the variable
  lnast->add_child(idx_dot_root, Lnast_node::create_ref(get_token(ast->get_data(idx_ref).token_entry)));
  
  auto idx_first_fluid = ast->get_sibling_next(idx_ref);
  auto first_command_text = scan_text(ast->get_data(idx_first_fluid).token_entry);
  if(first_command_text == "?"){
    lnast->add_child(idx_dot_root, Lnast_node::create_ref("__valid"));
  }
  else{ // must be either ! or !!
    auto second_fluid = ast->get_sibling_next(idx_first_fluid);
    if(second_fluid == ast->invalid_index()){
      lnast->add_child(idx_dot_root, Lnast_node::create_ref("__retry"));
    }
    else{
      lnast->add_child(idx_dot_root, Lnast_node::create_ref("__fluid_reset"));
    }
  }
  return retnode;
}

/*
 * Main function
 */
std::unique_ptr<Lnast> Prp_lnast::prp_ast_to_lnast(std::string_view module_name) {
  lnast = std::make_unique<Lnast>(module_name, transfer_memblock_ownership());

  lnast->set_root(Lnast_node(Lnast_ntype::create_top()));

  //generate_op_map();
  generate_priority_map();
  generate_expr_rules();

  translate_code_blocks(ast->get_root(), lnast->get_root());

  return std::move(lnast);
}

/*
 * Translation helper functions
 */

Lnast_node Prp_lnast::gen_operator(mmap_lib::Tree_index idx, uint8_t *skip_sibs) {
  auto tid   = scan_text(ast->get_data(idx).token_entry);
  *skip_sibs = 0;
  switch(tid[0]){
    case '|': return Lnast_node::create_or("");
    case '&': return Lnast_node::create_and("");
    case '^': return Lnast_node::create_xor("");
    case 'x': return Lnast_node::create_xor("");
    case '/': return Lnast_node::create_div("");
    case '*': return Lnast_node::create_mult("");
    case '=': return Lnast_node::create_same("");
    case '!': 
      if(tid.size() > 1)
        return Lnast_node::create_eq("");
      else
        return Lnast_node::create_logical_not("");
    case '>': 
      if(tid.size() > 1)
        return Lnast_node::create_ge("");
      else{
        if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == ">") {
          idx = ast->get_sibling_next(idx);
          if(scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == ">"){
            *skip_sibs = 2;
            return Lnast_node::create_rotate_shift_right("");
          }
          *skip_sibs = 1;
          return Lnast_node::create_shift_right("");
        }
        return Lnast_node::create_gt("");
      }
    case '<': 
      if(tid.size() > 1)
        return Lnast_node::create_le("");
      else{
        if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "<") {
          idx = ast->get_sibling_next(idx);
          if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "<") {
            *skip_sibs = 2;
            return Lnast_node::create_rotate_shift_left("");
          }
          *skip_sibs = 1;
          return Lnast_node::create_shift_left("");
        }
        return Lnast_node::create_lt("");
      }
    case 'o': return Lnast_node::create_logical_or("");
    case 'a': return Lnast_node::create_logical_and("");
    case 'i': return Lnast_node::create_same("");
    case '~': return Lnast_node::create_not("");
    case '+':
      if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "+") {
        *skip_sibs = 1;
        return Lnast_node::create_tuple_concat("");
      }
      return Lnast_node::create_plus("");
    case '-':
      if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "-"){
        *skip_sibs = 1;
        return Lnast_node::create_tuple_delete("");
      }
      return Lnast_node::create_minus("");
    default: // unimplemented operator
      fmt::print("Operator {} is not yet supported.", tid);
      return Lnast_node::create_phi(tid); // not sure what phi is
  }
  
  /*
  if (tid == ">") {
    if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == ">") {
      idx = ast->get_sibling_next(idx);
      if(scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == ">"){
        fmt::print("!!!!!!!!!!!!\nWARNING: >>> operator not implemented; aliased as right shift.\n!!!!!!!!!!!!\n");
        *skip_sibs = 2;
        return operator_map[">>>"];
      }
      *skip_sibs = 1;
      return operator_map[">>"];
    }
  }
  if (tid == "<") {
    if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "<") {
      if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "<") {
        fmt::print("!!!!!!!!!!!!\nWARNING: <<< operator not implemented; aliased as left shift.\n!!!!!!!!!!!!\n");
        *skip_sibs = 2;
        return operator_map["<<<"];
      }
      *skip_sibs = 1;
      return operator_map["<<"];
    }
  }
  if (tid == "+") {
    if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "+") {
      *skip_sibs = 1;
      return operator_map["++"];
    }
  }
  if (tid == "-"){
    if (scan_text(ast->get_data(ast->get_sibling_next(idx)).token_entry) == "-"){
      fmt::print("!!!!!!!!!!!!\nWARNING: -- operator not implemented; aliased as arithmetic minus.\n!!!!!!!!!!!!\n");
      *skip_sibs = 1;
      return operator_map["--"];
    }
  }
  return operator_map[tid];*/
}

inline void Prp_lnast::generate_op_map() {
  operator_map[">>"]  = Lnast_node::create_shift_right(">>");
  operator_map[">"]   = Lnast_node::create_gt(">");
  operator_map["<<"]  = Lnast_node::create_shift_left("<<");
  operator_map["<"]   = Lnast_node::create_lt("<");
  operator_map["|"]   = Lnast_node::create_or("|");
  operator_map["/"]   = Lnast_node::create_div("/");
  operator_map["*"]   = Lnast_node::create_mult("*");
  operator_map["++"]  = Lnast_node::create_tuple_concat("++");
  operator_map["+"]   = Lnast_node::create_plus("+");
  operator_map["-"]   = Lnast_node::create_minus("-");
  operator_map["=="]  = Lnast_node::create_same("==");
  operator_map["!="]  = Lnast_node::create_eq("!=");
  operator_map[">="]  = Lnast_node::create_ge(">=");
  operator_map["<="]  = Lnast_node::create_le("<=");
  operator_map["&"]   = Lnast_node::create_and("&");
  operator_map["^"]   = Lnast_node::create_xor("^");
  operator_map["xor"] = Lnast_node::create_xor("xor");
  operator_map["or"]  = Lnast_node::create_logical_or("or");
  operator_map["and"] = Lnast_node::create_logical_and("and");
  operator_map["is"]  = Lnast_node::create_same("is");
  operator_map["!"]   = Lnast_node::create_logical_not("!");
  operator_map["~"]   = Lnast_node::create_not("~");
  operator_map[">>>"] = Lnast_node::create_shift_right(">>>");
  operator_map["<<<"] = Lnast_node::create_shift_left("<<<");
  operator_map["--"]  = Lnast_node::create_minus("--");
}

inline void Prp_lnast::generate_priority_map() {
  priority_map[Lnast_ntype::Lnast_ntype_logical_and]        = 3;
  priority_map[Lnast_ntype::Lnast_ntype_logical_or]         = 3;
  priority_map[Lnast_ntype::Lnast_ntype_gt]                 = 2;
  priority_map[Lnast_ntype::Lnast_ntype_lt]                 = 2;
  priority_map[Lnast_ntype::Lnast_ntype_ge]                 = 2;
  priority_map[Lnast_ntype::Lnast_ntype_le]                 = 2;
  priority_map[Lnast_ntype::Lnast_ntype_same]               = 2;
  priority_map[Lnast_ntype::Lnast_ntype_eq]                 = 2;
  priority_map[Lnast_ntype::Lnast_ntype_tuple_concat]       = 1;
  priority_map[Lnast_ntype::Lnast_ntype_tuple_delete]       = 1;
  priority_map[Lnast_ntype::Lnast_ntype_shift_left]         = 1;
  priority_map[Lnast_ntype::Lnast_ntype_shift_right]        = 1;
  priority_map[Lnast_ntype::Lnast_ntype_rotate_shift_left]  = 1;
  priority_map[Lnast_ntype::Lnast_ntype_rotate_shift_right] = 1;
  priority_map[Lnast_ntype::Lnast_ntype_minus]              = 1;
  priority_map[Lnast_ntype::Lnast_ntype_plus]               = 1;
  priority_map[Lnast_ntype::Lnast_ntype_xor]                = 1;
  priority_map[Lnast_ntype::Lnast_ntype_or]                 = 1;
  priority_map[Lnast_ntype::Lnast_ntype_and]                = 1;
  priority_map[Lnast_ntype::Lnast_ntype_div]                = 1;
  priority_map[Lnast_ntype::Lnast_ntype_mult]               = 1;
}

/*
inline void Prp_lnast::generate_priority_matrix() {
  auto op_list[] = {Lnast_ntype::Lnast_ntype_logical_and, Lnast_ntype::Lnast_ntype_logical_or, Lnast_ntype::Lnast_ntype_gt, Lnast_ntype::Lnast_ntype_lt, Lnast_ntype::Lnast_ntype_ge, Lnast_ntype::Lnast_ntype_le, Lnast_ntype::Lnast_ntype_same, Lnast_ntype::Lnast_ntype_eq, Lnast_ntype::Lnast_ntype_tuple_concat, Lnast_ntype::Lnast_ntype_shift_left, Lnast_ntype::Lnast_ntype_shift_right, Lnast_ntype::Lnast_ntype_rotate_shift_left, Lnast_ntype::Lnast_ntype_rotate_shift_right, Lnast_ntype::Lnast_ntype_minus, Lnast_ntype::Lnast_ntype_plus, Lnast_ntype::Lnast_ntype_xor, Lnast_ntype::Lnast_ntype_or, Lnast_ntype::Lnast_ntype_and, Lnast_ntype::Lnast_ntype_div,Lnast_ntype::Lnast_ntype_mult};
  
  for(auto op0 : op_list){
    for(auto op1 : op_list){
      if(priority_map[op0] == priority_map)
    }
  }
}
*/

inline void Prp_lnast::generate_expr_rules(){
  expr_rules.insert(Prp_rule_logical_expression);
  expr_rules.insert(Prp_rule_relational_expression);
  expr_rules.insert(Prp_rule_additive_expression);
  expr_rules.insert(Prp_rule_bitwise_expression);
  expr_rules.insert(Prp_rule_multiplicative_expression);
  expr_rules.insert(Prp_rule_unary_expression);
  expr_rules.insert(Prp_rule_tuple_notation);
  //expr_rules.insert(Prp_rule_range_notation);
  expr_rules.insert(Prp_rule_tuple_array_notation);
  expr_rules.insert(Prp_rule_identifier);
  expr_rules.insert(Prp_rule_tuple_dot_notation);
  expr_rules.insert(Prp_rule_fcall_explicit);
  expr_rules.insert(Prp_rule_bit_selection_notation);
  expr_rules.insert(Prp_rule_scope_declaration);
}

std::string Prp_lnast::Lnast_type_to_string(Lnast_ntype type) {
  switch (type.get_raw_ntype()) {
    case Lnast_ntype::Lnast_ntype_invalid: return "invalid";
    case Lnast_ntype::Lnast_ntype_top: return "top";
    case Lnast_ntype::Lnast_ntype_stmts: return "statements";
    case Lnast_ntype::Lnast_ntype_cstmts: return "cstmts";
    case Lnast_ntype::Lnast_ntype_if: return "if";
    case Lnast_ntype::Lnast_ntype_cond: return "condition";
    case Lnast_ntype::Lnast_ntype_uif: return "unique if";
    case Lnast_ntype::Lnast_ntype_elif: return "elif";
    case Lnast_ntype::Lnast_ntype_for: return "for";
    case Lnast_ntype::Lnast_ntype_while: return "while";
    case Lnast_ntype::Lnast_ntype_func_call: return "func call";
    case Lnast_ntype::Lnast_ntype_func_def: return "func def";
    case Lnast_ntype::Lnast_ntype_select: return "select";
    case Lnast_ntype::Lnast_ntype_bit_select: return "bit select";
    case Lnast_ntype::Lnast_ntype_assign: return "assign";
    case Lnast_ntype::Lnast_ntype_dp_assign: return "dp_assign";
    case Lnast_ntype::Lnast_ntype_as: return "as";
    case Lnast_ntype::Lnast_ntype_label: return "label";
    case Lnast_ntype::Lnast_ntype_dot: return "dot";
    case Lnast_ntype::Lnast_ntype_logical_and: return "logical and";
    case Lnast_ntype::Lnast_ntype_logical_or: return "logical or";
    case Lnast_ntype::Lnast_ntype_and: return "and";
    case Lnast_ntype::Lnast_ntype_or: return "or";
    case Lnast_ntype::Lnast_ntype_xor: return "xor";
    case Lnast_ntype::Lnast_ntype_plus: return "plus";
    case Lnast_ntype::Lnast_ntype_minus: return "minus";
    case Lnast_ntype::Lnast_ntype_mult: return "mult";
    case Lnast_ntype::Lnast_ntype_div: return "divide";
    case Lnast_ntype::Lnast_ntype_eq: return "not equal";
    case Lnast_ntype::Lnast_ntype_same: return "same";
    case Lnast_ntype::Lnast_ntype_lt: return "less than";
    case Lnast_ntype::Lnast_ntype_le: return "less or equal";
    case Lnast_ntype::Lnast_ntype_gt: return "greater than";
    case Lnast_ntype::Lnast_ntype_ge: return "greater or equal";
    case Lnast_ntype::Lnast_ntype_tuple: return "tuple";
    case Lnast_ntype::Lnast_ntype_tuple_concat: return "tuple_concat";
    case Lnast_ntype::Lnast_ntype_tuple_delete: return "tuple_delete";
    case Lnast_ntype::Lnast_ntype_ref: return "ref";
    case Lnast_ntype::Lnast_ntype_const: return "const";
    case Lnast_ntype::Lnast_ntype_attr: return "attribute";
    case Lnast_ntype::Lnast_ntype_assert: return "assert";
    case Lnast_ntype::Lnast_ntype_not: return "bitwise not";
    case Lnast_ntype::Lnast_ntype_logical_not: return "logical not";
    case Lnast_ntype::Lnast_ntype_shift_left: return "left shift";
    case Lnast_ntype::Lnast_ntype_shift_right: return "right shift";
    case Lnast_ntype::Lnast_ntype_rotate_shift_left: return "left rotate";
    case Lnast_ntype::Lnast_ntype_rotate_shift_right: return "right rotate";
    default: return "unknown type";
  }
};

inline bool Prp_lnast::is_expr(mmap_lib::Tree_index idx) {
  PRINT_DBG_LN("Hello from is_expr\n");
  print_ast_node(idx);
  auto node        = ast->get_data(idx);
  if(node.token_entry != 0)
    return false;
  if (expr_rules.find(node.rule_id) != expr_rules.end()) {
    PRINT_DBG_LN("Found an expression node.\n");
    return true;
  }
  return false;
}

inline bool Prp_lnast::is_expr_with_operators(mmap_lib::Tree_index idx) {
  auto node        = ast->get_data(idx);
  if(node.token_entry != 0)
    return false;
  if (node.rule_id == Prp_rule_logical_expression || node.rule_id == Prp_rule_relational_expression || node.rule_id == Prp_rule_additive_expression || node.rule_id == Prp_rule_identifier) {
    return true;
  }
  return false;
}

inline uint8_t Prp_lnast::maybe_child_expr(mmap_lib::Tree_index idx) {
  auto rule_id = ast->get_data(idx).rule_id;
  if (rule_id == Prp_rule_assignment_expression) {  // TODO: add more true cases, if applicable
    if(ast->get_data(ast->get_last_child(idx)).rule_id == Prp_rule_tuple_notation || ast->get_data(ast->get_last_child(idx)).rule_id == Prp_rule_scope_declaration){
      return 2;
    }
    return true;
  }
  return false;
}

inline void Prp_lnast::create_simple_lhs_expr(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                              Lnast_node rhs_node) {
  auto base_rule_node = ast->get_data(idx_start_ast);
  if (base_rule_node.rule_id == Prp_rule_assignment_expression) {
    auto expr_root_idx = lnast->add_child(idx_start_ln, Lnast_node::create_assign(""));
    auto expr_lhs_idx  = ast->get_child(idx_start_ast);
    lnast->add_child(expr_root_idx, Lnast_node::create_ref(get_token(ast->get_data(expr_lhs_idx).token_entry)));
    lnast->add_child(expr_root_idx, rhs_node);
  } else {
    PRINT_LN("ERROR: simple lhs expression was passed an invalid AST node.\n");
  }
}

inline Lnast_node Prp_lnast::create_const_node(mmap_lib::Tree_index idx) {
  auto node      = ast->get_data(idx);
  bool is_string = node.rule_id == Prp_rule_string_constant;
  if (is_string) {
    if (node.token_entry) {  // this must be a double (") string
      return Lnast_node::create_const(get_token(node.token_entry));
    } else {  // merge the single ' string into one token
      auto idx_cur_string = ast->get_child(idx);
      // skip the starting '
      idx_cur_string             = ast->get_sibling_next(idx_cur_string);
      uint64_t    string_length  = 0;
      auto        cur_node       = ast->get_data(idx_cur_string);
      auto        cur_token      = get_token(cur_node.token_entry);
      std::string new_token_text = "";
      auto        string_start   = cur_token.pos1;
      while (scan_text(cur_node.token_entry) != "\'") {
        if (string_length != 0) {
          // insert space(s)
          auto spaces_needed = cur_token.pos1 - (string_start + string_length);
          new_token_text.append(spaces_needed, ' ');
        }
        string_length += cur_token.text.size();
        absl::StrAppend(&new_token_text, std::string(cur_token.text));
        idx_cur_string = ast->get_sibling_next(idx_cur_string);
        cur_node       = ast->get_data(idx_cur_string);
        cur_token      = get_token(cur_node.token_entry);
      }
      auto new_token_view = lnast->add_string(new_token_text);
      return Lnast_node::create_const(new_token_view, cur_token.line, string_start, string_start + string_length + 1);
    }
  } else {  // we need to add the datatype to the token string if it's implicit (decimal only)
    bool negative = false;
    if(node.token_entry == 0){ // negative sign
      negative = true;
      auto idx_num = ast->get_last_child(idx);
      node = ast->get_data(idx_num);
    }
    auto token = get_token(node.token_entry);
    if (is_decimal(token.text)) {
      std::string decimal_string;
      if(negative){
        decimal_string.assign(absl::StrCat("-0d", token.text));
      }
      else{
        decimal_string.assign(absl::StrCat("0d", token.text));
      }
      auto ln_decimal_view = lnast->add_string(decimal_string);
      return Lnast_node::create_const(ln_decimal_view, token.line, token.pos1, token.pos2);
    } else
      if(negative){
        auto ln_decimal_view = lnast->add_string(absl::StrCat("-", token.text));
        return Lnast_node::create_const(ln_decimal_view, token.line, token.pos1, token.pos2);
      }
      else{
        return Lnast_node::create_const(token);
      }
  }
}

inline bool Prp_lnast::is_decimal(std::string_view number) {
  if (number.size() < 3)  // must have at least 0x/n/b(something), otherwise it must be decimal
    return true;
  if(number[0] == 't' || number[2] == 'l'){
    return false; // must be "true" or "false"
  }

  if (number[0] != '0') return true;  // must start with 0;

  if (number[1] != 'x' || number[1] != 'X' || number[1] == 'b' || number[1] == 'B') return false;

  return true;
}
