#include <ctype.h>
#include <algorithm>
#include <iostream>

#include "fmt/format.h"
#include "prp.hpp"

void Prp::eat_comments(){
    while (scan_is_token(Token_id_comment) && !scan_is_end()) scan_next();
}

uint8_t Prp::rule_start(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_start.");

  if(!CHECK_RULE(&Prp::rule_code_blocks)){
    RULE_FAILED("Failed rule_start.\n");
  }
  if(!scan_is_end()){
    RULE_FAILED("Failed rule_start; still input left.\n");
  }
  RULE_SUCCESS("Matched rule_start.\n", Prp_rule_start);
}

uint8_t Prp::rule_code_blocks(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_code_blocks.");

  if(!CHECK_RULE(&Prp::rule_code_block_int)){
    RULE_FAILED("Failed rule_code_blocks; no first rule.\n");
  }

  bool next = true;
  while(next){
    if(!check_eos()){
      next = false;
    }
    else{
      if(!CHECK_RULE(&Prp::rule_code_block_int))
        next = false;
    }
  }

  RULE_SUCCESS("Matched rule_code_blocks.\n", Prp_rule_code_blocks);
}

uint8_t Prp::rule_code_block_int(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_code_block_int.");

  check_lb();

  if (CHECK_RULE(&Prp::rule_if_statement)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_for_statement)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_while_statement)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_try_statement)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_punch_format)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_assignment_expression)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_fcall_implicit)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_fcall_explicit)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_return_statement)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_compile_check_statement)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_negation_statement)) { RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }
  else if (CHECK_RULE(&Prp::rule_assertion_statement)){ RULE_SUCCESS("Matched rule_code_block_int.\n", Prp_rule_code_block_int); }

  RULE_FAILED("Failed rule_code_block_int.\n");
}

uint8_t Prp::rule_if_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_if_statement.");

  if(!(SCAN_IS_TOKEN(Pyrope_id_if, Prp_rule_if_statement) || SCAN_IS_TOKEN(Pyrope_id_unique, Prp_rule_if_statement))){
    if(!SCAN_IS_TOKEN(Token_id_ob, Prp_rule_if_statement)){
      RULE_FAILED("Failed rule_if_statement; couldn't find a starting open brace (option 2).\n");
    }
    if(!CHECK_RULE(&Prp::rule_block_body)){
      RULE_FAILED("Failed rule_if_statement; couldn't find an answering block_body (option 2).\n");
    }
    RULE_SUCCESS("Matched rule_if_statement (no condition or if).\n", Prp_rule_if_statement);
  }

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(!CHECK_RULE(&Prp::rule_assignment_expression)){
      next = false;
    }
    else{
      bool next_prime = false;
      do{
        if(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_if_statement)){ next_prime = true; }
      } while(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_if_statement));
      if(!next_prime){
        next = false;
        PSEUDO_FAIL();
      }
    }
  }

  if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_if_statement; couldn't find a logical_expression.\n"); }
  if(!CHECK_RULE(&Prp::rule_empty_scope_colon)){ RULE_FAILED("Failed rule_if_statement; couldn't find an empty_scope_colon.\n"); }
  if(!CHECK_RULE(&Prp::rule_block_body)){ RULE_FAILED("Failed rule_if_statement; couldn't find a block_body.\n"); }

  UPDATE_PSEUDO_FAIL();

  // optional
  check_lb();
  if(!CHECK_RULE(&Prp::rule_else_statement)){
    PSEUDO_FAIL();
  }

  RULE_SUCCESS("Matched rule_if_statement (with condition).\n", Prp_rule_if_statement);
}

uint8_t Prp::rule_for_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_for_statement.");

  if(!SCAN_IS_TOKEN(Pyrope_id_for)){ RULE_FAILED("Failed rule_for_statement; couldn't find a for token.\n"); }

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(!CHECK_RULE(&Prp::rule_assignment_expression)){
      next = false;
    }
    else{
    bool next_prime = false;
      do{
        if(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_for_index)){ next_prime = true; }
      } while(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_for_index));
      if(!next_prime){
        next = false;
        PSEUDO_FAIL();
      }
    }
  }

  if(!CHECK_RULE(&Prp::rule_for_index)){ RULE_FAILED("Failed rule_for_statement; couldn't find a for_index.\n"); }
  if(!CHECK_RULE(&Prp::rule_empty_scope_colon)){ RULE_FAILED("Failed rule_for_statement; couldn't find an empty_scope_colon.\n"); }
  if(!CHECK_RULE(&Prp::rule_block_body)){ RULE_FAILED("Failed rule_for_statement; couldn't find a block_body.\n"); }

  RULE_SUCCESS("Matched rule_for_statement.\n", Prp_rule_for_statement);
}

uint8_t Prp::rule_for_in_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_in_notation.");

  if(!CHECK_RULE(&Prp::rule_identifier)){ RULE_FAILED("Failed rule_for_in_notation; couldn't find an identifier.\n"); }
  if(!SCAN_IS_TOKEN(Pyrope_id_in, Prp_rule_for_in_notation)){ RULE_FAILED("Failed rule_for_in_notation; couldn't find an in token.\n"); }
  if(!(CHECK_RULE(&Prp::rule_fcall_explicit) || CHECK_RULE(&Prp::rule_tuple_notation))){ RULE_FAILED("Failed rule_for_in_notation; couldn't find either an fcall_explicit or a tuple_notation.\n"); }

  RULE_SUCCESS("Matched rule_in_notation.\n", Prp_rule_for_in_notation);
}

uint8_t Prp::rule_for_index(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_for_index.");

  if(!CHECK_RULE(&Prp::rule_for_in_notation)){ RULE_FAILED("Failed rule_for_index; couldn't find a for_in_notation.\n"); }

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    bool next_prime = false;
    do{
      if(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_for_index)){ next_prime = true; }
    } while(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_for_index));
    if(next_prime){
      if(!CHECK_RULE(&Prp::rule_for_in_notation)){
        next = false;
        PSEUDO_FAIL();
      }
    }
    else{
      next = false;
    }
  }
  /*
  if(!CHECK_RULE(&Prp::rule_rhs_expression_property)){ RULE_FAILED("Failed rule_for_index; couldn't find an rhs_expression_property.\n"); }

  next = true;
  while(next){
    if(!CHECK_RULE(&Prp::rule_rhs_expression_property)){
      next = false;
    }
  }
  */
  RULE_SUCCESS("Matched rule_for_index.\n", Prp_rule_for_index);
}

uint8_t Prp::rule_else_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_else_statement.");

  // option 1
  if(SCAN_IS_TOKEN(Pyrope_id_elif, Prp_rule_else_statement)){
    if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_else_statement (ELIF path); couldn't find a condition for the elif.\n"); }
    if(!CHECK_RULE(&Prp::rule_empty_scope_colon)){ RULE_FAILED("Failed rule_else_statement; couldn't find an empty_scope_colon.\n"); }
    if(!CHECK_RULE(&Prp::rule_block_body)){ RULE_FAILED("Failed rule_else_statement; couldn't find a block_body.\n"); }

    auto lines_start = cur_line;
    auto tokens_start = tokens_consumed;

    // optional
    check_lb();
    if(!CHECK_RULE(&Prp::rule_else_statement)){
      cur_line = lines_start;
      go_back(tokens_consumed - tokens_start);
    }

    RULE_SUCCESS("Matched rule_else_statement (ELIF path).\n", Prp_rule_else_statement);
  }
  // option 2
  else if(!SCAN_IS_TOKEN(Pyrope_id_else, Prp_rule_else_statement)){ RULE_FAILED("Failed rule_else_statement; couldn't find either an else or an elif.\n"); }
  if(!CHECK_RULE(&Prp::rule_empty_scope_colon)){ RULE_FAILED("Failed rule_else_statement (ELSE path); couldn't find an empty_scope_colon.\n"); }
  if(!CHECK_RULE(&Prp::rule_block_body)){ RULE_FAILED("Failed rule_else_statement; couldn't find a block_body.\n"); }

  RULE_SUCCESS("Matched rule_else_statement.\n", Prp_rule_else_statement);
}

