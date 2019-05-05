//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <cassert>
#include <cstdlib>
#include <unordered_set>
#include <vector>

#include "cfg_node_data.hpp"
#include "lgedge.hpp"
#include "lgedgeiter.hpp"
#include "pass_dfg.hpp"
#include "eprp_utils.hpp"

//SH:FIXME: (1)need to extend to real tuple function, for now it acts as a pure assignment
//SH:FIXME: (2)should add some flag to identify the uniqueness of "as" op
//SH:FIXME: (3)differentiate logical AND <-> bitwise AND <-> reduced AND in Pyrope

void setup_pass_dfg() {
  Pass_dfg p;
  p.setup();
}

void Pass_dfg::setup() {
  Eprp_method m1("pass.dfg.generate", "generate a dfg lgraph from a cfg lgraph", &Pass_dfg::generate);
  m1.add_label_optional("path", "lgraph path");
  m1.add_label_required("name", "lgraph name");
  register_pass(m1);

  Eprp_method m2("pass.dfg.optimize", "optimize a dfg lgraph", &Pass_dfg::optimize);
  m2.add_label_optional("path", "lgraph path");
  m2.add_label_optional("name", "lgraph name");

  register_pass(m2);

  Eprp_method m3("pass.dfg.finalize_bitwidth", "patch fake bitwidth for a dfg lgraph", &Pass_dfg::finalize_bitwidth);
  m3.add_label_optional("path", "lgraph path");
  m3.add_label_optional("name", "lgraph name");

  register_pass(m3);
}

void Pass_dfg::generate(Eprp_var &var) {
  Pass_dfg p;

  std::vector<LGraph *> dfgs;
  for(auto &cfg : var.lgs) {
    if(!graph_name_ends_with(cfg->get_name(), "_cfg"))
      continue;

    dfgs = p.hier_generate_dfgs(cfg);
  }

  if(dfgs.empty()) {
    warn(fmt::format("pass.dfg.generate needs an input cfg lgraph. Either name or |> from lgraph.open"));
    I(false);
    return;
  }
}

std::vector<LGraph*> Pass_dfg::hier_generate_dfgs(LGraph *cfg_parent){
  std::vector<LGraph *> dfgs;
  fmt::print("hierarchical dfg generation start!\n");
  fmt::print("top graph lgid:{}\n", cfg_parent->lg_id());
  auto dfg_name = cfg_parent->get_name().substr(0, cfg_parent->get_name().size() - 4);
  LGraph *dfg = LGraph::create(cfg_parent->get_path(), dfg_name, cfg_parent->get_name());
  I(dfg);
  do_generate(cfg_parent, dfg);
  dfgs.push_back(dfg);
  fmt::print("top module cfg->dfg done!\n\n");

  cfg_parent->each_sub_graph_fast([cfg_parent, &dfgs, this](Node node, Lg_type_id lgid){
    fmt::print("sub-graph lgid:{}\n", lgid);
    LGraph *cfg_child = LGraph::open(cfg_parent->get_path(), lgid);
    if(cfg_child== nullptr){
      Pass::error("hierarchy for {} could not open instance {} with lgid {}", cfg_parent->get_name(), lgid);
    } else {
      I(graph_name_ends_with(cfg_child->get_name(),"_cfg"));
      std::string_view child_dfg_name(cfg_child->get_name().data(), cfg_child->get_name().size()-4); // _cfg
      LGraph *dfg_child = LGraph::create(cfg_child->get_path(), child_dfg_name, cfg_child->get_name());
      I(dfg_child);

      do_generate(cfg_child, dfg_child);
      dfgs.push_back(dfg_child);
    }
  });
  return dfgs;
}

void Pass_dfg::optimize(Eprp_var &var) {
  for(auto &g : var.lgs) {
    Pass_dfg p;
    p.hier_optimize_dfgs(g);
  }
}

