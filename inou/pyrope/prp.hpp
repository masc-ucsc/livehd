//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <queue>
#include <tuple>
#include <fstream>

#include <string>

#include "absl/container/flat_hash_map.h"

#include "ast.hpp"
#include "elab_scanner.hpp"

// variable argument number macro (adapted from stackoverflow.com/questions/3046889/optional-parameters-with-c-macros)
#define SCAN_IS_TOKEN_1_ARGS(tok) chk_and_consume(tok)
#define SCAN_IS_TOKEN_2_ARGS(tok, rid) chk_and_consume(tok, rid)

#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
#define SCAN_IS_TOKEN_MACRO_CHOOSER(...) \
  GET_3RD_ARG(__VA_ARGS__, SCAN_IS_TOKEN_2_ARGS, SCAN_IS_TOKEN_1_ARGS)
  
#define SCAN_IS_TOKEN(...) SCAN_IS_TOKEN_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

// function-like macros
#define INIT_FUNCTION(...) \
  debug_stat.rules_called++; \
  fmt::print(__VA_ARGS__); \
  uint64_t starting_tokens = tokens_consumed; \
  uint32_t starting_add = add_stack.size(); \
  uint32_t starting_down = down_stack.size(); \
  uint32_t starting_subtree = subtree_stack.size(); \
  uint32_t starting_subtree_indices = subtree_indices.size(); \
  int down_calls = 0; \
  int up_calls = 0; \
  DEBUG_DOWN() 
  
#define RULE_FAILED(...) \
  fmt::print(__VA_ARGS__); \
  if(tokens_consumed - starting_tokens > 0){ \
    go_back(tokens_consumed - starting_tokens); \
  }\
  debug_stat.rules_failed++; \
  DEBUG_UP(0); \
  add_stack.resize(starting_add); \
  down_stack.resize(starting_down); \
  subtree_stack.resize(starting_subtree); \
  subtree_indices.resize(starting_subtree_indices); \
  return false
  
#define RULE_SUCCESS(message, rule) \
  fmt::print(message); \
  DEBUG_UP(rule); \
  debug_stat.rules_matched++;\
  return true
  
#define DEBUG_DOWN() \
  down_calls++; \
  debug_down()

#define DEBUG_UP(rule) \
  up_calls++; \
  debug_up(rule)
  
// control
constexpr Token_id Pyrope_id_if     = 128;
constexpr Token_id Pyrope_id_else   = 129;
constexpr Token_id Pyrope_id_for    = 130;
constexpr Token_id Pyrope_id_while  = 131;
constexpr Token_id Pyrope_id_elif   = 132;
constexpr Token_id Pyrope_id_return = 133;
constexpr Token_id Pyrope_id_unique = 134;
constexpr Token_id Pyrope_id_when   = 135;
// type
constexpr Token_id Pyrope_id_as = 136;
constexpr Token_id Pyrope_id_is = 137;
// Debug
constexpr Token_id Pyrope_id_I       = 138;
constexpr Token_id Pyrope_id_N       = 139;
constexpr Token_id Pyrope_id_yield   = 140;
constexpr Token_id Pyrope_id_waitfor = 141;
// logic op
constexpr Token_id Pyrope_id_and = 142;
constexpr Token_id Pyrope_id_or  = 143;
constexpr Token_id Pyrope_id_not = 144;
// Range ops
constexpr Token_id Pyrope_id_intersect = 145;
constexpr Token_id Pyrope_id_union     = 145;
constexpr Token_id Pyrope_id_until     = 146;
constexpr Token_id Pyrope_id_in        = 147;
constexpr Token_id Pyrope_id_by        = 148;

// new
constexpr Token_id Pyrope_id_try = 149;
constexpr Token_id Pyrope_id_punch = 150;
constexpr Token_id Pyrope_id_assertion = 151;
constexpr Token_id Pyrope_id_negation = 152;
constexpr Token_id Pyrope_id_tilde = 153;
constexpr Token_id Pyrope_id_xor  = 154;

