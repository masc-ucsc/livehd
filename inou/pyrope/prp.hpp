//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fstream>
#include <string>
#include <tuple>

#include "absl/container/flat_hash_map.h"
#include "ast.hpp"
#include "elab_scanner.hpp"

#define OUTPUT_AST
#define OUTPUT_LN

#define DEBUG_AST
//#define DEBUG_LN

// variable argument number macro (adapted from stackoverflow.com/questions/3046889/optional-parameters-with-c-macros)
#define SCAN_IS_TOKEN_1_ARGS(tok) chk_and_consume(tok, Prp_invalid, &sub_cnt, loc_list)

#define SCAN_IS_TOKEN_2_ARGS(tok, rid) chk_and_consume(tok, rid, &sub_cnt, loc_list)

#define GET_3RD_ARG(arg1, arg2, arg3, ...) arg3
#define SCAN_IS_TOKEN_MACRO_CHOOSER(...) GET_3RD_ARG(__VA_ARGS__, SCAN_IS_TOKEN_2_ARGS, SCAN_IS_TOKEN_1_ARGS)

#define SCAN_IS_TOKEN(...) SCAN_IS_TOKEN_MACRO_CHOOSER(__VA_ARGS__)(__VA_ARGS__)

#ifdef DEBUG_LN
#define PRINT_DBG_LN(...) fmt::print(__VA_ARGS__)
#else
#define PRINT_DBG_LN(...)
#endif

#ifdef DEBUG_AST
#define PRINT_DBG_AST(...) fmt::print(__VA_ARGS__)
#else
#define PRINT_DBG_AST(...)
#endif

#ifdef OUTPUT_AST
#define PRINT_AST(...) fmt::print(__VA_ARGS__)
#else
#define PRINT_AST(...)
#endif

#ifdef OUTPUT_LN
#define PRINT_LN(...) fmt::print(__VA_ARGS__)
#else
#define PRINT_LN(...)
#endif

// function-like macros
#ifdef DEBUG_AST
#define INIT_FUNCTION(...)                                                       \
  debug_stat.rules_called++;                                                     \
  rule_call_stack.push_back(__VA_ARGS__);                                        \
  print_rule_call_stack();                                                       \
  auto                                        starting_tokens = tokens_consumed; \
  auto                                        starting_line   = cur_line;        \
  uint64_t                                    sub_cnt         = 0;               \
  std::list<std::tuple<Rule_id, Token_entry>> loc_list
#else
#define INIT_FUNCTION(...)                                                       \
  auto                                        starting_tokens = tokens_consumed; \
  auto                                        starting_line   = cur_line;        \
  uint64_t                                    sub_cnt         = 0;               \
  std::list<std::tuple<Rule_id, Token_entry>> loc_list
#endif

#ifdef DEBUG_AST
#define RULE_FAILED(...)                                                        \
  fmt::print(__VA_ARGS__);                                                      \
  rule_call_stack.pop_back();                                                   \
  print_rule_call_stack();                                                      \
  if (loc_list.size() > 0) {                                                    \
    PRINT_DBG_AST("tokens_consumed: {}.\n", tokens_consumed - starting_tokens); \
    print_loc_list(loc_list);                                                   \
  } else {                                                                      \
    fmt::print("No problem; loc_list had nothing in it.\n");                    \
  }                                                                             \
  go_back(tokens_consumed - starting_tokens);                                   \
  cur_line = starting_line;                                                     \
  debug_stat.rules_failed++;                                                    \
  return false
#else
#define RULE_FAILED(...)                      \
  go_back(tokens_consumed - starting_tokens); \
  cur_line = starting_line;                   \
  return false
#endif

#ifdef DEBUG_AST
#define RULE_SUCCESS(message, rule)    \
  fmt::print(message);                                                                                                  \
  rule_call_stack.pop_back();                                                                                           \
  print_rule_call_stack();                                                                                              \
  debug_stat.rules_matched++;                                                                                           \
  fmt::print("Rule {} had a sub_cnt of {}.\n", rule_id_to_string(rule), sub_cnt);                                       \
  if (sub_cnt > 1) {                                                                                                    \
    fmt::print("Had a subtree of at least size two in rule {} it was of size {}.\n", rule_id_to_string(rule), sub_cnt); \
    loc_list.push_front(std::make_tuple(0, 0));                                                                         \
    loc_list.push_back(std::make_tuple(rule, 0));                                                                       \
  }                                                                                                                     \
  pass_list.splice(pass_list.end(), loc_list);                                                                          \
  return true
#else
#define RULE_SUCCESS(message, rule)               \
  if (sub_cnt > 1) {                              \
    loc_list.emplace_front(0, 0);   \
    loc_list.emplace_back(rule, 0); \
  }                                               \
  pass_list.splice(pass_list.end(), loc_list);    \
  return true
#endif

#define CHECK_RULE(func) check_function(func, &sub_cnt, loc_list)

#define INIT_PSEUDO_FAIL()    \
  uint64_t cur_tokens;        \
  uint64_t cur_loc_list_size; \
  uint64_t sub_cnt_start;     \
  uint64_t lines_start

#define UPDATE_PSEUDO_FAIL()           \
  cur_tokens        = tokens_consumed; \
  cur_loc_list_size = loc_list.size(); \
  sub_cnt_start     = sub_cnt;         \
  lines_start       = cur_line