void Pass_dfg::hier_optimize_dfgs(LGraph *dfg_parent){
  fmt::print("hierarchical dfg optimization start!\n");
  fmt::print("top graph lgid:{}\n", dfg_parent->lg_id());
  do_optimize(dfg_parent);

  dfg_parent->each_sub_graph_fast([dfg_parent, this](Node node, Lg_type_id lgid){
    fmt::print("sub-graph lgid:{}\n", lgid);
    LGraph *dfg_child = LGraph::open(dfg_parent->get_path(), lgid);
    if(dfg_child==nullptr){
      Pass::error("hierarchy for {} could not open instance with lgid {}", dfg_parent->get_name(), lgid);
    } else {
      I(dfg_child);
      do_optimize(dfg_child);
    }
  });
}

void Pass_dfg::finalize_bitwidth(Eprp_var &var) {
  for(auto &g : var.lgs) {
    Pass_dfg p;
    p.hier_finalize_bits_dfgs(g);
    g->close();
  }
}

void Pass_dfg::hier_finalize_bits_dfgs(LGraph *dfg_parent){
  fmt::print("hierarchical dfg finalize_bits start!\n");
  fmt::print("topg lgid:{}\n", dfg_parent->lg_id());
  do_finalize_bitwidth(dfg_parent);

  dfg_parent->each_sub_graph_fast([dfg_parent, this](Node node, Lg_type_id lgid){
    fmt::print("subgraph lgid:{}\n", lgid);
    LGraph *dfg_child = LGraph::open(dfg_parent->get_path(), lgid);
    if (dfg_child == nullptr) {
      Pass::error("hierarchy for {} could not open instance with lgid {}", dfg_parent->get_name(), lgid);
    } else {
      I(dfg_child);
      do_finalize_bitwidth(dfg_child);
    }
  });
}

Pass_dfg::Pass_dfg():Pass("dfg"), mux_cnt(0){}

void Pass_dfg::do_generate(LGraph *cfg, LGraph *dfg) {
  cfg_2_dfg(cfg, dfg);
  dfg->close();//instead of using lg->sync(), you should just call close()
}

void Pass_dfg::do_optimize(LGraph *&ori_dfg) {
  trans(ori_dfg);
  ori_dfg->close();
}

void Pass_dfg::trans(LGraph *dfg) {
  // resolve pending graph instantiation
  for(auto nid : dfg->fast()) {
    auto node = Node(dfg, 0, Node::Compact(nid));
    if(node.get_type().op == DfgPendingGraph_Op) {
      //SH:FIXME: watch out! you've occasionally set sub-graph name to pid0
      //SH:FIXME: you might want to change whole structure to Node_pin based, and
      //SH:FIXME: store the subgraph name in "Node::set_name()"
      fmt::print("subgraph name is:{}\n", node.get_driver_pin(0).get_name());
      LGraph* sub_graph = LGraph::open(dfg->get_path(), node.get_driver_pin(0).get_name());
      I(sub_graph);

      node.set_type_subgraph(sub_graph->lg_id());//change from DfgPendingGraph_Op to Subgraph_Op
      fmt::print("resolve pending subG! lg_id:{}, name:{}\n",sub_graph->lg_id(), node.get_driver_pin(0).get_name());
      fmt::print("input name:{}\n",  sub_graph->get_graph_input_name_from_pid(1));
      fmt::print("input name:{}\n",  sub_graph->get_graph_input_name_from_pid(2));
      fmt::print("output name:{}\n", sub_graph->get_graph_output_name_from_pid(1));
    }
  }

  //SH:FIXME: new top<->subgraph connection afer Jose submit LGraph v0.2
  //resolve top <-> subgraph IO connection


  for(auto nid : dfg->fast()) {
    auto node = Node(dfg, 0, Node::Compact(nid));
      if(node.get_type().op == Equals_Op) {
        node.get_driver_pin().set_bits(1);
      } else if(node.get_type().op == GreaterEqualThan_Op) {
        node.get_driver_pin().set_bits(1);
      } else if(node.get_type().op == GreaterThan_Op) {
        node.get_driver_pin().set_bits(1);
      } else if(node.get_type().op == LessEqualThan_Op) {
        node.get_driver_pin().set_bits(1);
      } else if(node.get_type().op == LessThan_Op) {
        node.get_driver_pin().set_bits(1);
      }
  }
}//end of Pass_dfg::trans()

