//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cstdlib>
#include <cassert>
#include <vector>
#include <unordered_set>

#include "pass_dfg.hpp"
#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"

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

  fmt::print("calling sync\n");
}

Index_ID Pass_dfg::process_cfg(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID top_node) {
  Index_ID itr = top_node;
  Index_ID last_itr = 0;

  while (itr != 0) {
    last_itr = itr;
    itr = process_node(dfg, cfg, state, itr);
    fmt::print("node{} process finished!!\n\n", last_itr);
  }

  return last_itr;
}

Index_ID Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, Index_ID node) {
  CFG_Node_Data data(cfg, node);

  //sh dbg
  fmt::print("now processing CFG node:{}\n", node);
  fmt::print("target:[{}], operator:[{}], ", data.get_target(), data.get_operator());
  fmt::print("operands:[");
  for(const auto i:data.get_operands())
    fmt::print("{}, ", i);
  fmt::print("]");
  fmt::print("\n");

  std::vector<Index_ID> subgraph_nodes;

  switch (cfg->node_type_get(node).op) {
  case CfgAssign_Op:
    process_assign(dfg, cfg, state, data, node);
    return get_child(cfg, node);
  case CfgFunctionCall_Op:
    process_func_call(dfg, cfg, state, data, node);
    return get_child(cfg, node);
  case CfgIf_Op:
    return process_if(dfg, cfg, state, data, node);
  case CfgIfMerge_Op:
    return 0;
  default:
    fmt::print("\n\n*************Unrecognized node type[n={}]: {}\n", node, cfg->node_type_get(node).get_name());
    return get_child(cfg, node);
  }
}

void Pass_dfg::process_func_call(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  fmt::print("process_func_call\n");
  const auto &target = data.get_target();
  const vector<std::string> &operands = data.get_operands();
  const unordered_map<std::string, Index_ID > &name2id_subg = state->get_name2id_subg();
  const unordered_map<Index_ID, Index_ID >    &id2id_subg   = state->get_id2id_subg();
  LGraph* sub_graph = 0;
  Index_ID sub_graph_root_nid;

  if(sub_graph = LGraph::find_lgraph(cfg->get_path(), operands[0])){ //first time encounter the subgraph
    Index_ID target_nid = create_node(dfg, state, target);
    fmt::print("create node for func_call target:{}, nid:{}\n", target, target_nid);
    dfg->node_subgraph_set(target_nid, sub_graph->lg_id());
    fmt::print("set subgraph on nid:{}, sub_graph name:{}, sub_graph_id:{}\n", target_nid, operands[0], sub_graph->lg_id());
  }else{
    //find subgraph root node and connect operands to it
    auto iter_name2id_subg = name2id_subg.find(operands[0]);
    if(iter_name2id_subg != name2id_subg.end()){
      auto iter_id2id_subg = id2id_subg.find(iter_name2id_subg->second);
      sub_graph_root_nid = iter_id2id_subg->second;
    }

    vector<Index_ID> oprd_ids = process_operands(dfg, cfg, state, data, node); // all the operands should be created before, just get back oprd_ids

    for(int i = 1; i<oprd_ids.size();i++ ){
      Port_ID dst_pid = i-1; //dst_pid in sub-graph should be incremented
      Port_ID src_pid = 0;
      if(dfg->node_type_get(oprd_ids[i]).op == Or_Op) // create pure dummy assign op instead of reduced Or_op?
        src_pid = 1;// output pid=1 for reduced Or_Op

      dfg->add_edge(Node_Pin(oprd_ids[i], src_pid, false), Node_Pin(sub_graph_root_nid, dst_pid, true));
    }

    //create target node
    Index_ID target_nid = create_node(dfg, state, target);
    fmt::print("create node for internal target:{}, nid:{}\n", target, target_nid);
    dfg->set_node_instance_name(target_nid, target);
    dfg->node_type_set(target_nid, Or_Op); //temp directly assing node type

    //connect func call target with first operand
    Port_ID dst_pid = 0; //dst_pid in sub-graph should be incremented
    Port_ID src_pid = 0;
    if(dfg->node_type_get(oprd_ids[0]).op == Or_Op) // create pure dummy assign op instead of reduced Or_op?
      src_pid = 1;// output pid=1 for reduced Or_Op

    dfg->add_edge(Node_Pin(oprd_ids[0], src_pid, false), Node_Pin(target_nid, dst_pid, true));
    fmt::print("create edge ({}->{})\n", dfg->get_node_wirename(oprd_ids[0]), dfg->get_node_wirename(target_nid));
  }
}

