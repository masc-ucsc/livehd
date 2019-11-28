//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "pass.hpp"
#include "lnast_parser.hpp"
#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"


class Inou_lnast_dfg_options {
public:
  std::string files;
  std::string path;
};

class Inou_lnast_dfg : public Pass{
private:
  Inou_lnast_dfg_options   opack;
  Elab_scanner::Token_list token_list;
  std::string_view         memblock;
  Lnast_parser             lnast_parser;
  Lnast                   *lnast;

  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, Node_Type_Op> primitive_type_lnast2lg;
  absl::flat_hash_map<std::string, Node_pin>   name2dpin; //record dpin instead of node because the asymmetry between gio and normal node  ...
  int                    lginp_cnt;
  int                    lgout_cnt;

protected:

public:
  Inou_lnast_dfg() : Pass("lnast_dfg"), lginp_cnt(1), lgout_cnt(0)
  {
    setup_lnast_to_lgraph_primitive_type_mapping();
  };
  static void   tolg(Eprp_var &var);
  static void   build_lnast(Inou_lnast_dfg &p, Eprp_var &var);
  void          setup() final;

private:
  void                          setup_memblock();
  std::vector<LGraph *>         do_tolg();

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

};


