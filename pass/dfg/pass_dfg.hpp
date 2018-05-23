#ifndef PASS_DFG_HPP_
#define PASS_DFG_HPP_

#include "options.hpp"
#include "pass.hpp"

#include <string>

class Pass_dfg_options_pack : public Options_pack {
public:
};

class Pass_dfg : public Pass {
public:
  Pass_dfg() : Pass("dfg") { }

  void cfg_2_dfg(LGraph *dfg, const LGraph *cfg);

private:
  // CFG 2 DFG conversion methods
  Index_ID                 find_root(const LGraph *cfg);
  void                     process_node(LGraph *dfg, const LGraph *cfg, std::unordered_map<std::string, Index_ID> last_refs, Index_ID node);
  void                     process_assign(  LGraph *dfg,
                                            const LGraph *cfg,
                                            std::unordered_map<std::string, Index_ID> last_refs,
                                            const CFG_Node_Data &data,
                                            Index_ID node );
  void                     process_if(      LGraph *dfg,
                                            const LGraph *cfg,
                                            std::unordered_map<std::string, Index_ID> last_refs,
                                            const CFG_Node_Data &data,
                                            Index_ID node );
  void                     process_operands(LGraph *dfg,
                                            const LGraph *cfg,
                                            std::unordered_map<std::string, Index_ID> last_refs,
                                            const CFG_Node_Data &data,
                                            Index_ID node,
                                            std::vector<Index_ID>& );
};

#endif