void Pass_dfg::do_finalize_bitwidth(LGraph *dfg) {
  //SH:FIXME: change to dfg->fast() after MIT algorithm is ready
  for(auto nid: dfg->forward()){
    auto node = Node(dfg, 0, Node::Compact(nid));
    auto ntype = node.get_type().op;
    if(ntype == Mux_Op){
      Node_pin tpin_driver, fpin_driver;
      //retrieve true and false drivers
      for(auto& inp : node.inp_edges()){
        if(inp.sink.get_pid() == 1)
          fpin_driver = inp.driver;
        else if(inp.sink.get_pid() == 2)
          tpin_driver = inp.driver;
      }
      auto tpin_driver_bw = tpin_driver.get_bits();
      auto fpin_driver_bw = fpin_driver.get_bits();
      auto bw_diff = (uint16_t)abs(tpin_driver_bw - fpin_driver_bw);
      node.get_driver_pin().set_bits(std::max(tpin_driver_bw, fpin_driver_bw));

      //delete original edge
      for(auto &inp : node.inp_edges()){
        if(inp.sink.get_pid() == 1 && tpin_driver_bw > fpin_driver_bw)
          inp.del_edge();
        else if(inp.sink.get_pid() == 2 && tpin_driver_bw < fpin_driver_bw)
          inp.del_edge();
      }

      //create internal Join_Op to resolve bitwidth mismatch
      if(bw_diff == 0){
        continue;
      } else if (tpin_driver_bw > fpin_driver_bw){
        Node node_join = dfg -> create_node();
        node_join.set_type(Join_Op);
        node_join.setup_driver_pin().set_bits(tpin_driver_bw);

        Node_pin node_pin_unsign_ext = dfg->create_node_const(0,bw_diff).setup_driver_pin();
        dfg->add_edge(node_pin_unsign_ext, node_join.setup_sink_pin(1));
        dfg->add_edge(fpin_driver, node_join.setup_sink_pin(0));
        dfg->add_edge(node_join.setup_driver_pin(0), node.setup_sink_pin(1));

      } else if (tpin_driver_bw < fpin_driver_bw){
        Node node_join = dfg -> create_node();
        node_join.set_type(Join_Op);
        node_join.setup_driver_pin().set_bits(fpin_driver_bw);

        Node_pin node_pin_unsign_ext = dfg->create_node_const(0,bw_diff).setup_driver_pin();
        dfg->add_edge(node_pin_unsign_ext, node_join.setup_sink_pin(1));
        dfg->add_edge(tpin_driver, node_join.setup_sink_pin(0));
        dfg->add_edge(node_join.setup_driver_pin(0), node.setup_sink_pin(2));
      }

      //SH:FIXME: wait for MIT bitwidth algorithm
      if(node.get_driver_pin().get_bits() < node.get_sink_pin(1).get_bits())
        node.get_driver_pin().set_bits( node.get_sink_pin(1).get_bits());
    } else if(ntype == GraphIO_Op){
      //after MIT algo. and Mux_Op processing, bw of every node should be synced
      //except an output gio connected to a Mux_Op
      for(auto &inp : node.inp_edges()){
        //SH:FIXME: Warning! this is workaround will be eventually wrong after MIT could handle subgraph
        if(inp.driver.get_bits() > inp.sink.get_bits()){
          inp.sink.get_node().setup_driver_pin(0).set_bits(inp.driver.get_bits());
          fmt::print("gio bitwidth less then source\n");
        } else {
          inp.driver.set_bits(inp.sink.get_bits());
          fmt::print("gio bitwidth larger then source\n");
        }
      }
    } else if(ntype == And_Op || ntype == Or_Op || ntype == Xor_Op){
      //SH:FIXME: deprecate after MIT bitwidth algorithm ready
      for(auto& inp : node.inp_edges()){
        if(node.get_driver_pin(0).get_bits() < inp.driver.get_bits())
          node.setup_driver_pin(0).set_bits(inp.driver.get_bits());
      }
    }


    //SH:FIXME: wait for MIT bitwidth algorithm
    //SH:FIXME: temporarily set 1-bit for all Node_pins w/o bitwidth define
    if(node.get_driver_pin(0).get_bits() == 0){
      node.get_driver_pin(0).set_bits(1);
    }






  }//end of g->fast()
}//end of finalize bitwidth

