//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "pass.hpp"
#include "lnast_parser.hpp"

class Inou_lnast_dfg_options {
public:
  std::string files;
  std::string path;
};

class Inou_lnast_dfg : public Pass{
private:
  Inou_lnast_dfg_options opack;
  std::string_view       memblock;     //SH:FIXME:cannot initialize through constructor?
  Lnast_parser           lnast_parser; //SH:FIXME:cannot initialize through constructor?
  Language_neutral_ast   *lnast;       //SH:FIXME:cannot initialize through constructor?

protected:

public:
  Inou_lnast_dfg() : Pass("lnast_dfg") {};
  static void   tolg(Eprp_var &var);
  void          setup() final;

private:
  std::string_view              setup_memblock();
  std::vector<LGraph *>         do_tolg();

  void  process_ast_top            (LGraph *dfg);
  void  process_ast_statements     (LGraph *dfg, const std::vector<Tree_index> &sts);
  void  process_ast_pure_assign_op (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_binary_op      (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_unary_op       (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_logical_op     (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_as_op          (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_label_op       (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_if_op          (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_uif_op         (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_func_call_op   (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_func_def_op    (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_sub_op         (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_for_op         (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_while_op       (LGraph *dfg, const Tree_index &ast_idx);
  void  process_ast_dp_assign_op   (LGraph *dfg, const Tree_index &ast_idx);

  constexpr bool  is_logical_op     (Lnast_ntype_id op) const { return (op == Lnast_ntype_logical_and) or
                                                                       (op == Lnast_ntype_logical_or); }
  constexpr bool  is_binary_op      (Lnast_ntype_id op) const { return (op == Lnast_ntype_and) or
                                                                       (op == Lnast_ntype_or) or
                                                                       (op == Lnast_ntype_xor) or
                                                                       (op == Lnast_ntype_plus) or
                                                                       (op == Lnast_ntype_minus) or
                                                                       (op == Lnast_ntype_mult) or
                                                                       (op == Lnast_ntype_div) or
                                                                       (op == Lnast_ntype_eq) or
                                                                       (op == Lnast_ntype_lt) or
                                                                       (op == Lnast_ntype_le) or
                                                                       (op == Lnast_ntype_gt) or
                                                                       (op == Lnast_ntype_ge); }
  constexpr bool  is_pure_assign_op (Lnast_ntype_id op) const { return (op == Lnast_ntype_pure_assign); }
  constexpr bool  is_as_op          (Lnast_ntype_id op) const { return (op == Lnast_ntype_as); }
  constexpr bool  is_label_op       (Lnast_ntype_id op) const { return (op == Lnast_ntype_label); }
  constexpr bool  is_if_op          (Lnast_ntype_id op) const { return (op == Lnast_ntype_if); }
  constexpr bool  is_uif_op         (Lnast_ntype_id op) const { return (op == Lnast_ntype_uif); }
  constexpr bool  is_func_call_op   (Lnast_ntype_id op) const { return (op == Lnast_ntype_func_call); }
  constexpr bool  is_func_def_op    (Lnast_ntype_id op) const { return (op == Lnast_ntype_func_def); }
  constexpr bool  is_for_op         (Lnast_ntype_id op) const { return (op == Lnast_ntype_for); }
  constexpr bool  is_while_op       (Lnast_ntype_id op) const { return (op == Lnast_ntype_while); }
  constexpr bool  is_dp_assign_op   (Lnast_ntype_id op) const { return (op == Lnast_ntype_dp_assign); }
  constexpr bool  is_unary_op       (Lnast_ntype_id op) const { return false; } //sh:todo
};


