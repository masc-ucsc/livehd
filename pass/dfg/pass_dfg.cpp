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

unsigned int Pass_dfg::temp_counter = 0;

Pass_dfg::Pass_dfg(const std::string &key, const std::string &value) : Pass() { opack.set(key,value); }

LGraph * Pass_dfg::generate_dfg() {
  assert(!opack.src.empty());

  //LGraph *cfg = new LGraph(opack.path, opack.src, false);
  const LGraph *cfg = new LGraph(opack.path, opack.src, false);
  regen(cfg);
  delete cfg;

  LGraph *dfg = new LGraph(opack.path, opack.name, false);
  return dfg;
}

LGraph *Pass_dfg::regen(const LGraph *cfg) {
  assert(!opack.name.empty());
  LGraph *dfg = new LGraph(opack.path, opack.name, false); // true? should it clear the DFG?

  cfg_2_dfg(dfg, cfg);
  dfg->sync();
  return dfg;
  //delete dfg;//do we need to manually delete it?
}

void  Pass_dfg::optimize() {
  assert(!opack.src.empty());
  LGraph *dfg = new LGraph(opack.path, opack.src, false);
  trans(dfg);
  dfg->sync();
}
void Pass_dfg::trans(LGraph *dfg) {
  LGraph* sub_graph = nullptr;
  //resolve pending graph
  for(auto idx : dfg->fast()) {
    if(dfg->node_type_get(idx).op == DfgPendingGraph_Op){
      if((sub_graph = LGraph::find_lgraph(dfg->get_path(), ((std::string)(dfg->get_node_wirename(idx))+"_dfg")))){
        dfg->node_subgraph_set(idx, (uint32_t)sub_graph->lg_id());
        fmt::print("resolve pending subgraph! nid:{}, sub_graph name:{}, sub_graph_id:{}\n", idx, dfg->get_node_wirename(idx), sub_graph->lg_id());
      }
      else{
        fmt::print("cannot resolve pending subgraph!!\n");
        assert(0);
      }
    }
  }

  //bits inference
  for(auto idx : dfg->fast()) {
    for(const auto &out : dfg->out_edges(idx)) {
      Index_ID src_nid      = idx;
      Index_ID dst_nid      = out.get_idx();
      Port_ID  src_pid      = out.get_out_pin().get_pid();
      Port_ID  dst_pid      = out.get_inp_pin().get_pid();
      uint16_t src_nid_size = dfg->get_bits(src_nid);
      uint16_t dst_nid_size = dfg->get_bits(dst_nid);

      if(dfg->node_type_get(idx).op == SubGraph_Op){
        LGraph *subgraph = dfg->get_library()->get_graph(dfg->subgraph_id_get(idx));
        const char *out_name = subgraph->get_graph_output_name_from_pid(src_pid);
        uint16_t    out_size = subgraph->get_bits(subgraph->get_graph_output(out_name).get_nid());
        dfg->set_bits(dst_nid,out_size);
      }else if(dfg->node_type_get(dst_nid).op == Mux_Op){
        ;//don't infetence when dst is a mux, it is handle at the mux construction step
      }else if(dfg->node_type_get(dst_nid).op == Equals_Op){
        ;//don't infetence when dst is a comparator, the result should be a bool
      }else{
        if(src_nid_size > dst_nid_size)
          dfg->set_bits(dst_nid, src_nid_size);
      }
    }
  }
}

void Pass_dfg::cfg_2_dfg(LGraph *dfg, const LGraph *cfg) {
  Index_ID    itr = find_cfg_root(cfg);
  //Aux_node auxnd(dfg);
  Aux_node auxnd_global;
  Aux_tree aux_tree(&auxnd_global);
  process_cfg(dfg, cfg, &aux_tree, itr);
  finalize_gconnect(dfg, &auxnd_global);

  //attach_outputs(dfg, &auxnd);
  fmt::print("calling sync\n");
}

