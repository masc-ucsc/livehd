// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once
#include <mutex>
#include <string>
#include <vector>

#include "cell.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "likely.hpp"
#include "lnast.hpp"
#include "pass.hpp"

class Lnast_tolg {
public:
  explicit Lnast_tolg(std::string_view _module_name, std::string_view _path);
  std::vector<Lgraph *> do_tolg(std::shared_ptr<Lnast> ln, const Lnast_nid &top_stmts);

private:
  inline static std::mutex lgs_mutex;
  std::shared_ptr<Lnast>   lnast;
  std::string_view         module_name;
  std::string_view         path;
  std::string              tuple_assign_str;

  absl::flat_hash_map<Lnast_ntype::Lnast_ntype_int, Ntype_op> primitive_type_lnast2lg;
  absl::flat_hash_map<std::string_view, Node_pin> vname2attr_dpin;  // for dummy attribute node construction, vn = variable non-ssa
                                                                    // name, dpin = last attr dpin within "any" attributes
  absl::flat_hash_map<std::string, Node_pin>                    name2dpin;  // for scalar variable
  absl::flat_hash_map<std::string, Node_pin>                    field2dpin;
  absl::flat_hash_map<std::string_view, std::vector<Node>>      driver_var2wire_nodes;  // for __last_value temporarily wire nodes
  absl::flat_hash_map<Node_pin, std::vector<Node_pin>>          inp2leaf_tg_spins;
  absl::flat_hash_map<std::string, Node>                        vname2tuple_head;  // record the tuple_chain head, which will be driven by the #register variable with the largest_ssa
  absl::flat_hash_map<Node::Compact, absl::flat_hash_set<Node>> inp_artifacts;

protected:
  void top_stmts2lgraph(Lgraph *lg, const Lnast_nid &lnidx_stmts);
  void process_ast_stmts(Lgraph *lg, const Lnast_nid &lnidx_stmts);
  Node process_ast_assign_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_dp_assign_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_nary_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_logical_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_if_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_phi_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_uif_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_func_call_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_func_def_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_for_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_while_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_reduce_and(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_tuple_struct(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_concat_op(Lgraph *lg, const Lnast_nid &lnidx);
  void process_ast_tuple_add_op(Lgraph *lg, const Lnast_nid &lnidx_ta);
  void process_ast_tuple_get_op(Lgraph *lg, const Lnast_nid &lnidx_tg);
  void process_ast_attr_set_op(Lgraph *lg, const Lnast_nid &lnidx_aset);
  void process_ast_attr_get_op(Lgraph *lg, const Lnast_nid &lnidx_aget);
  void process_hier_inp_bits_set(Lgraph *lg, const Lnast_nid &lnidx_ta);
  void setup_lgraph_ios_and_final_var_name(Lgraph *lg);

  Node_pin create_scalar_access_tg(Lgraph *lg, const Node_pin &tg_tupname_dpin);
  Node_pin create_scalar_access_tg(Lgraph *lg, const Node_pin &tg_tupname_dpin, const Node_pin &field_dpin);
  Node     setup_node_opr_and_lhs(Lgraph *lg, const Lnast_nid &lnidx_opr, std::string_view fir_func_name);
  Node_pin setup_tuple_assignment(Lgraph *lg, const Lnast_nid &lnidx_opr);
  Node_pin setup_node_assign_and_lhs(Lgraph *lg, const Lnast_nid &lnidx_opr);
  Node_pin setup_ref_node_dpin(Lgraph *lg, const Lnast_nid &lnidx, bool from_ta_assign = false, bool from_phi = false);

  Ntype_op decode_lnast_op(const Lnast_nid &lnidx_opr);
  void     setup_dpin_ssa(Node_pin &dpin, std::string_view var_name, uint16_t subs);
  void     nary_node_rhs_connections(Lgraph *lg, Node &opr_node, const std::vector<Node_pin> &opds, bool is_subt);
  void     setup_scalar_reg_clkrst(Lgraph *lg, Node &reg_node);
  void     setup_lnast_to_lgraph_primitive_type_mapping();

  static bool is_tmp_var(std::string_view name) {
    return name.substr(0, 3) == "___";
  }  // FIXME->sh: any other way to avoic create tmp string every time?
  static bool is_register(std::string_view name) { return name.at(0) == '#'; }
  static bool is_input(std::string_view name) { return name.at(0) == '$'; }
  static bool is_output(std::string_view name) { return name.at(0) == '%'; }
  static bool is_bool_true(std::string_view name) { return name.substr(0, 4) == "true"; }
  static bool is_bool_false(std::string_view name) { return name.substr(0, 5) == "false"; }
  static bool is_scalar(Node_pin dpin) { return dpin.get_node().get_type_op() != Ntype_op::TupAdd; }

  // FIXME: this are always constant (REMOVE and side effects too)
  static bool is_const_num(std::string_view name) {
    (void)name;
    return true;
  }  // (std::isdigit(name.at(0)) || name.at(0) == '-'); }
  static bool is_err_var_undefined(std::string_view name) {
    I(name.substr(0, 17) != "err_var_undefined");
    return false;
  }

  bool subgraph_outp_is_tuple(Sub_node *sub);
  void subgraph_io_connection(Lgraph *lg, Sub_node *sub, std::string_view arg_tup_name, std::string_view res_name, Node subg_node);
  void process_direct_op_connection(Lgraph *lg, const Lnast_nid &lnidx_fc);
  void split_hier_name(std::string_view hier_name, std::vector<std::string_view> &hier_inp_subnames);

  // tuple related
  Node_pin    setup_tuple_ref(Lgraph *lg, std::string_view tup_name);
  Node_pin    setup_ta_ref_previous_ssa(Lgraph *lg, std::string_view tup_name, int16_t);
  Node_pin    setup_field_dpin(Lgraph *lg, std::string_view key_name);
  void        reconnect_to_ff_qpin(Lgraph *lg, const Node &tg_node);
  static bool tuple_get_has_key_name(const Node &tup_get);
  static bool tuple_get_has_key_pos(const Node &tup_get);
  static bool is_tup_get_target(const Node &tup_add, std::string_view tup_get_target);
  static bool is_tup_get_target(const Node &tup_add, uint32_t tup_get_target);
  Node_pin    create_inp_tg(Lgraph *lg, std::string_view input_field);
  void        create_out_ta(Lgraph *lg, std::string_view key_name, Node_pin &val_dpin);
  void        create_inp_ta4dynamic_idx(Lgraph *lg, const Node_pin &inp_dpin, std::string_view full_inp_hier_name);
  void        handle_inp_tg_runtime_idx(std::string_view hier_name, Node &chain_head, Node &cur_node);
  void        create_ginp_as_runtime_idx(Lgraph *lg, std::string_view hier_name, Node &chain_head, Node &cur_node);

  void     try_create_flattened_inp(Lgraph *lg);
  void     post_process_ginp_attr_connections(Lgraph *lg);
  void     dfs_try_create_flattened_inp(Lgraph *lg, Node_pin &cur_node_spin, std::string hier_name, Node &chain_head);
  Node_pin create_const(Lgraph *lg, std::string_view const_str);

  // attribute related
  bool is_new_var_chain(const Lnast_nid &lnidx_opr);
  bool check_is_attrset_ta(Node &node, std::string &var_name, std::string &attr_name, Lconst &bits, Node &chain_head);
  bool check_is_tup_assign(Node node) { return !node.setup_sink_pin("value").is_connected(); };
  bool is_hier_inp_bits_set(const Lnast_nid &lnidx_ta);
  bool is_tuple_struct_ta(const Lnast_nid &lnidx_ta);

  // firrtl related
  void process_firrtl_op_connection(Lgraph *lg, const Lnast_nid &lnidx_fc);

  void dump() const;  // debug debugging
};