class Prp : public Elab_scanner {
protected:
  struct debug_statistics{
    uint16_t rules_called;
    uint16_t rules_matched;
    uint16_t rules_failed;
    uint16_t tokens_consumed;
    uint16_t tokens_unconsumed;
    uint16_t ast_up_calls;
    uint16_t ast_down_calls;
    uint16_t ast_add_calls;
  };
  
  debug_statistics debug_stat{0,0,0,0,0,0,0,0};
  std::vector<std::string> ast_call_trace;
  
  std::vector<std::tuple<uint64_t, uint8_t, Rule_id, Token_entry>> add_stack;
  std::vector<std::tuple<uint64_t, uint8_t, Rule_id, Token_entry>> down_stack;
  std::vector<std::tuple<uint64_t, uint8_t, Rule_id, Token_entry>> subtree_stack;
  std::vector<std::tuple<uint32_t, uint32_t>> subtree_indices;
  
  uint64_t tokens_consumed = 0;
  uint64_t subtree_index = 0;
  
  std::unique_ptr<Ast_parser> ast;
  absl::flat_hash_map<std::string, Token_id> pyrope_keyword;
  
  enum Prp_rules: Rule_id {
    Prp_invalid = 0,
    Prp_rule,
    Prp_rule_start,
    Prp_rule_code_blocks,
    Prp_rule_code_block_int,
    Prp_rule_if_statement,
    Prp_rule_else_statement,
    Prp_rule_for_statement,
    Prp_rule_while_statement,
    Prp_rule_try_statement,
    Prp_rule_punch_format,
    Prp_rule_function_pipe,
    Prp_rule_fcall_explicit,
    Prp_rule_fcall_implicit, 
    Prp_rule_for_index,
    Prp_rule_assignment_expression,
    Prp_rule_logical_expression,
    Prp_rule_relational_expression,
    Prp_rule_additive_expression,
    Prp_rule_bitwise_expression,
    Prp_rule_multiplicative_expression,
    Prp_rule_unary_expression,
    Prp_rule_factor,
    Prp_rule_tuple_by_notation,
    Prp_rule_tuple_notation_no_bracket,
    Prp_rule_tuple_notation,
    Prp_rule_tuple_notation_with_object,
    Prp_rule_range_notation,
    Prp_rule_bit_selection_bracket,
    Prp_rule_bit_selection_notation,
    Prp_rule_tuple_array_bracket,
    Prp_rule_tuple_array_notation,
    Prp_rule_lhs_expression,
    Prp_rule_lhs_var_name,
    Prp_rule_rhs_expression_property,
    Prp_rule_rhs_expression,
    Prp_rule_identifier,
    Prp_rule_constant,
    Prp_rule_assignment_operator,
    Prp_rule_tuple_dot_notation,
    Prp_rule_tuple_dot_dot,
    Prp_rule_overload_notation,
    Prp_rule_scope_else,
    Prp_rule_scope_body,
    Prp_rule_scope_declaration,
    Prp_rule_scope,
    Prp_rule_scope_condition,
    Prp_rule_scope_argument,
    Prp_rule_punch_rhs,
    Prp_rule_fcall_arg_notation,
    Prp_rule_return_statement,
    Prp_rule_compile_check_statement,
    Prp_rule_block_body,
    Prp_rule_empty_scope_colon,
    Prp_rule_assertion_statement,
    Prp_rule_negation_statement,
    Prp_rule_scope_colon,
    Prp_rule_numerical_constant,
    Prp_rule_string_constant,
    Prp_rule_overload_name,
    Prp_rule_overload_exception,
    
  };
  
  void elaborate();
  
  void eat_comments();
  
