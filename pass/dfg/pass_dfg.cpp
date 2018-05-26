#include "pass_dfg.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"
#include <cstdlib>
#include <cassert>
#include <vector>
#include <unordered_set>
using std::string;
using std::unordered_map;
using std::vector;

void Pass_dfg::transform() {
  LGraph *dfg = new LGraph(opack.lgdb_path, opack.output_name, false);
  transform(dfg);
}

void Pass_dfg::transform(LGraph *dfg) {
  assert(!opack.graph_name.empty());
  LGraph *cfg = new LGraph(opack.lgdb_path, opack.graph_name, false);

  cfg_2_dfg(dfg, cfg);

  if (opack.generate_dots_flag)
    system(("./inou/dump/lgdump " + opack.output_name + " 2>" + opack.output_name + ".dot").c_str());
}

void Pass_dfg::cfg_2_dfg(LGraph *dfg, const LGraph *cfg) {
  Index_ID    itr = find_root(cfg);
  CF2DF_State state;

  process_cfg(dfg, cfg, &state, itr);

  dfg->sync();
}

void Pass_dfg::process_cfg(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID top_node) {
  Index_ID itr = top_node;

  while (itr != 0) {
    process_node(dfg, cfg, state, itr);
    itr = get_child(cfg, itr);
  }
}

void Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID node) {
  CFG_Node_Data data(cfg, node);

  switch (cfg->node_type_get(node).op) {
  case CfgAssign_Op:
    process_assign(dfg, cfg, state, data, node);
    break;
  case CfgIf_Op:
    process_if(dfg, cfg, state, data, node);
    break;
  default:
    ;
  }
}

void Pass_dfg::process_assign(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  Index_ID dfnode           = dfg->create_node().get_nid();

  dfg->node_type_set(dfnode, CfgAssign_Op);
  vector<Index_ID> operands = process_operands(dfg, cfg, state, data, node);

  for (Index_ID id : operands)
    dfg->add_edge(Node_Pin(dfnode, 0, false), Node_Pin(id, 0, true));

  state->update_reference(data.get_target(), dfnode);
}

void Pass_dfg::process_if(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  Index_ID cond = state->get_reference(data.get_target());
  const auto &operands = data.get_operands();

  Index_ID tbranch = std::stol(operands[0]);
  CF2DF_State tstate = state->copy();

  Index_ID fbranch = std::stol(operands[1]);
  CF2DF_State fstate = state->copy();

  process_cfg(dfg, cfg, &tstate, tbranch);
  process_cfg(dfg, cfg, &fstate, fbranch);

  add_phis(dfg, cfg, state, &tstate, &fstate, cond);
}

void Pass_dfg::add_phis(LGraph *dfg, const LGraph *cfg, CF2DF_State *parent, CF2DF_State *tstate, CF2DF_State *fstate, Index_ID condition) {
  std::unordered_set<string> phis_added;

  for (const auto &pair : tstate->references()) {
    add_phi(dfg, parent, tstate, fstate, condition, pair.first);
    phis_added.insert(pair.first);
  }

  for (const auto &pair : fstate->references()) {
    if (phis_added.find(pair.first) == phis_added.end())
      add_phi(dfg, parent, tstate, fstate, condition, pair.first);
  }
}

void Pass_dfg::add_phi(LGraph *dfg, CF2DF_State *parent, CF2DF_State *tstate, CF2DF_State *fstate, Index_ID condition, const string &variable) {
  Index_ID tid = (tstate->has_reference(variable)) ? tstate->get_reference(variable) : parent->get_reference(variable);
  Index_ID fid = (fstate->has_reference(variable)) ? fstate->get_reference(variable) : parent->get_reference(variable);

  Index_ID phi = dfg->create_node().get_nid();
  dfg->node_type_set(phi, Mux_Op);
  auto tp = dfg->node_type_get(phi);

  Port_ID tin = tp.get_input_match("B");
  Port_ID fin = tp.get_input_match("A");
  Port_ID cin = tp.get_input_match("S");

  dfg->add_edge(Node_Pin(phi, tin, true), Node_Pin(tid, 0, false));
  dfg->add_edge(Node_Pin(phi, fin, true), Node_Pin(fid, 0, false));
  dfg->add_edge(Node_Pin(phi, cin, true), Node_Pin(condition, 0, false));

  parent->update_reference(variable, phi);
}

vector<Index_ID> Pass_dfg::process_operands(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node)
{
  const auto &dops = data.get_operands();
  vector<Index_ID> ops(dops.size());

  for (int i = 0; i < dops.size(); i++) {
    if (state->has_reference(dops[i]))
      ops[i] = state->get_reference(dops[i]);
    else {
      if (is_constant(dops[i]))
        ops[i] = default_constant(dfg, state);
      else if (is_register(dops[i]))
        ops[i] = create_register(dfg, state, dops[i]);
      else if (is_input(dops[i]))
        ops[i] = create_input(dfg, state, dops[i]);
      else if (is_output(dops[i]))
        ops[i] = create_output(dfg, state, dops[i]);
      else
        ops[i] = create_private(dfg, state, dops[i]);
    }
  }

  return ops;
}

Index_ID Pass_dfg::find_root(const LGraph *cfg) {
  for(auto idx : cfg->fast()) {
    if(cfg->is_root(idx))
      return idx;
  }

  assert(false);
}

Index_ID Pass_dfg::get_child(const LGraph *cfg, Index_ID node) {
  vector<Index_ID> children;

  for(const auto &cedge : cfg->out_edges(node))
    children.push_back(cedge.get_inp_pin().get_nid());

  if(children.size() == 1)
    return children[0];
  else if(children.size() == 0)
    return 0;
  else
    assert(false);
}

Index_ID Pass_dfg::create_register(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, Flop_Op);
  state->update_reference(var_name, nid);
  state->add_register(var_name, nid);

  return nid;
}

Index_ID Pass_dfg::create_input(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = g->create_node().get_nid();
  g->add_graph_input(var_name.c_str(), nid);
  state->update_reference(var_name, nid);

  return nid;
}

Index_ID Pass_dfg::create_output(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = g->create_node().get_nid();
  g->add_graph_output(var_name.c_str(), nid);
  state->update_reference(var_name, nid);
  
  return nid;
}

Index_ID Pass_dfg::create_private(LGraph *g, CF2DF_State *state, const string &var_name) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, CfgAssign_Op);
  state->update_reference(var_name, nid);

  g->add_edge(Node_Pin(nid, 0, false), Node_Pin(default_constant(g, state), 0, true));
 
  return nid;
}

Index_ID Pass_dfg::default_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);

  return nid;
}

Pass_dfg_options_pack::Pass_dfg_options_pack() : Options_pack() {

  Options::get_desc()->add_options()(
      "dot,d", boost::program_options::value(&generate_dots_flag), "dump dot files for LGraphs")(
      "output,o", boost::program_options::value(&output_name), "output graph-name");

  boost::program_options::variables_map vm;
  boost::program_options::store(
      boost::program_options::command_line_parser(Options::get_cargc(), Options::get_cargv()).options(*Options::get_desc()).allow_unregistered().run(), vm);

  generate_dots_flag = vm.count("dot") > 0;
  output_name        = (vm.count("output") > 0) ? vm["output"].as<string>() : graph_name + "_df";
  console->info("inou_cfg graph_name:{}, gen-dots:{}", graph_name, generate_dots_flag);
}
