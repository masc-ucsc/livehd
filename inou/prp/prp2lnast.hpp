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

  // a::[b] = c::[d = f::[g]]
  // \___________0__________/   <-- 
  //    \1/      \____1_____/   <--
  //                 \0/ \1/    <--
  // 
  std::stack<bool> attr_scope_stack;

  using attr_map_t = std::vector<std::pair<Lnast_node, Lnast_node>>;
  using node_attr_t = std::pair<Lnast_node, attr_map_t>;

  // FIXME: Temporary fix for generating input/output refs
  bool is_function_input;
  bool is_function_output;
  absl::flat_hash_map<std::string, std::string> ref_name_map;

  // TODO: Replace this with Prp_type
  uint16_t bitwidth;

  // Top
  void process_description();

  // Statements
  void process_statement(TSNode);

  // Non-terminal rules
  void process_node(TSNode);

  // Statements
  void process_scope_statement(TSNode);
  void process_expression_statement(TSNode);
  void process_if_statement(TSNode);
  void process_for_statement(TSNode);
  void process_while_statement(TSNode);

  // Functions
  void process_function_call_statement(TSNode);
  void process_simple_function_call(TSNode);
  void process_function_definition(TSNode);

  // Assignment/Declaration
  void process_assignment_or_declaration(TSNode);
  void process_simple_assignment(TSNode);
  void process_simple_declaration(TSNode);

  // Expressions
  void process_binary_expression(TSNode);
  void process_unary_expression(TSNode);
  void process_dot_expression(TSNode);
  void process_member_selection(TSNode);

  // Type
  void process_type_specification(TSNode);
  void process_primitive_type(TSNode);
  void process_unsized_integer_type(TSNode);
  void process_sized_integer_type(TSNode);
  void process_range_type();
  void process_string_type();
  void process_boolean_type();
  void process_type_type();
  void process_array_type(TSNode);

  // Select
  void process_select(TSNode);
  void process_member_select(TSNode);

  // Attributes
  void process_attribute_entry(TSNode);
  void process_attr_list(TSNode);

  // Basics
  void process_tuple(TSNode);
  void process_tuple_or_expression_list(TSNode);
  void process_lvalue_list(TSNode);
  void process_rvalue_list(TSNode);
  void process_tuple_type_list(TSNode);
  void process_declaration_list(TSNode);
  void process_identifier(TSNode);
  void process_constant(TSNode);

  // Ref-Attribute Helpers
  // NOTE: This function adds `ref_node` with all attributes `parent` index
  void add_ref_child(lh::Tree_index parent, node_attr_t ref_node);
  void add_ref_child(lh::Tree_index parent); // NOTE: add `primary_node_stack.top()`
  // NOTE: add `primary_node_stack.top()` if it has any attribute
  void add_ref_child_conditional(lh::Tree_index parent);

  // Lnast Tree Helpers
  std::unique_ptr<Lnast>              lnast;
  lh::Tree_index                      stmts_index;

  lh::Tree_index                      prev_stmt_index;
  std::stack<lh::Tree_index>          tuple_index_stack;

  lh::Tree_index                      type_index;
  std::vector<int>                    tuple_lvalue_positions;
  // NOTE: suboptimal - copying the whole attribute vector
  std::stack<Lnast_node> rvalue_node_stack;
  std::stack<Lnast_node> lvalue_node_stack;
  std::stack<Lnast_node> primary_node_stack;
  std::stack<std::vector<Lnast_node>> select_stack;
  std::stack<std::vector<std::pair<Lnast_node, Lnast_node>>> tuple_rvalue_stack;

  std::stack<std::vector<Lnast_node>> scope_node_stack;

  void add_attr_set(const Lnast_node key, const Lnast_node value);
  Lnast_node add_attr_get(const Lnast_node key);
  void add_attr_check(const Lnast_node key);

  // Lnast_node Helpers
  // TODO: Forward location to Lnast_node
  int               tmp_ref_count;
  std::string       get_tmp_name();
  inline Lnast_node get_tmp_ref();
  
  void enter_scope(Expression_state);
  void leave_scope();
  std::string str(Expression_state);

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