  bool rule_start();
  bool rule_code_blocks();
  bool rule_code_block_int();
  bool rule_if_statement();
  bool rule_else_statement();
  bool rule_for_statement();
  bool rule_while_statement();
  bool rule_try_statement();
  bool rule_punch_format();
  bool rule_function_pipe();
  bool rule_fcall_explicit();
  bool rule_fcall_implicit();
  bool rule_for_index();
  bool rule_assignment_expression();
  bool rule_logical_expression();
  bool rule_relational_expression();
  bool rule_additive_expression();
  bool rule_bitwise_expression();
  bool rule_multiplicative_expression();
  bool rule_unary_expression();
  bool rule_factor();
  bool rule_tuple_by_notation();
  bool rule_tuple_notation_no_bracket();
  bool rule_tuple_notation();
  bool rule_tuple_notation_with_object();
  bool rule_range_notation();
  bool rule_bit_selection_bracket();
  bool rule_bit_selection_notation();
  bool rule_tuple_array_bracket();
  bool rule_tuple_array_notation();
  bool rule_lhs_expression();
  bool rule_lhs_var_name();
  bool rule_rhs_expression_property();
  bool rule_rhs_expression();
  uint8_t rule_identifier();
  bool rule_constant();
  bool rule_assignment_operator();
  bool rule_tuple_dot_notation();
  bool rule_tuple_dot_dot();
  bool rule_overload_notation();
  bool rule_overload_name();
  bool rule_overload_exception();
  bool rule_scope_else();
  bool rule_scope_body();
  bool rule_scope_declaration();
  bool rule_scope();
  bool rule_scope_condition();
  bool rule_scope_argument();
  bool rule_punch_rhs();
  bool rule_fcall_arg_notation();
  bool rule_return_statement();
  bool rule_compile_check_statement();
  bool rule_block_body();
  bool rule_empty_scope_colon();
  bool rule_assertion_statement();
  bool rule_negation_statement();
  bool rule_scope_colon();
  bool rule_numerical_constant();
  bool rule_string_constant();
  
  bool debug_unconsume();
  bool debug_consume();
  bool go_back(uint64_t num_tok);
  void debug_up(Rule_id rid);
  void debug_down();
  void debug_add(Rule_id rid, Token_entry token);
  bool chk_and_consume(Token_id tok, Rule_id rid=Prp_invalid);
  std::string rule_id_to_string(Rule_id rid);
  std::string tok_id_to_string(Token_id tok);
  
  void ast_handler();
  void ast_builder();
  void ast_trimmer();
  void process_ast();
  
public:
  Prp() {
    pyrope_keyword["if"]     = Pyrope_id_if;
    pyrope_keyword["else"]   = Pyrope_id_else;
    pyrope_keyword["for"]    = Pyrope_id_for;
    pyrope_keyword["while"]  = Pyrope_id_while;  // FUTURE
    pyrope_keyword["elif"]   = Pyrope_id_elif;
    pyrope_keyword["return"] = Pyrope_id_return;
    pyrope_keyword["unique"] = Pyrope_id_unique;
    pyrope_keyword["when"]   = Pyrope_id_when;
    pyrope_keyword["try"]    = Pyrope_id_try;

    pyrope_keyword["as"] = Pyrope_id_as;
    pyrope_keyword["is"] = Pyrope_id_is;

    pyrope_keyword["and"] = Pyrope_id_and;
    pyrope_keyword["or"]  = Pyrope_id_or;
    pyrope_keyword["not"] = Pyrope_id_not;
    pyrope_keyword["xor"] = Pyrope_id_xor;

    pyrope_keyword["I"]       = Pyrope_id_I;
    pyrope_keyword["N"]       = Pyrope_id_N;
    pyrope_keyword["yield"]   = Pyrope_id_yield;
    pyrope_keyword["waitfor"] = Pyrope_id_waitfor;

    pyrope_keyword["intersect"] = Pyrope_id_intersect;
    pyrope_keyword["union"]     = Pyrope_id_union;
    pyrope_keyword["until"]     = Pyrope_id_until;
    pyrope_keyword["in"]        = Pyrope_id_in;
    pyrope_keyword["by"]        = Pyrope_id_by;
    pyrope_keyword["punch"]     = Pyrope_id_punch;
    
    pyrope_keyword["I"] = Pyrope_id_assertion;
    pyrope_keyword["N"] = Pyrope_id_negation;
    pyrope_keyword["~"] = Pyrope_id_tilde;
  }
};
