#include "pass_dfg.hpp"
#include "nodetype.hpp"
#include <string>

Index_ID Pass_dfg::create_reference(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
  Index_ID nid = create_node(g, aux_tree, var_name.substr(1));
  fmt::print("create node nid:{}\n", nid);
  g->node_type_set(nid, DfgRef_Op);
  return nid;
}
//Index_ID Pass_dfg::create_register(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
//  Index_ID nid = create_node(g, aux_tree, var_name);
//  fmt::print("create node nid:{}\n", nid);
//  g->node_type_set(nid, Flop_Op);
//  aux_tree->add_register(var_name, nid);
//  return nid;
//}

Index_ID Pass_dfg::create_input(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits) {
  Index_ID nid = create_node(g, aux_tree, var_name, bits);
  g->add_graph_input(var_name.substr(1).c_str(), nid, bits); //get rid of $mark

  return nid;
}

Index_ID Pass_dfg::create_output(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits) {
  Index_ID nid = create_node(g, aux_tree, var_name);
  g->add_graph_output(var_name.substr(1).c_str(), nid, bits);

  return nid;
}

Index_ID Pass_dfg::create_private(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
  Index_ID nid = create_node(g, aux_tree, var_name);
  g->node_type_set(nid, Or_Op);

  return nid;
}

Index_ID Pass_dfg::create_default_const(LGraph *g, Aux_tree *aux_tree) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);

  return nid;
}

Index_ID Pass_dfg::create_true_const(LGraph *g, Aux_tree *aux_tree) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 1);

  std::string var_name = temp();
  g->set_node_wirename(nid, var_name.c_str());

  return nid;
}

Index_ID Pass_dfg::create_false_const(LGraph *g, Aux_tree *aux_tree) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);

  std::string var_name = temp();
  g->set_node_wirename(nid, var_name.c_str());

  return nid;
}

Index_ID Pass_dfg::create_node(LGraph *g, Aux_tree *aux_tree, const std::string &v, const uint16_t bits) {
  Index_ID nid = g->create_node().get_nid();
  g->set_node_wirename(nid, v.c_str());
  g->set_bits(nid,bits);
  aux_tree->set_alias(v, nid);
  return nid;
}

Index_ID Pass_dfg::create_AND(LGraph *g, Aux_tree *aux_tree, Index_ID op1, Index_ID op2) {
  return create_binary(g, aux_tree, op1, op2, And_Op);
}

Index_ID Pass_dfg::create_OR(LGraph *g, Aux_tree *aux_tree, Index_ID op1, Index_ID op2) {
  return create_binary(g, aux_tree, op1, op2, Or_Op);
}

Index_ID Pass_dfg::create_binary(LGraph *g, Aux_tree *aux_tree, Index_ID op1, Index_ID op2, Node_Type_Op oper) {
  auto target = temp();
  Index_ID dfnode = create_node(g, aux_tree, target);

  g->node_type_set(dfnode, oper);

  g->add_edge(Node_Pin(op1, 0, false), Node_Pin(dfnode, 0, true));
  g->add_edge(Node_Pin(op2, 0, false), Node_Pin(dfnode, 0, true));

  return dfnode;
}

Index_ID Pass_dfg::create_NOT(LGraph *g, Aux_tree *aux_tree, Index_ID op1) {
  auto target = temp();
  Index_ID dfnode = create_node(g, aux_tree, target);

  g->node_type_set(dfnode, Not_Op);

  g->add_edge(Node_Pin(op1, 0, false), Node_Pin(dfnode, 0, true));

  return dfnode;
}

Node_Type_Op Pass_dfg::node_type_from_text(const std::string &operator_text) {

  if (operator_text == "==") {
    return Equals_Op;
  } else if (operator_text == "=" || operator_text =="as" || operator_text == ":") {
    return Or_Op; //reduction or
  } else if (operator_text == "+"){
    return Sum_Op;
  }else {
    fmt::print("Operator: {}\n", operator_text);
    fflush(stdout);
    assert(false);
  }
}