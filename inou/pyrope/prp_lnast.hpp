//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <array>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "prp.hpp"

class Prp_lnast : public Prp {
protected:
  std::unique_ptr<Lnast> lnast;  // translate the AST to lnAST

  bool           in_lhs;
  Lnast_node     in_lhs_rhs_node;
  hhds::Tree::Node_class in_lhs_sel_root;

  hhds::Tree::Node_class cur_stmts = hhds::Tree::Node_class();

  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, uint8_t> priority_map;
  absl::flat_hash_set<Rule_id>                               expr_rules;

  // std::list<std::string> temp_vars;
  int              last_temp_var_counter;
  std::string      last_temp_var;
  Lnast_node       current_return_node;
  const Lnast_node lnast_node_invalid;

  std::string get_temp_string();
  Lnast_node  get_lnast_temp_ref();

  void       translate_code_blocks(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln, Rule_id term_rule = Prp_invalid,
                                   bool check_return_stmt = false);
  Lnast_node eval_rule(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);

  // rules that don't produce an RHS expression
  void eval_assignment_expression(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  void eval_if_statement(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  void eval_for_statement(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  void eval_while_statement(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  void eval_fcall_arg_notation(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  void eval_for_index(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  void eval_range_notation(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);

  // rules that produce an RHS expression
  Lnast_node eval_expression(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  Lnast_node eval_tuple(const hhds::Tree::Node_class& idx_start_ast, const hhds::Tree::Node_class& idx_start_ln,
                        hhds::Tree::Node_class idx_pre_tuple_vals  = hhds::Tree::Node_class(),
                        hhds::Tree::Node_class idx_post_tuple_vals = hhds::Tree::Node_class());
  Lnast_node eval_for_in_notation(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  Lnast_node eval_tuple_array_notation(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  Lnast_node eval_fcall_explicit(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln,
                                 hhds::Tree::Node_class idx_piped_val = hhds::Tree::Node_class(), Lnast_node piped_node = Lnast_node(),
                                 Lnast_node name_node = Lnast_node());
  Lnast_node eval_fcall_implicit(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln,
                                 hhds::Tree::Node_class idx_piped_val = hhds::Tree::Node_class(), Lnast_node piped_node = Lnast_node(),
                                 Lnast_node name_node = Lnast_node());
  Lnast_node eval_tuple_dot_notation(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  Lnast_node eval_bit_selection_notation(hhds::Tree::Node_class idx_start_ast, const Lnast_node& lhs_node);
  Lnast_node eval_fluid_ref(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln);
  Lnast_node eval_scope_declaration(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln, Lnast_node name_node = Lnast_node());
  Lnast_node eval_sub_expression(hhds::Tree::Node_class idx_start_ast, Lnast_node operator_node);

  void       add_tuple_nodes(hhds::Tree::Node_class idx_start_ln, std::vector<std::array<Lnast_node, 3>>& tuple_nodes);
  Lnast_node evaluate_all_tuple_nodes(const hhds::Tree::Node_class& idx_start_ast, const hhds::Tree::Node_class& idx_start_ln);

  Lnast_node        gen_operator(hhds::Tree::Node_class idx, uint8_t* skip_sibs);
  inline bool       is_expr(hhds::Tree::Node_class idx);
  inline bool       is_expr_with_operators(hhds::Tree::Node_class idx);
  inline uint8_t    maybe_child_expr(hhds::Tree::Node_class idx);
  inline void       create_simple_lhs_expr(hhds::Tree::Node_class idx_start_ast, hhds::Tree::Node_class idx_start_ln, Lnast_node rhs_node);
  inline Lnast_node create_const_node(hhds::Tree::Node_class idx);
  inline bool       is_decimal(std::string_view number);

  inline void generate_op_map();
  inline void generate_expr_rules();
  inline void generate_priority_map();

  // debugging
  void dump(hhds::Tree::Node_class idx) const;

public:
  Prp_lnast();

  std::unique_ptr<Lnast> prp_ast_to_lnast(std::string_view module_name);
};
