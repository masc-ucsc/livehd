//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include <functional>
#include <stack>
#include <string>
#include <string_view>
#include <vector>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "tree_sitter/api.h"

class Prp2lnast {
protected:
  // TS Parsing
  std::string prp_file;
  TSParser*   parser;
  TSNode      ts_root_node;

  // LNAST output
  std::unique_ptr<Lnast> lnast;
  lh::Tree_index         stmts_index;

  // Temporary variable counter for compiler-generated tmps
  int tmp_ref_count;

  // Directed name tracking for function input/output refs ($a / %x)
  enum class Name_role { None, FuncInput, FuncOutput, Register };
  Name_role                                     current_name_role;
  absl::flat_hash_map<std::string, std::string> ref_name_map;

  // Top
  void process_description();

  // Statements
  void process_statement(TSNode n);
  void process_scope_statement(TSNode n, lh::Tree_index target_stmts);
  void process_assignment(TSNode n);
  void process_declaration_statement(TSNode n);
  void process_assert_statement(TSNode n);
  void process_while_statement(TSNode n);
  void process_for_statement(TSNode n);
  void process_loop_statement(TSNode n);
  void process_control_statement(TSNode n);
  void process_function_call_statement(TSNode n);
  void process_lambda_statement(TSNode n);
  void process_enum_assignment(TSNode n);
  void process_type_statement(TSNode n);
  void process_import_statement(TSNode n);
  void process_test_statement(TSNode n);
  void process_spawn_statement(TSNode n);
  void process_impl_statement(TSNode n);

  // Expressions: returns an Lnast_node (ref or const) naming the result
  Lnast_node expr_to_node(TSNode n);
  Lnast_node binary_expr_to_node(TSNode n);
  Lnast_node unary_expr_to_node(TSNode n);
  Lnast_node if_expr_to_node(TSNode n);
  Lnast_node match_expr_to_node(TSNode n);
  Lnast_node bit_selection_to_node(TSNode n);
  Lnast_node member_selection_to_node(TSNode n);
  Lnast_node dot_expression_to_node(TSNode n);
  Lnast_node function_call_expr_to_node(TSNode n);
  Lnast_node tuple_to_node(TSNode n, bool is_square);
  Lnast_node identifier_to_node(TSNode n, bool for_lvalue);
  Lnast_node constant_text_to_node(std::string_view text);
  Lnast_node type_specification_to_node(TSNode n);

  // Type handling
  void emit_type_spec(const Lnast_node& target, TSNode type_cast_node);
  void emit_attribute_list(const Lnast_node& target, TSNode attribute_list_node);
  void emit_type_expr(const lh::Tree_index& type_index, TSNode type_node);

  // Lvalue helpers
  Lnast_node process_lvalue_for_assign(TSNode lvalue, const Lnast_node& rvalue, TSNode decl_node, TSNode type_cast_node);

  // Helpers
  std::string      get_tmp_name();
  Lnast_node       make_tmp_ref();
  std::string_view get_text(const TSNode& n) const;
  std::string      get_text_str(const TSNode& n) const { return std::string(get_text(n)); }
  static std::string trim(std::string_view s);
  std::string_view text_between(uint32_t start, uint32_t end) const;

  // Get rvalue text even when the rvalue field is a hidden token (numbers,
  // bool/string literals, '?'). Returns the text between the '=' operator's
  // end and the end of the enclosing parent span.
  std::string rvalue_text_fallback(TSNode parent, TSNode after_field) const;
  // Get binary_expression right operand text when hidden.
  std::string binary_right_text(TSNode bin, TSNode operator_node) const;
  // Get binary_expression left operand text when hidden.
  std::string binary_left_text(TSNode bin, TSNode operator_node) const;

  // TS API helpers
  inline TSNode   child_by_field(const TSNode& n, const char* field) const;
  inline uint32_t child_count(const TSNode& n) const { return ts_node_child_count(n); }
  inline TSNode   child(const TSNode& n, uint32_t i) const { return ts_node_child(n, i); }
  inline TSNode   named_child(const TSNode& n, uint32_t i) const { return ts_node_named_child(n, i); }
  inline uint32_t named_child_count(const TSNode& n) const { return ts_node_named_child_count(n); }

public:
  Prp2lnast(std::string_view filename, std::string_view module_name, bool parse_only);

  ~Prp2lnast();

  std::unique_ptr<Lnast> get_lnast() { return std::move(lnast); }
  void                   dump_tree_sitter() const;
  void                   dump_tree_sitter(TSTreeCursor* tc, int level) const;
  void                   dump() const;
};