void Pass_dfg::cfg_2_dfg(LGraph *cfg, LGraph *dfg) {
  Node cfg_iter = find_cfg_root(cfg);
  Aux aux_global;
  Aux_tree aux_tree(&aux_global);
  process_cfg(dfg, cfg, &aux_tree, cfg_iter);
  finalize_global_connect(dfg, &aux_global);
  fmt::print("cfg_2_dfg finish!\n");
}

void Pass_dfg::finalize_global_connect(LGraph *dfg, const Aux *aux_global) {
  fmt::print("finalize global connect\n");
  for(const auto &pair : aux_global->get_pendtab()) {
    if(is_output(pair.first)) {
      auto spin = dfg->get_graph_output(pair.first.substr(1));
      auto dpin = pair.second;

      dfg->add_edge(dpin, spin);
    } else if(is_register(pair.first)) {
      ; // balabala
    }
  }
}

Node Pass_dfg::process_cfg(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, const Node& top_node) {
  Node cfg_iter = top_node; //SH:FIXME: optimization?
  bool finished = false;

  while(!finished) {
    cfg_iter = process_node(dfg, cfg, aux_tree, cfg_iter);
    if(cfg_iter.get_driver_pin(0).is_graph_output() or cfg_iter.get_type().op == CfgIfMerge_Op)
      finished = true;

    fmt::print("process_node finished!!\n\n");
  }
  fmt::print("\n\nprocess_cfg finished!!\n\n");
  aux_tree->print_cur_aux();
  fmt::print("\n\n");
  return cfg_iter;
}

Node Pass_dfg::process_node(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, const Node& cfg_node) {
  if(cfg_node.get_type().op == GraphIO_Op)
    return get_cfg_child(cfg, cfg_node);

  const CFG_Node_Data data(cfg, cfg_node);

  fmt::print("Processing CFG node:{}\n", cfg_node.get_driver_pin(0).get_name());
  fmt::print("target:[{}], operator:[{}], ", data.get_target(), data.get_operator());
  fmt::print("operands:[");
  for(const auto &i : data.get_operands())
    fmt::print("{}, ", i);
  fmt::print("]\n");

  switch(cfg_node.get_type().op) {
  case CfgAssign_Op:{
    process_assign(dfg, aux_tree, data);
    return get_cfg_child(cfg, cfg_node);
  }
  case CfgFunctionCall_Op:{
    process_func_call(dfg, cfg, aux_tree, data);
    return get_cfg_child(cfg, cfg_node);
  }
  case CfgIf_Op:{
    aux_tree->print_cur_aux();
    return process_if(dfg, cfg, aux_tree, data, cfg_node);
  }
  case CfgIfMerge_Op:{
    I(false);
    return get_cfg_child(cfg, cfg_node);
  }
  default:
    fmt::print("\n\n*************Unrecognized cfg_node type\n");
    return get_cfg_child(cfg, cfg_node);
  }
}


void Pass_dfg::process_func_call(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  // for func_call, all the node should be created before, you just connect them. No need to create target node

  //const auto& target    = data.get_target();
  //const auto& operands  = data.get_operands(); //return strings
  //auto        op        = data.get_operator();

  //const auto &target    = data.get_target();
  //const auto &operands     = data.get_operands();
  //const auto &oprd_nodes  = process_operands(dfg, aux_tree, data); // all operands should be in auxtab, just retrieve oprd_nodes
  //I(!operands.empty());
  //Node subg_root_node = aux_tree->get_alias(operands[0]);

  //fmt::print("process function call:{}!!!!!\n",subg_root_node.get_driver_pin(0).get_name());

  //LGraph *sub_graph = LGraph::open(cfg->get_path(), subg_root_node.get_driver_pin(0).get_name());
  //if(sub_graph) {
  //  subg_root_node.set_type_subgraph(sub_graph->lg_id());
  //  fmt::print("set subgraph, name:{}, lgid:{}\n", subg_root_node.get_driver_pin(0).get_name(), sub_graph->lg_id());
  //} else {
  //  subg_root_node.set_type(DfgPendingGraph_Op);
  //  //re-assign correct subgraph name
  //  //SH:FIXME: should store subgraph name at "Node" if moving to "Node_pin" based design
  //  subg_root_node.setup_driver_pin(0).set_name(operands.at(0));
  //  fmt::print("set pending graph on node:{}, sub_graph name should be:{}\n",
  //              subg_root_node.get_compact(), subg_root_node.setup_driver_pin(0).set_name(operands.at(0)));
  //}

  //aux_tree->set_alias(target, oprd_nodes[0]);

  //// connect 1st operand with [2nd,3rd,...] operands
  //std::vector<Node> subg_input_ids(oprd_nodes.begin() + 1, oprd_nodes.end());
  //process_connections(dfg, subg_input_ids, subg_root_node);
}

