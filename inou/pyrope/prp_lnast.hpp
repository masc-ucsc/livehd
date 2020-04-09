#include "prp.hpp"
#include "lnast_ntype.hpp"
#include "lnast.hpp"

class Prp_lnast : public Prp {
protected:
  std::unique_ptr<Lnast> lnast; // translate the AST to lnAST
  // this is equal to the index of the statements node the current rule adds to
  mmap_lib::Tree_index cur_stmts = mmap_lib::Tree_index(-1,-1);
  // the node and a number that says how far to travel down the right of the tree before find the parent of the new node
  std::list<std::tuple<Lnast_node, uint16_t>> lnast_buffer;
  
  std::list<std::string> temp_vars;
  std::string current_temp_var = "___a";
  void get_next_temp_var();
  
  void translate_code_blocks(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Rule_id term_rule=Prp_invalid);
  Lnast_node eval_rule(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  
  // rules that don't produce an RHS expression
  void eval_assignment_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_if_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_for_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_range_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_while_statement(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_scope_declaration(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  void eval_fcall_arg_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  
  // rules that produce an RHS expression
  Lnast_node eval_expression(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_for_in_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple_array_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_fcall_explicit(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  Lnast_node eval_tuple_dot_notation(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln);
  
  inline Lnast_node gen_operator(mmap_lib::Tree_index idx, uint8_t *skip_sibs);
  inline bool is_expr(mmap_lib::Tree_index idx);
  inline bool maybe_child_expr(mmap_lib::Tree_index idx);
  inline void create_simple_lhs_expr(mmap_lib::Tree_index idx_start_ast, mmap_lib::Tree_index idx_start_ln, Lnast_node rhs_node);
  std::string Lnast_type_to_string(Lnast_ntype type);
  
public:
  std::unique_ptr<Lnast> prp_ast_to_lnast();
};
