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

Pass_dfg::Pass_dfg():Pass("dfg"){}

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

  //SH:FIXME: top<->subgraph connection might be wrong, exam carefully
  //resolve top <-> subgraph IO connection
  for(auto nid : dfg->fast()){
    auto node = Node(dfg, 0, Node::Compact(nid));
    if(node.get_type().op == SubGraph_Op){
      fmt::print("resolve connection, subgraph is:{}\n",node.get_driver_pin(0).get_name());
      LGraph* sub_graph = LGraph::open(dfg->get_path(), node.get_driver_pin(0).get_name()) ;
      I(sub_graph);

      absl::flat_hash_map<Node_pin, Node_pin> subg_inp_edges;
      for(auto &inp : node.inp_edges()){
        Node dnode = inp.driver.get_node();
        auto inp_name = dnode.get_driver_pin(0).get_name();
        Node_pin dpin = dnode.get_driver_pin(0);
        Node_pin spin = sub_graph->get_graph_input(inp_name);

        fmt::print("inp_name:{}\n",inp_name);
        //fmt::print("src_nid:{}, src_pid:{}, dst_nid:{}, dst_pid:{}\n", src_nid, src_pid, dst_nid, dst_pid);
        subg_inp_edges[dpin] = spin;
        inp.del_edge();
      }

      for(auto &edge : subg_inp_edges){
        dfg->add_edge(edge.first, edge.second);
      }

      //resolve subgraph output connections
      absl::flat_hash_map<Node_pin, Node_pin> subg_out_edges;

      for(auto &out : node.out_edges()){
        Node_pin dpin = out.driver;
        Node_pin spin = out.sink;
        uint16_t bw;
        sub_graph->each_graph_output([&sub_graph, &spin, &bw](const Node_pin &pin) {
            fmt::print("outputs of subgraph: name:{}, bitwidth:{}\n",
              sub_graph->get_graph_output_name_from_pid(pin.get_pid()), pin.get_bits());
            spin = pin;
            bw = pin.get_bits();
            });
        subg_out_edges[dpin] = spin;

        dpin.set_bits(bw);
        out.del_edge();
      }

      for(auto &edge : subg_out_edges){
        dfg->add_edge(edge.first,edge.second);
      }
    }
  }

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
  for(auto nid : dfg->fast()){
    auto node = Node(dfg, 0, Node::Compact(nid));
    uint16_t node_size = node.setup_driver_pin(0).get_bits();
    if(node_size == 0){
      //SH:FIXME: should set bits for all output node_pin?
      //SH:FIXME: yes, modify the algorithm to decide all bitwidth on all output pins
      //Node_bitwidth &nb = dfg->node_bitwidth_get(nid);
      //fmt::print("nid:{},max:{}\n",nid, nb.i.max);
      //dfg->set_bits(dfg->get_node(nid).setup_driver_pin(0), ((uint16_t)floor(log2(nb.i.max))+1));

      //SH:FIXME: wait for MIT bitwidth algorithm
      //SH:FIXME: temporarily set 1-bit for all Node_pins
      node.get_driver_pin(0).set_bits(1);
    }
  }

  for(auto nid: dfg->fast()){
    auto node = Node(dfg, 0, Node::Compact(nid));
    if(node.get_type().op == Mux_Op){
      for (auto &inp : node.inp_edges()) {
        if(inp.sink.get_pid() == 0) //the mux select pin
          continue;
        //assume MIT algo. will at least set mux.bits equals to the larger input
        //auto src_bits = dfg->get_bits(dfg->get_node(src_nid).get_driver_pin(0));
        //auto dst_bits = dfg->get_bits(dfg->get_node(dst_nid).get_driver_pin(0));
        auto dpin_bits = inp.driver.get_bits();
        auto spin_bits = inp.sink.get_bits();
        auto bw_diff = (uint16_t)abs(dpin_bits - spin_bits);
        if(spin_bits > dpin_bits) {
          //SH:FIXME: using unsigned extend to fix mux bitwidth mismatch, what if the network is signed representation?
          Node_pin node_pin_unsign_ext = create_const32(dfg, 0, bw_diff, false);
          Node node_join = dfg->create_node();
          node_join.set_type(Join_Op);
          node_join.setup_driver_pin(0).set_bits(spin_bits);
          dfg->add_edge(node_pin_unsign_ext, node_join.setup_sink_pin(1));
          dfg->add_edge(inp.driver                         , node_join.setup_sink_pin(0));
          dfg->add_edge(node_join.setup_driver_pin(0)      , inp.sink);
          inp.del_edge();
          //dfg->set_bits(dfg->get_node(nid_join).setup_driver_pin(0), dst_bits);
          //dfg->add_edge(dfg->get_node(unsign_ext_nid).setup_driver_pin(0), dfg->get_node(nid_join).setup_sink_pin(1));
          //dfg->add_edge(dfg->get_node(src_nid).setup_driver_pin(0), dfg->get_node(nid_join).setup_sink_pin(0));
          //dfg->add_edge(dfg->get_node(nid_join).setup_driver_pin(0), dfg->get_node(dst_nid ).setup_sink_pin(dst_pid));
          //dfg->del_edge(inp.driver, inp.sink);
          //break;
        }
      }
    }

    //after MIT algo. and Mux_Op processing, bw of every node should be synced
    //except an output gio connected to a Mux_Op
    if(node.get_type().op == GraphIO_Op){
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
    }
  }//end of g->fast()
}//end of finalize bitwidth

