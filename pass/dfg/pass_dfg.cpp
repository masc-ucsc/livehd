//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <cassert>
#include <vector>
#include <unordered_set>

#include "pass_dfg.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"

using std::string; // FIXME: we use std::string and std::... all the time
using std::unordered_map;
using std::vector;

unsigned int Pass_dfg::temp_counter = 0;

Pass_dfg::Pass_dfg(const py::dict &dict) : Pass("dfg") { opack.set(dict); }

LGraph * Pass_dfg::transform() {
  assert(!opack.src.empty());

  LGraph *cfg = new LGraph(opack.lgdb, opack.src, false);
  transform(cfg);
  delete cfg;

  LGraph *dfg = new LGraph(opack.lgdb, opack.graph_name, false);
  return dfg;
}

void Pass_dfg::transform(LGraph *cfg) {
  assert(!opack.graph_name.empty());
  LGraph *dfg = new LGraph(opack.lgdb, opack.graph_name, false); // true? should it clear the DFG?

  cfg_2_dfg(dfg, cfg);
  dfg->sync();
  delete dfg;
}

void Pass_dfg::cfg_2_dfg(LGraph *dfg, const LGraph *cfg) {
  Index_ID    itr = find_root(cfg);
  CF2DF_State state(dfg);

  process_cfg(dfg, cfg, &state, itr);
  attach_outputs(dfg, &state);

  fmt::print("calling sync");
}

Index_ID Pass_dfg::process_cfg(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID top_node) {
  Index_ID itr = top_node;
  Index_ID last_itr = 0;

  while (itr != 0) {
    last_itr = itr;
    itr = process_node(dfg, cfg, state, itr);
  }

  return last_itr;
}

Index_ID Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID node) {
  CFG_Node_Data data(cfg, node);

  switch (cfg->node_type_get(node).op) {
  case CfgAssign_Op:
    process_assign(dfg, cfg, state, data, node);
    return get_child(cfg, node);
  case CfgIf_Op:
    return process_if(dfg, cfg, state, data, node);
  case CfgIfMerge_Op:
    return 0;
  default:
    fmt::print("*************Unrecognized node type[n={}]: {}\n",
      node, cfg->node_type_get(node).get_name());
    return get_child(cfg, node);
  }
}

void Pass_dfg::process_assign(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  const auto &target = data.get_target();
  Index_ID dfnode = create_node(dfg, state, target);

  dfg->set_node_instance_name(dfnode, data.get_target());

  dfg->node_type_set(dfnode, node_type_from_text(data.get_operator()));
  vector<Index_ID> operands = process_operands(dfg, cfg, state, data, node);

  for (Index_ID id : operands)
    dfg->add_edge(Node_Pin(id, 0, false), Node_Pin(dfnode, 0, true));

  if (state->fluid_df() && (is_output(target) || is_register(target)))
    add_write_marker(dfg, state, target);
}

Index_ID Pass_dfg::process_if(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  Index_ID cond = state->get_reference(data.get_target());
  const auto &operands = data.get_operands();

  Index_ID tbranch = std::stol(operands[0]);
  CF2DF_State tstate = state->copy();
  Index_ID tb_next = get_child(cfg, process_cfg(dfg, cfg, &tstate, tbranch));

  Index_ID fbranch = std::stol(operands[1]);

  if (fbranch != node) {                                // there is an 'else' clause
    CF2DF_State fstate = state->copy();
    Index_ID fb_next = get_child(cfg, process_cfg(dfg, cfg, &fstate, fbranch));
    assert(tb_next == fb_next);
    add_phis(dfg, cfg, state, &tstate, &fstate, cond);
  } else {
    add_phis(dfg, cfg, state, &tstate, state, cond);    // if there's no else, the 'state' of the 'else' branch is the same as the parent
  }

  return tb_next;
}

void Pass_dfg::add_phis(LGraph *dfg, const LGraph *cfg, CF2DF_State *parent, CF2DF_State *tstate, CF2DF_State *fstate, Index_ID condition) {
  std::unordered_set<string> phis_added;

  for (const auto &pair : tstate->references()) {
    if (reference_changed(parent, tstate, pair.first)) {
      add_phi(dfg, parent, tstate, fstate, condition, pair.first);
      phis_added.insert(pair.first);
    }
  }

  for (const auto &pair : fstate->references()) {
    if (phis_added.find(pair.first) == phis_added.end() && reference_changed(parent, fstate, pair.first))
      add_phi(dfg, parent, tstate, fstate, condition, pair.first);
  }
}

