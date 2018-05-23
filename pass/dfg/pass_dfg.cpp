#include "pass_dfg.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"
#include <vector>
using std::string;
using std::unordered_map;
using std::vector;

void Pass_dfg::cfg_2_dfg(LGraph *dfg, const LGraph *cfg)
{
  Index_ID itr = find_root(cfg);
  unordered_map<string, Index_ID> last_refs;

  while (itr != 0) {
    process_node(dfg, cfg, last_refs, itr);

  }
}

void Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, std::unordered_map<string, Index_ID> last_refs, Index_ID node)
{
  /*const WireName_ID data_str = cfg->get_node_wirename(node);
  CFG_Node_Data data(cfg->node_type_get(node), data_str);

  switch (data.get_operator()) {
    case CfgAssign_Op:
      process_assign(dfg, cfg, last_refs, data, node);
      break;
    case CfgIf_Op:
      process_if(dfg, cfg, last_refs, data, node);
      break;
    default:
      ;
  }*/
}

void Pass_dfg::process_assign(LGraph *dfg, const LGraph *cfg, unordered_map<string, Index_ID> last_refs, const CFG_Node_Data &data, Index_ID node)
{
  dfg->node_type_set(node, data.get_operator());
  
  vector<Index_ID> operands(data.get_operands().size());
  process_operands(dfg, cfg, last_refs, data, node, operands);

  for (Index_ID id : operands)
    dfg->add_edge(Node_Pin(node, 0, false), Node_Pin(id, 0, true));
  
  last_refs[data.get_target()] = node;
}

void Pass_dfg::process_if(LGraph *dfg, const LGraph *cfg, unordered_map<string, Index_ID> last_refs, const CFG_Node_Data &data, Index_ID node)
{

}

void Pass_dfg::process_operands(LGraph *dfg, const LGraph *cfg, unordered_map<string, Index_ID> last_refs, const CFG_Node_Data &data, Index_ID node,
  vector<Index_ID> &operands)
{
  for (const auto &op : data.get_operands()) {
    if (last_refs.find(op) != last_refs.end())
      operands.push_back(last_refs[op]);
    else {
      // create an input or something else
    }
  }
}

Index_ID find_root(const LGraph *cfg)
{
  for (auto idx : cfg->fast()) {
    if (cfg->is_root(idx))
      return idx;
  }

  assert(false);
}