void Pass_dfg::cfg_2_dfg(LGraph *cfg, LGraph *dfg) {
  Node cfg_iter = find_cfg_root(cfg);
  Aux_node auxnd_global;
  Aux_tree aux_tree(&auxnd_global);
  process_cfg(dfg, cfg, &aux_tree, cfg_iter);
  finalize_global_connect(dfg, &auxnd_global);
  fmt::print("cfg_2_dfg finish!\n");
}

void Pass_dfg::finalize_global_connect(LGraph *dfg, const Aux_node *auxnd_global) {
  fmt::print("finalize global connect\n");
  for(const auto &pair : auxnd_global->get_pendtab()) {
    if(is_output(pair.first)) {
      auto spin = dfg->get_graph_output(pair.first.substr(1));
      auto dpin = pair.second.get_driver_pin(0);

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
    if(cfg_iter.get_driver_pin(0).is_graph_output() or cfg_iter.get_type().op == CfgIfMerge_Op){
      finished = true;
    }
    if(cfg_iter.get_driver_pin(0).has_name())
      fmt::print("cfg node:{} process finished!!\n\n", cfg_iter.get_driver_pin(0).get_name());
  }
  aux_tree->print_cur_auxnd();
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
    aux_tree->print_cur_auxnd();
    Node tmp = process_if(dfg, cfg, aux_tree, data, cfg_node);
    return tmp;
  }
  case CfgIfMerge_Op:{
    I(false); //CfgIfMerge_Op should never enter process_node
    return get_cfg_child(cfg, cfg_node);
  }
  default:
    fmt::print("\n\n*************Unrecognized cfg_node type\n");
    return get_cfg_child(cfg, cfg_node);
  }
}


void Pass_dfg::process_func_call(LGraph *dfg, const LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  // for func_call, all the node should be created before, you just connect them. No need to create target node
  //const auto &target    = data.get_target();
  //const auto &oprds     = data.get_operands();
  //const auto &oprd_nodes  = process_operands(dfg, aux_tree, data); // all operands should be in auxtab, just retrieve oprd_nodes
  //I(!oprds.empty());
  //Node subg_root_node = aux_tree->get_alias(oprds[0]);

  //fmt::print("process function call:{}!!!!!\n",subg_root_node.get_driver_pin(0).get_name());

  //LGraph *sub_graph = LGraph::open(cfg->get_path(), subg_root_node.get_driver_pin(0).get_name());
  //if(sub_graph) {
  //  subg_root_node.set_type_subgraph(sub_graph->lg_id());
  //  fmt::print("set subgraph, name:{}, lgid:{}\n", subg_root_node.get_driver_pin(0).get_name(), sub_graph->lg_id());
  //} else {
  //  subg_root_node.set_type(DfgPendingGraph_Op);
  //  //re-assign correct subgraph name
  //  //SH:FIXME: should store subgraph name at "Node" if moving to "Node_pin" based design
  //  subg_root_node.setup_driver_pin(0).set_name(oprds.at(0));
  //  fmt::print("set pending graph on node:{}, sub_graph name should be:{}\n",
  //              subg_root_node.get_compact(), subg_root_node.setup_driver_pin(0).set_name(oprds.at(0)));
  //}

  //aux_tree->set_alias(target, oprd_nodes[0]);

  //// connect 1st operand with [2nd,3rd,...] operands
  //std::vector<Node> subg_input_ids(oprd_nodes.begin() + 1, oprd_nodes.end());
  //process_connections(dfg, subg_input_ids, subg_root_node);
}

