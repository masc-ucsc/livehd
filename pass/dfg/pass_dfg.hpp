//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef PASS_DFG_HPP_
#define PASS_DFG_HPP_

#include <string>
#include <unordered_map>
#include <vector>

#include "pass.hpp"
#include "lgedge.hpp"
#include "options.hpp"
#include "options.hpp"
//#include "cf2df_state.hpp"
#include "aux_tree.hpp"
#include "cfg_node_data.hpp"
#include "cfg_node_data.hpp"

class Pass_dfg_options : public Options_base {
public:
  std::string src;
  Pass_dfg_options() { }

  void set(const std::string &key, const std::string &value);

};

class Pass_dfg : public Pass {
public:
  Pass_dfg():Pass() { }
  Pass_dfg(const std::string &key, const std::string &value);

  LGraph *     generate_dfg(); //calls regen() to generate dfg from cfg
  LGraph *     regen(const LGraph *orig);
  void         optimize();     //calls trans() to perform optimization
  void         trans(LGraph *orig);
  void         cfg_2_dfg(LGraph *dfg, const LGraph *cfg);
  void         test_const_conversion();

  void set(const std::string &key, const std::string &value) final {
    opack.set(key,value);
  }

protected:
  Pass_dfg_options opack;

private:
  Index_ID                 find_cfg_root(        const LGraph *cfg);
  Index_ID                 process_cfg(          LGraph *dfg,
                                                 const LGraph *cfg,
                                                 Aux_tree *aux_tree,
                                                 Index_ID top_node);

  Index_ID                 process_node(         LGraph *dfg,
                                                 const LGraph *cfg,
                                                 Aux_tree *aux_tree,
                                                 Index_ID node);

  void                     process_assign(       LGraph *dfg,
                                                 Aux_tree *aux_tree,
                                                 const CFG_Node_Data &data);
  void                     finalize_gconnect(    LGraph *dfg,
                                                 const Aux_node *auxand_global);
  void                     process_connections(  LGraph *dfg,
                                                 const std::vector<Index_ID> &src_nids,
                                                 const Index_ID &dst_nid);

  void                     process_func_call(    LGraph *dfg,
                                                 const LGraph *cfg,
                                                 Aux_tree *aux_tree,
                                                 const CFG_Node_Data &data);

  Index_ID                 process_if(           LGraph *dfg,
                                                 const LGraph *cfg,
                                                 Aux_tree *aux_tree,
                                                 const CFG_Node_Data &data,
                                                 Index_ID node );

  Index_ID                 process_operand(      LGraph *dfg,
                                                 Aux_tree *aux_tree,
                                                 const std::string oprd);

  std::vector<Index_ID>    process_operands(     LGraph *dfg,
                                                 Aux_tree *aux_tree,
                                                 const CFG_Node_Data &data);

//  void                     add_phis(             LGraph *dfg,
//                                                 const LGraph *cfg,
//                                                 Aux_node *parent,
//                                                 Aux_node *tnode,
//                                                 Aux_node *fnode,
//                                                 Index_ID condition);

//  void                     add_phi(              LGraph *dfg,
//                                                 Aux_node *parent,
//                                                 Aux_node *tnode,
//                                                 Aux_node *fnode,
//                                                 Index_ID condition,
//                                                 const std::string &variable);

  Index_ID get_cfg_child(const LGraph *cfg, Index_ID node);
  //Index_ID resolve_phi_branch(LGraph *dfg, Aux_node *parent, Aux_node *branch, const std::string &variable);

  void resolve_phis(LGraph *dfg, Aux_tree *aux_tee, Aux_node *pauxnd, Aux_node *tauxnd, Aux_node *fauxnd, Index_ID cond);
  void create_mux(LGraph *dfg, Aux_node *pauxnd, Index_ID tid, Index_ID fid, Index_ID cond, const std::string &var);