void Pass_dfg::add_phi(LGraph *dfg, CF2DF_State *parent, CF2DF_State *tstate, CF2DF_State *fstate, Index_ID condition, const string &variable) {
  Index_ID tid = resolve_phi_branch(dfg, parent, tstate, variable);
  Index_ID fid = resolve_phi_branch(dfg, parent, fstate, variable);

  Index_ID phi = dfg->create_node().get_nid();
  dfg->node_type_set(phi, Mux_Op);
  auto tp = dfg->node_type_get(phi);

  Port_ID tin = tp.get_input_match("B");
  Port_ID fin = tp.get_input_match("A");
  Port_ID cin = tp.get_input_match("S");

  dfg->add_edge(Node_Pin(tid, 0, false), Node_Pin(phi, tin, true));
  dfg->add_edge(Node_Pin(fid, 0, false), Node_Pin(phi, fin, true));
  dfg->add_edge(Node_Pin(condition, 0, false), Node_Pin(phi, cin, true));

  parent->update_reference(variable, phi);
}

Index_ID Pass_dfg::resolve_phi_branch(LGraph *dfg, CF2DF_State *parent, CF2DF_State *branch, const std::string &variable) {
  if (branch->has_reference(variable))
    return branch->get_reference(variable);
  else if (parent->has_reference(variable))
    return parent->get_reference(variable);
  else if (is_register(variable))
    return create_register(dfg, parent, variable);
  else if (is_input(variable))
    return create_input(dfg, parent, variable);
  else if (is_output(variable))
    return create_output(dfg, parent, variable);
  else
    return default_constant(dfg, parent);
}

vector<Index_ID> Pass_dfg::process_operands(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  const auto &dops = data.get_operands();
  vector<Index_ID> ops(dops.size());

  for (size_t i = 0; i < dops.size(); i++) {
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

    if (state->fluid_df() && is_input(dops[i]))
      add_read_marker(dfg, state, dops[i]);
  }

  return ops;
}

void Pass_dfg::assign_to_true(LGraph *dfg, CF2DF_State *state, const std::string &v) {
  Index_ID node = create_node(dfg, state, v);
  dfg->node_type_set(node, Or_Op);

  dfg->add_edge(Node_Pin(true_constant(dfg, state), 0, false), Node_Pin(node, 0, true));
  dfg->add_edge(Node_Pin(true_constant(dfg, state), 0, false), Node_Pin(node, 0, true));
}

void Pass_dfg::attach_outputs(LGraph *dfg, CF2DF_State *state) {
  for (const auto &pair : state->copy().references()) {
    const auto &var = pair.first;

    if (is_register(var) || is_output(var)) {
      Index_ID lref = pair.second;

      Index_ID oid = create_output(dfg, state, var);
      dfg->add_edge(Node_Pin(lref, 0, false), Node_Pin(oid, 0, true));
    }
  }

  if (state->fluid_df())
    add_fluid_behavior(dfg, state);
}

void Pass_dfg::add_fluid_behavior(LGraph *dfg, CF2DF_State *state) {
  vector<Index_ID> inputs, outputs;
  add_fluid_ports(dfg, state, inputs, outputs);

}

void Pass_dfg::add_fluid_ports(LGraph *dfg, CF2DF_State *state, vector<Index_ID> &data_inputs, vector<Index_ID> &data_outputs) {
  for (const auto &pair : state->outputs()) {
    if (!is_valid_marker(pair.first) && !is_retry_marker(pair.first)) {
      auto valid_output = valid_marker(pair.first);
      auto retry_input = retry_marker(pair.first);
      data_outputs.push_back(state->get_reference(pair.first));

      if (!state->has_reference(valid_output))
        create_output(dfg, state, valid_output);

      if (!state->has_reference(retry_input))
        create_input(dfg, state, retry_input);
    }
  }

  for (const auto &pair : state->inputs()) {
    if (!is_valid_marker(pair.first) && !is_retry_marker(pair.first)) {
      auto valid_input = valid_marker(pair.first);
      auto retry_output = retry_marker(pair.first);
      data_inputs.push_back(state->get_reference(pair.first));

      if (!state->has_reference(valid_input))
        create_input(dfg, state, valid_input);

      if (!state->has_reference(retry_output))
        create_output(dfg, state, retry_output);
    }
  }
}

void Pass_dfg::add_fluid_logic(LGraph *dfg, CF2DF_State *state, const vector<Index_ID> &data_inputs, const vector<Index_ID> &data_outputs) {
  //Index_ID abort_id = add_abort_logic(dfg, state, data_inputs, data_outputs);


}

void Pass_dfg::add_abort_logic(LGraph *dfg, CF2DF_State *state, const vector<Index_ID> &data_inputs, const vector<Index_ID> &data_outputs) {

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

