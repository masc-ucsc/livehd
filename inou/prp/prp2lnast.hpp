//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "tree_sitter/api.h"

#include <functional>
#include <stack>
#include <vector>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"

class Prp2lnast {
protected:

  // TS Parsing
  std::string prp_file;
  TSParser   *parser;
  TSNode      ts_root_node;

  // AST States
  enum class Expression_state { Type, Lvalue, Rvalue, Const };

  std::stack<Expression_state> expr_state_stack;

  // Top
  void process_description();
  
  // Statements
  void process_statement(TSTreeCursor*);

  // Non-terminal rules
  void process_node(TSNode);

  // Assignment/Declaration
  void process_assignment(TSNode);
  void process_assignment_or_declaration(TSNode);

  // Expressions
  void process_binary_expression(TSNode);
  void process_dot_expression(TSNode);
  void process_member_selection(TSNode);

  // Basics
  void process_tuple(TSNode);
  void process_tuple_or_expression_list(TSNode);
  void process_lvalue_list(TSNode);
  void process_rvalue_list(TSNode);
  void process_identifier(TSNode);
  void process_simple_number(TSNode);

  // Lnast Tree Helpers
  std::unique_ptr<Lnast> lnast;
  mmap_lib::Tree_index stmts_index;
  std::stack<Lnast_node> rvalue_node_stack;
  std::vector<int> tuple_lvalue_positions;
  std::stack<std::vector<Lnast_node>> tuple_rvalue_stack;
  std::stack<Lnast_node> primary_node_stack;
  std::stack<std::vector<Lnast_node>> member_select_stack;
  
  // Lnast_node Helpers
  int tmp_ref_count;
  mmap_lib::str get_tmp_name();
  inline Lnast_node get_tmp_ref();

  // TS API Helpers
  mmap_lib::str get_text(const TSNode &node) const;
  inline TSNode get_child(const TSNode &, const char*) const;
  inline TSNode get_child(const TSNode &) const;
  inline TSNode get_sibling(const TSNode &) const;
  inline TSNode get_named_child(const TSNode &) const ;
  inline TSNode get_named_sibling(const TSNode &) const;
public:
  Prp2lnast(const mmap_lib::str filename, const mmap_lib::str module_name);

  ~Prp2lnast();

  std::unique_ptr<Lnast> get_lnast() {
    return std::move(lnast);
  }
  void dump_tree_sitter() const;
  void dump_tree_sitter(TSTreeCursor *tc, int level) const;
  void dump() const;
};