uint8_t Prp::rule_while_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_while_statement.");

  if(!SCAN_IS_TOKEN(Pyrope_id_while)){ RULE_FAILED("Failed rule_while_statement; couldn't find a while.\n"); }

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(!CHECK_RULE(&Prp::rule_assignment_expression)){
      next = false;
    }
    else{
      bool next_prime = false;
      do{
        if(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_for_index)){ next_prime = true; }
      } while(SCAN_IS_TOKEN(Token_id_semicolon, Prp_rule_for_index));
      if(!next_prime){
        next = false;
        PSEUDO_FAIL();
      }
    }
  }

  if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_while_statement; couldn't find a logical_expression.\n"); }
  if(!CHECK_RULE(&Prp::rule_empty_scope_colon)){ RULE_FAILED("Failed rule_while_statement; couldn't find an empty_scope_colon.\n"); }
  if(!CHECK_RULE(&Prp::rule_block_body)){ RULE_FAILED("Failed rule_while_statement; couldn't find a block_body.\n"); }

  RULE_SUCCESS("Failed rule_while_statement; couldn't find a while token.\n", Prp_rule_while_statement);
}

uint8_t Prp::rule_try_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_try_statement.");

  if(!SCAN_IS_TOKEN(Pyrope_id_try, Prp_rule_try_statement)){ RULE_FAILED("Failed rule_try_statement; couldn't find a try token.\n"); }
  if(!CHECK_RULE(&Prp::rule_empty_scope_colon)){ RULE_FAILED("Failed rule_try_statement; couldn't find an empty_scope_colon\n"); }
  if(!CHECK_RULE(&Prp::rule_block_body)){ RULE_FAILED("Failed rule_try_statement; couldn't find a block_body.\n"); }

  auto lines_start = cur_line;
  auto tokens_start = tokens_consumed;

  // optional
  check_lb();
  if(!CHECK_RULE(&Prp::rule_scope_else)){
    cur_line = lines_start;
    go_back(tokens_consumed - tokens_start);
  }

  RULE_SUCCESS("Matched rule_try_statement.\n", Prp_rule_try_statement);
}


// TODO: check correctness of scanner with ASSERTION token ("I")
uint8_t Prp::rule_assertion_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_assertion_statement.");

  if(!SCAN_IS_TOKEN(Pyrope_id_assertion, Prp_rule_assertion_statement)){ RULE_FAILED("Failed rule_assertion_statement; couldn't find an assertion token.\n"); }
  else{
    if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_assertion_statement; couldn't find a logical_expression.\n"); }
  }

  RULE_SUCCESS("Matched rule_assertion_statement.\n", Prp_rule_assertion_statement);
}

// TODO: check correctness of scanner with NEGATION token ("N")
uint8_t Prp::rule_negation_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_negation_statement.");

  if(!SCAN_IS_TOKEN(Pyrope_id_negation)){ RULE_FAILED("Failed rule_negation_statement; couldn't find a negation token.\n"); }
  else{
    if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_negation_statement; couldn't find a logical_expression.\n"); }
  }

  RULE_SUCCESS("Matched rule_logical_expression.\n", Prp_rule_negation_statement);
}

uint8_t Prp::rule_empty_scope_colon(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_empty_scope_colon.");

  // optional
  if(SCAN_IS_TOKEN(Token_id_colon, Prp_rule_empty_scope_colon)){
    if(!SCAN_IS_TOKEN(Token_id_colon), Prp_rule_empty_scope_colon){ RULE_FAILED("Failed rule_empty_scope_colon; couldn't find a second colon.\n"); }
  }

  if(!SCAN_IS_TOKEN(Token_id_ob, Prp_rule_empty_scope_colon)){ RULE_FAILED("Failed rule_empty_scope_colon; couldn't find an opening brace.\n"); }
  check_eos();
  check_lb();

  RULE_SUCCESS("Matched rule_empty_scope_colon.\n", Prp_rule_empty_scope_colon);
}

uint8_t Prp::rule_scope_else(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_scope_else.");

  if(!SCAN_IS_TOKEN(Pyrope_id_else, Prp_rule_scope_else)){ RULE_FAILED("Failed rule_scope_else; couldn't find an else token.\n"); }

  if(!SCAN_IS_TOKEN(Token_id_ob, Prp_rule_scope_else)){ RULE_FAILED("Failed rule_scope_else; couldn't find an open brace.\n"); }

  check_eos();
  check_lb();

  if(!CHECK_RULE(&Prp::rule_scope_body)){ RULE_FAILED("Failed rule_scope_else; couldn't find a scope_body.\n"); }

  RULE_SUCCESS("Matched rule_scope_else.\n", Prp_rule_scope_else);
}

// WARNING: this rule was modified to look more like block_body
uint8_t Prp::rule_scope_body(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_scope_body.");

  // option 1 and 2 - logical expression inside only or just a closing brace
  if(!CHECK_RULE(&Prp::rule_code_blocks)){
    if(!CHECK_RULE(&Prp::rule_logical_expression)){
      check_eos();
      check_lb();
      if(!SCAN_IS_TOKEN(Token_id_cb, Prp_rule_scope_body)){ RULE_FAILED("Failed rule_scope_body (option 1); couldn't find a closing brace.\n"); }
    }
    check_lb();
    check_eos();
    check_lb();
    if(!SCAN_IS_TOKEN(Token_id_cb, Prp_rule_scope_body)){ RULE_FAILED("Failed rule_scope_body (option 2), couldn't find a closing bracket.\n"); }
    RULE_SUCCESS("Matched rule_scope_body (option 2).\n", Prp_rule_scope_body);
  }


  // option 3 - code blocks plus optional logical expression
  if(check_eos()){
    if(CHECK_RULE(&Prp::rule_logical_expression)){
      check_lb();
    }
  }

  check_eos();
  check_lb();
  if(!SCAN_IS_TOKEN(Token_id_cb, Prp_rule_scope_body)){ RULE_FAILED("Failed rule_scope_body (option 3); couldn't find a closing bracket."); }

  RULE_SUCCESS("Matched rule_scope_body.\n", Prp_rule_scope_body);
}

uint8_t Prp::rule_scope(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_scope.");

  if(SCAN_IS_TOKEN(Token_id_colon, Prp_rule_scope)){
    // optional
    CHECK_RULE(&Prp::rule_scope_condition);
    PRINT_DBG_AST("{}\n", scan_text());
    PRINT_DBG_AST("cur_line = {}\n", cur_line);
    if(SCAN_IS_TOKEN(Token_id_colon, Prp_rule_scope)){
      
      // optional
      //CHECK_RULE(&Prp::rule_logical_expression);
      RULE_SUCCESS("Matched rule_scope.\n", Prp_rule_scope);
    }
  }

  RULE_FAILED("Failed rule_scope; generic.\n");
}

uint8_t Prp::rule_scope_condition(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_scope_condition.");

  // optional
  CHECK_RULE(&Prp::rule_scope_argument);

  if(SCAN_IS_TOKEN(Pyrope_id_when, Prp_rule_scope_condition)){
    if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_scope_condition; couldn't find an answering logical expression.\n"); }
  }

  RULE_SUCCESS("Matched rule_scope_condition.\n", Prp_rule_scope_condition);
}

uint8_t Prp::rule_scope_colon(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_scope_colon.");

  // option 1
  if(!CHECK_RULE(&Prp::rule_scope)){
    if(!SCAN_IS_TOKEN(Token_id_ob)){ RULE_FAILED("Failed rule_scope_colon; couldn't find an opening brace or a scope.\n"); }
    if(!check_eos()){
      check_lb();
    }
    RULE_SUCCESS("Matched rule_scope_colon.\n", Prp_rule_scope_condition);
  }

  // option 2
  if(!SCAN_IS_TOKEN(Token_id_ob)){ RULE_FAILED("Failed rule_scope_colon; couldn't find an answering opening brace.\n"); }
  if(!check_eos()){
    check_lb();
  }

  RULE_SUCCESS("Matched rule_scope_colon.\n", Prp_rule_scope_colon);
}

uint8_t Prp::rule_scope_argument(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_scope_argument.");
  if(!CHECK_RULE(&Prp::rule_fcall_arg_notation)){
    RULE_FAILED("Failed rule_scope_argument; couldn't find an fcall_arg_notation.\n");
  }

  RULE_SUCCESS("Matched rule_scope_argument.\n", Prp_rule_scope_argument);
}