void Pass_dfg::process_assign(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  fmt::print("process_assign\n");
  const auto& target    = data.get_target();
  const auto& operands  = data.get_operands(); //return strings
  auto        op        = data.get_operator();

  Node_pin target_pin = process_target(dfg, aux_tree, target, op);

  std::vector<Node_pin> oprd_pins;
  for(const auto& iter : operands)
    oprd_pins.emplace_back( process_operand(dfg, aux_tree, iter));

  I(!oprd_pins.empty());

  //if target is %, @, mux, put into pending table
  //else, check globally, if it is only a local variable, don't need to create mux in parent scope
  //it's just used as intermediate variable in branch block
  if(is_pure_assign_op(op)) {

    aux_tree->set_alias(target, oprd_pins[0]);
    if(is_output(target))
      I(target_pin.is_graph_output());

    if(target_pin.is_graph_output() || target_pin.get_node().get_type().op == FFlop_Op){
      fmt::print("hello222!!!!!!!!!\n");
      aux_tree->set_pending(target, oprd_pins[0]);
    } else if (aux_tree->get_parent(aux_tree->get_cur_aux())-> has_pending(target)){
      aux_tree->set_pending(target, oprd_pins[0]);
    }

  } else if (is_tuple_op(op)) {

    aux_tree->set_alias(target, oprd_pins[0]);
    //SH:FIXME: potential candidate to set pending

  } else if(is_label_op(op)) {

    I(operands.size() > 1);
    if(operands[0] == "__bits") {
      fmt::print("__bits size assignment\n");
      aux_tree->set_alias(target, oprd_pins[1]);
    } else if(operands[0] == "__fluid") {
      ; //todo
    } else {
      fmt::print("function argument assign\n");
      aux_tree->set_alias(target, oprd_pins[1]);
      oprd_pins[1].set_name(operands[0]);
      fmt::print("sink io of subgraph:{}, assigned by:{}\n",operands[0], operands[1]);
    }

  } else if(is_as_op(op)) {

    if(oprd_pins[0].get_node().get_type().op == U32Const_Op) {//explicit bitwidth assignment
      I(oprd_pins[0].get_node().get_type_const_value());
      auto bits = (uint16_t)oprd_pins[0].get_node().get_type_const_value();
      target_pin.set_bits(bits);
      fmt::print("set_bits({}) for target:{}\n",bits, target);
    } else {
      aux_tree->set_alias(target, oprd_pins[0]);
    }

  } else if(is_unary_op(op)) {

    //SH:FIXME: is there a Not op in CFG?
    aux_tree->set_alias(target, oprd_pins[0]);

  } else if(is_binary_op(op)) {

    I(operands.size() > 1);
    Node tnode = target_pin.get_node();
    process_connections(dfg, oprd_pins, tnode);

  }
}

Node_pin Pass_dfg::process_operand(LGraph *dfg, Aux_tree *aux_tree, std::string_view oprd) {
  Node_pin oprd_pin;
  if(aux_tree->has_alias(oprd)) {
    //why check whole tree? Since upper scope variable should be seen by if-else branch
    oprd_pin = aux_tree->get_alias(oprd);
    I(oprd_pin.has_name());
    fmt::print("operand:{} has an alias:{}\n", oprd, oprd_pin.get_name());
  } else {
    if(is_constant(oprd)) { // process "as __bits" here!
      oprd_pin = resolve_constant(dfg, oprd).setup_driver_pin();
      fmt::print("create node for constant operand:{}\n", oprd);
      aux_tree->set_alias(oprd, oprd_pin);
    } else if(is_input(oprd)) {
      oprd_pin = create_input(dfg, aux_tree, oprd);
      fmt::print("create node for input operand:{}\n", oprd);
      aux_tree->set_alias(oprd, oprd_pin);
    } else if(is_output(oprd)) {
      oprd_pin = create_output(dfg, aux_tree, oprd);
      fmt::print("create node for output operand:{}\n", oprd);
      aux_tree->set_alias(oprd, oprd_pin);
    } else if(is_reference(oprd)) {
      oprd_pin = create_reference(dfg, aux_tree, oprd);
      fmt::print("create node for reference operand:{}\n", oprd);
      aux_tree->set_alias(oprd, oprd_pin);
    } else {
      oprd_pin = create_private(dfg, aux_tree, oprd);
      fmt::print("create node for private operand:{}\n", oprd);
      aux_tree->set_alias(oprd, oprd_pin);
    }
  }
  return oprd_pin;
}