void Pass_dfg::process_assign(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  fmt::print("process_assign\n");
  const auto& target = data.get_target();
  const auto& oprds  = data.get_operands(); //return strings
  auto        op     = data.get_operator();
  Node_pin    target_pin, oprd_p0, oprd_p1;
  target_pin = process_operand(dfg, aux_tree, target);
  oprd_p0  = process_operand(dfg, aux_tree, oprds[0]);
  if(oprds.size()>1)
    oprd_p1 = process_operand(dfg, aux_tree, oprds[1]);

  I(!oprds.empty());
  if(is_pure_assign_op(op)) {
    aux_tree->set_alias(target, oprd_p0);
    aux_tree->set_pending(target, oprd_p0);
  } else if(is_label_op(op)) {
    I(oprds.size() > 1);
    if(oprds[0] == "__bits") {
      fmt::print("__bits size assignment\n");
      aux_tree->set_alias(target, oprd_p1);
    } else if(oprds[0] == "__fluid") {
      ; //todo
    } else {
      fmt::print("function argument assign\n");
      aux_tree->set_alias(target, oprd_p1);
      oprd_p1.set_name(oprds[0]);
      fmt::print("sink io of subgraph:{}, assigned by:{}\n",oprds[0], oprds[1]);
    }
  } else if(is_tuple_op(op)){
    //SH:FIXME: need to extend to real tuple function, for now it acts as a pure assignment
    aux_tree->set_alias(target, oprd_p0);
    aux_tree->set_pending(target, oprd_p0);
  } else if(is_as_op(op)) {
    // process explicit bitwidth assignment
    if(is_input(target) || is_output(target)) {
      I(oprd_p0.get_node().get_type_const_value());
      auto bits = (uint16_t)oprd_p0.get_node().get_type_const_value();
      target_pin.set_bits(bits);
      fmt::print("set_bits for i/o target:{}\n", target);
    } else {
      aux_tree->set_alias(target, oprd_p0);
    }
  } else if(is_unary_op(op)) {
    aux_tree->set_alias(target, oprd_p0);
  } else if(is_compute_op(op) || is_compare_op(op)) {
    I(oprds.size() > 1);
    std::vector<Node_pin> oprd_pins;
    oprd_pins.push_back(oprd_p0);
    oprd_pins.push_back(oprd_p1);
    target_pin.get_node().set_type(node_type_from_text(op));
    process_connections(dfg, oprd_pins, target_pin.get_node());
  }
}

