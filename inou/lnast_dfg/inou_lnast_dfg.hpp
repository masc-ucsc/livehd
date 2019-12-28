//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "pass.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

class Inou_lnast_dfg : public Pass {
private:
  std::string_view         memblock;
  std::unique_ptr<Lnast>   lnast;

  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, Node_Type_Op> primitive_type_lnast2lg;
  absl::flat_hash_map<std::string, Node_pin>   name2dpin; //record dpin instead of node because the asymmetry between gio and normal node  ...
  int                    lginp_cnt;
  int                    lgout_cnt;

protected:
  void                          setup_memblock();
  std::vector<LGraph *>         do_tolg();
  std::vector<LGraph *>         do_gen_temp_lg();

  void  process_ast_top            (LGraph *dfg);
  void  process_ast_statements     (LGraph *dfg, const mmap_lib::Tree_index &stmt_parent);
  void  process_ast_pure_assign_op (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_binary_op      (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_unary_op       (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_logical_op     (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_as_op          (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_label_op       (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_if_op          (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_uif_op         (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_func_call_op   (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_func_def_op    (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_sub_op         (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_for_op         (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_while_op       (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  void  process_ast_dp_assign_op   (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);

  Node_pin     setup_node_operator_and_target (LGraph *dfg, const mmap_lib::Tree_index &ast_op_idx);
  Node_pin     setup_node_pure_assign_and_target (LGraph *dfg, const mmap_lib::Tree_index &ast_op_idx);
  Node_pin     setup_node_operand  (LGraph *dfg, const mmap_lib::Tree_index &ast_idx);
  Node_Type_Op decode_lnast_op    (const mmap_lib::Tree_index &ast_op_idx);
  void         setup_lnast_to_lgraph_primitive_type_mapping();

  static void   build_lnast(Inou_lnast_dfg &p, Eprp_var &var);

  // eprp callbacks
  static void   tolg(Eprp_var &var);
  static void   gen_temp_lg(Eprp_var &var);
public:
  Inou_lnast_dfg(const Eprp_var &var);

  static void   setup();
};