Node_pin Pass_dfg::process_target(LGraph *dfg, Aux_tree *aux_tree, std::string_view target, std::string_view op) {
  Node_pin tpin;
  I(!is_constant(target));

  if(is_input(target)) {
    if(dfg->is_graph_input(target.substr(1))){
      //tpin = aux_tree->get_alias(target);
      tpin = dfg->get_graph_input(target.substr(1));
    } else {
      tpin = create_input(dfg, aux_tree, target);
      fmt::print("create node for input target:{}\n", target);
    }
  } else if(is_output(target)) {
    if(dfg->is_graph_output(target.substr(1))){
      //tpin = aux_tree->get_alias(target);
      tpin = dfg->get_graph_output_driver(target.substr(1));
    } else {
      tpin = create_output(dfg, aux_tree, target);
      fmt::print("create node for output target:{}\n", target);
    }
  } else if(is_reference(target)) {
    tpin = create_reference(dfg, aux_tree, target);
    fmt::print("create node for reference target:{}\n", target);
  } else {
    tpin = create_node_and_pin(dfg, aux_tree, target);
    tpin.get_node().set_type(node_type_from_text(op));
    fmt::print("create node for private target:{}\n", target);
  }

  I(!tpin.is_invalid());

  //only update local scope when re-defining a var
  if(aux_tree->get_cur_aux()->has_alias(target) && !tpin.is_graph_io()){
    aux_tree->update_alias(target, tpin);
  } else {
    aux_tree->set_alias(target, tpin); //self var<-> node_pin assignment. ex. %s <-> %s
  }

  return tpin;
}

void Pass_dfg::process_connections(LGraph *dfg, const std::vector<Node_pin> &dpins, Node &snode) {
  bool one_srcs_is_signed = false; //SH:FIXME: wait for bitwidth algorithm

  for(std::vector<Node_pin>::size_type i = 0; i < dpins.size(); i++){
    auto ntype = snode.get_type().op;
    I(ntype != SubGraph_Op);        //SH:FIXME: Handled separate as it is a more complicated case
    I(ntype != DfgPendingGraph_Op); //SH:FIXME: Handled separate as it is a more complicated case

    Node_pin dpin = dpins.at(i);
    Port_ID  spin_pid =
      (ntype == Sum_Op &&  one_srcs_is_signed) ? (uint16_t)0 :
      (ntype == Sum_Op && !one_srcs_is_signed) ? (uint16_t)1 :
      (ntype == And_Op )                       ? (uint16_t)0 :
      (ntype == Or_Op  )                       ? (uint16_t)0 :
      (ntype == Xor_Op )                       ? (uint16_t)0 :
      (ntype == LessThan_Op && i == 0)         ? (uint16_t)0 :
      (ntype == LessThan_Op && i == 1)         ? (uint16_t)2 :
      (ntype == GreaterThan_Op && i == 0)      ? (uint16_t)0 :
      (ntype == GreaterThan_Op && i == 1)      ? (uint16_t)2 :
      (ntype == LessEqualThan_Op && i == 0)    ? (uint16_t)0 :
      (ntype == LessEqualThan_Op && i == 1)    ? (uint16_t)2 :
      (ntype == GreaterEqualThan_Op && i == 0) ? (uint16_t)0 :
      (ntype == GreaterEqualThan_Op && i == 1) ? (uint16_t)2 : (uint16_t)0;

    dfg->add_edge(dpin, snode.setup_sink_pin(spin_pid));

    I(dpin.has_name());
    I(snode.get_driver_pin(0).has_name());
    fmt::print("add_edge driver:{}->sink:{}\n", dpin.get_name(), snode.get_driver_pin(0).get_name());

    //SH:FIXME: wait for bitwidth algorithm v2
    //for(const auto& i:src_nids ){
    //  if(dfg->node_bitwidth_get(i).e.sign){
    //    one_srcs_is_signed = true;
    //    break;
    //  }
    //}

    /*
    //(ntype == DfgPendingGraph_Op)            ? (uint16_t)0 : (uint16_t)0;
    //(ntype == SubGraph_Op)                   ? (uint16_t)0 : (uint16_t)0;
    the sub-graph IOs connection cannot be resolved at the first pass
    so just initially connect the top->sub-graph IOs so we could traverse edges and
    resolve connections after resolving the sub-graph instantiation".
    */
  }
}