uint8_t Prp::rule_punch_format(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_punch_format.");

  if(!SCAN_IS_TOKEN(Pyrope_id_punch)){ RULE_FAILED("Failed rule_punch_format; couldn't find a punch token.\n"); }
  if(!CHECK_RULE(&Prp::rule_identifier)){ RULE_FAILED("Failed rule_punch_format; couldn't find an identifier.\n"); }

  if(!SCAN_IS_TOKEN(Token_id_at) || SCAN_IS_TOKEN(Token_id_percent)){ RULE_FAILED("Failed rule_punch_format; couldn't find a percent or an at symbol.\n"); }
  if(!CHECK_RULE(&Prp::rule_punch_rhs)){ RULE_FAILED("Failed rule_punch_format; couldn't find a punch_rhs.\n"); }

  RULE_SUCCESS("Matched rule_punch_format.\n", Prp_rule_punch_format);
}

uint8_t Prp::rule_scope_declaration(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_scope_declaration.");

  if(!CHECK_RULE(&Prp::rule_scope)){ RULE_FAILED("Failed rule_scope_declaration; couldn't find a scope.\n"); }

  if(!SCAN_IS_TOKEN(Token_id_ob, Prp_rule_scope_declaration)){ RULE_FAILED("Failed rule_scope_declaration; couldn't find an open brace.\n"); }

  check_eos();
  check_lb();

  if(!CHECK_RULE(&Prp::rule_scope_body)){ RULE_FAILED("Failed rule_scope_declaration; couldn't find a scope body.\n"); }

  auto lines_start = cur_line;
  auto tokens_start = tokens_consumed;

  // optional
  check_lb();
  if(!CHECK_RULE(&Prp::rule_scope_else)){
    go_back(tokens_consumed - tokens_start);
    cur_line = lines_start;
  }


  RULE_SUCCESS("Matched rule_scope_declaration.\n", Prp_rule_scope_declaration);
}

uint8_t Prp::rule_punch_rhs(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_punch_rhs.");

  if(!SCAN_IS_TOKEN(Token_id_div)){ RULE_FAILED("Failed rule_punch_rhs; couldn't find a div token.\n"); }

  // optional
  bool next = true;
  INIT_PSEUDO_FAIL();
  UPDATE_PSEUDO_FAIL();

  if(CHECK_RULE(&Prp::rule_identifier)){
    while(next){
      if(!SCAN_IS_TOKEN(Token_id_dot))
        next = false;
      else{
        if(!CHECK_RULE(&Prp::rule_identifier)){
          PSEUDO_FAIL();
          next = false;
        }
      }
    }
    UPDATE_PSEUDO_FAIL();
  }

  if(!SCAN_IS_TOKEN(Token_id_div)){ RULE_FAILED("Failed rule_punch_rhs; couldn't find a div token."); }

  next = true;

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(!SCAN_IS_TOKEN(Token_id_dot)){
      next = false;
    }
    else{
      if(!CHECK_RULE(&Prp::rule_identifier)){
        PSEUDO_FAIL();
        next = false;
      }
    }
  }

  RULE_SUCCESS("Matched rule_punch_rhs.\n", Prp_rule_punch_rhs);
}

uint8_t Prp::rule_function_pipe(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_function_pipe.");

  if(!SCAN_IS_TOKEN(Token_id_pipe, Prp_rule_function_pipe)){ RULE_FAILED("Failed rule_function_pipe; couldn't find a pipe token."); }
  else{
    if(!CHECK_RULE(&Prp::rule_fcall_implicit)){
      if(!CHECK_RULE(&Prp::rule_fcall_explicit)){
        RULE_FAILED("Failed rule_function_pipe; couldn't find a function call.\n");
      }
    }
    bool next = true;
    INIT_PSEUDO_FAIL();

    /* zero or more of the following */
    while(next){
      UPDATE_PSEUDO_FAIL();
      if(!SCAN_IS_TOKEN(Token_id_pipe, Prp_rule_function_pipe)){ next = false; }
      else{
        if(!CHECK_RULE(&Prp::rule_fcall_implicit)){
          if(!CHECK_RULE(&Prp::rule_fcall_explicit)){
            PSEUDO_FAIL();
            next = false;
          }
        }
      }
    }
  }

  RULE_SUCCESS("Matched rule_function_pipe.\n", Prp_rule_function_pipe);
}

uint8_t Prp::rule_fcall_explicit(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_fcall_explicit.");

  if(CHECK_RULE(&Prp::rule_constant)){ RULE_FAILED("Failed rule_fcall_explicit; found a constant.\n"); }

  INIT_PSEUDO_FAIL();
  UPDATE_PSEUDO_FAIL();

  // optional

  if(CHECK_RULE(&Prp::rule_tuple_notation)){
    if(!SCAN_IS_TOKEN(Token_id_dot)){
      PSEUDO_FAIL();
    }
    UPDATE_PSEUDO_FAIL();
  }

  if(!CHECK_RULE(&Prp::rule_tuple_dot_notation)){ RULE_FAILED("Failed rule_fcall_explicit; couldn't find a tuple_dot_notation.\n"); }

  if(!CHECK_RULE(&Prp::rule_fcall_arg_notation)){ RULE_FAILED("Failed rule_fcall_explicit; couldn't find an fcall_arg_notation.\n"); }

  // optional
  CHECK_RULE(&Prp::rule_scope_declaration);

  bool next = true;

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(SCAN_IS_TOKEN(Token_id_dot)){
      if(!CHECK_RULE(&Prp::rule_fcall_explicit)){
        if(!CHECK_RULE(&Prp::rule_tuple_dot_notation)){
          next = false;
          PSEUDO_FAIL();
        }
      }
    }
    else
      next = false;
  }

  // optional
  CHECK_RULE(&Prp::rule_function_pipe);

  RULE_SUCCESS("Matched rule_fcall_explicit", Prp_rule_fcall_explicit);
}

uint8_t Prp::rule_fcall_arg_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_fcall_arg_notation.");

  if(!SCAN_IS_TOKEN(Token_id_op, Prp_rule_fcall_arg_notation)){ RULE_FAILED("Failed rule_fcall_arg_notation; couldn't find an opening parenthesis.\n"); }

  if(SCAN_IS_TOKEN(Token_id_cp, Prp_rule_fcall_arg_notation)){ RULE_SUCCESS("Matched rule_fcall_arg_notation.\n", Prp_rule_fcall_arg_notation); }

  if(!CHECK_RULE(&Prp::rule_assignment_expression)){
    if(!CHECK_RULE(&Prp::rule_logical_expression)){
      RULE_FAILED("Failed rule_fcall_arg_notation; couldn't find either an rhs_expression_property or a logical_expression.\n");
    }
  }

  bool next = true;
  INIT_PSEUDO_FAIL();

  // zero or more of the following
  while(next){
    UPDATE_PSEUDO_FAIL();
    if(SCAN_IS_TOKEN(Token_id_comma, Prp_rule_fcall_arg_notation)){
      if(!CHECK_RULE(&Prp::rule_assignment_expression)){
        if(!CHECK_RULE(&Prp::rule_logical_expression)){
          PSEUDO_FAIL();
          next = false;
        }
      }
    }
    else
      next = false;
  }
  // optional
  SCAN_IS_TOKEN(Token_id_comma, Prp_rule_fcall_arg_notation);

  if(!SCAN_IS_TOKEN(Token_id_cp, Prp_rule_fcall_arg_notation)){ RULE_FAILED("Failed rule_fcall_arg_notation; couldn't find a closing parenthesis.\n"); }

  RULE_SUCCESS("Matched rule_fcall_arg_notation.\n", Prp_rule_fcall_arg_notation);
}

uint8_t Prp::rule_fcall_implicit(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_fcall_implicit.");

  if(CHECK_RULE(&Prp::rule_constant)){ RULE_FAILED("Failed rule_fcall_implicit; found a constant.\n"); }
  if(!CHECK_RULE(&Prp::rule_tuple_dot_notation)){ RULE_FAILED("Failed rule_fcall_implicit; couldn't find a tuple_dot_notation.\n"); }

  // option 1
  if(!CHECK_RULE(&Prp::rule_scope_declaration)){
    // optional
    CHECK_RULE(&Prp::rule_function_pipe);
    if(CHECK_RULE(&Prp::rule_not_in_implicit)){
      RULE_FAILED("Failed rule_fcall_implicit; found an answering not_in_implicit.\n");
    }
  }
  // option 2
  else{
    // optional
    CHECK_RULE(&Prp::rule_function_pipe);
  }

  RULE_SUCCESS("Matched rule_fcall_implicit.\n", Prp_rule_fcall_implicit);
}

