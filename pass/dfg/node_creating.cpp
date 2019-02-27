#include "nodetype.hpp"
#include "pass_dfg.hpp"
#include <string>

Index_ID Pass_dfg::create_reference(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
  Index_ID nid = create_node(g, aux_tree, var_name.substr(1));
  fmt::print("create node nid:{}\n", nid);
  g->node_type_set(nid, DfgRef_Op);
  return nid;
}
// Index_ID Pass_dfg::create_register(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
//  Index_ID nid = create_node(g, aux_tree, var_name);
//  fmt::print("create node nid:{}\n", nid);
//  g->node_type_set(nid, SFlop_Op);
//  aux_tree->add_register(var_name, nid);
//  return nid;
//}

Index_ID Pass_dfg::create_input(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits) {
  Index_ID nid = create_node(g, aux_tree, var_name);
  g->add_graph_input(var_name.substr(1).c_str(), nid, bits, 0); // get rid of $mark
  g->set_bits(g->get_node(nid).setup_driver_pin(1), bits);
  return nid;
}

Index_ID Pass_dfg::create_output(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits) {
  Index_ID nid = create_node(g, aux_tree, var_name);
  g->add_graph_output(var_name.substr(1).c_str(), nid, bits, 0);
  g->set_bits(g->get_node(nid).setup_driver_pin(1), bits);
  return nid;
}

Index_ID Pass_dfg::create_private(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
  Index_ID nid = create_node(g, aux_tree, var_name);
  g->node_type_set(nid, Or_Op);

  return nid;
}

Index_ID Pass_dfg::create_const32_node(LGraph *g, uint32_t val, uint16_t node_bit_width, bool is_signed) {
  return create_const32_node(g, std::to_string(val), node_bit_width, is_signed);
}

Index_ID Pass_dfg::create_default_const(LGraph *g) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);
  g->set_bits(g->get_node(nid).setup_driver_pin(0), 1);

  return nid;
}

Index_ID Pass_dfg::create_true_const(LGraph *g, Aux_tree *aux_tree) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 1);

  return nid;
}

Index_ID Pass_dfg::create_false_const(LGraph *g, Aux_tree *aux_tree) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);

  return nid;
}

Index_ID Pass_dfg::create_node(LGraph *g, Aux_tree *aux_tree, const std::string &v) {
  assert(!v.empty());

  Index_ID nid = g->create_node().get_nid();
  g->set_node_wirename(g->get_node(nid).setup_driver_pin(0), v.c_str());
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

  Index_ID dfnode = g->create_node().get_nid();

  g->node_type_set(dfnode, oper);

  g->add_edge(g->get_node(op1).setup_driver_pin(0), g->get_node(dfnode).setup_sink_pin(0));
  g->add_edge(g->get_node(op2).setup_driver_pin(0), g->get_node(dfnode).setup_sink_pin(0));

  return dfnode;
}

Index_ID Pass_dfg::create_NOT(LGraph *g, Aux_tree *aux_tree, Index_ID op1) {

  Index_ID dfnode = g->create_node().get_nid();

  g->node_type_set(dfnode, Not_Op);

  g->add_edge(g->get_node(op1).setup_driver_pin(0), g->get_node(dfnode).setup_sink_pin(0));

  return dfnode;
}

Node_Type_Op Pass_dfg::node_type_from_text(std::string_view operator_text) const {

  if(operator_text == "==") {
    return Equals_Op;
  } else if(operator_text == ">=") {
    return GreaterEqualThan_Op;
  } else if(operator_text == ">") {
    return GreaterThan_Op;
  } else if(operator_text == "<=") {
    return LessEqualThan_Op;
  } else if(operator_text == ">") {
    return LessThan_Op;
  } else if(operator_text == "=" || operator_text == "as" || operator_text == ":") {
    return Or_Op; // reduction or
  } else if(operator_text == "+") {
    return Sum_Op;
  } else {
    fmt::print("Operator: {}\n", operator_text);
    assert(false);
  }
}