void Pass_dfg::assign_to_true(LGraph *dfg, Aux_tree *aux_tree, std::string_view v) {
  Node_pin dpin = create_true_const(dfg);
  Node_pin spin = create_node_and_pin(dfg, aux_tree, v);
  spin.get_node().set_type(Or_Op);
  dfg->add_edge(dpin, spin);
}

void Pass_dfg::attach_outputs(LGraph *dfg, Aux_tree *aux_tree) {
  ;
}

void Pass_dfg::add_fluid_behavior(LGraph *dfg, Aux_tree *aux_tree) {
  std::vector<Node> inputs, outputs;
  add_fluid_ports(dfg, aux_tree, inputs, outputs);
}

void Pass_dfg::add_fluid_ports(LGraph *dfg, Aux_tree *aux_tree, std::vector<Node> &data_inputs,
                               std::vector<Node> &data_outputs) {
  ;
}

//void Pass_dfg::add_fluid_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs,
//                               const std::vector<Index_ID> &data_outputs) {
//  ;
//}
//
//void Pass_dfg::add_abort_logic(LGraph *dfg, Aux_tree *aux_tree, const std::vector<Index_ID> &data_inputs,
//                               const std::vector<Index_ID> &data_outputs) {
//  ;
//}

Node Pass_dfg::find_cfg_root(LGraph *cfg) {
  Node root_node;
  cfg->each_graph_input([&root_node](const Node_pin &pin) {
    root_node = pin.get_node();
  });
  I(!root_node.is_invalid());
  return root_node;
}

Node Pass_dfg::get_cfg_child(LGraph *cfg, const Node& cfg_node) {
  //an graph output node is the terminate node
  if(cfg_node.get_driver_pin(0).is_graph_output())
    return cfg_node;

  I(cfg_node.out_edges().size()==1); //note: "CfgIf_Op" will be handled specially
  for(const auto &out_edge : cfg_node.out_edges())
    return out_edge.sink.get_node();

  return cfg_node;
}

Node Pass_dfg::process_if(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data, Node cfg_node) {
  fmt::print("process if start!\n");
  I(aux_tree->has_alias(data.get_target()));
  Node_pin      condition  = aux_tree->get_alias(data.get_target());
  const auto&   operands   = data.get_operands();
  Aux      taux;
  Aux      faux;
  Aux*     paux_ptr = aux_tree->get_cur_aux(); // parent aux

  I(operands.size() > 1);

  Node tbranch_cfg, fbranch_cfg;
  for(const auto &out_edge : cfg_node.out_edges()) {
    if(out_edge.driver.get_pid() == 0)
      tbranch_cfg = out_edge.sink.get_node();
    else if(out_edge.driver.get_pid() == 1)
      fbranch_cfg = out_edge.sink.get_node();//fbranch might be phi node directly
    else
      I(false); // at most 2 output Node_pins
  }

  aux_tree->set_parent_child(paux_ptr, &taux, true);
  Node tb_next = process_cfg(dfg, cfg, aux_tree, tbranch_cfg) ;
  fmt::print("branch true finish! \n");

  if(fbranch_cfg.get_type().op != CfgIfMerge_Op) { //there is an 'else' clause
    aux_tree->set_parent_child(paux_ptr, &faux, false);
    Node fb_next = process_cfg(dfg, cfg, aux_tree, fbranch_cfg);
    I(tb_next == fb_next);
    fmt::print("branch false finish! \n");
  }

  resolve_phis(dfg, aux_tree, paux_ptr, &taux, &faux, condition);

  if(fbranch_cfg.get_type().op != CfgIfMerge_Op) {
    aux_tree->disconnect_child(aux_tree->get_cur_aux(), &faux, false);
    aux_tree->auxes_stack_pop();
  }

  aux_tree->disconnect_child(aux_tree->get_cur_aux(), &taux, true);
  aux_tree->auxes_stack_pop();

  fmt::print("process_if() done!!\n");
  //tb_next is phi node, return its child to keep flow runs
  return get_cfg_child(cfg, tb_next);
  //return tb_next;
}