uint8_t Prp::rule_not_in_implicit(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_not_in_implicit");

  if(CHECK_RULE(&Prp::rule_assignment_operator)){
    RULE_SUCCESS("Matched rule_not_in_implicit; found an assignment operator.\n", Prp_rule_not_in_implicit);
  }

  if(SCAN_IS_TOKEN(Token_id_op) || SCAN_IS_TOKEN(Token_id_colon)){
    RULE_SUCCESS("Matched rule_not_in_implicit; found a single character token without a previous line break.\n", Prp_rule_not_in_implicit);
  }

  if(SCAN_IS_TOKEN(Token_id_obr)){
    if(SCAN_IS_TOKEN(Token_id_obr)){
      RULE_SUCCESS("Matched rule_not_in_implicit; found two open brackets.\n", Prp_rule_not_in_implicit);
    }
    RULE_SUCCESS("Matched rule_not_in_implicit; found an bracket.\n", Prp_rule_not_in_implicit);
  }

  if(SCAN_IS_TOKEN(Token_id_dot)){
    if(SCAN_IS_TOKEN(Token_id_dot)){
      RULE_SUCCESS("Matched rule_not_in_implicit; found two dots.\n", Prp_rule_not_in_implicit);
    }
  }

  check_lb();

  if(CHECK_RULE(&Prp::rule_overload_notation)){
    RULE_SUCCESS("Matched rule_not_in_implicit; found an overload notation.\n", Prp_rule_not_in_implicit);
  }

  if(SCAN_IS_TOKEN(Token_id_minus) || SCAN_IS_TOKEN(Token_id_mult) || SCAN_IS_TOKEN(Token_id_div) || SCAN_IS_TOKEN(Pyrope_id_or) || SCAN_IS_TOKEN(Pyrope_id_and) || SCAN_IS_TOKEN(Pyrope_id_xor) || SCAN_IS_TOKEN(Token_id_same) || SCAN_IS_TOKEN(Token_id_diff) || SCAN_IS_TOKEN(Pyrope_id_is) || SCAN_IS_TOKEN(Token_id_le) || SCAN_IS_TOKEN(Token_id_ge) || SCAN_IS_TOKEN(Token_id_lt) || SCAN_IS_TOKEN(Token_id_gt) || SCAN_IS_TOKEN(Pyrope_id_union) || SCAN_IS_TOKEN(Pyrope_id_intersect) || SCAN_IS_TOKEN(Pyrope_id_in) || SCAN_IS_TOKEN(Token_id_xor) || SCAN_IS_TOKEN(Token_id_and) || SCAN_IS_TOKEN(Token_id_pipe)){
    RULE_SUCCESS("Matched rule_not_in_implicit; found a single character token.\n", Prp_rule_not_in_implicit);
  }

  if(SCAN_IS_TOKEN(Token_id_plus)){
    if(SCAN_IS_TOKEN(Token_id_plus)){
      RULE_SUCCESS("Matched rule_not_in_implicit; found two plus tokens.\n", Prp_rule_not_in_implicit);
    }
    if(SCAN_IS_TOKEN(Token_id_mult)){
      RULE_SUCCESS("Matched rule_not_in_implicit; found plus and mult tokens.\n", Prp_rule_not_in_implicit);
    }
    RULE_SUCCESS("Matched rule_not_in_implicit; found a plus.\n", Prp_rule_not_in_implicit);
  }

  RULE_FAILED("Failed rule_not_in_implicit.\n");
}

uint8_t Prp::rule_assignment_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_assignment_expression.");

  if(CHECK_RULE(&Prp::rule_constant)){ RULE_FAILED("Failed rule_assignment_expression; found a constant.\n"); }
  if(!CHECK_RULE(&Prp::rule_lhs_expression)){
    if(!CHECK_RULE(&Prp::rule_overload_notation)){
      RULE_FAILED("Failed rule_assignment_expression; couldn't find either an overload_notation or an lhs_expression.\n");
    }
  }

  check_lb();
  if(!CHECK_RULE(&Prp::rule_assignment_operator)){ RULE_FAILED("Failed rule_assignment_expression; couldn't find an assignment_operator.\n"); }

  check_lb();
  if(!CHECK_RULE(&Prp::rule_logical_expression)){
    if(!CHECK_RULE(&Prp::rule_fcall_implicit)){
      RULE_FAILED("Failed rule_assignment_expression; couldn't find an fcall_implicit, or a logical_expression.\n");
    }
  }

  RULE_SUCCESS("Matched rule_assignment_expression.\n", Prp_rule_assignment_expression);
}

uint8_t Prp::rule_return_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_return_statement.");

  if(!SCAN_IS_TOKEN(Pyrope_id_return)){ RULE_FAILED("Failed rule_return_statement; couldn't find a return token.\n"); }
  if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_return_statement; couldn't a logical_expression.\n"); }

  RULE_SUCCESS("Matched rule_return_statement.\n", Prp_rule_return_statement);
}

uint8_t Prp::rule_compile_check_statement(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_compile_check_statement.");

  if(!scan_is_token(Token_id_pound)){ RULE_FAILED("Failed rule_compile_check_statement; couldn't find a pound operator.\n"); }
  if(!CHECK_RULE(&Prp::rule_code_block_int)){ RULE_FAILED("Failed rule_compile_check_statement; couldn't find a code_block_int.\n"); }

  RULE_SUCCESS("Matched rule_compile_check_statement.\n", Prp_rule_compile_check_statement);
}

uint8_t Prp::rule_block_body(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_block_body.\n");

  // optional
  CHECK_RULE(&Prp::rule_code_blocks);

  check_eos();
  check_lb();

  if(!SCAN_IS_TOKEN(Token_id_cb, Prp_rule_block_body)){ RULE_FAILED("Failed rule_block_body; couldn't find a closing brace.\n"); }

  RULE_SUCCESS("Matched rule_block_body.\n", Prp_rule_block_body);
}

uint8_t Prp::rule_lhs_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_lhs_expression.");

  if(SCAN_IS_TOKEN(Token_id_backslash)){
    if(!SCAN_IS_TOKEN(Token_id_backslash)){ RULE_FAILED("Failed rule_lhs_expression; couldn't find a second backslash.\n"); }
  }
  if(!CHECK_RULE(&Prp::rule_range_notation)){
    if(!CHECK_RULE(&Prp::rule_tuple_notation)){
      RULE_FAILED("Failed rule_lhs_expression; couldn't find either a range_notation or a tuple_notation.\n");
    }
  }

  RULE_SUCCESS("Matched rule_lhs_expression.\n", Prp_rule_lhs_expression);
}

uint8_t Prp::rule_tuple_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_tuple_notation.");

  // options 1 and 2
  if(SCAN_IS_TOKEN(Token_id_op, Prp_rule_tuple_notation)){
    if(SCAN_IS_TOKEN(Token_id_cp, Prp_rule_tuple_notation)){ RULE_SUCCESS("Matched rule_tuple_notation; first option.\n", Prp_rule_tuple_notation); }

    if(!CHECK_RULE(&Prp::rule_assignment_expression)){
      if(!CHECK_RULE(&Prp::rule_logical_expression)){
        RULE_FAILED("Failed rule_tuple_notation; couldn't find either an assignment_expression or a logical_expression.\n");
      }
    }

    bool next = true;
    INIT_PSEUDO_FAIL();

    while(next){
      UPDATE_PSEUDO_FAIL();
      if(!SCAN_IS_TOKEN(Token_id_comma, Prp_rule_tuple_notation)){ next = false; }
      else{
        if(!CHECK_RULE(&Prp::rule_assignment_expression)){
          if(!CHECK_RULE(&Prp::rule_logical_expression)){
            PSEUDO_FAIL();
            next = false;
          }
          else{
            check_lb();
          }
        }
        else{
          check_lb();
        }
      }
    }

    // optional
    SCAN_IS_TOKEN(Token_id_comma, Prp_rule_tuple_notation);

    if(SCAN_IS_TOKEN(Token_id_cp, Prp_rule_tuple_notation)){
      // optional
      if(!CHECK_RULE(&Prp::rule_tuple_by_notation)){
        CHECK_RULE(&Prp::rule_bit_selection_bracket);
      }
      // optional
      CHECK_RULE(&Prp::rule_function_pipe);
      RULE_SUCCESS("Matched rule_tuple_notation; second option.\n", Prp_rule_tuple_notation);
    }
    else{ RULE_FAILED("Failed rule_tuple_notation; couldn't match option 1 or 2.\n"); }
  }

  // option 3
  if(!CHECK_RULE(&Prp::rule_bit_selection_notation)){ RULE_FAILED("Failed rule_tuple_notation; couldn't match any options.\n"); }

  RULE_SUCCESS("Matched rule_tuple_notation; third option.\n", Prp_rule_tuple_notation);
}