void Pass_dfg::process_assign(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  fmt::print("process_assign\n");
  const auto &target = data.get_target();

  if (is_output(target)){
    Index_ID nid_o_target = create_output(dfg, state, target);//sh dbg
    fmt::print("create node for output target:{}, nid:{} \n", target, nid_o_target); //sh dbg
    //To Do: need to connect this output node with sum op
    //dfg->add_edge(Node_Pin(state->get_reference(target), 2, false),Node_Pin(nid_o_target,2,true)); //buggy source!!! 7/28/2018
    vector<Index_ID> oprd_ids = process_operands(dfg, cfg, state, data, node);
    for (Index_ID src_nid : oprd_ids){
      Port_ID src_pid = 0;
      Port_ID dst_pid = 0;
      if(dfg->node_type_get(src_nid).op == Or_Op)// create pure dummy assign op instead of reduced Or_op?
        src_pid = 1; // output pid=1 for reduced Or_Op
      dfg->add_edge(Node_Pin(src_nid, src_pid, false), Node_Pin(nid_o_target, dst_pid, true));
      fmt::print("create edge ({}->{})\n", dfg->get_node_wirename(src_nid), dfg->get_node_wirename(nid_o_target));
    }
  }else{
    Index_ID target_nid = create_node(dfg, state, target);
    fmt::print("create node for internal target:{}, nid:{}\n", target, target_nid);
    dfg->set_node_instance_name(target_nid, target);
    dfg->node_type_set(target_nid, node_type_from_text(data.get_operator()));
    vector<Index_ID> oprd_ids = process_operands(dfg, cfg, state, data, node);

    for (Index_ID src_nid : oprd_ids){
      Port_ID src_pid = 0;
      Port_ID dst_pid = 0;
      if(dfg->node_type_get(src_nid).op == Or_Op)// create pure dummy assign op instead of reduced Or_op?
        src_pid = 1; // output pid=1 for reduced Or_Op

      if(data.get_operator() == ":" ){
        if(src_nid != oprd_ids[0]){ //To Do:prevent connecting dummy node for function argument assignment. ex. tmp = a:$a
          dfg->add_edge(Node_Pin(src_nid, src_pid, false), Node_Pin(target_nid, dst_pid, true));
          fmt::print("create edge ({}->{})\n", dfg->get_node_wirename(src_nid), dfg->get_node_wirename(target_nid));}
      }else if(data.get_operator() == "as"){
        dfg->add_edge(Node_Pin(src_nid, src_pid, false), Node_Pin(target_nid, dst_pid, true));
        fmt::print("create edge ({}->{})\n", dfg->get_node_wirename(src_nid), dfg->get_node_wirename(target_nid));
        state->set_id2id_subg(target_nid,src_nid);
        state->set_name2id_subg(target,target_nid);
      }else{
        dfg->add_edge(Node_Pin(src_nid, src_pid, false), Node_Pin(target_nid, dst_pid, true));
        fmt::print("create edge ({}->{})\n", dfg->get_node_wirename(src_nid), dfg->get_node_wirename(target_nid));
      }
    }
  }

  fmt::print("is_fluid:{}, is_output:{}, is_register:{}\n", state->fluid_df(),is_output(target), is_register(target));
  if (state->fluid_df() && (is_output(target) || is_register(target)))
    add_write_marker(dfg, state, target);
}



Index_ID Pass_dfg::process_if(LGraph *dfg, const LGraph *cfg, CF2DF_State *state, const CFG_Node_Data &data, Index_ID node) {
  fmt::print("process_if\n");
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
  std::unordered_set<std::string> phis_added;

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

void Pass_dfg::add_phi(LGraph *dfg, CF2DF_State *parent, CF2DF_State *tstate, CF2DF_State *fstate, Index_ID condition, const std::string &variable) {
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
  const vector<std::string> &oprds = data.get_operands();
  vector<Index_ID> oprd_ids(oprds.size());

  for (size_t i = 0; i < oprds.size(); i++) {
    if (state->has_reference(oprds[i])){
      oprd_ids[i] = state->get_reference(oprds[i]);
      fmt::print("operand:{} has been created before\n", oprds[i]);//sh dbg
    }
    else {
      if (is_constant(oprds[i])){
        oprd_ids[i] = default_constant(dfg, state);
        fmt::print("create node for constant operand:{}, nid:{}\n", oprds[i], oprd_ids[i]); //sh dbg
      }
      else if (is_register(oprds[i])){
        oprd_ids[i] = create_register(dfg, state, oprds[i]);
        fmt::print("create node for register operand:{}, nid:{}\n", oprds[i], oprd_ids[i]); // sh dbg
      }
      else if (is_input(oprds[i])){
        oprd_ids[i] = create_input(dfg, state, oprds[i]);
        fmt::print("create node for input operand:{}, nid:{}\n", oprds[i], oprd_ids[i]); // sh dbg
      }
      else if (is_output(oprds[i])){
        oprd_ids[i] = create_output(dfg, state, oprds[i]);
        fmt::print("create node for output operand:{}, nid:{}\n", oprds[i], oprd_ids[i]); // sh dbg
      }
      else{
        //undefined oprd_ids[0]!!!
        //if(data.get_operator() == ":" ){
        //  if(i != 0){ //prevent connecting dummy node for function argument assignment. ex. tmp = a:$a
        //    oprd_ids[i] = create_private(dfg, state, oprds[i]);
        //    fmt::print("create node for private operand:{}, nid:{}\n", oprds[i], oprd_ids[i]); // sh dbg
        //  }
        //}
        //else{
        //  oprd_ids[i] = create_private(dfg, state, oprds[i]);
        //  fmt::print("create node for private operand:{}, nid:{}\n", oprds[i], oprd_ids[i]); // sh dbg
        //}
        oprd_ids[i] = create_private(dfg, state, oprds[i]);
        fmt::print("create node for private operand:{}, nid:{}\n", oprds[i], oprd_ids[i]); // sh dbg
      }
    }

    if (state->fluid_df() && is_input(oprds[i]))
      add_read_marker(dfg, state, oprds[i]);
  }

  return oprd_ids;
}

void Pass_dfg::assign_to_true(LGraph *dfg, CF2DF_State *state, const std::string &v) {
  Index_ID node = create_node(dfg, state, v);
  fmt::print("create node nid:{}\n", node);
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