  void attach_outputs(LGraph *dfg, Aux_tree *aux_tree);
  void add_fluid_behavior(LGraph *dfg, Aux_tree *aux_tree);
  void add_fluid_ports(LGraph *dfg, Aux_tree *aux_tree, std::vector<Index_ID> &data_inputs, std::vector<Index_ID> &data_outputs);
  void add_fluid_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs, const std::vector<Index_ID> &data_outputs);
  void add_abort_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs, const std::vector<Index_ID> &data_outputs);

  void add_read_marker(LGraph *dfg, Aux_tree *aux_tree, const std::string &v) { assign_to_true(dfg, aux_tree, read_marker(v)); }
  void add_write_marker(LGraph *dfg, Aux_tree *aux_tree, const std::string &v) { assign_to_true(dfg, aux_tree, write_marker(v)); }

  std::string read_marker(const std::string &v) { return READ_MARKER + v; }
  std::string write_marker(const std::string &v) { return WRITE_MARKER + v; }
  std::string valid_marker(const std::string &v) { return VALID_MARKER + v; }
  std::string retry_marker(const std::string &v) { return RETRY_MARKER + v; }

  void assign_to_true(LGraph *dfg, Aux_tree *aux_tree, const std::string &v);

  bool reference_changed(const Aux_node *parent, const Aux_node *branch, const std::string &v) {
    if (!parent->has_alias(v)) return true;
    return parent->get_alias(v) != branch->get_alias(v);
  }

  bool is_register(const std::string &v)     { return v.at(0) == REGISTER_MARKER; }
  bool is_input(const std::string &v)        { return v.at(0) == INPUT_MARKER; }
  bool is_output(const std::string &v)       { return v.at(0) == OUTPUT_MARKER; }
  bool is_reference(const std::string &v)    { return v.at(0) == REFERENCE_MARKER; }
  bool is_constant(const std::string &v)     { return (v.at(0) == '0' || v.at(0) == '-'); }
  bool is_read_marker(const std::string &v)  { return v.substr(0, READ_MARKER.length()) == READ_MARKER; }
  bool is_write_marker(const std::string &v) { return v.substr(0, WRITE_MARKER.length()) == WRITE_MARKER; }
  bool is_valid_marker(const std::string &v) { return v.substr(0, VALID_MARKER.length()) == VALID_MARKER; }
  bool is_retry_marker(const std::string &v) { return v.substr(0, RETRY_MARKER.length()) == RETRY_MARKER; }

  bool is_pure_assign_op(const std::string &v) {return  v == "=";}
  bool is_label_op      (const std::string &v) {return  v == ":";}
  bool is_as_op         (const std::string &v) {return  v == "as";}
  bool is_unary_op      (const std::string &v) {return (v == "!")||(v == "not");}
  bool is_compute_op    (const std::string &v) {return (v == "+");}
  bool is_compare_op    (const std::string &v) {return (v == "==")||(v == ">")||(v == ">=")||(v == "<")||(v == "<=") ;}

  //Index_ID create_register(LGraph *g, Aux_tree *aux_tree, const std::string &var_name);
  Index_ID create_input(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits=1);
  Index_ID create_output(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits=1);
  Index_ID create_private(LGraph *g, Aux_tree *aux_tree, const std::string &var_name);
  Index_ID create_reference(LGraph *g, Aux_tree *aux_tree, const std::string &var_name);
  Index_ID create_node(LGraph *g, Aux_tree *aux_tree, const std::string &v, const uint16_t bits=1);
  Index_ID create_default_const(LGraph *g);
  Index_ID create_true_const(LGraph *g, Aux_tree *aux_tree);
  Index_ID create_false_const(LGraph *g, Aux_tree *aux_tree);

  Index_ID create_AND(LGraph *g, Aux_tree *aux_tree, Index_ID op1, Index_ID op2);
  Index_ID create_OR(LGraph *g, Aux_tree *aux_tree, Index_ID op1, Index_ID op2);
  Index_ID create_binary(LGraph *g, Aux_tree *aux_tree, Index_ID op1, Index_ID op2, Node_Type_Op oper);
  Index_ID create_NOT(LGraph *g, Aux_tree *aux_tree, Index_ID op1);

  Node_Type_Op node_type_from_text(const std::string &operator_text);

  std::string temp() { return TEMP_MARKER + std::to_string(temp_counter++); }
  static unsigned int temp_counter;

  //Sheng zone
  Index_ID resolve_constant          (LGraph *g, Aux_tree *aux_tree, const std::string& str_in);
                                      //bool& is_signed,
                                      //bool& is_in32b,
                                      //bool& is_explicit_signed,
                                      //bool& has_bool_dc,
                                      //bool& is_pure_dc,
                                      //uint32_t& val,
                                      //uint32_t& explicit_bits,
                                      //size_t& bit_width);
  Index_ID process_bin_token         (LGraph *g, const std::string& token1st, const uint16_t & bit_width, uint32_t& val);
  Index_ID process_bin_token_with_dc (LGraph *g, const std::string& token1st);
  uint32_t cal_bin_val_32b           (const std::string&);
  Index_ID create_const32_node       (LGraph *g, const std::string&, uint16_t node_bit_width, uint32_t& val);
  Index_ID create_dontcare_node      (LGraph *g, uint16_t node_bit_width );
  std::string hex_char_to_bin        (char c);
  std::string hex_msb_char_to_bin    (char c);
};

#endif