uint8_t Prp::rule_range_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_range_notation.");

  // optional
  CHECK_RULE(&Prp::rule_bit_selection_notation);

  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_range_notation; couldn't find the first dot.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_range_notation; couldn't find the second dot.\n"); }

  // optional
  CHECK_RULE(&Prp::rule_additive_expression);

  // optional
  CHECK_RULE(&Prp::rule_tuple_by_notation);

  RULE_SUCCESS("Matched rule_range_notation.\n", Prp_rule_range_notation);
}

uint8_t Prp::rule_bit_selection_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_bit_selection_notation.");

  if(!CHECK_RULE(&Prp::rule_tuple_dot_notation)){ RULE_FAILED("Failed rule_bit_selection_notation; couldn't find a tuple_dot_notation.\n"); }
  if(!CHECK_RULE(&Prp::rule_bit_selection_bracket)){ RULE_FAILED("Failed rule_bit_selection_notation; couldn't find a bit_selection_bracket.\n"); }

  RULE_SUCCESS("Matched rule_bit_selection_notation.\n", Prp_rule_bit_selection_notation);
}

uint8_t Prp::rule_tuple_dot_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_tuple_dot_notation.");

  if(!CHECK_RULE(&Prp::rule_tuple_array_notation)){ RULE_FAILED("Failed tuple_dot_notation; couldn't find a tuple_array_notation.\n"); }
  if(!CHECK_RULE(&Prp::rule_tuple_dot_dot)){ RULE_FAILED("Failed rule_tuple_dot_notation; couldn't find a tuple_dot_dot.\n"); }

  RULE_SUCCESS("Matched rule_tuple_dot_notation.\n", Prp_rule_tuple_dot_notation);
}

uint8_t Prp::rule_tuple_dot_dot(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_tuple_dot_dot.");
  bool next = true;

  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(!SCAN_IS_TOKEN(Token_id_dot, Prp_rule_tuple_dot_dot))
      next = false;
    else{
      if(!CHECK_RULE(&Prp::rule_tuple_array_notation)){
        PSEUDO_FAIL();
        next = false;
      }
    }
  }

  RULE_SUCCESS("Matched rule_tuple_dot_dot\n", Prp_rule_tuple_dot_dot);
}

uint8_t Prp::rule_tuple_array_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_tuple_array_notation.");

  if(!CHECK_RULE(&Prp::rule_lhs_var_name)){ RULE_FAILED("Failed rule_tuple_array_notation; couldn't find an lhs_var_name.\n"); }
  if(!CHECK_RULE(&Prp::rule_tuple_array_bracket)){ RULE_FAILED("Failed rule_tuple_array_notation; couldn't find a tuple_array_bracket.\n"); }

  RULE_SUCCESS("Matched rule_tuple_array_notation.\n", Prp_rule_tuple_array_notation);
}

uint8_t Prp::rule_lhs_var_name(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_lhs_var_name.");

  if(!(CHECK_RULE(&Prp::rule_identifier) || CHECK_RULE(&Prp::rule_constant))){ RULE_FAILED("Failed rule_lhs_var_name; couldn't find an identifier or a constant.\n"); }

  RULE_SUCCESS("Matched rule_lhs_var_name.\n", Prp_rule_lhs_var_name);
}

uint8_t Prp::rule_tuple_array_bracket(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_tuple_array_bracket.");

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(!SCAN_IS_TOKEN(Token_id_obr, Prp_rule_tuple_array_bracket)){ next = false; }
    else{
      if(!CHECK_RULE(&Prp::rule_logical_expression)){
        PSEUDO_FAIL();
        next = false;
      }
      if(!SCAN_IS_TOKEN(Token_id_cbr, Prp_rule_tuple_array_bracket)){
        PSEUDO_FAIL();
        next = false;
      }
    }
  }

  RULE_SUCCESS("Matched rule_tuple_array_bracket.\n", Prp_rule_tuple_array_bracket);
}

uint8_t Prp::rule_identifier(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_identifier.");

  if(CHECK_RULE(&Prp::rule_keyword)){ RULE_FAILED("Failed rule identifier; found a keyword instead.\n"); }

  // optional
  SCAN_IS_TOKEN(Token_id_bang, Prp_rule_identifier) || SCAN_IS_TOKEN(Pyrope_id_tilde, Prp_rule_identifier);

  if(!(SCAN_IS_TOKEN(Token_id_register, Prp_rule_identifier) || SCAN_IS_TOKEN(Token_id_input, Prp_rule_identifier) || SCAN_IS_TOKEN(Token_id_output, Prp_rule_identifier) || SCAN_IS_TOKEN(Token_id_alnum, Prp_rule_identifier))){ RULE_FAILED("Failed rule_identifier; couldn't find a name.\n"); }

  // optional
  SCAN_IS_TOKEN(Token_id_qmark);

  RULE_SUCCESS("Matched rule_identifier.\n", Prp_rule_identifier);
}

// some rules want there to not be a constant; in this case, we don't want AST changes or tokens consumed if the rule is matched.
uint8_t Prp::rule_constant(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_constant");

  // option 1
  if(CHECK_RULE(&Prp::rule_numerical_constant)){ RULE_SUCCESS("Matched rule_constant.\n", Prp_rule_constant); }

  // option 2
  if(!CHECK_RULE(&Prp::rule_string_constant)){ RULE_FAILED("Failed rule_constant; couldn't find either a numerical or string constant.\n"); }

  RULE_SUCCESS("Matched rule_constant.\n", Prp_rule_constant);
}

uint8_t Prp::rule_numerical_constant(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_numerical_constant.");

  if(!SCAN_IS_TOKEN(Token_id_num, Prp_rule_numerical_constant)){ RULE_FAILED("Failed rule_numerical_constant; couldn't find a number.\n"); }

  RULE_SUCCESS("Matched rule_numerical_constant.\n", Prp_rule_numerical_constant);
}

// TODO: add support for single tick strings
uint8_t Prp::rule_string_constant(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_string_constant.");

  // option 1: double " string
  if(!SCAN_IS_TOKEN(Token_id_string, Prp_rule_string_constant)){
    // option 2: single ' string
    if(!SCAN_IS_TOKEN(Token_id_tick, Prp_rule_string_constant)){ RULE_FAILED("Failed rule_string_constant; couldn't find either a single tick or double tick string.\n"); }
    bool next = true;
    while(next){
      auto start_line = cur_line;
      check_lb();
      if(cur_line > start_line) { RULE_FAILED("Failed rule_string_constant (single tick path); found a line break in the middle of the string.\n"); }
      if(scan_is_end()) { RULE_FAILED("Failed rule_string_constant (single tick path); found the end of the scan before a terminating tick.\n"); }
      if(SCAN_IS_TOKEN(Token_id_tick, Prp_rule_string_constant)){
        next = false;
      }
      else{ // WARNING: unfortunately, since we cannot easily make SCAN_IS_TOKEN match and add any token to the list, we need this extra code here.
        loc_list.push_back(std::make_tuple(2, Prp_rule_string_constant, scan_token()));
        sub_cnt++;
        consume_token();
      }
    }
  }

  RULE_SUCCESS("Matched rule_string_constant.\n", Prp_rule_string_constant);
}

