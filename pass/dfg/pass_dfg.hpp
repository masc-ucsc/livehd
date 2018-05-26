#ifndef PASS_DFG_HPP_
#define PASS_DFG_HPP_

#include "cfg_node_data.hpp"
#include "options.hpp"
#include "pass.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"

#include <string>
#include <unordered_map>
#include <vector>

class Pass_dfg_options_pack : public Options_pack {
public:
  Pass_dfg_options_pack();
  std::string output_name;
  bool        generate_dots_flag;
};

class CF2DF_State {
public:
  std::unordered_map<std::string, Index_ID> last_refs;
  std::unordered_map<std::string, Index_ID> registers;
};

const char REGISTER_MARKER = '@';
const char INPUT_MARKER = '$';
const char OUTPUT_MARKER = '%';
const Port_ID REG_INPUT = 'D';
const Port_ID REG_OUTPUT = 'Q';

class Pass_dfg : public Pass {
public:
  Pass_dfg() : Pass("dfg") {}

  void         cfg_2_dfg(LGraph *dfg, const LGraph *cfg);
  void         transform();
  virtual void transform(LGraph *g);

protected:
  Pass_dfg_options_pack opack;

private:
  Index_ID                 find_root(const LGraph *cfg);
  void                     process_node(    LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            Index_ID node);
  void                     process_assign(  LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            const CFG_Node_Data &data,
                                            Index_ID node );
  void                     process_if(      LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            const CFG_Node_Data &data,
                                            Index_ID node );
  std::vector<Index_ID>    process_operands(LGraph *dfg,
                                            const LGraph *cfg,
                                            CF2DF_State *state,
                                            const CFG_Node_Data &data,
                                            Index_ID node );
  
  Index_ID get_child(const LGraph *cfg, Index_ID node);

  bool is_register(const std::string &v) { return v[0] == REGISTER_MARKER; }
  bool is_input(const std::string &v) { return v[0] == INPUT_MARKER; }
  bool is_output(const std::string &v) { return v[0] == OUTPUT_MARKER; }
  bool is_constant(const std::string &v) { return v[0] == '0'; }

  Index_ID create_register(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID create_input(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID create_output(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID create_private(LGraph *g, CF2DF_State *state, const std::string &var_name);
  Index_ID default_constant(LGraph *g, CF2DF_State *state);
};

#endif
