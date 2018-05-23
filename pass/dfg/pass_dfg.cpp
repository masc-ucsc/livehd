#include "pass_dfg.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"
#include <vector>
using std::string;
using std::unordered_map;
using std::vector;

void Pass_dfg::transform(LGraph *g)
{

}

void Pass_dfg::cfg_2_dfg(LGraph *dfg, const LGraph *cfg)
{
  Index_ID itr = find_root(cfg);
  unordered_map<string, Index_ID> last_refs;

  while (itr != 0) {
    process_node(dfg, cfg, last_refs, itr);
    itr = get_child(cfg, itr);
  }
}

void Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, std::unordered_map<string, Index_ID> last_refs, Index_ID node)
{
  const char *data_str = cfg->get_node_wirename(node);
  CFG_Node_Data data(cfg->node_type_get(node).op, data_str);

  switch (data.get_operator()) {
    case CfgAssign_Op:
      process_assign(dfg, cfg, last_refs, data, node);
      break;
    case CfgIf_Op:
      process_if(dfg, cfg, last_refs, data, node);
      break;
    default:
      ;
  }
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
  Index_ID cond = last_refs[data.get_target()];
  

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

Index_ID Pass_dfg::find_root(const LGraph *cfg)
{
  for (auto idx : cfg->fast()) {
    if (cfg->is_root(idx))
      return idx;
  }

  assert(false);
}

Index_ID Pass_dfg::get_child(const LGraph *cfg, Index_ID node)
{
  vector<Index_ID> children;

  for (const auto &cedge : cfg->out_edges(node))
    children.push_back(cedge.get_inp_pin().get_nid());

  if (children.size() == 1)
    return children[0];
  else if (children.size() == 0)
    return 0;
  else
    assert(false);  
}

Pass_dfg_options_pack::Pass_dfg_options_pack() : Options_pack() {

  Options::get_desc()->add_options()(
    "dfg_output,o", boost::program_options::value(&dfg_output),
    "cfg output <filename> for graph")("cfg_input,i", boost::program_options::value(&cfg_input),
    "cfg input <filename> for graph");

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(*Options::get_desc()).allow_unregistered().run(), vm);

  if(vm.count("dfg_output")) {
    dfg_output = vm["dfg_output"].as<string>();
  } else {
    dfg_output = "output.dfg";
  }

  if(vm.count("cfg_input")) {
    cfg_input = vm["cfg_input"].as<string>();
  } else {
    cfg_input = "test.cfg";
  }

  console->info("inou_cfg dfg_output:{} cfg_input:{} graph_name:{}", dfg_output, cfg_input, graph_name);
}