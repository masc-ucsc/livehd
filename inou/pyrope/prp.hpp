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

#include <string>

#include "absl/container/flat_hash_map.h"

#include "ast.hpp"
#include "elab_scanner.hpp"

// using node_data = std::tuple<Rule_id, Token_entry>;

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

class Prp : public Elab_scanner {
protected:
  std::unique_ptr<Ast_parser> ast;
  absl::flat_hash_map<std::string, Token_id> pyrope_keyword;
  
  enum Prp_rules: Rule_id {
    Prp_invalid = 0,
    Prp_rule,
    Prp_rule_top,
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
  };
  
  void elaborate();
  
  void eat_comments();
  
  bool rule_top();
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
	bool rule_identifier();
	bool rule_constant();
	bool rule_assignment_operator();
  bool rule_tuple_dot_notation();
  bool rule_tuple_dot_dot();
  bool rule_overload_notation();
  bool rule_scope_else();
  bool rule_scope_body();
  bool rule_scope_declaration();
  bool rule_scope();
  bool rule_scope_condition();
  bool rule_scope_argument();
  bool rule_punch_rhs();
  bool rule_fcall_arg_notation();
  
  bool debug_unconsume();
  bool debug_consume();
  bool go_back(int num_tok);
  void debug_up(Rule_id rid);
  void debug_down();
  
  void ast_handler();
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
  }
};
