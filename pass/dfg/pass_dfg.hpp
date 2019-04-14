//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_DFG_HPP_
#define PASS_DFG_HPP_

#include <string>
#include <vector>

#include "lgraph.hpp"
#include "pass.hpp"

#include "aux_tree.hpp"
#include "cfg_node_data.hpp"

class Pass_dfg : public Pass {
protected:
  static void generate(Eprp_var &var);
  static void optimize(Eprp_var &var);
  static void finalize_bitwidth(Eprp_var &var);
  static bool graph_name_ends_with(std::string_view str, std::string_view suffix){
    return str.rfind(suffix) == (str.size() - suffix.size());
  }

  std::vector<LGraph *> hier_generate_dfgs(LGraph *cfg_parent);
  void                  hier_optimize_dfgs(LGraph *dfg_parent);
  void                  hier_finalize_bits_dfgs(LGraph *dfg_parent);
  void do_generate(const LGraph *cfg, LGraph *dfg);
  void do_optimize(LGraph *&ori_dfg); // calls trans() to perform optimization
  void do_finalize_bitwidth(LGraph *dfg);

  void trans(LGraph *orig);
  void cfg_2_dfg(LGraph *cfg, LGraph *dfg);

private:
  Node find_cfg_root(LGraph *cfg);
  Node get_cfg_child(LGraph *cfg, Node cfg_node);
  Node process_cfg(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, Node top_node);
  Node process_node(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, Node node);

  void process_assign(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data);
  void finalize_global_connect(LGraph *dfg, const Aux_node *auxand_global);
  void process_connections(LGraph *dfg, const std::vector<Node> &driver_nodes, const Node &sink_node);

  void process_func_call(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data);

  Node process_if(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data, Node node);

  Node process_loop(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data, Node node);

  Node process_operand(LGraph *dfg, Aux_tree *aux_tree, const std::string &oprd);

  std::vector<Node> process_operands(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data);


  void resolve_phis(LGraph *dfg, Aux_tree *aux_tee, Aux_node *pauxnd, Aux_node *tauxnd, Aux_node *fauxnd, Node cond);
  void create_mux(LGraph *dfg, Aux_node *pauxnd, Node tid, Node fid, Node cond, const std::string &var);

  void attach_outputs(LGraph *dfg, Aux_tree *aux_tree);
  void add_fluid_behavior(LGraph *dfg, Aux_tree *aux_tree);
  void add_fluid_ports(LGraph *dfg, Aux_tree *aux_tree, std::vector<Node> &data_inputs, std::vector<Node> &data_outputs);
  void add_fluid_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Node> &data_inputs,
                       const std::vector<Node> &data_outputs){}
  void add_abort_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Node> &data_inputs,
                       const std::vector<Node> &data_outputs){}

  void add_read_marker(LGraph *dfg, Aux_tree *aux_tree, const std::string &v) {
    assign_to_true(dfg, aux_tree, read_marker(v));
  }
  void add_write_marker(LGraph *dfg, Aux_tree *aux_tree, const std::string &v) {
    assign_to_true(dfg, aux_tree, write_marker(v));
  }

  // TODO: This code us not used, but if it were it should be string_view (all const, no memory alloc)
  std::string read_marker(const std::string &v) const {
    return std::string(READ_MARKER) + v;
  }
  std::string write_marker(const std::string &v) const {
    return std::string(WRITE_MARKER) + v;
  }
  std::string valid_marker(const std::string &v) const {
    return std::string(VALID_MARKER) + v;
  }
  std::string retry_marker(const std::string &v) const {
    return std::string(RETRY_MARKER) + v;
  }

  void assign_to_true(LGraph *dfg, Aux_tree *aux_tree, const std::string &v);

  bool reference_changed(const Aux_node *parent, const Aux_node *branch, const std::string &v) {
    if(!parent->has_alias(v))
      return true;
    return parent->get_alias(v) != branch->get_alias(v);
  }

  constexpr bool is_register(std::string_view v)     const { return v.at(0) == REGISTER_MARKER; }
  constexpr bool is_input(std::string_view v)        const { return v.at(0) == INPUT_MARKER; }
  constexpr bool is_output(std::string_view v)       const { return v.at(0) == OUTPUT_MARKER; }
  constexpr bool is_reference(std::string_view v)    const { return v.at(0) == REFERENCE_MARKER; }
  constexpr bool is_constant(std::string_view v)     const { return (v.at(0) == '0' || v.at(0) == '-'); }
  constexpr bool is_read_marker(std::string_view v)  const { return v.substr(0, READ_MARKER.length()) == READ_MARKER; }
  constexpr bool is_write_marker(std::string_view v) const { return v.substr(0, WRITE_MARKER.length()) == WRITE_MARKER; }
  constexpr bool is_valid_marker(std::string_view v) const { return v.substr(0, VALID_MARKER.length()) == VALID_MARKER; }
  constexpr bool is_retry_marker(std::string_view v) const { return v.substr(0, RETRY_MARKER.length()) == RETRY_MARKER; }

  constexpr bool is_pure_assign_op(std::string_view v) const { return v == "="; }
  constexpr bool is_label_op(std::string_view v)       const { return v == ":"; }
  constexpr bool is_as_op(std::string_view v)          const { return v == "as"; }

  constexpr bool is_unary_op(std::string_view v)   const { return (v == "!") || (v == "not"); }
  constexpr bool is_compute_op(std::string_view v) const { return (v == "+"); }
  constexpr bool is_compare_op(std::string_view v) const { return (v == "==") || (v == ">") || (v == ">=") || (v == "<") || (v == "<="); }

  Node create_register(LGraph *g, Aux_tree *aux_tree, const std::string &var_name);
  Node create_input(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits = 0);
  Node create_output(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits = 0);
  Node create_private(LGraph *g, Aux_tree *aux_tree, const std::string &var_name);
  Node create_reference(LGraph *g, Aux_tree *aux_tree, const std::string &var_name);
  Node create_node(LGraph *g, Aux_tree *aux_tree, const std::string &v);
  Node create_default_const(LGraph *g);
  Node create_true_const(LGraph *g, Aux_tree *aux_tree);
  Node create_const32_node(LGraph *g, uint32_t val, uint16_t node_bit_width, bool is_signed);
  Node create_false_const(LGraph *g, Aux_tree *aux_tree);

  Node create_AND(LGraph *g, Aux_tree *aux_tree, Node op1, Node op2);
  Node create_OR(LGraph *g, Aux_tree *aux_tree, Node op1, Node op2);
  Node create_binary(LGraph *g, Aux_tree *aux_tree, Node op1, Node op2, Node_Type_Op oper);
  Node create_NOT(LGraph *g, Aux_tree *aux_tree, Node op1);

  Node_Type_Op node_type_from_text(std::string_view operator_text) const;

  Node     resolve_constant(LGraph *g, Aux_tree *aux_tree, const std::string &str_in);
  Node     process_bin_token(LGraph *g, const std::string &token1st, const uint16_t &bit_width, bool is_signed);
  Node     process_bin_token_with_dc(LGraph *g, const std::string &token1st,bool is_signed);
  uint32_t cal_bin_val_32b(const std::string &);
  Node     create_const32_node(LGraph *g, const std::string &, uint16_t node_bit_width, bool is_signed);
  Node     create_dontcare_node(LGraph *g, uint16_t node_bit_width);
  std::string hex_char_to_bin(char c);
  std::string hex_msb_char_to_bin(char c);

public:
  Pass_dfg();

  void setup() final;
};

#endif
