#ifndef PASS_DFG_HPP_
#define PASS_DFG_HPP_

#include "cfg_node_data.hpp"
#include "options.hpp"
#include "pass.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "symbol_table.hpp"
#include "cf2df_state.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class Pass_dfg_options_pack : public Options_pack {
public:
  Pass_dfg_options_pack();
  std::string output_name;
};

class Pass_dfg : public Pass {
public:
  Pass_dfg() : Pass("dfg") {}

  void         cfg_2_dfg(LGraph *dfg, const LGraph *cfg);
  void         transform();
  virtual void transform(LGraph *g);
  void         test_const_conversion();

protected:
  Pass_dfg_options_pack opack;

private:
  Index_ID                 find_root(const LGraph *cfg);
  Index_ID                 process_cfg(     LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            Index_ID top_node);

  Index_ID                 process_node(    LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            Index_ID node);

  void                     process_assign(  LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            const CFG_Node_Data &data,
                                            Index_ID node );

  Index_ID                 process_if(      LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            const CFG_Node_Data &data,
                                            Index_ID node );

  std::vector<Index_ID>    process_operands(LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            const CFG_Node_Data &data,
                                            Index_ID node );

  void                     add_phis(        LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *parent,
                                            CF2DF_State *tstate,
                                            CF2DF_State *fstate,
                                            Index_ID condition);

  void                     add_phi(         LGraph *dfg,
                                            CF2DF_State *parent,
                                            CF2DF_State *tstate,
                                            CF2DF_State *fstate,
                                            Index_ID condition,
                                            const std::string &variable);

  Index_ID get_child(const LGraph *cfg, Index_ID node);
  Index_ID resolve_phi_branch(LGraph *dfg, CF2DF_State *parent, CF2DF_State *branch, const std::string &variable);
  void attach_outputs(LGraph *dfg, CF2DF_State *state);

  void add_fluid_behavior(LGraph *dfg, CF2DF_State *state);
  void add_fluid_ports(LGraph *dfg, CF2DF_State *state, std::vector<Index_ID> &data_inputs, std::vector<Index_ID> &data_outputs);
  void add_fluid_logic(LGraph *dfg, CF2DF_State *state, const std::vector<Index_ID> &data_inputs, const std::vector<Index_ID> &data_outputs);

  void add_read_marker(LGraph *dfg, CF2DF_State *state, const std::string &v) { assign_to_true(dfg, state, read_marker(v)); }
  void add_write_marker(LGraph *dfg, CF2DF_State *state, const std::string &v) { assign_to_true(dfg, state, write_marker(v)); }
  
  std::string read_marker(const std::string &v) { return READ_MARKER + v; }
  std::string write_marker(const std::string &v) { return WRITE_MARKER + v; }
  std::string valid_marker(const std::string &v) { return VALID_MARKER + v; }
  std::string retry_marker(const std::string &v) { return RETRY_MARKER + v; }

  void assign_to_true(LGraph *dfg, CF2DF_State *state, const std::string &v);

  bool reference_changed(const CF2DF_State *parent, const CF2DF_State *branch, const std::string &v) {
    if (!parent->has_reference(v)) return true;
    return parent->get_reference(v) != branch->get_reference(v);
  }

  bool is_register(const std::string &v) { return v[0] == REGISTER_MARKER; }
  bool is_input(const std::string &v) { return v[0] == INPUT_MARKER; }
  bool is_output(const std::string &v) { return v[0] == OUTPUT_MARKER; }
  bool is_constant(const std::string &v) { return v[0] == '0'; }
  bool is_read_marker(const std::string &v) { return v.substr(0, READ_MARKER.length()) == READ_MARKER; }
  bool is_write_marker(const std::string &v) { return v.substr(0, WRITE_MARKER.length()) == WRITE_MARKER; }
  bool is_valid_marker(const std::string &v) { return v.substr(0, VALID_MARKER.length()) == VALID_MARKER; }
  bool is_retry_marker(const std::string &v) { return v.substr(0, RETRY_MARKER.length()) == RETRY_MARKER; }

  Index_ID create_register(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID create_input(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID create_output(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID create_private(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID create_node(LGraph *g, CF2DF_State *state, const std::string &v);
  Index_ID default_constant(LGraph *g, CF2DF_State *state);
  Index_ID true_constant(LGraph *g, CF2DF_State *state);

  Index_ID create_AND(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2);
  Index_ID create_OR(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2);
  Index_ID create_binary(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2, const char *oper);
  Index_ID create_NOT(LGraph *g, CF2DF_State *state, Index_ID op1);

  Index_ID temp() { return TEMP_MARKER + std::to_string(temp_counter++); }
  static unsigned int temp_counter;

  //Sheng zone
  Index_ID resolve_constant(LGraph *g, const std::string& str_in, bool& is_signed, bool& is_in32b, bool& is_explicit_signed, uint32_t& val, uint32_t& explicit_bits, size_t& bit_width);
  Index_ID process_hex_token (LGraph *g, const std::string& token1st, const uint16_t & bit_width, uint32_t& val,  bool& is_in32b);
  Index_ID process_bin_token (LGraph *g, const std::string& token1st, const uint16_t & bit_width, uint32_t& val,  bool& is_in32b);
  Index_ID process_dec_token (LGraph *g, const std::string& token1st, const uint16_t & bit_width, uint32_t& val,  bool& is_in32b);
  uint32_t cal_hex_val_32b(const std::string&);
  uint32_t cal_bin_val_32b(const std::string&);

};

#endif