#define PSEUDO_FAIL()                    \
  go_back(tokens_consumed - cur_tokens); \
  loc_list.resize(cur_loc_list_size);    \
  sub_cnt = sub_cnt_start; \
  cur_line = lines_start

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
constexpr Token_id Pyrope_id_c         = 153;
constexpr Token_id Pyrope_id_assertion = 138;
constexpr Token_id Pyrope_id_negation  = 139;
constexpr Token_id Pyrope_id_yield     = 140;
constexpr Token_id Pyrope_id_waitfor   = 141;
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
constexpr Token_id Pyrope_id_try   = 149;
constexpr Token_id Pyrope_id_punch = 150;
constexpr Token_id Pyrope_id_tilde = 151;
constexpr Token_id Pyrope_id_xor   = 152;
// boolean constants
constexpr Token_id Pyrope_id_true  = 154;
constexpr Token_id Pyrope_id_TRUE  = 155;
constexpr Token_id Pyrope_id_false = 156;
constexpr Token_id Pyrope_id_FALSE = 157;
// default keyword
constexpr Token_id Pyrope_id_default = 158;

class Prp : public Elab_scanner {
protected:
  struct debug_statistics {
    uint16_t rules_called;
    uint16_t rules_matched;
    uint16_t rules_failed;
    uint16_t tokens_consumed;
    uint16_t tokens_unconsumed;
    uint16_t ast_up_calls;
    uint16_t ast_down_calls;
    uint16_t ast_add_calls;
  };

  debug_statistics         debug_stat{0, 0, 0, 0, 0, 0, 0, 0};
  std::vector<std::string> ast_call_trace;

  uint64_t tokens_consumed = 0;
  uint64_t subtree_index   = 0;
  uint64_t cur_line        = 0;

  std::unique_ptr<Ast_parser>                ast;
  absl::flat_hash_map<std::string, Token_id> pyrope_keyword;
  std::vector<std::string>                   rule_call_stack;
  uint64_t                                   term_token = 1;

  void elaborate();

  uint8_t rule_start(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_code_blocks(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_code_block_int(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_if_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_else_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_for_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_while_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_try_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_punch_format(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_function_pipe(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_fcall_explicit(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_fcall_implicit(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_for_index(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_assignment_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_logical_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_relational_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_additive_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_bitwise_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_multiplicative_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_unary_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_factor(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_by_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_notation_no_bracket(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_notation_with_object(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_range_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_bit_selection_bracket(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_bit_selection_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_array_bracket(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_array_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_lhs_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_lhs_var_name(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_rhs_expression_property(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_rhs_expression(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_identifier(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_reference(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_constant(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_assignment_operator(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_dot_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_tuple_dot_dot(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_overload_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_overload_name(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_overload_exception(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_scope_else(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_scope_body(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_scope_declaration(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_scope(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_scope_condition(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_scope_argument(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_punch_rhs(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_fcall_arg_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_return_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_compile_check_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_block_body(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_empty_scope_colon(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_assertion_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_negation_statement(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_scope_colon(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_numerical_constant(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_string_constant(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_for_in_notation(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_not_in_implicit(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);
  uint8_t rule_keyword(std::list<std::tuple<Rule_id, Token_entry>> &pass_list);

  inline void check_lb();
  inline bool check_eos();
  inline bool inc_line_cnt();

  inline bool unconsume_token();
  inline bool consume_token();
  bool        go_back(uint64_t num_tok);
  std::string rule_id_to_string(Rule_id rid);
  std::string tok_id_to_string(Token_id tok);

  uint8_t check_function(uint8_t (Prp::*rule)(std::list<std::tuple<Rule_id, Token_entry>> &), uint64_t *sub_cnt,
                         std::list<std::tuple<Rule_id, Token_entry>> &loc_list);
  bool    chk_and_consume(Token_id tok, Rule_id rid, uint64_t *sub_cnt, std::list<std::tuple<Rule_id, Token_entry>> &loc_list);

  void ast_handler();
  void ast_builder(std::list<std::tuple<Rule_id, Token_entry>> &passed_list);

#ifdef DEBUG_AST
  void print_loc_list(std::list<std::tuple<Rule_id, Token_entry>> &loc_list);
  void print_rule_call_stack();
#endif

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

    pyrope_keyword["C"]       = Pyrope_id_c;
    pyrope_keyword["I"]       = Pyrope_id_assertion;
    pyrope_keyword["N"]       = Pyrope_id_negation;
    pyrope_keyword["yield"]   = Pyrope_id_yield;
    pyrope_keyword["waitfor"] = Pyrope_id_waitfor;

    pyrope_keyword["intersect"] = Pyrope_id_intersect;
    pyrope_keyword["union"]     = Pyrope_id_union;
    pyrope_keyword["until"]     = Pyrope_id_until;
    pyrope_keyword["in"]        = Pyrope_id_in;
    pyrope_keyword["by"]        = Pyrope_id_by;
    pyrope_keyword["punch"]     = Pyrope_id_punch;

    pyrope_keyword["false"] = Pyrope_id_false;
    pyrope_keyword["FALSE"] = Pyrope_id_FALSE;
    pyrope_keyword["true"]  = Pyrope_id_true;
    pyrope_keyword["TRUE"]  = Pyrope_id_TRUE;

    pyrope_keyword["default"] = Pyrope_id_default;
  }

  enum Prp_rules : Rule_id {
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
    Prp_rule_reference,
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
    Prp_rule_for_in_notation,
    Prp_rule_not_in_implicit,
    Prp_rule_keyword,
    Prp_rule_sentinel // last rule is a special one for communicating with the LNAST translator
  };
};