void Pass_dfg::finalize_gconnect(LGraph *dfg, const Aux_node *auxnd_global) {
  for (const auto &pair : auxnd_global->get_auxtab()) {
    if (is_output(pair.first)){
      Index_ID dst_nid = dfg->get_graph_output(pair.first.substr(1)).get_nid();
      Index_ID src_nid = pair.second;
      auto bits = dfg->get_bits(src_nid);
      dfg->set_bits(dst_nid,bits);
      dfg->add_edge(Node_Pin(src_nid, 0, false), Node_Pin(dst_nid, 0, true));
    }
    else if(is_register(pair.first)){
      ;//balabala
    }
  }
}

Index_ID Pass_dfg::process_cfg(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, Index_ID top_node) {
  Index_ID itr = top_node;
  Index_ID last_itr = 0;

  while (itr != 0) {
    last_itr = itr;
    itr = process_node(dfg, cfg, aux_tree, itr);
    fmt::print("cfg nid:{} process finished!!\n\n", last_itr);
  }
  aux_tree->print_cur_auxnd();
  fmt::print("\n\n");
  return last_itr;
}

Index_ID Pass_dfg::process_node(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, Index_ID cfg_node) {
  CFG_Node_Data data(cfg, cfg_node);

  //sh dbg
  fmt::print("Processing CFG node:{}\n", cfg_node);
  fmt::print("target:[{}], operator:[{}], ", data.get_target(), data.get_operator());
  fmt::print("operands:[");
  for(const auto &i:data.get_operands())
    fmt::print("{}, ", i);
  fmt::print("]");
  fmt::print("\n");

  switch (cfg->node_type_get(cfg_node).op) {
  case CfgAssign_Op:
    process_assign(dfg, aux_tree, data);
    return get_cfg_child(cfg, cfg_node);
  case CfgFunctionCall_Op:
    process_func_call(dfg, cfg, aux_tree, data);
    return get_cfg_child(cfg, cfg_node);
  case CfgIf_Op:
    return process_if(dfg, cfg, aux_tree, data, cfg_node);
  case CfgIfMerge_Op:
    return 0;
  default:
    fmt::print("\n\n*************Unrecognized cfg_node type[n={}]: {}\n", cfg_node, cfg->node_type_get(cfg_node).get_name());
    return get_cfg_child(cfg, cfg_node);
  }
}

void Pass_dfg::process_func_call(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  //for func_call, all the node should be created before, you just connect them. No need to create target node
  fmt::print("process_func_call\n");
  const auto &target = data.get_target();
  const auto &oprds  = data.get_operands();
  const auto &oprd_ids = process_operands(dfg, aux_tree, data);// all the operands should be created before, just get back oprd_ids
  LGraph* sub_graph = nullptr;
  Index_ID subg_root_nid = aux_tree->get_alias(oprds[0]);

  if((sub_graph = LGraph::find_lgraph(cfg->get_path(), ((std::string)(dfg->get_node_wirename(subg_root_nid))+"_dfg")))){
    dfg->node_subgraph_set(subg_root_nid, (uint32_t)sub_graph->lg_id());
    fmt::print("set subgraph on nid:{}, sub_graph name:{}, sub_graph_id:{}\n", subg_root_nid, dfg->get_node_wirename(subg_root_nid), sub_graph->lg_id());
  }else{
    dfg->node_type_set(subg_root_nid, DfgPendingGraph_Op);
    fmt::print("set pending graph on nid:{}, sub_graph name should be:{}\n", subg_root_nid, dfg->get_node_wirename(subg_root_nid));
  }

  aux_tree->set_alias(target, oprd_ids[0]);

  //connect 1st operand with [2nd,3rd,...] operands
  std::vector<Index_ID> subg_input_ids(oprd_ids.begin()+1, oprd_ids.end());
  process_connections(dfg, subg_input_ids, subg_root_nid);
}