uint8_t Prp::rule_assignment_operator(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_assignment_operator.");

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

uint8_t Prp::rule_tuple_by_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_tuple_by_notation.");

  if(!SCAN_IS_TOKEN(Pyrope_id_by)){ RULE_FAILED("Failed rule_tuple_by_notation; couldn't find a by token.\n"); }
  if(!CHECK_RULE(&Prp::rule_lhs_var_name)){ RULE_FAILED("Failed rule_tuple_by_notation; couldn't find a rule_lhs_var_name.\n"); }

  RULE_SUCCESS("Matched rule_tuple_by_notation.\n", Prp_rule_tuple_by_notation);
}

uint8_t Prp::rule_bit_selection_bracket(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_bit_selection_bracket.");

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    if(!SCAN_IS_TOKEN(Token_id_obr)){ next = false; }
    else{
      if(!SCAN_IS_TOKEN(Token_id_obr)){
        PSEUDO_FAIL();
        next = false;
      }
      // optional
      CHECK_RULE(&Prp::rule_logical_expression);
      if(!SCAN_IS_TOKEN(Token_id_cbr)){
        PSEUDO_FAIL();
        next = false;
      }
      if(!SCAN_IS_TOKEN(Token_id_cbr)){
        PSEUDO_FAIL();
        next = false;
      }
    }
  }

  RULE_SUCCESS("Matched rule_bit_selection_bracket.\n", Prp_rule_bit_selection_bracket);
}

uint8_t Prp::rule_logical_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_logical_expression.");

  if(!CHECK_RULE(&Prp::rule_relational_expression)){ RULE_FAILED("Failed rule_logical_expression; couldn't find a relational_expression.\n"); }

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    check_lb();
    if(SCAN_IS_TOKEN(Pyrope_id_or, Prp_rule_logical_expression) || SCAN_IS_TOKEN(Pyrope_id_and, Prp_rule_logical_expression) || SCAN_IS_TOKEN(Pyrope_id_xor, Prp_rule_logical_expression)){
      if(!CHECK_RULE(&Prp::rule_relational_expression)){ RULE_FAILED("Failed rule_logical_expression; couldn't find an answering relational_expression.\n"); }
    }
    else{
      PSEUDO_FAIL();
      next = false;
    }
  }

  RULE_SUCCESS("Matched rule_logical_expression.\n", Prp_rule_logical_expression);
}

uint8_t Prp::rule_relational_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_relational_expression.");

  if(!CHECK_RULE(&Prp::rule_additive_expression)){ RULE_FAILED("Failed rule_relational_expression; couldn't find an additive_expression.\n"); }

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    check_lb();
    if(SCAN_IS_TOKEN(Token_id_le, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_ge, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_lt, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_gt, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_same, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Token_id_diff, Prp_rule_relational_expression) || SCAN_IS_TOKEN(Pyrope_id_is, Prp_rule_relational_expression)){
      if(!CHECK_RULE(&Prp::rule_additive_expression)){ RULE_FAILED("Failed Prp_rule_relational_expression; couldn't find an answering additive_expression.\n"); }
    }
    else{
      PSEUDO_FAIL();
      next = false;
    }
  }

  RULE_SUCCESS("Matched rule_relational_expression.\n", Prp_rule_relational_expression);
}

uint8_t Prp::rule_additive_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_additive_expression.");

  if(!CHECK_RULE(&Prp::rule_bitwise_expression)){ RULE_FAILED("Failed rule_additive_expression; couldn't find a bitwise expression.\n"); }
  PRINT_DBG_AST("rule_additive_expression: just checked for first operand; sub_cnt = {}.\n", sub_cnt);
  bool next = true;
  bool found_op = false;

  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    check_lb();
    if(SCAN_IS_TOKEN(Token_id_plus, Prp_rule_additive_expression)){ // can be +, ++, or +*
      // optional
      SCAN_IS_TOKEN(Token_id_plus, Prp_rule_additive_expression) || SCAN_IS_TOKEN(Token_id_mult, Prp_rule_additive_expression);
      found_op = true;
      PRINT_DBG_AST("rule_additive_expression: just found operator; sub_cnt = {}.\n", sub_cnt);
    }
    else if(SCAN_IS_TOKEN(Token_id_lt)){ // left and right shift operators
      if(SCAN_IS_TOKEN(Token_id_lt, Prp_rule_additive_expression)){
        unconsume_token();
        loc_list.push_back(std::make_tuple(2, Prp_rule_additive_expression, scan_token()));
        consume_token();
        found_op = true;
      }
      else{
        PSEUDO_FAIL();
        next = false;
      }
    }
    else if(SCAN_IS_TOKEN(Token_id_gt)){
      if(SCAN_IS_TOKEN(Token_id_gt, Prp_rule_additive_expression)){
        unconsume_token();
        loc_list.push_back(std::make_tuple(2, Prp_rule_additive_expression, scan_token()));
        consume_token();
        found_op = true;
      }
      else{
        PSEUDO_FAIL();
        next = false;
      }
    }
    else if(SCAN_IS_TOKEN(Token_id_minus, Prp_rule_additive_expression) || SCAN_IS_TOKEN(Pyrope_id_intersect, Prp_rule_additive_expression) || SCAN_IS_TOKEN(Pyrope_id_union, Prp_rule_additive_expression)){ // can only be a single token
      found_op = true;
    }
    else if(CHECK_RULE(&Prp::rule_overload_notation)){
      found_op = true;
    }
    else{
      PSEUDO_FAIL();
      next = false;
    }
    if(found_op){
      if(!CHECK_RULE(&Prp::rule_bitwise_expression)){ RULE_FAILED("Failed rule_additive_expression; couldn't find an answering bitwise expression.\n"); }
      PRINT_DBG_AST("rule_additive_expression: just checked for second operand; sub_cnt = {}.\n", sub_cnt);
      found_op = false;
    }
  }
  // optional
  UPDATE_PSEUDO_FAIL();

  if(SCAN_IS_TOKEN(Token_id_dot, Prp_rule_additive_expression)){
    if(!SCAN_IS_TOKEN(Token_id_dot, Prp_rule_additive_expression)){
      PSEUDO_FAIL();
    }
    else{
      // optional
      CHECK_RULE(&Prp::rule_additive_expression);
    }
  }

  RULE_SUCCESS("Matched rule_additive_expression.\n", Prp_rule_additive_expression);
}

uint8_t Prp::rule_bitwise_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_bitwise_expression.");

  if(!CHECK_RULE(&Prp::rule_multiplicative_expression)){ RULE_FAILED("Failed rule_bitwise_expression; couldn't find a multiplicative_expression.\n"); }

  bool next = true;

  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    check_lb();
    if(SCAN_IS_TOKEN(Token_id_or, Prp_rule_bitwise_expression) || SCAN_IS_TOKEN(Token_id_and, Prp_rule_bitwise_expression) || SCAN_IS_TOKEN(Token_id_xor, Prp_rule_bitwise_expression)){
      if(!CHECK_RULE(&Prp::rule_multiplicative_expression)){ RULE_FAILED("Failed rule_bitwise_expression; couldn't find an answering multiplicative expression.\n"); }
    }
    else{
      PSEUDO_FAIL();
      next = false;
    }
  }

  RULE_SUCCESS("Matched rule_bitwise_expression.\n", Prp_rule_bitwise_expression);
}

uint8_t Prp::rule_multiplicative_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_multiplicative_expression.");

  if(!CHECK_RULE(&Prp::rule_unary_expression)){ RULE_FAILED("Failed rule_multiplicative_expression; couldn't find a unary_expression.\n"); }

  bool next = true;
  INIT_PSEUDO_FAIL();

  while(next){
    UPDATE_PSEUDO_FAIL();
    check_lb();
    if(SCAN_IS_TOKEN(Token_id_mult, Prp_rule_multiplicative_expression) || SCAN_IS_TOKEN(Token_id_div, Prp_rule_multiplicative_expression)){
      if(!CHECK_RULE(&Prp::rule_unary_expression)){ RULE_FAILED("Failed rule_multiplicative_expression; couldn't find an answering unary expression.\n"); }
    }
    else{
      PSEUDO_FAIL();
      next = false;
    }
  }

  RULE_SUCCESS("Matched rule_multiplicative_expression.\n", Prp_rule_multiplicative_expression);
}

