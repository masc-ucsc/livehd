//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lbench.hpp"
#include "lnast.hpp"
#include "pass.hpp"
#include "likely.hpp"
#include "lnast_parser.hpp"
#include "pass_bitwidth.hpp"

class Inou_lnast_dfg : public Pass {
private:
  Lnast *lnast{};

  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, Node_Type_Op> primitive_type_lnast2lg;

  absl::flat_hash_map<std::string, Node_pin>     name2dpin;
  absl::flat_hash_map<std::string, Lnast_nid>    name2lnidx; //mainly for dot and select recording
  absl::flat_hash_map<std::string, std::string>  keyname2pos;
  static constexpr uint8_t TN = 0;  // tuple name
  static constexpr uint8_t KN = 1;  // tuple element key name
  static constexpr uint8_t KP = 2;  // tuple element key position
  static constexpr uint8_t KV = 3;  // tuple element key value


protected:
  void                  setup_memblock();
  std::vector<LGraph *> do_tolg();
  static void           do_resolve_tuples(LGraph *dfg);
  static void           do_reduced_or_elimination(LGraph *dfg);

  void lnast2lgraph             (LGraph *dfg);
  void process_ast_stmts        (LGraph *dfg, const Lnast_nid &lnidx_stmts);
  void process_ast_assign_op    (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_nary_op      (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_logical_op   (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_as_op        (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_label_op     (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_if_op        (LGraph *dfg, const Lnast_nid &lnidx);
  void process_ast_phi_op       (LGraph *dfg, const Lnast_nid &lnidx);
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
  void process_ast_concat_op    (LGraph *dfg, const Lnast_nid &lnidx);

  Node_pin     setup_node_opr_and_lhs         (LGraph *dfg, const Lnast_nid &lnidx_opr);
  Node_pin     setup_node_assign_and_lhs      (LGraph *dfg, const Lnast_nid &lnidx_opr);
  Node_pin     setup_ref_node_dpin            (LGraph *dfg, const Lnast_nid &lnidx);
  Node_Type_Op decode_lnast_op                (const Lnast_nid &lnidx_opr);
  void         setup_lnast_to_lgraph_primitive_type_mapping();
  void         nary_node_rhs_connections      (LGraph *dfg, Node &opr_node, const std::vector<Node_pin> &opds);


  static bool is_register          (std::string_view name) {return name.substr(0, 1) == "#" ; }
  static bool is_input             (std::string_view name) {return name.substr(0, 1) == "$" ; }
  static bool is_output            (std::string_view name) {return name.substr(0, 1) == "%" ; }
  static bool is_const             (std::string_view name) {return name.substr(0, 2) == "0d" or name.substr(0, 3) == "-0d"; }
  static bool is_default_const     (std::string_view name) {return name.substr(0,13) == "default_const"; }
  static bool is_err_var_undefined (std::string_view name) {return name.substr(0,17) == "err_var_undefined"; }
  static bool is_bit_attr_tuple_add(const Node &node) {
    return (node.get_sink_pin(1).inp_edges().size() == 1) &&
           (node.get_sink_pin(1).inp_edges().begin()->driver.get_name().substr(0,6) == "__bits");
  }


  // tuple related
  Node_pin     add_tuple_add_from_dot        (LGraph *dfg, const Lnast_nid &lnidx_dot, const Lnast_nid &lnidx_assign);
  Node_pin     add_tuple_add_from_sel        (LGraph *dfg, const Lnast_nid &lnidx_sel, const Lnast_nid &lnidx_assign);
  Node_pin     add_tuple_get_from_dot_or_sel (LGraph *dfg, const Lnast_nid &lnidx_opr);
  Node_pin     setup_tuple_ref               (LGraph *dfg, std::string_view tup_name);
  Node_pin     setup_tuple_key               (LGraph *dfg, std::string_view key_name);
  Node_pin     setup_tuple_chain_new_max_pos (LGraph *dfg, const Node_pin &tn_dpin);
  static bool  tuple_get_has_key_name        (const Node &tup_get);
  static bool  tuple_get_has_key_pos         (const Node &tup_get);
  static bool  is_tup_get_target             (const Node &tup_add, std::string_view tup_get_target);
  static bool  is_tup_get_target             (const Node &tup_add, uint32_t         tup_get_target);


  // constant resolving
  static Node         resolve_constant(LGraph *g, std::string_view str_in);
  static Node         process_bin_token(LGraph *g, const std::string &token1st, uint16_t bit_width, bool is_signed);
  static Node         process_bin_token_with_dc(LGraph *g, const std::string &token1st,bool is_signed);
  static uint32_t     cal_bin_val_32b(const std::string &);
  static Node         create_const32_node(LGraph *g, const std::string &, uint16_t node_bit_width, bool is_signed);
  static Node         create_dontcare_node(LGraph *g, uint16_t node_bit_width);
  static std::string  hex_char_to_bin(char c);
  static std::string  hex_msb_char_to_bin(char c);

  // static void build_lnast(Inou_lnast_dfg &p, Eprp_var &var);

  // eprp callbacks
  static void tolg(Eprp_var &var);
  static void resolve_tuples(Eprp_var &var);
  static void reduced_or_elimination(Eprp_var &var);

public:
  explicit Inou_lnast_dfg(const Eprp_var &var);

  static void setup();
};
