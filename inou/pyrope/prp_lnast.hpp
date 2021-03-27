//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <array>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "prp.hpp"

class Prp_lnast : public Prp {
protected:
  std::unique_ptr<Lnast> lnast;  // translate the AST to lnAST

  bool       in_lhs;
  Lnast_node in_lhs_rhs_node;

  mmap_lib::Tree_index cur_stmts = mmap_lib::Tree_index(-1, -1);

  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, uint8_t> priority_map;
  absl::flat_hash_set<Rule_id>                               expr_rules;

  // std::list<std::string> temp_vars;
  std::string last_temp_var;
  Lnast_node  current_return_node;

  std::string get_temp_string();
  Lnast_node  get_lnast_temp_ref();

  void translate_code_blocks(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Rule_id term_rule = Prp_invalid,
                             bool check_return_stmt = false);
  Lnast_node eval_rule(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);

  // rules that don't produce an RHS expression
  void eval_assignment_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_if_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_for_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_while_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_fcall_arg_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_for_index(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_range_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);

  // rules that produce an RHS expression
  Lnast_node eval_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple(const mmap_lib::Tree_index &idx_start_ast, const mmap_lib::Tree_index &idx_start_ln,
                        mmap_lib::Tree_index idx_pre_tuple_vals  = mmap_lib::Tree_index(-1, -1),
                        mmap_lib::Tree_index idx_post_tuple_vals = mmap_lib::Tree_index(-1, -1));
  Lnast_node eval_for_in_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple_array_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_fcall_explicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                 mmap_lib::Tree_index idx_piped_val = mmap_lib::Tree_index(-1, -1),
                                 Lnast_node piped_node = Lnast_node(), Lnast_node name_node = Lnast_node());
  Lnast_node eval_fcall_implicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                 mmap_lib::Tree_index idx_piped_val = mmap_lib::Tree_index(-1, -1),
                                 Lnast_node piped_node = Lnast_node(), Lnast_node name_node = Lnast_node());
  Lnast_node eval_tuple_dot_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_bit_selection_notation(mmap_lib::Tree_index idx_start_ast, const Lnast_node &lhs_node);
  Lnast_node eval_fluid_ref(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_scope_declaration(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                    Lnast_node name_node = Lnast_node());
  Lnast_node eval_sub_expression(mmap_lib::Tree_index idx_start_ast, Lnast_node operator_node);

  void       add_tuple_nodes(mmap_lib::Tree_index idx_start_ln, std::vector<std::array<Lnast_node, 3>> &tuple_nodes);
  Lnast_node evaluate_all_tuple_nodes(const mmap_lib::Tree_index &idx_start_ast, const mmap_lib::Tree_index &idx_start_ln);

  Lnast_node     gen_operator(mmap_lib::Tree_index idx, uint8_t *skip_sibs);
  inline bool    is_expr(mmap_lib::Tree_index idx);
  inline bool    is_expr_with_operators(mmap_lib::Tree_index idx);
  inline uint8_t maybe_child_expr(mmap_lib::Tree_index idx);
  inline void    create_simple_lhs_expr(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Lnast_node rhs_node);
  inline Lnast_node create_const_node(mmap_lib::Tree_index idx);
  inline bool       is_decimal(std::string_view number);

  inline void generate_op_map();
  inline void generate_expr_rules();
  inline void generate_priority_map();

  // debugging
  void dump(mmap_lib::Tree_index idx) const;

public:
  Prp_lnast();

  std::unique_ptr<Lnast> prp_ast_to_lnast(std::string_view module_name);
};
