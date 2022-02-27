//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <functional>
#include <stack>
#include <vector>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"
#include "tree_sitter/api.h"

class Prp2lnast {
protected:
  // TS Parsing
  std::string prp_file;
  TSParser   *parser;
  TSNode      ts_root_node;

  // AST States
  enum class Expression_state { Type, Lvalue, Rvalue, Const, Decl };
  std::stack<Expression_state> expr_state_stack;

  // FIXME: Temporary fix for generating input/output refs
  bool                                          is_function_input;
  bool                                          is_function_output;
  absl::flat_hash_map<std::string, std::string> ref_name_map;

  // Top
  void process_description();

  // Statements
  void process_statement(TSTreeCursor *);

  // Non-terminal rules
  void process_node(TSNode);

  // Statements
  void process_scope_statement(TSNode);
  void process_if_statement(TSNode);
  void process_for_statement(TSNode);
  void process_while_statement(TSNode);

  // Assignment/Declaration
  void process_assignment_or_declaration(TSNode);

  // Expressions
  void process_binary_expression(TSNode);
  void process_dot_expression(TSNode);
  void process_member_selection(TSNode);
  void process_function_definition(TSNode);

  // Select
  void process_select(TSNode);
  void process_member_select(TSNode);

  // Basics
  void process_tuple(TSNode);
  void process_tuple_or_expression_list(TSNode);
  void process_lvalue_list(TSNode);
  void process_rvalue_list(TSNode);
  void process_tuple_type_list(TSNode);
  void process_declaration_list(TSNode);
  void process_identifier(TSNode);
  void process_simple_number(TSNode);

  // Lnast Tree Helpers
  std::unique_ptr<Lnast>              lnast;
  lh::Tree_index                      stmts_index;
  std::stack<Lnast_node>              rvalue_node_stack;
  std::vector<int>                    tuple_lvalue_positions;
  std::stack<std::vector<Lnast_node>> tuple_rvalue_stack;
  std::stack<Lnast_node>              primary_node_stack;
  std::stack<std::vector<Lnast_node>> select_stack;

  // Lnast_node Helpers
  // TODO: Forward location to Lnast_node
  int               tmp_ref_count;
  std::string       get_tmp_name();
  inline Lnast_node get_tmp_ref();

  // TS API Helpers
  std::string_view get_text(const TSNode &node) const;
  inline TSNode    get_child(const TSNode &, const char *) const;
  inline TSNode    get_child(const TSNode &, unsigned int) const;
  inline TSNode    get_child(const TSNode &) const;
  inline TSNode    get_sibling(const TSNode &) const;
  inline TSNode    get_named_child(const TSNode &) const;
  inline TSNode    get_named_sibling(const TSNode &) const;

public:
  Prp2lnast(std::string_view filename, std::string_view module_name);

  ~Prp2lnast();

  std::unique_ptr<Lnast> get_lnast() { return std::move(lnast); }
  void                   dump_tree_sitter() const;
  void                   dump_tree_sitter(TSTreeCursor *tc, int level) const;
  void                   dump() const;
};