void Pass_dfg::process_connections(LGraph *dfg, const std::vector<Node_pin> &dpins, const Node &snode) {
  bool one_srcs_is_signed = false;

  for(std::vector<Node_pin>::size_type i = 0; i < dpins.size(); i++){
    auto ntype = snode.get_type().op;
    I(ntype != SubGraph_Op);        //todo: Handled separate as it is a more complicated case
    I(ntype != DfgPendingGraph_Op); //todo: Handled separate as it is a more complicated case

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


    dfg->add_edge(dpin, snode.get_sink_pin(spin_pid));
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

Node_pin Pass_dfg::process_operand(LGraph *dfg, Aux_tree *aux_tree, std::string_view oprd) {
  Node_pin oprd_pin;
  if(aux_tree->has_alias(oprd)) {
    oprd_pin = aux_tree->get_alias(oprd);
    //fmt::print("operand:{} has an alias:{}\n", oprd, oprd_node);
  } else {
    if(is_constant(oprd)) { // process "as __bits" here!
      oprd_pin = resolve_constant(dfg, oprd).setup_driver_pin();
      aux_tree->set_alias(oprd, oprd_pin);
      fmt::print("create node for constant operand:{}\n", oprd);
    } else if(is_input(oprd)) {
      oprd_pin = create_input(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_pin);
      fmt::print("create node for input operand:{}\n", oprd);
    } else if(is_output(oprd)) {
      oprd_pin = create_output(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_pin);
      fmt::print("create node for output operand:{}\n", oprd);
    } else if(is_reference(oprd)) {
      oprd_pin = create_reference(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_pin);
      fmt::print("create node for reference operand:{}\n", oprd);
    } else {
      oprd_pin = create_private(dfg, aux_tree, oprd);
      aux_tree->set_alias(oprd, oprd_pin);
      fmt::print("create node for private operand:{}\n", oprd);
    }
    // else if (is_register(oprd)){
    //  //oprd_id = create_register(dfg, aux_tree, oprd);
    //  //aux_tree->set_alias(oprd, oprd_id);
    //  //fmt::print("create node for register operand:{}, nid:{}\n", oprd, oprd_id);
    //}
  }
  // if (aux_tree->fluid_df() && is_input(oprd))
  //  add_read_marker(dfg, aux_tree, oprd);
  return oprd_pin;
}

Node Pass_dfg::process_if(LGraph *dfg, LGraph *cfg, Aux_tree *aux_tree, const CFG_Node_Data &data, Node cfg_node) {
  //fmt::print("process if start!\n");
  //I(aux_tree->has_alias(data.get_target()));
  //Node    cond     = aux_tree->get_alias(data.get_target());
  //const auto &operands = data.get_operands();
  ////auto *      tauxnd   = new Aux_node;
  ////auto *      fauxnd   = new Aux_node; //don't dynamic allocate here!!
  //Aux_node      tauxnd;
  //Aux_node      fauxnd;
  //auto *pauxnd = aux_tree->get_cur_auxnd(); // parent aux

  //I(operands.size() > 1);

  //Node tbranch, fbranch;
  //for(const auto &out_edge : cfg_node.out_edges()) {
  //  if(out_edge.driver.get_pid() == 0)
  //    tbranch = out_edge.sink.get_node();
  //  else if(out_edge.driver.get_pid() == 1)
  //    fbranch = out_edge.sink.get_node();//fbranch might be phi node directly
  //  else
  //    I(false); //should only have 2 output Node_pins
  //}

  //aux_tree->set_parent_child(pauxnd, &tauxnd, true);
  //Node tb_next = process_cfg(dfg, cfg, aux_tree, tbranch) ;
  //fmt::print("branch true finish! \n");

  //if(fbranch.get_type().op != CfgIfMerge_Op) { //there is an 'else' clause
  //  aux_tree->set_parent_child(pauxnd, &fauxnd, false);
  //  Node fb_next = process_cfg(dfg, cfg, aux_tree, fbranch);
  //  //SH:FIXME:ASK: how to compare if two Nodes are identical?
  //  I(tb_next.get_type().op == CfgIfMerge_Op);
  //  I(fb_next.get_type().op == CfgIfMerge_Op);
  //  I(tb_next.get_compact() == fb_next.get_compact());
  //  fmt::print("branch false finish! \n");
  //}

  //// SH:FIXME: put assertion on auxT, F emptiness
  //resolve_phis(dfg, aux_tree, pauxnd, &tauxnd, &fauxnd, cond);

  //if(fbranch.get_type().op != CfgIfMerge_Op) {
  //  aux_tree->disconnect_child(aux_tree->get_cur_auxnd(), &fauxnd, false);
  //  aux_tree->auxes_stack_pop();
  //}

  //aux_tree->disconnect_child(aux_tree->get_cur_auxnd(), &tauxnd, true);
  //aux_tree->auxes_stack_pop();

  //fmt::print("process_if() done!!\n");
  ////tb_next == CfgIfMerge_Op, should return it's child node to keep program going on
  //return get_cfg_child(cfg, tb_next);
  return get_cfg_child(cfg, cfg_node); //SH:FIXME: this is wrong, just for compile pass
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

std::vector<Node_pin> Pass_dfg::process_operands(LGraph *dfg, Aux_tree *aux_tree, const CFG_Node_Data &data) {
  //SH:FIXME: should reduce this function, just use Pass_dfg::process_opreand();
  const std::vector<std::string> &oprds = data.get_operands();
  std::vector<Node_pin>           oprd_pins(oprds.size());
  // const std::string &op = data.get_operator();
  //for(size_t i = 0; i < oprd_nodes.size(); i++) {
  //  if(aux_tree->has_alias(oprds[i])) {
  //    oprd_nodes[i] = aux_tree->get_alias(oprds[i]);
  //    //fmt::print("operand:{} has an alias:{}\n", oprds[i], oprd_nodes[i]);
  //  } else {
  //    if(is_constant(oprds[i])) {
  //      // oprd_nodes[i] = create_default_const(dfg, aux_tree);
  //      oprd_nodes[i] = resolve_constant(dfg, oprds[i]);
  //      //fmt::print("create node for constant operand:{}, nid:{}\n", oprds[i], oprd_nodes[i]);
  //    } else if(is_input(oprds[i])) {
  //      oprd_nodes[i] = create_input(dfg, aux_tree, oprds[i]);
  //      //fmt::print("create node for input operand:{}, nid:{}\n", oprds[i], oprd_nodes[i]);
  //    } else if(is_output(oprds[i])) {
  //      oprd_nodes[i] = create_output(dfg, aux_tree, oprds[i]);
  //      //fmt::print("create node for output operand:{}, nid:{}\n", oprds[i], oprd_nodes[i]);
  //    } else if(is_reference(oprds[i])) {
  //      oprd_nodes[i] = create_reference(dfg, aux_tree, oprds[i]);
  //      //fmt::print("create node for reference operand:{}, nid:{}\n", oprds[i], oprd_nodes[i]);
  //    } else {
  //      oprd_nodes[i] = create_private(dfg, aux_tree, oprds[i]);
  //      //fmt::print("create node for private operand:{}, nid:{}\n", oprds[i], oprd_nodes[i]);
  //    }
  //    // else if (is_register(oprds[i])){
  //    //  //oprd_nodes[i] = create_register(dfg, aux_tree, oprds[i]);
  //    //  //fmt::print("create node for register operand:{}, nid:{}\n", oprds[i], oprd_nodes[i]);
  //    //}
  //  }

  //  // if (aux_tree->fluid_df() && is_input(oprds[i]))
  //  //  add_read_marker(dfg, aux_tree, oprds[i]);
  //}

  return oprd_pins;
}

void Pass_dfg::resolve_phis(LGraph *dfg, Aux_tree *aux_tree, Aux_node *pauxnd, Aux_node *tauxnd, Aux_node *fauxnd, Node cond) {
  //fmt::print("resolve phis\n");
  //// resolve phi in branch true
  //auto iter = tauxnd->get_pendtab().begin();
  //while(iter != tauxnd->get_pendtab().end()) {
  //  fmt::print("key is:{}, ", iter->first);
  //  if(fauxnd && fauxnd->has_pending(iter->first)) {
  //    fmt::print("has same pend in fault\n");
  //    Node tnode = iter->second;
  //    Node fnode = fauxnd->get_pending(iter->first); // return Node
  //    fauxnd->del_pending(iter->first);
  //    create_mux(dfg, pauxnd, tnode, fnode, cond, iter->first);
  //  } else if(pauxnd->has_pending(iter->first)) {
  //    fmt::print("has same pend in parent\n");
  //    Node tnode = iter->second;
  //    Node fnode = pauxnd->get_pending(iter->first);
  //    pauxnd->del_pending(iter->first);
  //    create_mux(dfg, pauxnd, tnode, fnode, cond, iter->first);
  //  } else {
  //    fmt::print("has no same pend\n");
  //    Node tnode = iter->second;
  //    Node fnode = aux_tree->has_pending(iter->first) ? aux_tree->get_pending(iter->first) : create_default_const(dfg);
  //    create_mux(dfg, pauxnd, tnode, fnode, cond, iter->first);
  //  }
  //  tauxnd->del_pending(iter++->first);
  //}
  //// resolve phi in branch false
  //iter = fauxnd->get_pendtab().begin();
  //while(iter != fauxnd->get_pendtab().end()) {
  //  if(pauxnd->has_pending(iter->first)) {
  //    Node tnode = pauxnd->get_pending(iter->first);
  //    Node fnode = iter->second;
  //    pauxnd->del_pending(iter->first);
  //    create_mux(dfg, pauxnd, tnode, fnode, cond, iter->first);
  //  } else {
  //    Node tnode = aux_tree->has_pending(iter->first) ? aux_tree->get_pending(iter->first) : create_default_const(dfg);
  //    Node fnode = iter->second;
  //    create_mux(dfg, pauxnd, tnode, fnode, cond, iter->first);
  //  }
  //  fauxnd->del_pending(iter++->first);
  //}
  //so far pendtab of tauxnd and fauxnd should be empty
  //SH:FIXME: put an assertion instead of just comment
}

void Pass_dfg::create_mux(LGraph *dfg, Aux_node *pauxnd, Node tnode, Node fnode, Node cnode, const std::string &var) {
  ////fmt::print("create mux:{}, tid:{}, fid:{}\n", var, tid, fid);
  //Node phi_node = dfg->create_node();
  //phi_node.set_type(Mux_Op);
  //auto tp = phi_node.get_type();

  //Port_ID tin = tp.get_input_match("B");
  //Port_ID fin = tp.get_input_match("A");
  //Port_ID cin = tp.get_input_match("S");

  //dfg->add_edge(tnode.setup_driver_pin(), phi_node.setup_sink_pin(tin));
  //dfg->add_edge(fnode.setup_driver_pin(), phi_node.setup_sink_pin(fin));
  //dfg->add_edge(cnode.setup_driver_pin(), phi_node.setup_sink_pin(cin));
  //pauxnd->set_alias(var, phi_node);
  //pauxnd->set_pending(var, phi_node);
}

