//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "tree_sitter/api.h"

#include <stack>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "symbol_table.hpp"

struct Prp_tuple_item {
public:
  mmap_lib::str ref_name;
  bool is_named;
};

using Prp_tuple = mmap_lib::tree<Prp_tuple_item>;

class Prp2lnast {
protected:

  // TS Parsing
  std::string prp_file;
  TSParser   *parser;
  TSNode      ts_root_node;

  // AST States
  enum class Expression_state { Type, Lvalue, Rvalue };
  enum class Identifier_state { Set, Get, None };

  std::stack<Identifier_state> id_state_stack;

  // Tuple Handling (Matching)
  // TODO: change these to stack to allow recursive assignments
  // Prp_tuple lvalue_tuple;
  // Prp_tuple rvalue_tuple;

  // Top
  void process_description();
  
  // Statements
  void process_statement(TSTreeCursor*);

  // Non-terminal rules
  void process_node(TSNode);
  void process_each_child(TSNode);
  void process_each_named_child(TSNode);

  // Assignment/Declaration
  void process_assignment(TSNode);
  void process_declaration(TSNode);

  // Expressions
  void process_expression_list(TSNode);
  void process_binary_expression(TSNode);

  // Basics
  void process_tuple(TSNode);
  void process_identifier(TSNode);
  void process_simple_number(TSNode);

  // Lnast Tree Helpers
  std::unique_ptr<Lnast> lnast;
  mmap_lib::Tree_index stmts_index;
  std::stack<Lnast_node> primary_node_stack; // ref or const

  // Lnast_node Helpers
  int tmp_ref_count;
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