void Pass_dfg::resolve_phis(LGraph *dfg, Aux_tree *aux_tree, Aux *paux, Aux *taux, Aux *faux, Node_pin cond) {
  fmt::print("resolve phis\n");
  // resolve phi in branch TRUE
  auto iter = taux->get_pendtab().begin();
  while(iter != taux->get_pendtab().end()) {
    fmt::print("key is:{}, ", iter->first);
    if(faux && faux->has_pending(iter->first)) {
      fmt::print("has same pending in fault\n");
      Node_pin tpin = iter->second;
      Node_pin fpin = faux->get_pending(iter->first); // return Node_pin
      faux->del_pending(iter->first);
      create_mux(dfg, paux, tpin, fpin, cond, iter->first);
    } else if(paux->has_pending(iter->first)) {
      fmt::print("has same pending in parent\n");
      Node_pin tpin = iter->second;
      Node_pin fpin = paux->get_pending(iter->first);
      paux->del_pending(iter->first);
      create_mux(dfg, paux, tpin, fpin, cond, iter->first);
    } else {
      fmt::print("has no same pending\n");
      Node_pin tpin = iter->second;
      Node_pin fpin = aux_tree->has_pending(iter->first) ? aux_tree->get_pending(iter->first) : create_default_const(dfg);
      create_mux(dfg, paux, tpin, fpin, cond, iter->first);
    }
    taux->del_pending(iter++->first);
  }
  // resolve phi in branch FALSE
  iter = faux->get_pendtab().begin();
  while(iter != faux->get_pendtab().end()) {
    if(paux->has_pending(iter->first)) {
      Node_pin tpin = paux->get_pending(iter->first);
      Node_pin fpin = iter->second;
      paux->del_pending(iter->first);
      create_mux(dfg, paux, tpin, fpin, cond, iter->first);
    } else {
      Node_pin tpin = aux_tree->has_pending(iter->first) ? aux_tree->get_pending(iter->first) : create_default_const(dfg);
      Node_pin fpin = iter->second;
      create_mux(dfg, paux, tpin, fpin, cond, iter->first);
    }
    faux->del_pending(iter++->first);
  }
  I(taux->get_pendtab().empty());
  I(faux->get_pendtab().empty());
}

void Pass_dfg::create_mux(LGraph *dfg, Aux *paux, Node_pin tp, Node_pin fp, Node_pin cp, std::string_view var) {
  mux_cnt += 1;
  Node phi_node = dfg->create_node();
  phi_node.set_type(Mux_Op);
  //SH:FIXME: cant tp.get_name() get full "$a" name instead of just "a"?
  //phi_node.setup_driver_pin().set_name(absl::StrCat("mux",  "_T_", tp.get_name(), "_F_", fp.get_name()));
  phi_node.setup_driver_pin().set_name(absl::StrCat("mux", mux_cnt ));
  auto type = phi_node.get_type();

  Port_ID f_pid = type.get_input_match("A");
  Port_ID t_pid = type.get_input_match("B");
  Port_ID c_pid = type.get_input_match("S");

  dfg->add_edge(tp, phi_node.setup_sink_pin(t_pid));
  dfg->add_edge(fp, phi_node.setup_sink_pin(f_pid));
  dfg->add_edge(cp, phi_node.setup_sink_pin(c_pid));
  paux->set_alias(var, phi_node.get_driver_pin());
  paux->set_pending(var, phi_node.get_driver_pin());
}

