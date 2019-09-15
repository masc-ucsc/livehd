#include <ctype.h>
#include <algorithm>
#include <iostream>

#include "fmt/format.h"
#include "prp.hpp"

void Prp::eat_comments(){
	while (scan_is_token(Token_id_comment) && !scan_is_end()) scan_next();
}

bool Prp::rule_start(){
  //fmt::print("Called rule_start.\n");
  
  bool ok = rule_code_blocks();
  if(ok){
    //fmt::print("Matched rule_start.\n");
    return true;
  }
  else{
    //fmt::print("Failed rule_start.\n");
    return false;
  }
}

bool Prp::rule_code_blocks(){
  INIT_FUNCTION("Called rule_code_blocks.\n");
  
  eat_comments();
  if (!rule_code_block_int()){
    RULE_FAILED("Failed rule_code_blocks.\n");
  }
  while(rule_code_block_int() && !scan_is_end());
  
  RULE_SUCCESS("Matched rule_code_blocks.\n", Prp_rule_code_blocks);
}

bool Prp::rule_code_block_int(){
  INIT_FUNCTION("Called rule_code_block_int.\n");
  
  eat_comments();
  
  if (rule_if_statement()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_for_statement()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_while_statement()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_try_statement()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_punch_format()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_assignment_expression()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_fcall_implicit()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_fcall_explicit()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_return_statement()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_compile_check_statement()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (rule_assertion_statement()){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  
  RULE_FAILED("Failed rule_code_block_int.\n");
}

bool Prp::rule_if_statement(){
  INIT_FUNCTION("Called rule_if_statement.\n");
  
  if(!(SCAN_IS_TOKEN(Pyrope_id_if, Prp_rule_if_statement) || SCAN_IS_TOKEN(Pyrope_id_unique))){ RULE_FAILED("Failed rule_if_statement; couldn't find an if or a unique token.\n"); }
  if(!rule_logical_expression()){ RULE_FAILED("Failed rule_if_statement; couldn't find a logical_expression.\n"); }
  if(!rule_empty_scope_colon()){ RULE_FAILED("Failed rule_if_statement; couldn't find an empty_scope_colon.\n"); }
  if(!rule_block_body()){ RULE_FAILED("Failed rule_if_statement; couldn't find a block_body.\n"); }
  // optional
  rule_else_statement();
  
  RULE_SUCCESS("Matched rule_if_statement.\n", Prp_rule_if_statement);
}

bool Prp::rule_for_statement(){
  INIT_FUNCTION("Called rule_for_statement.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_for)){ RULE_FAILED("Failed rule_for_statement; couldn't find a for token.\n"); }
  if(!rule_for_index()){ RULE_FAILED("Failed rule_for_statement; couldn't find a for_index.\n"); }
  if(!rule_empty_scope_colon()){ RULE_FAILED("Failed rule_for_statement; couldn't find an empty_scope_colon.\n"); }
  if(!rule_block_body()){ RULE_FAILED("Failed rule_for_statement; couldn't find a block_body.\n"); }
  
  RULE_SUCCESS("Matched rule_for_statement.\n", Prp_rule_for_statement);
}

bool Prp::rule_for_index(){
  INIT_FUNCTION("Called rule_for_index.\n");
  
  if(!rule_rhs_expression_property()){ RULE_FAILED("Failed rule_for_index; couldn't find an rhs_expression_property.\n"); }
  
  bool next = true;
  while(next){
    if(!rule_rhs_expression_property()){
      next = false;
    }
  }
  
  RULE_SUCCESS("Matched rule_for_index.\n", Prp_rule_for_index);
}

bool Prp::rule_else_statement(){
  INIT_FUNCTION("Called rule_else_statement.\n");
  
  // option 1
  if(SCAN_IS_TOKEN(Pyrope_id_elif, Prp_rule_else_statement)){
    if(!rule_logical_expression()){ RULE_FAILED("Failed rule_else_statement (ELIF path); couldn't find a condition for the elif.\n"); }
    if(!rule_empty_scope_colon()){ RULE_FAILED("Failed rule_else_statement; couldn't find an empty_scope_colon.\n"); }
    if(!rule_block_body()){ RULE_FAILED("Failed rule_else_statement; couldn't find a block_body.\n"); }
    // optional
    rule_else_statement();
    RULE_SUCCESS("Matched rule_else_statement (ELIF path).\n", Prp_rule_else_statement);
  }
  else if(!SCAN_IS_TOKEN(Pyrope_id_else, Prp_rule_else_statement)){ RULE_FAILED("Failed rule_else_statement; couldn't find either an else or an elif.\n"); }
  if(!rule_empty_scope_colon()){ RULE_FAILED("Failed rule_else_statement (ELSE path); couldn't find an empty_scope_colon.\n"); }
  if(!rule_block_body()){ RULE_FAILED("Failed rule_else_statement; couldn't find a block_body.\n"); }
  
  RULE_SUCCESS("Matched rule_else_statement.\n", Prp_rule_else_statement);
}

bool Prp::rule_while_statement(){
  INIT_FUNCTION("Called rule_while_statement.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_while)){ RULE_FAILED("Failed rule_while_statement; couldn't find a while.\n"); }
  if(!rule_logical_expression()){ RULE_FAILED("Failed rule_while_statement; couldn't find a logical_expression.\n"); }
  if(!rule_empty_scope_colon()){ RULE_FAILED("Failed rule_while_statement; couldn't find an empty_scope_colon.\n"); }
  if(!rule_block_body()){ RULE_FAILED("Failed rule_while_statement; couldn't find a block_body.\n"); }
  
  RULE_SUCCESS("Failed rule_while_statement; couldn't find a while token.\n", Prp_rule_while_statement);
}

bool Prp::rule_try_statement(){
  INIT_FUNCTION("Called rule_try_statement.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_try, Prp_rule_try_statement)){ RULE_FAILED("Failed rule_try_statement; couldn't find a try token.\n"); }
  if(!rule_empty_scope_colon()){ RULE_FAILED("Failed rule_try_statement; couldn't find an empty_scope_colon\n"); }
  if(!rule_block_body()){ RULE_FAILED("Failed rule_try_statement; couldn't find a block_body.\n"); }
  //if(!SCAN_IS_TOKEN(Pyrope_id_else, Prp_rule_try_statement)){ RULE_FAILED("Failed rule_try_statement; couldn't find an else token.\n"); }
  
  // optional
  rule_scope_else();
  
  RULE_SUCCESS("Matched rule_try_statement.\n", Prp_rule_try_statement);
}


// TODO: check correctness of scanner with ASSERTION token ("I")
bool Prp::rule_assertion_statement(){
  INIT_FUNCTION("Called rule_assertion_statement.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_assertion, Prp_rule_assertion_statement)){ RULE_FAILED("Failed rule_assertion_statement; couldn't find an assertion token.\n"); }
  else{
    if(!rule_logical_expression()){ RULE_FAILED("Failed rule_assertion_statement; couldn't find a logical_expression.\n"); }
  }
  
  RULE_SUCCESS("Matched rule_assertion_statement.\n", Prp_rule_assertion_statement);
}

// TODO: check correctness of scanner with NEGATION token ("N")
bool Prp::rule_negation_statement(){
  INIT_FUNCTION("Called rule_negation_statement.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_negation)){ RULE_FAILED("Failed rule_negation_statement; couldn't find a negation token.\n"); }
  else{
    if(!rule_logical_expression()){ RULE_FAILED("Failed rule_negation_statement; couldn't find a logical_expression.\n"); }
  }
  
  RULE_SUCCESS("Matched rule_logical_expression.\n", Prp_rule_negation_statement);
}

bool Prp::rule_empty_scope_colon(){
  INIT_FUNCTION("Called rule_empty_scope_colon.\n");
  
  // optional
  if(SCAN_IS_TOKEN(Token_id_colon, Prp_rule_empty_scope_colon)){
    if(!SCAN_IS_TOKEN(Token_id_colon), Prp_rule_empty_scope_colon){ RULE_FAILED("Failed rule_empty_scope_colon; couldn't find a second colon.\n"); }
  }
  
  if(!SCAN_IS_TOKEN(Token_id_ob, Prp_rule_empty_scope_colon)){ RULE_FAILED("Failed rule_empty_scope_colon; couldn't find an opening brace.\n"); }

  RULE_SUCCESS("Matched rule_empty_scope_colon.\n", Prp_rule_empty_scope_colon);
}

bool Prp::rule_scope_else(){
  INIT_FUNCTION("Called rule_scope_else.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_else, Prp_rule_scope_else)){ RULE_FAILED("Failed rule_scope_else; couldn't find an else token.\n"); }
  
  if(!SCAN_IS_TOKEN(Token_id_ob, Prp_rule_scope_else)){ RULE_FAILED("Failed rule_scope_else; couldn't find an open brace.\n"); }
  
  // optional
  rule_scope_body();
  
  if(!SCAN_IS_TOKEN(Token_id_cb, Prp_rule_scope_else))
  
  RULE_SUCCESS("Matched rule_scope_else.\n", Prp_rule_scope_else);
}

bool Prp::rule_scope_body(){
  INIT_FUNCTION("Called rule_scope_body.\n");
  
  if(!rule_code_blocks()){
    if(!rule_logical_expression()){ RULE_FAILED("Failed rule_scope_body; couldn't find either a logical_expression or a code_blocks.\n"); }
    RULE_SUCCESS("Matched rule_scope_body", Prp_rule_scope_body);
  }
  
  // option 2
  if(!rule_logical_expression()){ RULE_FAILED("Failed rule_scope_body\n", Prp_rule_scope_body); }
  
  RULE_SUCCESS("Matched rule_scope_body.\n", Prp_rule_scope_body);
}

bool Prp::rule_scope(){
  INIT_FUNCTION("Called rule_scope.\n");
  
  if(SCAN_IS_TOKEN(Token_id_colon)){
    // optional
    rule_scope_condition();
    if(SCAN_IS_TOKEN(Token_id_colon)){
      // optional
      rule_logical_expression();
      RULE_SUCCESS("Matched rule_scope.\n", Prp_rule_scope);
    }
  }
  
  RULE_FAILED("Failed rule_scope; generic.\n");
}

bool Prp::rule_scope_condition(){
  INIT_FUNCTION("Called rule_scope_condition.\n");
  
  // optional
  rule_scope_argument();
  
  if(SCAN_IS_TOKEN(Pyrope_id_when)){
    if(!rule_logical_expression()){ RULE_FAILED("Failed rule_scope_condition; couldn't find an answering logical expression.\n"); }
  }
  
  RULE_SUCCESS("Matched rule_scope_condition.\n", Prp_rule_scope_condition);
}

bool Prp::rule_scope_colon(){
  INIT_FUNCTION("Called rule_scope_colon.\n");
  
  if(!rule_scope()){ RULE_FAILED("Failed rule_scope_colon; couldn't find a scope.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_ob)){ RULE_FAILED("Failed rule_scope_colon; couldn't find an opening brace.\n"); }
  
  RULE_SUCCESS("Matched rule_scope_colon.\n", Prp_rule_scope_colon);
}

bool Prp::rule_scope_argument(){
  INIT_FUNCTION("Called rule_scope_argument.\n");
  if(!rule_fcall_arg_notation()){ 
    RULE_FAILED("Failed rule_scope_argument; couldn't find an fcall_arg_notation.\n"); 
  }
  
  RULE_SUCCESS("Matched rule_scope_argument.\n", Prp_rule_scope_argument);
}

bool Prp::rule_punch_format(){
  INIT_FUNCTION("Called rule_punch_format.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_punch)){ RULE_FAILED("Failed rule_punch_format; couldn't find a punch token.\n"); }
  if(!rule_identifier()){ RULE_FAILED("Failed rule_punch_format; couldn't find an identifier.\n"); }
  
  if(!SCAN_IS_TOKEN(Token_id_at) || SCAN_IS_TOKEN(Token_id_percent)){ RULE_FAILED("Failed rule_punch_format; couldn't find a percent or an at symbol.\n"); }
  if(!rule_punch_rhs()){ RULE_FAILED("Failed rule_punch_format; couldn't find a punch_rhs.\n"); }
  
  RULE_SUCCESS("Matched rule_punch_format.\n", Prp_rule_punch_format);
}

bool Prp::rule_scope_declaration(){
  INIT_FUNCTION("Called rule_scope_declaration.\n");
  
  if(!rule_scope()){ RULE_FAILED("Failed rule_scope_declaration; couldn't find a scope.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_ob)){ RULE_FAILED("Failed rule_scope_declaration; couldn't find an open brace.\n"); }
  
  // optional
  rule_scope_body();
  
  if(!SCAN_IS_TOKEN(Token_id_cb)){ RULE_FAILED("Failed rule_scope_declaration; couldn't find a closing brace.\n"); }
  
  // optional
  rule_scope_else();
  
  RULE_SUCCESS("Matched rule_scope_declaration.\n", Prp_rule_scope_declaration);
}

bool Prp::rule_punch_rhs(){
  INIT_FUNCTION("Called rule_punch_rhs.\n");
  
  if(!SCAN_IS_TOKEN(Token_id_div)){ RULE_FAILED("Failed rule_punch_rhs; couldn't find a div token.\n"); }
  
  // optional
  bool next = true;
  if(rule_identifier()){
    while(next){
      if(!SCAN_IS_TOKEN(Token_id_dot))
        next = false;
      else{
        if(!rule_identifier()){ RULE_FAILED("Failed rule_punch_rhs; couldn't find an identifier.\n") ;}
      }
    }
  }
  
  if(!SCAN_IS_TOKEN(Token_id_div)){ RULE_FAILED("Failed rule_punch_rhs; couldn't find a div token."); }
  
  next = true;
  while(next){
    if(!SCAN_IS_TOKEN(Token_id_dot)){
      next = false;
    }
    else{
      if(!rule_identifier()){ RULE_FAILED("Failed rule_punch_rhs; couldn't find an identifier.\n") ;}
    }
  }
  
  RULE_SUCCESS("Matched rule_punch_rhs.\n", Prp_rule_punch_rhs);
}

bool Prp::rule_function_pipe(){
  INIT_FUNCTION("Called rule_function_pipe.\n");
  
  if(!SCAN_IS_TOKEN(Token_id_pipe)){ RULE_FAILED("Failed rule_function_pipe; couldn't find a pipe token."); }
  else{
    debug_consume();
    tokens_consumed++;
    if(!(rule_fcall_implicit() || rule_fcall_explicit())){ RULE_FAILED("Failed rule_function_pipe; couldn't find a function call.\n"); }
    else{
      bool next = true;
      /* zero or more of the following */
      while(next){
        if(!(rule_fcall_implicit() || rule_fcall_explicit())){
          next = false;
        }
      }
    }
  }
  
  RULE_SUCCESS("Matched rule_function_pipe.\n", Prp_rule_function_pipe);
}

bool Prp::rule_fcall_explicit(){
  INIT_FUNCTION("Called rule_fcall_explicit.\n");
  
  if(rule_constant()){ RULE_FAILED("Failed rule_fcall_explicit; found a constant.\n"); }
  
  // optional
  if(rule_tuple_notation()){
    if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_fcall_explicit; couldn't find an answering dot token.\n"); }
  }
  
  if(!rule_tuple_dot_notation()){ RULE_FAILED("Failed rule_fcall_explicit; couldn't find a tuple_dot_notation.\n"); }
  
  if(!rule_fcall_arg_notation()){ RULE_FAILED("Failed rule_fcall_explicit; couldn't find an fcall_arg_notation.\n"); }
  
  // optional
  rule_scope_declaration();
  
  bool next = true;
  while(next){
    if(SCAN_IS_TOKEN(Token_id_dot)){
      if(!(rule_fcall_explicit() || rule_tuple_dot_notation())){ RULE_FAILED("Failed rule_fcall_explicit; couldn't find answering fcall_explicit or tuple_dot_notation\n"); }
    }
    else 
      next = false;
  }
  
  RULE_SUCCESS("Matched rule_fcall_explicit", Prp_rule_fcall_explicit);
}

bool Prp::rule_fcall_arg_notation(){
  INIT_FUNCTION("Called rule_fcall_arg_notation.\n");
  bool next = true;
  
  if(!SCAN_IS_TOKEN(Token_id_op)){ RULE_FAILED("Failed rule_fcall_arg_notation; couldn't find an opening parenthesis.\n"); }
  
  if(SCAN_IS_TOKEN(Token_id_cp)){ RULE_SUCCESS("Matched rule_fcall_arg_notation.\n", Prp_rule_fcall_arg_notation); }

  if(!(rule_rhs_expression_property() || rule_logical_expression())){ RULE_FAILED("Failed rule_fcall_arg_notation; couldn't find either an rhs_expression_property or a logical_expression.\n"); }
  // zero or more of the following
  while(next){
    if(SCAN_IS_TOKEN(Token_id_comma)){
      if(!(rule_rhs_expression_property() || rule_logical_expression())){ RULE_FAILED("Failed rule_fcall_arg_notation; couldn't find either an rhs_expression_property or a logical_expression."); }
    }
    else 
      next = false;
  }
  // optional
  SCAN_IS_TOKEN(Token_id_comma);
  
  if(!SCAN_IS_TOKEN(Token_id_cp)){ RULE_FAILED("Failed rule_fcall_arg_notation; couldn't find a closing parenthesis.\n"); }
  
  RULE_SUCCESS("Matched rule_fcall_arg_notation.\n", Prp_rule_fcall_arg_notation);
}

bool Prp::rule_fcall_implicit(){
  INIT_FUNCTION("Called rule_fcall_implicit.\n");
  
  if(rule_constant()){ RULE_FAILED("Failed rule_fcall_implicit; found a constant.\n"); }
  if(!rule_tuple_dot_notation()){ RULE_FAILED("Failed rule_fcall_implicit; couldn't find a tuple_dot_notation.\n"); }
  if(!rule_scope_declaration()){ RULE_FAILED("Failed rule_fcall_implicit; couldnt find a scope_declaration.\n"); }
  
  RULE_SUCCESS("Matched rule_fcall_implicit.\n", Prp_rule_fcall_implicit);
}

bool Prp::rule_assignment_expression(){
  INIT_FUNCTION("Called rule_assignment_expression.\n");
  
  if(rule_constant()){ RULE_FAILED("Failed rule_assignment_expression; found a constant.\n"); }
  if(!(rule_lhs_expression() || rule_overload_notation())){ RULE_FAILED("Failed rule_assignment_expression; couldn't find either an overload_notation or an lhs_expression.\n"); }
  if(!rule_assignment_operator()){ RULE_FAILED("Failed rule_assignment_expression; couldn't find an assignment_operator.\n"); }
  if(!(rule_rhs_expression_property() || rule_logical_expression() || rule_fcall_implicit())){ RULE_FAILED("Failed rule_assignment_expression; couldn't find an rhs_expression_property, an fcall_implicit, or a logical_expression.\n"); }
  
  
  RULE_SUCCESS("Matched rule_assignment_expression.\n", Prp_rule_assignment_expression);
}

bool Prp::rule_return_statement(){
  INIT_FUNCTION("Called rule_return_statement.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_return)){ RULE_FAILED("Failed rule_return_statement; couldn't find a return token.\n"); }
  if(!rule_logical_expression()){ RULE_FAILED("Failed rule_return_statement; couldn't a logical_expression.\n"); }
  
  RULE_SUCCESS("Matched rule_return_statement.\n", Prp_rule_return_statement);
}

bool Prp::rule_compile_check_statement(){
  INIT_FUNCTION("Called rule_compile_check_statement.\n");
  
  if(!scan_is_token(Token_id_pound)){ RULE_FAILED("Failed rule_compile_check_statement; couldn't find a pound operator.\n"); }
  if(!rule_code_block_int()){ RULE_FAILED("Failed rule_compile_check_statement; couldn't find a code_block_int.\n"); }
  
  RULE_SUCCESS("Matched rule_compile_check_statement.\n", Prp_rule_compile_check_statement);
}

bool Prp::rule_block_body(){
  INIT_FUNCTION("Called rule_block_body.\n");
  
  // optional
  rule_code_blocks();
  
  if(!SCAN_IS_TOKEN(Token_id_cb, Prp_rule_block_body)){ RULE_FAILED("Failed rule_block_body; couldn't find a closing brace.\n"); }
  
  RULE_SUCCESS("Matched rule_block_body.\n", Prp_rule_block_body);
}

bool Prp::rule_lhs_expression(){
  INIT_FUNCTION("Called rule_lhs_expression.\n");
  
  if(SCAN_IS_TOKEN(Token_id_backslash)){
    if(!SCAN_IS_TOKEN(Token_id_backslash)){ RULE_FAILED("Failed rule_lhs_expression; couldn't find a second backslash.\n"); }
  }
  if(!( rule_tuple_notation() || rule_range_notation())){ RULE_FAILED("Failed rule_lhs_expression; couldn't find either a range_notation or a tuple_notation.\n"); }
  
  RULE_SUCCESS("Matched rule_lhs_expression.\n", Prp_rule_lhs_expression);
}

bool Prp::rule_rhs_expression_property(){
  INIT_FUNCTION("Called rule_rhs_expression_property.\n");
  
  if(!(rule_identifier() == 2)){ RULE_FAILED("Failed rule_rhs_expression_property; couldn't find an identifier that was a label.\n"); }
  
  // optional
  rule_fcall_explicit() || rule_tuple_notation();
  
  RULE_SUCCESS("Matched rule_rhs_expression_property.\n", Prp_rule_rhs_expression_property);
}

bool Prp::rule_tuple_notation(){
  INIT_FUNCTION("Called rule_tuple_notation.\n");
  
  // options 1 and 2
  if(SCAN_IS_TOKEN(Token_id_op, Prp_rule_tuple_notation)){
    if(SCAN_IS_TOKEN(Token_id_cp, Prp_rule_tuple_notation)){ RULE_SUCCESS("Matched rule_tuple_notation; first option.\n", Prp_rule_tuple_notation); }
    
    if(!(rule_rhs_expression_property() || rule_logical_expression())){ RULE_FAILED("Failed rule_tuple_notation; couldn't find either an rhs_expression_property or a logical_expression.\n"); }
    
    bool next = true;
    while(next){
      if(!SCAN_IS_TOKEN(Token_id_comma, Prp_rule_tuple_notation)){ next = false; }
      else{
        if(!(rule_rhs_expression_property() || rule_logical_expression())){ RULE_FAILED("Failed rule_tuple_notation; couldn't find either an rhs_expression_property or a logical_expression.\n"); }
      }
    }
    
    // optional
    SCAN_IS_TOKEN(Token_id_comma, Prp_rule_tuple_notation);
    
    if(SCAN_IS_TOKEN(Token_id_cp, Prp_rule_tuple_notation)){
      // optional
      rule_tuple_by_notation() || rule_bit_selection_bracket();
      
      RULE_SUCCESS("Matched rule_tuple_notation; second option.\n", Prp_rule_tuple_notation); 
    }
  }
  
  // option 3
  if(!rule_bit_selection_notation()){ RULE_FAILED("Failed rule_tuple_notation; couldn't match any options.\n"); }
  
  RULE_SUCCESS("Matched rule_tuple_notation; third option.\n", Prp_rule_tuple_notation);
}

bool Prp::rule_range_notation(){
  INIT_FUNCTION("Called rule_range_notation.\n");
  
  // optional
  rule_bit_selection_notation();
  
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_range_notation; couldn't find the first dot.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_range_notation; couldn't find the second dot.\n"); }
  
  // optional
  rule_additive_expression();
  
  // optional
  rule_tuple_by_notation();
  
  RULE_SUCCESS("Matched rule_range_notation.\n", Prp_rule_range_notation);
}

bool Prp::rule_bit_selection_notation(){
  INIT_FUNCTION("Called rule_bit_selection_notation.\n");
  
  if(!rule_tuple_dot_notation()){ RULE_FAILED("Failed rule_bit_selection_notation; couldn't find a tuple_dot_notation.\n"); }
  if(!rule_bit_selection_bracket()){ RULE_FAILED("Failed rule_bit_selection_notation; couldn't find a bit_selection_bracket.\n"); }
  
  RULE_SUCCESS("Matched rule_bit_selection_notation.\n", Prp_rule_bit_selection_notation);
}

bool Prp::rule_tuple_dot_notation(){
  INIT_FUNCTION("Called rule_tuple_dot_notation.\n");
  
  if(!rule_tuple_array_notation()){ RULE_FAILED("Failed tuple_dot_notation; couldn't find a tuple_array_notation.\n"); }
  if(!rule_tuple_dot_dot()){ RULE_FAILED("Failed rule_tuple_dot_notation; couldn't find a tuple_dot_dot.\n"); }
  
  RULE_SUCCESS("Matched rule_tuple_dot_notation.\n", Prp_rule_tuple_dot_notation);
}

bool Prp::rule_tuple_dot_dot(){
  INIT_FUNCTION("Called rule_tuple_dot_dot.\n");
  bool next = true;
  
  while(next){
    if(!SCAN_IS_TOKEN(Token_id_dot)){ next = false; }
    else{
      if(!rule_tuple_array_notation()){ RULE_FAILED("Failed rule_tuple_dot_dot; couldn't find a tuple_array_notation.\n"); }
    }
  }
  
  RULE_SUCCESS("Matched rule_tuple_dot_dot\n", Prp_rule_tuple_dot_dot);
}

bool Prp::rule_tuple_array_notation(){
  INIT_FUNCTION("Called rule_tuple_array_notation.\n");
  
  if(!rule_lhs_var_name()){ RULE_FAILED("Failed rule_tuple_array_notation; couldn't find an lhs_var_name.\n"); }
  if(!rule_tuple_array_bracket()){ RULE_FAILED("Failed rule_tuple_array_notation; couldn't find a tuple_array_bracket.\n"); }
  
  RULE_SUCCESS("Matched rule_tuple_array_notation.\n", Prp_rule_tuple_array_notation);
}

bool Prp::rule_lhs_var_name(){
  INIT_FUNCTION("Called rule_lhs_var_name.\n");
  
  if(!(rule_identifier() || rule_constant())){ RULE_FAILED("Failed rule_lhs_var_name; couldn't find an identifier or a constant.\n"); }
  
  RULE_SUCCESS("Matched rule_lhs_var_name.\n", Prp_rule_lhs_var_name);
}

bool Prp::rule_tuple_array_bracket(){
  INIT_FUNCTION("Called rule_tuple_array_bracket.\n");
  bool next = true;
  
  while(next){
    if(!SCAN_IS_TOKEN(Token_id_obr)){ next = false; }
    else{
      if(!rule_logical_expression()){ RULE_FAILED("Failed rule_tuple_array_bracket; couldn't find a logical_expression.\n"); }
      if(!SCAN_IS_TOKEN(Token_id_cbr)){ RULE_FAILED("Failed rule_tuple_array_bracket; couldn't find a closing bracket.\n"); }
    }
  }
  
  RULE_SUCCESS("Matched rule_tuple_array_bracket.\n", Prp_rule_tuple_array_bracket);
}

uint8_t Prp::rule_identifier(){
  INIT_FUNCTION("Called rule_identifier.\n");
  
  // optional
  SCAN_IS_TOKEN(Token_id_bang) || SCAN_IS_TOKEN(Pyrope_id_tilde);
  
  if(SCAN_IS_TOKEN(Token_id_label, Prp_rule_identifier)){
    //fmt::print("Matched rule_identifier; found a label.\n");
    debug_stat.rules_matched++;
    DEBUG_UP(Prp_rule_identifier);
    return 2;
  }
  
  if(!(SCAN_IS_TOKEN(Token_id_register, Prp_rule_identifier) || SCAN_IS_TOKEN(Token_id_input, Prp_rule_identifier) || SCAN_IS_TOKEN(Token_id_output, Prp_rule_identifier) || SCAN_IS_TOKEN(Token_id_alnum, Prp_rule_identifier))){ RULE_FAILED("Failed rule_identifier; couldn't find a name.\n"); }
  
  // optional
  SCAN_IS_TOKEN(Token_id_qmark);
  
  RULE_SUCCESS("Matched rule_identifier.\n", Prp_rule_identifier);
}

// some rules want there to not be a constant; in this case, we don't want AST changes or tokens consumed if the rule is matched.
bool Prp::rule_constant(){
  INIT_FUNCTION("Called rule_constant\n");
  
  // option 1
  if(rule_numerical_constant()){ RULE_SUCCESS("Matched rule_constant.\n", Prp_rule_constant); }
  
  // option 2
  if(!rule_string_constant()){ RULE_FAILED("Failed rule_constant; couldn't find either a numerical or string constant.\n"); }
  
  RULE_SUCCESS("Matched rule_constant.\n", Prp_rule_constant);
}

// TODO: check if hex constants are supported.
bool Prp::rule_numerical_constant(){
  INIT_FUNCTION("Called rule_numerical_constant.\n");
  
  if(!SCAN_IS_TOKEN(Token_id_num, Prp_rule_numerical_constant)){ RULE_FAILED("Failed rule_numerical_constant; couldn't find a number.\n"); }
  
  RULE_SUCCESS("Matched rule_numerical_constant.\n", Prp_rule_numerical_constant);
}

// FIXME: add support for single tick strings.
bool Prp::rule_string_constant(){
  INIT_FUNCTION("Called rule_string_constant.\n");
  
  if(!SCAN_IS_TOKEN(Token_id_string, Prp_rule_string_constant)){ RULE_FAILED("Failed rule_string_constant; couldn't find a double string.\n"); }
  
  RULE_SUCCESS("Matched rule_string_constant.\n", Prp_rule_string_constant);
}

bool Prp::rule_assignment_operator(){
  INIT_FUNCTION("Called rule_assignment_operator.\n");
  
  if(SCAN_IS_TOKEN(Token_id_eq, Prp_rule_assignment_operator)){ RULE_SUCCESS("Matched rule_assignment_operator; found an equals.\n", Prp_rule_assignment_operator); }
  
  if(SCAN_IS_TOKEN(Pyrope_id_as, Prp_rule_assignment_operator)){ RULE_SUCCESS("Matched rule_assignment_operator; found an as.\n", Prp_rule_assignment_operator); }
  
  if(SCAN_IS_TOKEN(Token_id_plus, Prp_rule_assignment_operator) || SCAN_IS_TOKEN(Token_id_mult) || SCAN_IS_TOKEN(Token_id_minus)){
    SCAN_IS_TOKEN(Token_id_plus, Prp_rule_assignment_operator); // possible to have a second plus
    if(SCAN_IS_TOKEN(Token_id_eq, Prp_rule_assignment_operator)){ RULE_SUCCESS("Matched rule_assignment_operator; found an [operator] equals (two tokens or ++=).\n", Prp_rule_assignment_operator); }
  }
  
  if(SCAN_IS_TOKEN(Token_id_lt, Prp_rule_assignment_operator)){
    if(!SCAN_IS_TOKEN(Token_id_lt, Prp_rule_assignment_operator)){ RULE_FAILED("Failed rule_assignment_operator; only found one less than.\n"); }
    if(!SCAN_IS_TOKEN(Token_id_eq, Prp_rule_assignment_operator)){ RULE_FAILED("Failed rule_assignment_operator; couldn't find an equals.\n"); }
    
    RULE_SUCCESS("Matched rule_assignment_operator; found a <<=.\n", Prp_rule_assignment_operator);
  }
  
  if(SCAN_IS_TOKEN(Token_id_gt, Prp_rule_assignment_operator)){
    if(!SCAN_IS_TOKEN(Token_id_gt, Prp_rule_assignment_operator)){ RULE_FAILED("Failed rule_assignment_operator; only found one greater than.\n"); }
    if(!SCAN_IS_TOKEN(Token_id_eq, Prp_rule_assignment_operator)){ RULE_FAILED("Failed rule_assignment_operator; couldn't find an equals.\n"); }
    
    RULE_SUCCESS("Matched rule_assignment_operator; found a >>=.\n", Prp_rule_assignment_operator);
  }
  
  RULE_FAILED("Failed rule_assignment_operator; couldn't find any of the operators.\n");
}

bool Prp::rule_tuple_by_notation(){
  INIT_FUNCTION("Called rule_tuple_by_notation.\n");
  
  if(!SCAN_IS_TOKEN(Pyrope_id_by)){ RULE_FAILED("Failed rule_tuple_by_notation; couldn't find a by token.\n"); }
  if(!rule_lhs_var_name()){ RULE_FAILED("Failed rule_tuple_by_notation; couldn't find a rule_lhs_var_name.\n"); }
  
  RULE_SUCCESS("Matched rule_tuple_by_notation.\n", Prp_rule_tuple_by_notation);
}

bool Prp::rule_bit_selection_bracket(){
  INIT_FUNCTION("Called rule_bit_selection_bracket.\n");
  bool next = true;
  while(next){
    if(!SCAN_IS_TOKEN(Token_id_obr)){ next = false; }
    else{
      if(!SCAN_IS_TOKEN(Token_id_obr)){ RULE_FAILED("Failed rule_bit_selection_bracket; couldn't find a second open bracket.\n"); }
      // optional
      rule_logical_expression();
      if(!SCAN_IS_TOKEN(Token_id_cbr)){ RULE_FAILED("Failed rule_bit_selection_bracket; couldn't find a closing bracket.\n"); }
      if(!SCAN_IS_TOKEN(Token_id_cbr)){ RULE_FAILED("Failed rule_bit_selection_bracket; couldn't find a second closing bracket.\n"); }
    }
  }
  
  RULE_SUCCESS("Matched rule_bit_selection_bracket.\n", Prp_rule_bit_selection_bracket);
}

bool Prp::rule_logical_expression(){
  INIT_FUNCTION("Called rule_logical_expression.\n");
  
  if(!rule_relational_expression()){ RULE_FAILED("Failed rule_logical_expression; couldn't find a relational_expression.\n"); }
  
  bool next = true;
  
  while(next){
    if(SCAN_IS_TOKEN(Pyrope_id_or, Prp_rule_logical_expression) || SCAN_IS_TOKEN(Pyrope_id_and, Prp_rule_logical_expression) || SCAN_IS_TOKEN(Pyrope_id_xor, Prp_rule_logical_expression)){
      if(!rule_relational_expression()){ RULE_FAILED("Failed rule_logical_expression; couldn't find an answering relational_expression.\n"); }
    }
    else{ next = false; }
  }
  
  RULE_SUCCESS("Matched rule_logical_expression.\n", Prp_rule_logical_expression);
}

bool Prp::rule_relational_expression(){
  INIT_FUNCTION("Called rule_relational_expression.\n");
  
  if(!rule_additive_expression()){ RULE_FAILED("Failed rule_relational_expression; couldn't find an additive_expression.\n"); }
  
  bool next = true;
  while(next){
    if(SCAN_IS_TOKEN(Token_id_le, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_ge, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_lt, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_gt, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_same, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_diff, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Pyrope_id_is, Prp_rule_relational_expression)){
      if(!rule_additive_expression()){ RULE_FAILED("Failed Prp_rule_relational_expression; couldn't find an answering additive_expression.\n"); }
    }
    else{ next = false; }
  }
  
  RULE_SUCCESS("Matched rule_relational_expression.\n", Prp_rule_relational_expression);
}

bool Prp::rule_additive_expression(){
  INIT_FUNCTION("Called rule_additive_expression.\n");
  
  if(!rule_bitwise_expression()){ RULE_FAILED("Failed rule_additive_expression; couldn't find a bitwise expression.\n"); }
  
  bool next = true;
  bool found_op = false;
  
  int second_phase_starting_tokens = tokens_consumed;
  
  while(next){
    if(SCAN_IS_TOKEN(Token_id_plus, Prp_rule_additive_expression)){ // can be +, ++, or +*
      // optional
      SCAN_IS_TOKEN(Token_id_plus, Prp_rule_additive_expression) || SCAN_IS_TOKEN(Token_id_mult, Prp_rule_additive_expression);
      found_op = true;
    }
    else if(SCAN_IS_TOKEN(Token_id_lt)){ // left and right shift operators
      if(SCAN_IS_TOKEN(Token_id_lt, Prp_rule_additive_expression)){
        debug_unconsume();
        debug_add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        found_op = true;
      }
      else{
        go_back(tokens_consumed - second_phase_starting_tokens);
        next = false;
      }
    }
    else if(SCAN_IS_TOKEN(Token_id_gt)){
      if(SCAN_IS_TOKEN(Token_id_gt, Prp_rule_additive_expression)){
        debug_unconsume();
        debug_add(Prp_rule_additive_expression, scan_token());
        debug_consume();
        found_op = true;
      }
      else{
        go_back(tokens_consumed - second_phase_starting_tokens);
        next = false;
      }
    }
    else if(SCAN_IS_TOKEN(Token_id_minus, Prp_rule_additive_expression) || SCAN_IS_TOKEN(Pyrope_id_intersect, Prp_rule_additive_expression) || SCAN_IS_TOKEN(Pyrope_id_union, Prp_rule_additive_expression)){ // can only be a single token
      found_op = true;
    }
    else if(rule_overload_notation()){
      found_op = true;
    }
    else{
      next = false;
    }
    if(found_op){
      if(!rule_bitwise_expression()){ RULE_FAILED("Failed rule_additive_expression; couldn't find an answering bitwise expression.\n"); }
      found_op = false;
    }
  }
  // optional
  if(SCAN_IS_TOKEN(Token_id_dot, Prp_rule_additive_expression)){
    if(!SCAN_IS_TOKEN(Token_id_dot, Prp_rule_additive_expression)){
      RULE_FAILED("Failed rule_additive_expression; couldn't find a second dot.\n");
      // optional
      rule_additive_expression();
    }
  }
  
  RULE_SUCCESS("Matched rule_additive_expression.\n", Prp_rule_additive_expression);
}

bool Prp::rule_bitwise_expression(){
  INIT_FUNCTION("Called rule_bitwise_expression.\n");
  
  if(!rule_multiplicative_expression()){ RULE_FAILED("Failed rule_bitwise_expression; couldn't find a multiplicative_expression.\n"); }
  
  bool next = true;
  while(next){
    if(SCAN_IS_TOKEN(Token_id_or, Prp_rule_bitwise_expression) || SCAN_IS_TOKEN(Token_id_and, Prp_rule_bitwise_expression) || SCAN_IS_TOKEN(Token_id_xor, Prp_rule_bitwise_expression)){
      if(!rule_multiplicative_expression()){ RULE_FAILED("Failed rule_bitwise_expression; couldn't find an answering multiplicative expression.\n"); }
    }
    else{ next = false; }
  }
  
  RULE_SUCCESS("Matched rule_bitwise_expression.\n", Prp_rule_bitwise_expression);
}

bool Prp::rule_multiplicative_expression(){
  INIT_FUNCTION("Called rule_multiplicative_expression.\n");
  
  if(!rule_unary_expression()){ RULE_FAILED("Failed rule_multiplicative_expression; couldn't find a unary_expression.\n"); }
  
  bool next = true;
  while(next){
    if(SCAN_IS_TOKEN(Token_id_mult, Prp_rule_multiplicative_expression) || SCAN_IS_TOKEN(Token_id_div, Prp_rule_multiplicative_expression)){
      if(!rule_unary_expression()){ RULE_FAILED("Failed rule_multiplicative_expression; couldn't find an answering unary expression.\n"); }
    }
    else{ next = false; }
  }
  
  RULE_SUCCESS("Matched rule_multiplicative_expression.\n", Prp_rule_multiplicative_expression);
}

bool Prp::rule_unary_expression(){
  INIT_FUNCTION("Called rule_unary_expression.\n");
  
  // option 1
  if(rule_factor()){ RULE_SUCCESS("Matched rule_unary_expression.\n", Prp_rule_unary_expression); }
  if(!(SCAN_IS_TOKEN(Token_id_bang, Prp_rule_unary_expression) || SCAN_IS_TOKEN(Pyrope_id_tilde, Prp_rule_unary_expression))){ RULE_FAILED("Failed rule_unary_expression; couldn't find a factor or a unary operator.\n"); }
  if(!rule_factor()){ RULE_FAILED("Failed rule_unary_expression; couldn't find an answering factor.\n"); }
  
  RULE_SUCCESS("Matched rule_unary_expression.\n", Prp_rule_unary_expression);
}

bool Prp::rule_factor(){
  INIT_FUNCTION("Called rule_factor.\n");
  
  // option 1
  if(rule_rhs_expression()){ RULE_SUCCESS("Matched rule_factor; option 1.\n", Prp_rule_factor); }
  
  // option 2
  else if(SCAN_IS_TOKEN(Token_id_op, Prp_rule_factor)){
    if(!rule_logical_expression()){ RULE_FAILED("Failed rule_factor; couldn't find a logical_expression.\n"); }
    if(!SCAN_IS_TOKEN(Token_id_cp, Prp_rule_factor)){ RULE_FAILED("Failed rule_factor; couldn't find a closing parenthesis.\n"); }
    
    // optional
    rule_bit_selection_bracket();
  }
  
  RULE_SUCCESS("Matched rule_factor; option 2.\n", Prp_rule_factor);
}

bool Prp::rule_overload_notation(){
  INIT_FUNCTION("Called rule_overload_notation.\n");
  
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a starting dot.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a second dot.\n"); }
  if(!rule_overload_name()){ RULE_FAILED("Failed rule_overload_notation; couldn't find an overload name.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a first trailing dot.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a second trailing dot.\n"); }
  
  RULE_SUCCESS("Matched rule_overload_notation.\n", Prp_rule_overload_notation);
}

bool Prp::rule_overload_name(){
  INIT_FUNCTION("Called rule_overload_name.\n");
  
  bool next = rule_overload_exception();
  if(!next){ RULE_FAILED("Failed rule_overload_notation; found an overload_exception.\n"); }
  while(next)
    next = rule_overload_exception();
  
  RULE_SUCCESS("Matched rule_overload_name.\n", Prp_rule_overload_name);
}

bool Prp::rule_overload_exception(){
  INIT_FUNCTION("Called rule_overload_exception.\n");
  
  if(SCAN_IS_TOKEN(Token_id_dot) || SCAN_IS_TOKEN(Token_id_pound) || SCAN_IS_TOKEN(Token_id_semicolon) || SCAN_IS_TOKEN(Token_id_comma) || SCAN_IS_TOKEN(Token_id_eq) || SCAN_IS_TOKEN(Token_id_op)  || SCAN_IS_TOKEN(Token_id_cp)  || SCAN_IS_TOKEN(Token_id_obr) || SCAN_IS_TOKEN(Token_id_cbr) || SCAN_IS_TOKEN(Token_id_ob) || SCAN_IS_TOKEN(Token_id_cb) || SCAN_IS_TOKEN(Token_id_backslash) || SCAN_IS_TOKEN(Token_id_qmark) || SCAN_IS_TOKEN(Token_id_bang) || SCAN_IS_TOKEN(Token_id_or) || SCAN_IS_TOKEN(Token_id_tick)){ RULE_SUCCESS("Matched rule_overload_exception.\n", Prp_rule_overload_exception); }
  
  RULE_FAILED("Failed rule_overload_exception; couldn't find an excepting character.\n");
}

bool Prp::rule_rhs_expression(){
  INIT_FUNCTION("Called rule_rhs_expression.\n");
  
  if(!(rule_fcall_explicit() || rule_lhs_expression() || rule_scope_declaration())){ RULE_FAILED("Failed rule_rhs_expression; couldn't find an expression.\n"); }
  
  RULE_SUCCESS("Matched rule_rhs_expression.\n", Prp_rule_rhs_expression);
}

void Prp::elaborate(){
  patch_pass(pyrope_keyword);
  
  fmt::print("PART 1: RULE AND AST CALL TRACE \n\n");
  
  ast = std::make_unique<Ast_parser>(get_buffer(), Prp_rule);
  
  int failed = 0;
  
  while(!scan_is_end()){
    ////dump_token();
    eat_comments();
    if(!rule_start()){
      failed = 1;
      break;
    }
  }
  
  if(failed){
    fmt::print("\nParsing FAILED!\n");
  }
  else{
    fmt::print("\nParsing SUCCESSFUL!\n");
  }
  
  fmt::print("\nPART 2: SUBTREE STACK\n\n");
  for(auto it = subtree_stack.begin(); it != subtree_stack.end(); ++it){
    fmt::print("Operation: {}, Rule: {}, Token: {}\n", std::get<1>(*it), rule_id_to_string(std::get<2>(*it)), scan_text(std::get<3>(*it)));
  }
  
  fmt::print("\nPART 3: AST BUILD LOG\n\n");
  
  // build the ast
  ast_builder();
  
  fmt::print("\nPART 4: AST PREORDER TRAVERSAL\n\n");
  
  // next, write the AST traversal
  ast_handler();
  
  fmt::print("\nPART 5: STATISTICS\n\n");
  
  // finally, write the statistics
  fmt::print(fmt::format("Number of rules called: {}\n", debug_stat.rules_called));
  fmt::print(fmt::format("Number of rules matched: {}\n", debug_stat.rules_matched));
  fmt::print(fmt::format("Number of rules failed: {}\n", debug_stat.rules_failed));
  fmt::print(fmt::format("Number of tokens consumed: {}\n", debug_stat.tokens_consumed));
  fmt::print(fmt::format("Number of tokens unconsumed: {}\n", debug_stat.tokens_unconsumed));
  fmt::print(fmt::format("Number of ast->up() calls: {}\n", debug_stat.ast_up_calls));
  fmt::print(fmt::format("Number of ast->down() calls: {}\n", debug_stat.ast_down_calls));
  fmt::print(fmt::format("Number of debug_add() calls: {}\n", debug_stat.ast_add_calls));
  
  ast = nullptr;
}

/* Consumes a token and dumps the new one */
bool Prp::debug_consume(){
  //fmt::print("Consuming token: ");
  debug_stat.tokens_consumed++;
  tokens_consumed++;
  ////dump_token();
  bool ok = scan_next();
  return ok;
}

/* Unconsumes a token and dumps it */
bool Prp::debug_unconsume(){
  //fmt::print("Unconsuming token: ");
  debug_stat.tokens_unconsumed++;
  tokens_consumed--;
  bool ok = scan_prev();
  ////dump_token();
  return ok;
}

bool Prp::go_back(uint64_t num_tok){
  uint64_t i;
  bool ok;
  //fmt::print("Tokens consumed: {}\n", tokens_consumed);
  for(i=0;i<num_tok;i++){
    ok = debug_unconsume();
  }
  return ok;
}

void Prp::debug_down(){
  debug_stat.ast_down_calls++;
  //fmt::print("Added down call.\n");
  ast_call_trace.push_back(fmt::format("Added down call.\n"));
  down_stack.push_back(std::make_tuple(subtree_index++, 0, 0, 0));
}

void Prp::debug_up(Rule_id rid){
  debug_stat.ast_up_calls++;
  //fmt::print("Processing up call with rule {}.\n", rule_id_to_string(rid));
  
  if(add_stack.empty() && subtree_stack.empty()){
    down_stack.pop_back();
  }
  else{
    // we can define a good node recursively as follows:
    // base case: a good node has two leaf nodes as its children
    // base case: a leaf node is a good node
    // recursive cases: a good node has more than one good node and no leaf nodes as its children
    // or a good node has at least one leaf node and at least one good node as its children
    
    // a bad node is defined recursively as follows
    // base case: a bad node has one leaf node as its child and no others
    // recursive case: a bad node has only one bad node as its child and no others
    
    // remember, each up call adds a new node to the tree, if it was built correctly.
    
    // always keep these nodes:
    
    uint32_t add_cnt = 0;
    uint32_t sub_cnt = 0;
    
    if(!add_stack.empty()){
      while(std::get<0>(add_stack[add_stack.size()-1 - add_cnt]) > std::get<0>(down_stack.back())){
        add_cnt++;
        if(add_cnt >= add_stack.size()){
          break;
        }
      }
    }
    
    if(!subtree_indices.empty()){
      while(std::get<0>(subtree_stack[std::get<1>(subtree_indices[subtree_indices.size()-1-sub_cnt])]) > std::get<0>(down_stack.back())){
        sub_cnt++;
        if(sub_cnt >= subtree_indices.size())
          break;
      }
    }
    
    // first base case for good node: has 2 leaf nodes
    if(add_cnt > 1 && sub_cnt == 0){
      //fmt::print("Merging add calls only together for up call with rule {}.\n", rule_id_to_string(rid));
      // push all the operations for the subtree onto the subtree stack.
      uint32_t start_index = subtree_stack.size();
      subtree_stack.push_back(down_stack.back());
      down_stack.pop_back();
      std::tuple<uint32_t, uint8_t, Rule_id, Token_entry> temp_add_array[add_cnt];
//       //fmt::print("PRINTING THE ADD STACK.\n");
//       for(int k = 0; k < add_stack.size(); k++){
//         //fmt::print("Add stack index {}; rule: {} token: {}\n", k, std::get<2>(add_stack[k]), std::get<3>(add_stack[k]));
//       }
      for(uint32_t j = 0; j<add_cnt; j++){
        //fmt::print("Base case: saw add call for a {} token.\n", scan_text(std::get<3>(add_stack.back())));
        temp_add_array[j] = add_stack.back();
        add_stack.pop_back();
      }
      for(uint32_t j = 0; j<add_cnt; j++){
        subtree_stack.push_back(temp_add_array[(add_cnt-1)-j]);
      }
      //fmt::print("Pushing an up call with rule {} to the subtree stack.\n", rule_id_to_string(rid));
      subtree_stack.push_back(std::make_tuple(subtree_index++, 1, rid, 0));
      subtree_indices.push_back(std::make_tuple(start_index, subtree_stack.size()-1));
    }
    else if((add_cnt + sub_cnt) > 1){
      //fmt::print("Merging subtrees and add calls together for up call with rule {}.\n", rule_id_to_string(rid));
      uint32_t subtree_start = std::get<0>(subtree_indices[subtree_indices.size() - sub_cnt]);
      
      while((add_cnt + sub_cnt) > 0){
        // if the subtrees have already been placed at the top, just put the tokens in the front
        //fmt::print("add_cnt: {}, add_stack size: {}\n", add_cnt, add_stack.size());
        if(sub_cnt == 0){
          //fmt::print("Sub_cnt reached zero; placing an add call with token {} at the beginning of the subtree stack.\n", scan_text(std::get<3>(add_stack.back())));
          subtree_stack.emplace(subtree_stack.begin() + subtree_start, add_stack.back());
          //fmt::print("1 add_cnt: {}, add_stack size: {}\n", add_cnt, add_stack.size());
          add_stack.pop_back();
          add_cnt--;
          //fmt::print("2 add_cnt: {}, add_stack size: {}\n", add_cnt, add_stack.size());
        }
        else{
          // if the add calls came later, put them on first
          if(add_cnt == 0){
            //fmt::print("Subtree ending with call {} and rule {} at index {} comes after add call or is the last thing.\n", std::get<1>(subtree_stack[std::get<1>(subtree_indices.back())]), rule_id_to_string(std::get<2>(subtree_stack[std::get<1>(subtree_indices.back())])), std::get<1>(subtree_indices.back()));
            subtree_indices.pop_back();
            sub_cnt--;
          }
          else{
            if(std::get<0>(add_stack.back()) > std::get<0>(subtree_stack[std::get<1>(subtree_indices.back())])){
              //fmt::print("Placing an add call with token {} at index {} in the subtree stack; it had op {} at that position before.\n", scan_text(std::get<3>(add_stack.back())), std::get<1>(subtree_indices.back())+1, std::get<1>(subtree_stack[std::get<1>(subtree_indices.back())]));
              subtree_stack.emplace(subtree_stack.begin() + (std::get<1>(subtree_indices.back())+1), add_stack.back());
              add_stack.pop_back();
              add_cnt--;
              //fmt::print("add_cnt: {}, add_stack size: {}\n", add_cnt, add_stack.size());
            }
            // otherwise, just leave the subtree where it is; it is being merged into a single new subtree
            else{
              //fmt::print("Subtree ending with call {} and rule {} at index {} comes after add call with token {}.\n", std::get<1>(subtree_stack[std::get<1>(subtree_indices.back())]), rule_id_to_string(std::get<2>(subtree_stack[std::get<1>(subtree_indices.back())])), std::get<1>(subtree_indices.back()), scan_text(std::get<3>(add_stack.back())));
              subtree_indices.pop_back();
              sub_cnt--;
            }
          }
        }
      }
      //fmt::print("Down stack size should not be zero; down stack size: {}; subtree_start: {}\n", down_stack.size(), subtree_start);
      subtree_stack.emplace(subtree_stack.begin() + subtree_start, down_stack.back());
      down_stack.pop_back();
      //fmt::print("Pushing an up call with rule {} to the subtree stack.\n", rule_id_to_string(rid));
      subtree_stack.push_back(std::make_tuple(subtree_index++, 1, rid, 0));
      subtree_indices.push_back(std::make_tuple(subtree_start, subtree_stack.size()-1));
    }
    // we have a bad node, just pop the down call and move on.
    else{
      //fmt::print("Couldn't find 2 good children for up call to {}. Popping matching down call.\n", rule_id_to_string(rid));
      //subtree_stack[std::get<1>(subtree_indices.back())]
      down_stack.pop_back();
    }
    // print the entire subtree stack for debugging
//           //fmt::print("PRINTING THE SUBTREE STACK\n");
//           for(uint32_t j = 0; j<subtree_stack.size(); j++){
//             //fmt::print("Operation: {} Rule: {} Token: {}\n", std::get<0>(std::get<1>(subtree_stack[j])), rule_id_to_string(std::get<1>(std::get<1>(subtree_stack[j]))), scan_text(std::get<2>(std::get<1>(subtree_stack[j]))));
//           }
//           //fmt::print("PRINTING THE SUBTREE INDICES STACK.\n");
//           for(uint32_t j = 0; j<subtree_indices.size(); j++){
//             //fmt::print("Subtree {} indices: ({},{})\n", j, std::get<0>(subtree_indices[j]), std::get<1>(subtree_indices[j]));
//           }
  }
}

void Prp::debug_add(Rule_id rid, Token_entry token){
  fmt::print("Added add call with token {} from rule {}.\n", scan_text(), rule_id_to_string(rid));
  ast_call_trace.push_back(fmt::format("Added add call with token {} from rule {}.\n", scan_text(), rule_id_to_string(rid)));
  debug_stat.ast_add_calls++;
  add_stack.push_back(std::make_tuple(subtree_index++, 2, rid, token));
}

bool Prp::chk_and_consume(Token_id tok, Rule_id rid){
  fmt::print("called chk_and_consume: trying to match token {} to token {}\n", scan_is_end() ? "no token" : scan_text(), tok_id_to_string(tok));
  if(scan_is_token(tok)){
    fmt::print("chk_and_consume: matched a token.\n");
    if(rid != Prp_invalid){
      fmt::print("chk_and_consume: added a token {} to the ast.\n", rule_id_to_string(rid));
      debug_add(rid, scan_token());
    }
    debug_consume();
    return true;
  }
  return false;
}

void Prp::ast_handler(){
  std::string rule_name;
  for(const auto &it:ast->depth_preorder(ast->get_root())) {
    auto node = ast->get_data(it);
    rule_name = rule_id_to_string(node.rule_id);
    auto token_text = scan_text(node.token_entry);
    fmt::print("Rule name: {}, Token text: {}, Tree level: {}\n", rule_name, token_text, it.level);
  }
}

void Prp::ast_builder(){
  for(auto it = subtree_stack.begin(); it != subtree_stack.end(); ++it){
    auto ast_op = *it;
    switch(std::get<1>(ast_op)){
      case 0:
        ast->down();
        fmt::print("Went down; now at level {}.\n", ast->get_level());
        break;
      case 1:
        ast->up(std::get<2>(ast_op));
        fmt::print("Went up with rule {}, now at level {}.\n", rule_id_to_string(std::get<2>(ast_op)), ast->get_level());
        break;
      case 2:
        ast->down();
        ast->add(std::get<2>(ast_op), std::get<3>(ast_op));
        fmt::print("Added token {} from rule {} at level {}\n", scan_text(std::get<3>(ast_op)), rule_id_to_string(std::get<2>(ast_op)), ast->get_level());
        ast->up(std::get<2>(ast_op));
        break;
    }
  }
}

std::string Prp::rule_id_to_string(Rule_id rid){
  switch(rid){
    case Prp_invalid:
      return "Invalid";
    case Prp_rule:
      return "Program";
    case Prp_rule_start:
      return "Top level";
    case Prp_rule_code_blocks:
      return "Code blocks";
    case Prp_rule_code_block_int:
      return "Code block int";
    case Prp_rule_assignment_expression:
      return "Assignment expression";
    case Prp_rule_logical_expression:
      return "Logical expression";
    case Prp_rule_relational_expression:
      return "Relational expression";
    case Prp_rule_additive_expression:
      return "Additive expression";
    case Prp_rule_bitwise_expression:
      return "Bitwise expression";
    case Prp_rule_multiplicative_expression:
      return "Multiplicative expression";
    case Prp_rule_unary_expression:
      return "Unary expression";
    case Prp_rule_factor:
      return "Factor";
    case Prp_rule_tuple_by_notation:
      return "Tuple by notation";
    case Prp_rule_tuple_notation_no_bracket:
      return "Tuple notation non bracket";
    case Prp_rule_tuple_notation:
      return "Tuple notation";
    case Prp_rule_tuple_notation_with_object:
      return "Tuple notation with object";
    case Prp_rule_range_notation:
      return "Range notation";
    case Prp_rule_bit_selection_bracket:
      return "Bit selection bracket";
    case Prp_rule_bit_selection_notation:
      return "Bit selection notation";
    case Prp_rule_tuple_array_bracket:
      return "Tuple array bracket";
    case Prp_rule_tuple_array_notation:
      return "Tuple array notation";
    case Prp_rule_lhs_expression:
      return "LHS expression";
    case Prp_rule_lhs_var_name:
      return "LHS variable name";
    case Prp_rule_rhs_expression_property:
      return "RHS expression property";
    case Prp_rule_rhs_expression:
      return "RHS expression";
    case Prp_rule_identifier:
      return "Identifier";
    case Prp_rule_constant:
      return "Constant";
    case Prp_rule_assignment_operator:
      return "Assignment operator";
    case Prp_rule_tuple_dot_notation:
      return "Tuple dot notation";
    case Prp_rule_tuple_dot_dot:
      return "Tuple dot dot";
    case Prp_rule_numerical_constant:
      return "Numerical constant";
    case Prp_rule_string_constant:
      return "String constant";
    case Prp_rule_if_statement:
      return "If statement";
    case Prp_rule_block_body:
      return "Block body";
    case Prp_rule_empty_scope_colon:
      return "Empty scope colon";
    case Prp_rule_else_statement:
      return "Else statement";
    case Prp_rule_while_statement:
      return "While statement";
    case Prp_rule_try_statement:
      return "Try statement";
    case Prp_rule_scope_else:
      return "Scope else";
    case Prp_rule_assertion_statement:
      return "Assertion statement";
    default: return fmt::format("{}", rid);
  }
}
  
std::string Prp::tok_id_to_string(Token_id tok){
  switch(tok){
    case Token_id_nop:           // invalid token
      return "nop";
    case Token_id_comment:       // c-like comments
      return "comment";
    case Token_id_register:      // @asd @_asd
      return "register";
    case Token_id_pipe:          // |>
      return "|>";
    case Token_id_alnum:         // a..z..0..9.._
      return "alnum";
    case Token_id_ob:            // {
      return "{";
    case Token_id_cb:            // }
      return "}";
    case Token_id_colon:         // :
      return ":";
    case Token_id_gt:            // >
      return ">";
    case Token_id_or:            // |
      return "|";
    case Token_id_at:            // @
      return "@";
    case Token_id_label:         // foo:
      return "label";
    case Token_id_output:        // %outasd
      return "output";
    case Token_id_input:         // $asd
      return "input";
    case Token_id_dollar:        // $
      return "$";
    case Token_id_percent:       // %
      return "%";
    case Token_id_dot:           // .
      return ".";
    case Token_id_div:           // /
      return "/";
    case Token_id_string:        // "asd23*.v" string (double quote)
      return "double string";
    case Token_id_semicolon:     // ;
      return ";";
    case Token_id_comma:         // ,
      return ",";
    case Token_id_op:            // (
      return "(";
    case Token_id_cp:            // )
      return ")";
    case Token_id_pound:         // #
      return "#";
    case Token_id_mult:          // *
      return "*";
    case Token_id_num:           // 0123123 or 123123 or 0123ubits
      return "number";
    case Token_id_backtick:      // `ifdef
      return "backtick";
    case Token_id_synopsys:      // synopsys... directive in comment
      return "synopsys";
    case Token_id_plus:          // +
      return "+";
    case Token_id_minus:         // -
      return "_";
    case Token_id_bang:          // !
      return "!";
    case Token_id_lt:            // <
      return "<";
    case Token_id_eq:            // =
      return "=";
    case Token_id_same:          // ==
      return "==";
    case Token_id_diff:          // !=
      return "!=";
    case Token_id_coloneq:       // :=
      return ":=";
    case Token_id_ge:            // >=
      return ">=";
    case Token_id_le:            // <=
      return "<=";
    case Token_id_and:           // &
      return "&";
    case Token_id_xor:           // ^
      return "^";
    case Token_id_qmark:         // ?
      return "?";
    case Token_id_tick:          // '
      return "'";
    case Token_id_obr:           // [
      return "[";
    case Token_id_cbr:           // ]
      return "]";
    case Token_id_backslash:     // \ back slash
      return "\\";
    case Token_id_reference:     // \foo
      return "reference";
    case Token_id_keyword_first:
      return "first";
    case Token_id_keyword_last:
      return "last";
    case Pyrope_id_elif:
      return "elif";
    default: return fmt::format("{}", tok);
  }
}