uint8_t Prp::rule_unary_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_unary_expression.");

  // option 1
  if(CHECK_RULE(&Prp::rule_factor)){ RULE_SUCCESS("Matched rule_unary_expression, option 1.\n", Prp_rule_unary_expression); }
  if(!(SCAN_IS_TOKEN(Token_id_bang, Prp_rule_unary_expression) || SCAN_IS_TOKEN(Pyrope_id_tilde, Prp_rule_unary_expression))){ RULE_FAILED("Failed rule_unary_expression; couldn't find a factor or a unary operator.\n"); }
  if(!CHECK_RULE(&Prp::rule_factor)){ RULE_FAILED("Failed rule_unary_expression; couldn't find an answering factor.\n"); }

  RULE_SUCCESS("Matched rule_unary_expression, option 2.\n", Prp_rule_unary_expression);
}

uint8_t Prp::rule_factor(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_factor.");
  // option 1
  if(CHECK_RULE(&Prp::rule_rhs_expression)){
    RULE_SUCCESS("Matched rule_factor; option 1.\n", Prp_rule_factor);
  }
  // option 2
  else if(SCAN_IS_TOKEN(Token_id_op, Prp_rule_factor)){
    check_lb();
    if(!CHECK_RULE(&Prp::rule_logical_expression)){ RULE_FAILED("Failed rule_factor; couldn't find a logical_expression.\n"); }
    check_lb();
    if(!SCAN_IS_TOKEN(Token_id_cp, Prp_rule_factor)){ RULE_FAILED("Failed rule_factor; couldn't find a closing parenthesis.\n"); }

    // optional
    CHECK_RULE(&Prp::rule_bit_selection_bracket);
  }
  else{
    RULE_FAILED("Failed rule_factor; couldn't match either option.\n");
  }

  RULE_SUCCESS("Matched rule_factor; option 2.\n", Prp_rule_factor);
}

uint8_t Prp::rule_overload_notation(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_overload_notation.");

  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a starting dot.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a second dot.\n"); }
  if(!CHECK_RULE(&Prp::rule_overload_name)){ RULE_FAILED("Failed rule_overload_notation; couldn't find an overload name.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a first trailing dot.\n"); }
  if(!SCAN_IS_TOKEN(Token_id_dot)){ RULE_FAILED("Failed rule_overload_notation; couldn't find a second trailing dot.\n"); }

  RULE_SUCCESS("Matched rule_overload_notation.\n", Prp_rule_overload_notation);
}

// WARNING: not sure how this behaves when we aren't explicitly looking for a line terminator
uint8_t Prp::rule_overload_name(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_overload_name.");

  bool next = CHECK_RULE(&Prp::rule_overload_exception);
  if(next){ RULE_FAILED("Failed rule_overload_notation; found an overload_exception.\n"); }
  while(next)
    next = CHECK_RULE(&Prp::rule_overload_exception);

  RULE_SUCCESS("Matched rule_overload_name.\n", Prp_rule_overload_name);
}

uint8_t Prp::rule_overload_exception(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_overload_exception.\n");

  if(SCAN_IS_TOKEN(Token_id_dot) || SCAN_IS_TOKEN(Token_id_pound) || SCAN_IS_TOKEN(Token_id_semicolon) || SCAN_IS_TOKEN(Token_id_comma) || SCAN_IS_TOKEN(Token_id_eq) || SCAN_IS_TOKEN(Token_id_op)  || SCAN_IS_TOKEN(Token_id_cp)  || SCAN_IS_TOKEN(Token_id_obr) || SCAN_IS_TOKEN(Token_id_cbr) || SCAN_IS_TOKEN(Token_id_ob) || SCAN_IS_TOKEN(Token_id_cb) || SCAN_IS_TOKEN(Token_id_backslash) || SCAN_IS_TOKEN(Token_id_qmark) || SCAN_IS_TOKEN(Token_id_bang) || SCAN_IS_TOKEN(Token_id_or) || SCAN_IS_TOKEN(Token_id_tick)){ RULE_SUCCESS("Matched rule_overload_exception.\n", Prp_rule_overload_exception); }

  RULE_FAILED("Failed rule_overload_exception; couldn't find an excepting character.\n");
}

uint8_t Prp::rule_rhs_expression(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_rhs_expression.");

  if(!(CHECK_RULE(&Prp::rule_fcall_explicit) || CHECK_RULE(&Prp::rule_lhs_expression) || CHECK_RULE(&Prp::rule_scope_declaration))){ RULE_FAILED("Failed rule_rhs_expression; couldn't find an expression.\n"); }

  RULE_SUCCESS("Matched rule_rhs_expression.\n", Prp_rule_rhs_expression);
}

uint8_t Prp::rule_keyword(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &pass_list){
  INIT_FUNCTION("rule_keyword");

  if(SCAN_IS_TOKEN(Pyrope_id_if) || SCAN_IS_TOKEN(Pyrope_id_else) || SCAN_IS_TOKEN(Pyrope_id_elif) || SCAN_IS_TOKEN(Pyrope_id_as) || SCAN_IS_TOKEN(Pyrope_id_is) || SCAN_IS_TOKEN(Pyrope_id_and) || SCAN_IS_TOKEN(Pyrope_id_or) || SCAN_IS_TOKEN(Pyrope_id_xor) || SCAN_IS_TOKEN(Pyrope_id_intersect) || SCAN_IS_TOKEN(Pyrope_id_union) || SCAN_IS_TOKEN(Pyrope_id_until) || SCAN_IS_TOKEN(Pyrope_id_default) || SCAN_IS_TOKEN(Pyrope_id_try) || SCAN_IS_TOKEN(Pyrope_id_punch) || SCAN_IS_TOKEN(Pyrope_id_c) || SCAN_IS_TOKEN(Pyrope_id_c) || SCAN_IS_TOKEN(Pyrope_id_assertion) || SCAN_IS_TOKEN(Pyrope_id_negation) || SCAN_IS_TOKEN(Pyrope_id_in) || SCAN_IS_TOKEN(Pyrope_id_for) || SCAN_IS_TOKEN(Pyrope_id_while) || SCAN_IS_TOKEN(Pyrope_id_by) || SCAN_IS_TOKEN(Pyrope_id_return) || SCAN_IS_TOKEN(Pyrope_id_false) || SCAN_IS_TOKEN(Pyrope_id_FALSE) || SCAN_IS_TOKEN(Pyrope_id_true) || SCAN_IS_TOKEN(Pyrope_id_TRUE) || SCAN_IS_TOKEN(Pyrope_id_unique) || SCAN_IS_TOKEN(Pyrope_id_when)){
    RULE_SUCCESS("Matched rule_keyword.\n", Prp_rule_keyword);
  }

  RULE_FAILED("Failed rule_keyword.\n");
}

// corresponds to "__" rule in pegjs parser
inline void Prp::check_lb(){
  bool next = true;

  while(next){
    inc_line_cnt();
    if(scan_is_token(Token_id_comment)){
      scan_next();
      tokens_consumed++;
      next = true;
    }
    else
      next = false;
  }
}

inline bool Prp::check_eos(){
  bool next = true;
  bool found = false;
  // option 1 : an optional line break plus one or more semicolons
  bool start_line = cur_line;
  check_lb();
  while(next){
    if(scan_is_token(Token_id_semicolon)){
      scan_next();
      tokens_consumed++;
      found = true;
      next = true;
    }
    else
      next = false;
  }

  if(found){
    return true;
  }

  // option 2: a comment plus a line terminator
  if(cur_line > start_line){
    return true;
  }

  // option 3: end of file
  return scan_is_end();
}

