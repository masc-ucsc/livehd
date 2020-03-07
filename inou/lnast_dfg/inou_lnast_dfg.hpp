//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lnast.hpp"
#include "pass.hpp"
#include "likely.hpp"
#include "pass_bitwidth.hpp"

class Inou_lnast_dfg : public Pass {
private:
  Lnast *lnast;

  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, Node_Type_Op> primitive_type_lnast2lg;

  absl::flat_hash_map<std::string, Node_pin>  name2dpin;
  absl::flat_hash_map<std::string, Lnast_nid> name2lnidx_opr; //mainly for dot and select recording

  int lginp_cnt;
  int lgout_cnt;

protected:
  void                  setup_memblock();
  std::vector<LGraph *> do_tolg();
  void                  do_resolve_tuples(LGraph *dfg);
  std::vector<LGraph *> do_gen_temp_lg();

  void lnast2lgraph             (LGraph *dfg);
  void process_ast_stmts        (LGraph *dfg, const Lnast_nid &lnidx_stmts);
  void process_ast_assign_op    (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_binary_op    (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_unary_op     (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_logical_op   (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_as_op        (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_label_op     (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_if_op        (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_uif_op       (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_func_call_op (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_func_def_op  (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_sub_op       (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_for_op       (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_while_op     (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_dp_assign_op (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_dot_op       (const Lnast_nid &lnidx);
  void process_ast_select_op    (const Lnast_nid &lnidx);
  void process_ast_tuple_struct (LGraph *dfg, const Lnast_nid &lnidx);

  Node_pin     setup_node_operator_and_target (LGraph *dfg, const Lnast_nid &lnidx_opr);
  Node_pin     setup_node_assign_and_target   (LGraph *dfg, const Lnast_nid &lnidx_opr);
  Node_pin     setup_ref_node_dpin            (LGraph *dfg, const Lnast_nid &lnidx);
  Node_pin     setup_ref_node_spin            (LGraph *dfg, const Lnast_nid &lnidx);
  Node_Type_Op decode_lnast_op                (const Lnast_nid &lnidx_opr);
  void         setup_lnast_to_lgraph_primitive_type_mapping();


  bool is_bit_attr_tuple_add(Node node) {
    return (node.get_sink_pin(1).inp_edges().size() == 1) && (node.get_sink_pin(1).inp_edges().begin()->driver.get_name().substr(0,6) == "__bits");
  }

  Node_pin     add_tuple_add_from_dot (LGraph *dfg, const Lnast_nid &lnidx_dot, const Lnast_nid &lnidx_assign);
  Node_pin     add_tuple_add_from_sel (LGraph *dfg, const Lnast_nid &lnidx_sel, const Lnast_nid &lnidx_assign);
  Node_pin     add_tuple_get_from_dot (LGraph *dfg, const Lnast_nid &lnidx_dot, const Lnast_nid &lnidx_assign);
  Node_pin     add_tuple_get_from_sel (LGraph *dfg, const Lnast_nid &lnidx_sel, const Lnast_nid &lnidx_assign);
  Node_pin     setup_tuple_ref (LGraph *dfg, std::string_view tup_name);


  // constant resolving
  static Node         resolve_constant(LGraph *g, std::string_view str_in);
  static Node         process_bin_token(LGraph *g, const std::string &token1st, const uint16_t &bit_width, bool is_signed);
  static Node         process_bin_token_with_dc(LGraph *g, const std::string &token1st,bool is_signed);
  static uint32_t     cal_bin_val_32b(const std::string &);
  static Node         create_const32_node(LGraph *g, const std::string &, uint16_t node_bit_width, bool is_signed);
  static Node         create_dontcare_node(LGraph *g, uint16_t node_bit_width);
  static std::string  hex_char_to_bin(char c);
  static std::string  hex_msb_char_to_bin(char c);

  //static void build_lnast(Inou_lnast_dfg &p, Eprp_var &var);

  // eprp callbacks
  static void tolg(Eprp_var &var);
  static void resolve_tuples(Eprp_var &var);
  static void gen_temp_lg(Eprp_var &var);

public:
  explicit Inou_lnast_dfg(const Eprp_var &var);

  static void setup();
};
