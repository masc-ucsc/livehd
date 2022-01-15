//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "tree_sitter/api.h"

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"

class Prp2lnast {
protected:

  std::unique_ptr<Lnast> lnast;

  std::string prp_file;
  TSParser   *parser;
  TSNode      ts_root_node;

  std::stack<mmap_lib::Tree_index> tree_index; 

  mmap_lib::str get_text(const TSNode &node) const;

  // Top
  void process_description();
  
  // Statements
  void process_statement(TSTreeCursor*);
  void process_assignment_or_declaration_statement(TSTreeCursor*);
  // void process_function_call_statement(TSTreeCursor*);
  // void process_control_statement(TSTreeCursor*);
  // void process_if_statement(TSTreeCursor*);
  // void process_for_statement(TSTreeCursor*);
  // void process_while_statement(TSTreeCursor*);
  // void process_match_statement(TSTreeCursor*);
  // void process_enum_declaration(TSTreeCursor*);
  // void process_type_declaration(TSTreeCursor*);
  // void process_type_extension(TSTreeCursor*);
  void process_expression_statement(TSTreeCursor*);
  // void process_test_statement(TSTreeCursor*);
  // void process_restrict_statement(TSTreeCursor*);

  // Non-terminal rules
  void process_node(TSNode);

  // Expressions
  // void process_selection(TSNode);
  // void process_type_specification(TSNode);
  // void process_type_cast(TSNode);
  // void process_function_call(TSNode);
  // void process_function_definition(TSNode);
  // void process_unary_expression(TSNode);
  // void process_optional_expression(TSNode);
  void process_binary_expression(TSNode);
  // void process_for_expression(TSNode);
  // void process_if_expression(TSNode);
  // void process_match_expression(TSNode);
  // void process_scope_expression(TSNode);

  // Basics
  // void process_tuple(TSNode);
  // void process_identifier(TSNode);
  // void process_constant(TSNode);
  void process_simple_number(TSNode);

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