void Pass_dfg::process_assign(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data){
  fmt::print("process_assign\n");
  const auto                     &target = data.get_target();
  const std::vector<std::string> &oprds  = data.get_operands();
  const std::string              &op     = data.get_operator();
  Index_ID                       oprd_id0;
  Index_ID                       oprd_id1;
  if(is_pure_assign_op(op)){
    if(is_output(target) && !dfg->is_graph_output(target.substr(1)))
      create_output(dfg, aux_tree, target);
    oprd_id0 = process_operand(dfg, aux_tree, oprds[0]);
    aux_tree->set_alias(target, oprd_id0);
  }
  else if(is_label_op(op)){
    if(oprds[0] == "__bits"){
      //Index_ID floating_id = resolve_constant(dfg, aux_tree, oprds[1]);
      Index_ID floating_id = process_operand(dfg, aux_tree, oprds[1]);
      aux_tree->set_alias(target, floating_id);
    }else if(oprds[0] == "__fluid"){
      ;//balabala
    }else{//function argument assign
      oprd_id1 = process_operand(dfg, aux_tree, oprds[1]);
      aux_tree->set_alias(target, oprd_id1);
    }
  }
  else if(is_as_op(op)){
    oprd_id0 = process_operand(dfg, aux_tree, oprds[0]);
    //process target
    if(is_input(target)){
      assert(dfg->node_value_get(oprd_id0));
      auto bits = dfg->node_value_get(oprd_id0);
      Index_ID target_id = create_input(dfg, aux_tree, target, bits);
      fmt::print("create node for input target:{}, nid:{}\n", target, target_id);
    }else if(is_output(target)){
      assert(dfg->node_value_get(oprd_id0));
      auto bits = dfg->node_value_get(oprd_id0);
      Index_ID target_id = create_output(dfg, aux_tree, target, bits);
      fmt::print("create node for output target:{}, nid:{}\n", target, target_id);
    }else
      aux_tree->set_alias(target, oprd_id0);
  }
  else if(is_unary_op(op)){
    oprd_id0 = process_operand(dfg, aux_tree, oprds[0]);
    aux_tree->set_alias(target, oprd_id0);
  }
  else if(is_compute_op(op)){
    std::vector<Index_ID> oprd_ids;
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[0]));
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[1]));
    Index_ID target_id = create_node(dfg, aux_tree, target);
    fmt::print("create node for internal target:{}, nid:{}\n", target, target_id);
    dfg->node_type_set(target_id, node_type_from_text(op));
    auto max_bits = std::max(dfg->get_bits(oprd_ids[0]),dfg->get_bits(oprd_ids[1])) + 1;
    dfg->set_bits(target_id,max_bits);
    process_connections(dfg, oprd_ids, target_id);
  }
  else if(is_compare_op(op)){
    std::vector<Index_ID> oprd_ids;
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[0]));
    oprd_ids.push_back(process_operand(dfg, aux_tree, oprds[1]));
    Index_ID target_id = create_node(dfg, aux_tree, target);
    fmt::print("create node for internal target:{}, nid:{}\n", target, target_id);
    dfg->node_type_set(target_id, node_type_from_text(op));
    //dfg->set_bits(target_id,max_bits);
    process_connections(dfg, oprd_ids, target_id);
  }

}

