// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <string>
#include <vector>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lbench.hpp"
#include "lnast.hpp"
#include "likely.hpp"
#include "pass.hpp"
#include "cell.hpp"


class Lnast_tolg {
public:
  explicit Lnast_tolg(std::string_view _module_name, std::string_view _path);
  std::vector<LGraph *> do_tolg(std::shared_ptr<Lnast> ln, const Lnast_nid &top_stmts);

private:
  std::shared_ptr<Lnast> lnast;
  std::string_view module_name;
  std::string_view path;
  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, Ntype_op>   primitive_type_lnast2lg;
  absl::flat_hash_map<std::string_view, Node_pin>               vname2attr_dpin;       // for dummy attribute node construction, vn = variable non-ssa name, dpin = last attr dpin within "any" attributes
  absl::flat_hash_map<std::string, Node_pin>                    name2dpin;             // for scalar variable
  absl::flat_hash_map<std::string, Node_pin>                    field2dpin;
  absl::flat_hash_map<std::string_view, std::vector<Node>>      driver_var2wire_nodes; // for __last_value temporarily wire nodes
  absl::flat_hash_map<Node_pin, std::vector<Node_pin>>          inp2leaf_tg_spins;
  absl::flat_hash_map<Node::Compact, absl::flat_hash_set<Node>> inp_artifacts;
protected:
  void top_stmts2lgraph             (LGraph *lg, const Lnast_nid &lnidx_stmts);
  void process_ast_stmts            (LGraph *lg, const Lnast_nid &lnidx_stmts);
  Node process_ast_assign_op        (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_dp_assign_op     (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_nary_op          (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_logical_op       (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_as_op            (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_if_op            (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_phi_op           (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_uif_op           (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_func_call_op     (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_func_def_op      (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_for_op           (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_while_op         (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_tuple_struct     (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_concat_op        (LGraph *lg, const Lnast_nid &lnidx);
  void process_ast_tuple_add_op     (LGraph *lg, const Lnast_nid &lnidx_ta);
  void process_ast_tuple_get_op     (LGraph *lg, const Lnast_nid &lnidx_tg);
  void process_ast_attr_set_op      (LGraph *lg, const Lnast_nid &lnidx_aset);
  void process_ast_attr_get_op      (LGraph *lg, const Lnast_nid &lnidx_aget);
  void process_ast_tuple_phi_add_op (LGraph *lg, const Lnast_nid &lnidx_tpa);
  void process_hier_inp_bits_set    (LGraph *lg, const Lnast_nid &lnidx_ta);
  void setup_lgraph_ios_and_final_var_name(LGraph *lg);


  Node         setup_node_opr_and_lhs    (LGraph *lg, const Lnast_nid &lnidx_opr, bool from_fir_op = false);
  Node_pin     setup_tuple_assignment    (LGraph *lg, const Lnast_nid &lnidx_opr);
  Node_pin     setup_node_assign_and_lhs (LGraph *lg, const Lnast_nid &lnidx_opr);
  Node_pin     setup_ref_node_dpin       (LGraph *lg, const Lnast_nid &lnidx, 
                                          bool from_phi     = false, 
                                          bool from_concat  = false,
                                          bool from_tupstrc = false,
                                          bool from_assign  = false,
                                          bool want_reg_qpin = false);

  Ntype_op decode_lnast_op           (const Lnast_nid &lnidx_opr);
  void     setup_dpin_ssa            (Node_pin &dpin, std::string_view var_name, uint16_t subs);
  void     nary_node_rhs_connections (LGraph *lg, Node &opr_node, const std::vector<Node_pin> &opds, bool is_subt);
  void     setup_clk                 (LGraph *lg, Node &reg_node);
  void     setup_lnast_to_lgraph_primitive_type_mapping();


  static bool is_register          (std::string_view name) {return name.substr(0, 1) == "#" ; }
  static bool is_input             (std::string_view name) {return name.substr(0, 1) == "$" ; }
  static bool is_output            (std::string_view name) {return name.substr(0, 1) == "%" ; }
  static bool is_const             (std::string_view name) {return (std::isdigit(name[0]) || name.at(0) == '-'); }
  static bool is_bool_true         (std::string_view name) {return name.substr(0,4) == "true"; }
  static bool is_bool_false        (std::string_view name) {return name.substr(0,5) == "false"; }
  static bool is_err_var_undefined (std::string_view name) {return name.substr(0,17) == "err_var_undefined"; }
  static bool is_scalar            (Node_pin dpin) {return dpin.get_node().get_type_op() != Ntype_op::TupAdd; }

  bool        subgraph_outp_is_tuple (Sub_node* sub);
  void        subgraph_io_connection (LGraph *lg, Sub_node* sub, std::string_view arg_tup_name, std::string_view res_name, Node subg_node);
  std::vector<std::string_view> split_hier_name (std::string_view hier_name);

  // tuple related
  Node_pin     setup_tuple_ref           (LGraph *lg, std::string_view tup_name);
  Node_pin     setup_field_dpin          (LGraph *lg, std::string_view key_name);
  void         reconnect_to_ff_qpin      (LGraph *lg, const Node &tg_node);
  static bool  tuple_get_has_key_name    (const Node &tup_get);
  static bool  tuple_get_has_key_pos     (const Node &tup_get);
  static bool  is_tup_get_target         (const Node &tup_add, std::string_view tup_get_target);
  static bool  is_tup_get_target         (const Node &tup_add, uint32_t         tup_get_target);
  Node_pin     create_inp_tg             (LGraph *lg, std::string_view input_field);
  void         create_out_ta             (LGraph *lg, std::string_view key_name, Node_pin &val_dpin);

  void         try_create_flattened_inp     (LGraph *lg);
  void         dfs_try_create_flattened_inp (LGraph *lg, Node_pin &cur_node_spin, std::string hier_name, Node &chain_head);
  Node_pin     create_const                 (LGraph *lg, std::string_view const_str);

  // attribute related
  bool check_new_var_chain (const Lnast_nid &lnidx_opr);
  bool check_is_attrset_ta (Node &node, std::string &var_name, std::string &attr_name, Lconst &bits, Node &chain_head);
  bool check_is_tup_assign (Node node) { return !node.setup_sink_pin("value").is_connected();};
  bool is_hier_inp_bits_set(const Lnast_nid &lnidx_ta);

  // firrtl related
  void process_firrtl_op_connection(LGraph *lg, const Lnast_nid &lnidx_fc);

};