void Prp::elaborate(){
  patch_pass(pyrope_keyword);

  PRINT_DBG_AST("RULE AND AST CALL TRACE \n\n");

  ast = std::make_unique<Ast_parser>(get_memblock(), Prp_rule);
  std::list<std::tuple<uint8_t, Rule_id, Token_entry>> loc_list;

  int failed = 0;
  uint64_t sub_cnt = 0;
  /*
#ifdef DEBUG_AST
  int i = 0;
  while(!scan_is_end()){
    PRINT_DBG_AST("Token {}: {}\n", i, scan_text());
    scan_next();
    i++;
  }
  go_back(i);
#endif
*/
  if(!CHECK_RULE(&Prp::rule_start)){
      failed = 1;
  }

  if(failed){
    PRINT_AST("\nParsing FAILED!\n");
    exit(1);
  }
  else{
    PRINT_AST("\nParsing SUCCESSFUL!\n");
  }

  PRINT_DBG_AST("\nAST Call List\n\n");
  for(auto it = loc_list.begin(); it != loc_list.end(); ++it){
    PRINT_DBG_AST("Operation: {}, Rule: {}, Token: {}\n", std::get<0>(*it), rule_id_to_string(std::get<1>(*it)), scan_text(std::get<2>(*it)));
  }

  PRINT_DBG_AST("\nAST BUILD LOG\n\n");

  // build the ast
  ast_builder(loc_list);

  PRINT_AST("\nAST PREORDER TRAVERSAL\n\n");

  // next, write the AST traversal
  ast_handler();

  PRINT_DBG_AST("\nSTATISTICS\n\n");

  // finally, write the statistics
  PRINT_DBG_AST(fmt::format("Number of rules called: {}\n", debug_stat.rules_called));
  PRINT_DBG_AST(fmt::format("Number of rules matched: {}\n", debug_stat.rules_matched));
  PRINT_DBG_AST(fmt::format("Number of rules failed: {}\n", debug_stat.rules_failed));
  PRINT_DBG_AST(fmt::format("Number of tokens consumed: {}\n", debug_stat.tokens_consumed));
  PRINT_DBG_AST(fmt::format("Number of tokens unconsumed: {}\n", debug_stat.tokens_unconsumed));
  PRINT_DBG_AST(fmt::format("Number of ast->up() calls: {}\n", debug_stat.ast_up_calls));
  PRINT_DBG_AST(fmt::format("Number of ast->down() calls: {}\n", debug_stat.ast_down_calls));
  PRINT_DBG_AST(fmt::format("Number of ast_add() calls: {}\n", debug_stat.ast_add_calls));
}

/* Consumes a token and dumps the new one */
bool Prp::consume_token(){
  debug_stat.tokens_consumed++;
  tokens_consumed++;
  return scan_next();
}

/* Unconsumes a token and dumps it if debug is defined */
bool Prp::unconsume_token(){
  PRINT_DBG_AST("Unconsuming token: ");
  debug_stat.tokens_unconsumed++;
  tokens_consumed--;
#ifdef DEBUG_AST
  bool ok = scan_prev();
  dump_token();
  return ok;
#else
  return scan_prev();
#endif
}

bool Prp::go_back(uint64_t num_tok){
  uint64_t i;
  bool ok;
  if(num_tok == 0)
    return true;
  PRINT_DBG_AST("Going back {} token(s); total token(s) consumed: {}.\n", num_tok, tokens_consumed);
  for(i=0;i<num_tok;i++){
    ok = unconsume_token();
  }
  cur_line = scan_line();
  return ok;
}

void Prp::ast_handler() {
#ifdef DEBUG_AST
  std::string rule_name;
  for(const auto &it:ast->depth_preorder(ast->get_root())){
    auto node = ast->get_data(it);
    rule_name = rule_id_to_string(node.rule_id);
    auto token_text = scan_text(node.token_entry);
    PRINT_AST("Rule name: {}, Token text: {}, Tree level: {}\n", rule_name, token_text, it.level);
  }
#endif
}

void Prp::ast_builder(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &passed_list){
  for(auto it = passed_list.begin(); it != passed_list.end(); ++it){
    auto ast_op = *it;
    switch(std::get<0>(ast_op)){
      case 0:
        debug_stat.ast_down_calls++;
        ast->down();
        PRINT_DBG_AST("Went down.\n");
        break;
      case 1:
        debug_stat.ast_up_calls++;
        ast->up(std::get<1>(ast_op));
        PRINT_DBG_AST("Went up with rule {}.\n", rule_id_to_string(std::get<1>(ast_op)));
        break;
      case 2:
        debug_stat.ast_add_calls++;
        ast->down();
        ast->add(std::get<1>(ast_op), std::get<2>(ast_op));
        PRINT_DBG_AST("Added token {} from rule {}.\n", scan_text(std::get<2>(ast_op)), rule_id_to_string(std::get<1>(ast_op)));
        ast->up(std::get<1>(ast_op));
        break;
    }
  }
}

inline uint8_t Prp::check_function(uint8_t (Prp::*rule)(std::list<std::tuple<uint8_t, Rule_id, Token_entry>>&), uint64_t *sub_cnt, std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &loc_list){
  PRINT_DBG_AST("Called check_function.\n");
  uint64_t starting_size = loc_list.size();
  uint8_t ret = (this->*rule)(loc_list);
  if(ret == false){
    return false;
  }

  if(loc_list.size() > starting_size){
    (*sub_cnt)++;
    PRINT_DBG_AST("check_function: incremented sub_cnt to {}.\n", *sub_cnt);
  }

  return ret;
}

bool Prp::chk_and_consume(Token_id tok, Rule_id rid, uint64_t *sub_cnt, std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &loc_list){
  if (!scan_is_token(tok))
    return false;

  if(tok == Pyrope_id_elif || tok == Pyrope_id_if || tok == Pyrope_id_unique || tok == Pyrope_id_else || tok == Pyrope_id_while || tok == Pyrope_id_punch || tok == Pyrope_id_for || tok == Pyrope_id_when){
    scan_next();
    auto next_pos = get_token_pos();
    scan_prev();
    auto cur_pos_end = get_token_pos() + scan_text().length();
    if((next_pos - cur_pos_end) < 1)
      return false;
  }else if(tok == Token_id_comma)
    check_lb();

  if(rid != Prp_invalid){
    loc_list.push_back(std::make_tuple(2, rid, scan_token()));
    (*sub_cnt)++;
    PRINT_DBG_AST("chk_and_consume: incremented sub_cnt to {}\n", *sub_cnt);
  }
  PRINT_DBG_AST("Consuming token {} from rule {}.\n", scan_text(scan_token()), rule_id_to_string(rid));
#ifdef DEBUG_AST
  print_loc_list(loc_list);
#endif
  if(scan_line() == cur_line){
    consume_token();
    return true;
  }
}

inline bool Prp::inc_line_cnt(){
  if(scan_line() > cur_line){
    cur_line = scan_line();
    return true;
  }
  return false;
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
    case Prp_rule_fcall_implicit:
      return "Fcall implicit";
    case Prp_rule_for_statement:
      return "For statement";
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
    case Prp_rule_for_in_notation:
      return "For in notation";
    case Prp_rule_not_in_implicit:
      return "Not in implicit";
    case Prp_rule_fcall_arg_notation:
      return "Fcall arg notation";
    case Prp_rule_scope:
      return "Scope";
    case Prp_rule_scope_body:
      return "Scope body";
    case Prp_rule_scope_declaration:
      return "Scope declaration";
    case Prp_rule_scope_condition:
      return "Scope condition";
    case Prp_rule_fcall_explicit:
      return "Fcall explicit";
    default: return fmt::format("{}", rid);
  }
}

std::string Prp::tok_id_to_string(Token_id tok){
  switch(tok){
    case Token_id_nop:           // invalid token
      return "nop";
    case Token_id_comment:       // c-like comments
      return "comment";
    case Token_id_register:      // #asd #_asd
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
    case Token_id_tilde:         // ~
      return "~";
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

#ifdef DEBUG_AST
inline void Prp::print_loc_list(std::list<std::tuple<uint8_t, Rule_id, Token_entry>> &loc_list){
  int i = 0;
  for(auto it = loc_list.begin(); it != loc_list.end(); it++){
    PRINT_DBG_AST("loc_list[{}]: operation: {} rule: {} token: {}.\n", i++, std::get<0>(*it), rule_id_to_string(std::get<1>(*it)), scan_text(std::get<2>(*it)));
  }
}

inline void Prp::print_rule_call_stack(){
  for(auto it = rule_call_stack.begin(); it != rule_call_stack.end(); it++){
    PRINT_DBG_AST("{} ", *it);
  }
  PRINT_DBG_AST("\n");
}
#endif
