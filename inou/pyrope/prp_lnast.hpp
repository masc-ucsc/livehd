#include <queue>

#include "lnast.hpp"
#include "lnast_ntype.hpp"
#include "prp.hpp"

class Prp_lnast : public Prp {
protected:
  std::unique_ptr<Lnast> lnast;  // translate the AST to lnAST
  // this is equal to the index of the statements node the current rule adds to
  mmap_lib::Tree_index cur_stmts = mmap_lib::Tree_index(-1, -1);

  absl::flat_hash_map<std::string, Lnast_node> operator_map; // unused for now
  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, uint8_t> priority_map;
  absl::flat_hash_set<Rule_id> expr_rules;

  // std::list<std::string> temp_vars;
  std::string current_temp_var = "___a";
  uint64_t    current_seq      = 0;
  void        get_next_temp_var();

  void       translate_code_blocks(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln,
                                   Rule_id term_rule = Prp_invalid);
  Lnast_node eval_rule(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  
  // rules that don't produce an RHS expression
  void eval_assignment_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_if_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_for_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_while_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_fcall_arg_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_for_index(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);

  // rules that produce an RHS expression
  Lnast_node eval_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_for_in_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple_array_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_fcall_explicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_fcall_implicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple_dot_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_bit_selection_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_fluid_ref(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_range_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_scope_declaration(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  // FIXME: support implicit function calls

  Lnast_node  gen_operator(mmap_lib::Tree_index idx, uint8_t *skip_sibs);
  inline bool is_expr(mmap_lib::Tree_index idx);
  inline uint8_t maybe_child_expr(mmap_lib::Tree_index idx);
  inline void create_simple_lhs_expr(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Lnast_node rhs_node);
  inline Lnast_node create_const_node(mmap_lib::Tree_index idx);
  inline bool       is_decimal(std::string_view number);

  inline void generate_op_map();
  inline void generate_expr_rules();
  inline void generate_priority_map();

  // debugging
  void print_ast_node(mmap_lib::Tree_index idx);

public:
  std::string Lnast_type_to_string(Lnast_ntype type);

  std::unique_ptr<Lnast> prp_ast_to_lnast(std::string_view module_name);
};
