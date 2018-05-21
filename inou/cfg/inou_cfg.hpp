//
// Created by sheng hong  on 4/14/18.
//

#ifndef LGRAPH_MY_TEST_H
#define LGRAPH_MY_TEST_H

#include "inou.hpp"
#include "options.hpp"
#include <string>
#include <vector>

#include "cfg_node_data.hpp"

class Inou_cfg_options_pack : public Options_pack {
public:
  Inou_cfg_options_pack();

  std::string cfg_output;
  std::string cfg_input;
};

class Inou_cfg : public Inou {
private:
  static bool              space(char c) { return isspace(c); }
  static bool              not_space(char c) { return !isspace(c); }
  std::vector<std::string> split(const std::string &str);
  void                     build_graph(std::vector<std::string> &, std::string &, LGraph *, std::map<std::string, uint32_t> &, std::map<std::string, Index_ID> &, std::map<std::string, std::vector<std::string>> &, int64_t &);
  void                     cfg_2_lgraph(char **, std::vector<LGraph *> &);
  std::string              encode_cfg_data(const std::string &);

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

protected:
  Inou_cfg_options_pack opack;

public:
  Inou_cfg();

  virtual ~Inou_cfg();

  std::vector<LGraph *> generate() final;
  void                  cfg_2_dot(LGraph *, const std::string &path);

  void lgraph_2_cfg(const LGraph *g, const std::string &filename);
  void cfg_2_dfg(LGraph *dfg, const LGraph *cfg);

  using Inou::generate;

  void generate(std::vector<const LGraph *> &out) final;
};

#endif //LGRAPH_MY_TEST_H