void Pass_dfg::process_connections(LGraph *dfg, const std::vector<Index_ID> &src_nids, const Index_ID &dst_nid){
  for (unsigned i = 0; i<src_nids.size();i++){
    Index_ID src_nid =  src_nids.at(i);
    fmt::print("src_nid:{}\n", src_nid);
    fmt::print("src_nids size:{}\n", src_nids.size());
    //Port_ID  src_pid = (dfg->node_type_get(src_nid).op == Or_Op) ? (uint16_t)1 : (uint16_t)0; // output pid=1 for reduced Or_Op
    Port_ID  src_pid = 0;
    Port_ID  dst_pid = (dfg->node_type_get(dst_nid).op == Sum_Op            )? (uint16_t)1 :
                       (dfg->node_type_get(dst_nid).op == DfgPendingGraph_Op)? (uint16_t)i :
                       (dfg->node_type_get(dst_nid).op == SubGraph_Op       )? (uint16_t)i : (uint16_t)0;

    dfg->add_edge(Node_Pin(src_nid, src_pid, false), Node_Pin(dst_nid, dst_pid, true));
    //fmt::print("add_edge {}->{}\n", dfg->get_node_wirename(src_nid), dfg->get_node_wirename(dst_nid));
  }
}


Index_ID Pass_dfg::process_operand(LGraph *dfg, Aux_tree *aux_tree, const std::string oprd) {
  Index_ID oprd_id;
  if (aux_tree->has_alias(oprd)){
    oprd_id = aux_tree->get_alias(oprd);
    fmt::print("operand:{} has an alias:{}\n", oprd, oprd_id);
  }
  else {
    if (is_constant(oprd)){
      oprd_id = resolve_constant(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for constant operand:{}, nid:{}\n", oprd, oprd_id);
    }
    //else if (is_register(oprd)){
    //  //oprd_id = create_register(dfg, aux_tree, oprd);
    //  //aux_tree->set_alias(oprd, oprd_id);
    //  //fmt::print("create node for register operand:{}, nid:{}\n", oprd, oprd_id);
    //}
    else if (is_input(oprd)){
      oprd_id = create_input(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for input operand:{}, nid:{}\n", oprd, oprd_id);
    }
    else if (is_output(oprd)){
      oprd_id = create_output(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for output operand:{}, nid:{}\n", oprd, oprd_id);
    }
    else if (is_reference(oprd)){
      oprd_id = create_reference(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for reference operand:{}, nid:{}\n", oprd, oprd_id);
    }
    else{
      oprd_id = create_private(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_id);
      fmt::print("create node for private operand:{}, nid:{}\n", oprd, oprd_id);
    }
  }

  //if (aux_tree->fluid_df() && is_input(oprd))
  //  add_read_marker(dfg, aux_tree, oprd);

  return oprd_id;
}


Index_ID Pass_dfg::process_if(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data, Index_ID cfg_node) {
  fmt::print("process_if\n");
  assert(aux_tree->has_alias(data.get_target()));
  Index_ID cond = aux_tree->get_alias(data.get_target());
  const auto &operands = data.get_operands();
  auto *tauxnd = new Aux_node;
  auto *fauxnd = new Aux_node;
  auto *pauxnd = aux_tree->get_cur_auxnd(); //parent aux

  Index_ID tbranch = (Index_ID)std::stol(operands[0]);
  Index_ID fbranch = (Index_ID)std::stol(operands[1]);

  aux_tree->set_parent_child(pauxnd, tauxnd, true);
  Index_ID tb_next = get_cfg_child(cfg, process_cfg(dfg, cfg, aux_tree, tbranch));
  fmt::print("branch true finish! tb_next:{}\n", tb_next);

  //if (fbranch != cfg_node) {                       // original, buggy
  if (fbranch != 0){                                 // there is an 'else' clause
    aux_tree->set_parent_child(pauxnd,  fauxnd, false);
    Index_ID fb_next = get_cfg_child(cfg, process_cfg(dfg, cfg, aux_tree, fbranch));
    assert(tb_next == fb_next);
    fmt::print("branch false finish! tb_next:{}\n", tb_next);
  }

  resolve_phis (dfg, pauxnd, tauxnd, fauxnd, cond);//the auxT,F should be empty and are safe to be deleted after

  if(fbranch != 0){
    aux_tree->delete_child(aux_tree->get_cur_auxnd(),  fauxnd, false);
    aux_tree->auxes_stack_pop();
  }

  aux_tree->delete_child(aux_tree->get_cur_auxnd(),  tauxnd, true);
  aux_tree->auxes_stack_pop();

  return tb_next;
  //return 0;
}

//void Pass_dfg::add_phis(LGraph *dfg, const LGraph *cfg, Aux_node *parent, Aux_node *taux_tree, Aux_node *faux_tree, Index_ID condition) {
  //std::unordered_set<std::string> phis_added;

  //for (const auto &pair : taux_tree->get_auxtab()) {
  //  if (reference_changed(parent, taux_tree, pair.first)) {
  //    add_phi(dfg, parent, taux_tree, faux_tree, condition, pair.first);
  //    phis_added.insert(pair.first);
  //  }
  //}

  //for (const auto &pair : faux_tree->get_auxtab()) {
  //  if (phis_added.find(pair.first) == phis_added.end() && reference_changed(parent, faux_tree, pair.first))
  //    add_phi(dfg, parent, taux_tree, faux_tree, condition, pair.first);
  //}
//}

//void Pass_dfg::add_phi(LGraph *dfg, Aux_node *parent, Aux_node *taux_tree, Aux_node *faux_tree, Index_ID condition, const std::string &variable) {
  //Index_ID tid = resolve_phi_branch(dfg, parent, taux_tree, variable);
  //Index_ID fid = resolve_phi_branch(dfg, parent, faux_tree, variable);

  //Index_ID phi = dfg->create_node().get_nid();
  //dfg->node_type_set(phi, Mux_Op);
  //auto tp = dfg->node_type_get(phi);

  //Port_ID tin = tp.get_input_match("B");
  //Port_ID fin = tp.get_input_match("A");
  //Port_ID cin = tp.get_input_match("S");

  //dfg->add_edge(Node_Pin(tid, 0, false), Node_Pin(phi, tin, true));
  //dfg->add_edge(Node_Pin(fid, 0, false), Node_Pin(phi, fin, true));
  //dfg->add_edge(Node_Pin(condition, 0, false), Node_Pin(phi, cin, true));

  //parent->set_alias(variable, phi);
//}

//Index_ID Pass_dfg::resolve_phi_branch(LGraph *dfg, Aux_node *parent, Aux_node *branch, const std::string &variable) {
  //if (branch->has_alias(variable))
  //  return branch->get_alias(variable);
  //else if (parent->has_alias(variable))
  //  return parent->get_alias(variable);
  //else if (is_register(variable))
  //  return create_register(dfg, parent, variable);
  //else if (is_input(variable))
  //  return create_input(dfg, parent, variable);
  //else if (is_output(variable))
  //  return create_output(dfg, parent, variable);
  //else
  //  return create_default_const(dfg, parent);
//  return 0;
//}


void Pass_dfg::assign_to_true(LGraph *dfg, Aux_tree *aux_tree, const std::string &v) {
  Index_ID node = create_node(dfg, aux_tree, v);
  fmt::print("create node nid:{}\n", node);
  dfg->node_type_set(node, Or_Op);

  dfg->add_edge(Node_Pin(create_true_const(dfg, aux_tree), 0, false), Node_Pin(node, 0, true));
  dfg->add_edge(Node_Pin(create_true_const(dfg, aux_tree), 0, false), Node_Pin(node, 0, true));
}

void Pass_dfg::attach_outputs(LGraph *dfg, Aux_tree *aux_tree) {
//  for (const auto &pair : aux_tree->copy().get_auxtab()) {
//    const auto &var = pair.first;
//
//    if (is_register(var) || is_output(var)) {
//      Index_ID lref = pair.second;
//
//      Index_ID oid = create_output(dfg, aux_tree, var);
//      dfg->add_edge(Node_Pin(lref, 0, false), Node_Pin(oid, 0, true));
//    }
//  }

//  if (aux_tree->fluid_df())
//    add_fluid_behavior(dfg, aux_tree);
}

void Pass_dfg::add_fluid_behavior(LGraph *dfg, Aux_tree *aux_tree) {
  std::vector<Index_ID> inputs, outputs;
  add_fluid_ports(dfg, aux_tree, inputs, outputs);

}

void Pass_dfg::add_fluid_ports(LGraph *dfg, Aux_tree *aux_tree, std::vector<Index_ID> &data_inputs, std::vector<Index_ID> &data_outputs) {
  //for (const auto &pair : aux_tree->outputs()) {
  //  if (!is_valid_marker(pair.first) && !is_retry_marker(pair.first)) {
  //    auto valid_output = valid_marker(pair.first);
  //    auto retry_input = retry_marker(pair.first);
  //    data_outputs.push_back(aux_tree->get_alias(pair.first));

  //    if (!aux_tree->has_alias(valid_output))
  //      create_output(dfg, aux_tree, valid_output);

  //    if (!aux_tree->has_alias(retry_input))
  //      create_input(dfg, aux_tree, retry_input);
  //  }
  //}

  //for (const auto &pair : aux_tree->inputs()) {
  //  if (!is_valid_marker(pair.first) && !is_retry_marker(pair.first)) {
  //    auto valid_input = valid_marker(pair.first);
  //    auto retry_output = retry_marker(pair.first);
  //    data_inputs.push_back(aux_tree->get_alias(pair.first));

  //    if (!aux_tree->has_alias(valid_input))
  //      create_input(dfg, aux_tree, valid_input);

  //    if (!aux_tree->has_alias(retry_output))
  //      create_output(dfg, aux_tree, retry_output);
  //  }
  //}
}

void Pass_dfg::add_fluid_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs, const std::vector<Index_ID> &data_outputs) {
  //Index_ID abort_id = add_abort_logic(dfg, aux_tree, data_inputs, data_outputs);


}

void Pass_dfg::add_abort_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs, const std::vector<Index_ID> &data_outputs) {

}

Index_ID Pass_dfg::find_cfg_root(const LGraph *cfg) {
  for(auto idx : cfg->fast()) {
    if(cfg->is_root(idx))
      return idx;
  }

  assert(false);
}

Index_ID Pass_dfg::get_cfg_child(const LGraph *cfg, Index_ID node) {
  std::vector<Index_ID> children;

  for(const auto &cedge : cfg->out_edges(node))
    children.push_back(cedge.get_inp_pin().get_nid());

  if(children.size() == 1)
    return children[0];
  else if(children.size() == 0)//reach end of cfg
    return 0;
  else
    assert(false);
}

std::vector<Index_ID> Pass_dfg::process_operands(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  const std::vector<std::string> &oprds = data.get_operands();
  std::vector<Index_ID> oprd_ids(oprds.size());
  const std::string &op = data.get_operator();

  for (size_t i = 0 ; i <oprd_ids.size();i++) {
    if (aux_tree->has_alias(oprds[i])){
      oprd_ids[i] = aux_tree->get_alias(oprds[i]);
      fmt::print("operand:{} has an alias:{}\n", oprds[i], oprd_ids[i]);
    }
    else {
      if (is_constant(oprds[i])){
        //oprd_ids[i] = create_default_const(dfg, aux_tree);
        oprd_ids[i] = resolve_constant(dfg, aux_tree, oprds[i]);
        fmt::print("create node for constant operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      }
      //else if (is_register(oprds[i])){
      //  //oprd_ids[i] = create_register(dfg, aux_tree, oprds[i]);
      //  //fmt::print("create node for register operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      //}
      else if (is_input(oprds[i])){
        oprd_ids[i] = create_input(dfg, aux_tree, oprds[i]);
        fmt::print("create node for input operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      }
      else if (is_output(oprds[i])){
        oprd_ids[i] = create_output(dfg, aux_tree, oprds[i]);
        fmt::print("create node for output operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      }
      else if (is_reference(oprds[i])){
        oprd_ids[i] = create_reference(dfg, aux_tree, oprds[i]);
        fmt::print("create node for reference operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      }
      else{
        oprd_ids[i] = create_private(dfg, aux_tree, oprds[i]);
        fmt::print("create node for private operand:{}, nid:{}\n", oprds[i], oprd_ids[i]);
      }
    }

    //if (aux_tree->fluid_df() && is_input(oprds[i]))
    //  add_read_marker(dfg, aux_tree, oprds[i]);
  }

  return oprd_ids;
}

void Pass_dfg::resolve_phis(LGraph *dfg, Aux_node *pauxnd, Aux_node *tauxnd, Aux_node *fauxnd, Index_ID cond){
  fmt::print("resolve phis");
  //resolve phi in branch true
  for(auto &iter : tauxnd->get_auxtab()){
    if(fauxnd && fauxnd->has_alias(iter.first)){
      Index_ID tid = iter.second;
      Index_ID fid = fauxnd->get_alias(iter.first); //return Index_ID
      fauxnd->del_alias(iter.first);
      create_mux(dfg, pauxnd, tid, fid, cond, iter.first);
    }else if(pauxnd->has_alias(iter.first)){
      fmt::print("hello\n");
      Index_ID tid = iter.second;
      Index_ID fid = pauxnd->get_alias(iter.first);
      pauxnd->del_alias(iter.first);
      create_mux(dfg, pauxnd, tid, fid, cond, iter.first);
    }else{//design later
      //Index_ID tid = iter.second;
      //Index_ID fid = default_node; //or search through all parents?
      //create_mux(dfg, pauxnd, tid, fid, cond, iter.first);
    }
    tauxnd->del_alias(iter.first);
  }

  //resolve phi in branch false
  for(auto &iter : fauxnd->get_auxtab()){
    if(pauxnd->has_alias(iter.first)){
      Index_ID tid  = pauxnd->get_alias(iter.first);
      Index_ID fid  = iter.second;
      pauxnd->del_alias(iter.first);
      create_mux(dfg, pauxnd, tid, fid, cond, iter.first);
    }else{//design later
      //Index_ID tid  = default_node;
      //Index_ID fid  = iter.second; //or search through all parents?
      //create_mux(dfg, pauxnd, tid, fid, cond, iter.first);
    }
    fauxnd->del_alias(iter.first);
  }
  //so far the tauxnd and fauxnd should be empty
}

void Pass_dfg::create_mux(LGraph *dfg, Aux_node *pauxnd, Index_ID tid, Index_ID fid, Index_ID cond, const std::string &var){
  Index_ID phi = dfg->create_node().get_nid();
  dfg->node_type_set(phi, Mux_Op);
  auto tp = dfg->node_type_get(phi);

  Port_ID tin = tp.get_input_match("B");
  Port_ID fin = tp.get_input_match("A");
  Port_ID cin = tp.get_input_match("S");

  auto max_bits = std::max(dfg->get_bits(tid),dfg->get_bits(fid));
  dfg->set_bits(tid,max_bits);
  dfg->set_bits(fid,max_bits);
  dfg->set_bits(phi,max_bits);

  dfg->add_edge(Node_Pin(tid,  0, false), Node_Pin(phi, tin, true));
  dfg->add_edge(Node_Pin(fid,  0, false), Node_Pin(phi, fin, true));
  dfg->add_edge(Node_Pin(cond, 0, false), Node_Pin(phi, cin, true));
  pauxnd->set_alias(var, phi);
}

void Pass_dfg_options::set(const std::string &key, const std::string &value) {
  try {
    if (is_opt(key,"src") )
      src = value;
    else
      set_val(key,value);

  }catch (const std::invalid_argument& ia) {
    fmt::print("ERROR: key {} has an invalid argument {}\n",key);
  }

  console->warn("pass_dfg src:{} path:{} name:{}", src, path, name);
}

