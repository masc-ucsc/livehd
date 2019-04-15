//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>

#include "lgraph.hpp"
#include "pass_dfg.hpp"

Node Pass_dfg::create_reference(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
  Node node = create_node(g, aux_tree, var_name.substr(1));
  fmt::print("create node:{}\n");
  node.set_type(DfgRef_Op);
  return node;
}

Node Pass_dfg::create_register(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
  Node node = create_node(g, aux_tree, var_name);
  node.set_type(SFlop_Op);
  return node;
}

/*
SH:FIXME: return Node_pin will pollute the uniform of Aux table... need a new design!
need to store the input/output name string information
and use string comparison to match the target io
*/
Node Pass_dfg::create_input(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits) {
  auto pin = g->add_graph_input(var_name.substr(1).c_str(), bits, 0); // get rid of $mark
  return pin.get_node();
  //return g->get_node(pin).get_nid();
}

Node Pass_dfg::create_output(LGraph *g, Aux_tree *aux_tree, const std::string &var_name, uint16_t bits) {
  auto pin = g->add_graph_output(var_name.substr(1).c_str(), bits, 0); // get rid of %mark
  return pin.get_node();
  //return g->get_node(pin).get_nid();
}

Node Pass_dfg::create_private(LGraph *g, Aux_tree *aux_tree, const std::string &var_name) {
  Node node = create_node(g, aux_tree, var_name);
  node.set_type(Or_Op);
  return node;
}

Node Pass_dfg::create_const32_node(LGraph *g, uint32_t val, uint16_t node_bit_width, bool is_signed) {
  return create_const32_node(g, std::to_string(val), node_bit_width, is_signed);
}

Node Pass_dfg::create_default_const(LGraph *g) {
  Node node = g->create_node();
  node.set_type(U32Const_Op);
  node.set_type_const_value(0);
  node.setup_driver_pin(0).set_bits(1);
  return node;
}

Node Pass_dfg::create_true_const(LGraph *g, Aux_tree *aux_tree) {
  Node node = g->create_node();
  node.set_type(U32Const_Op);
  node.set_type_const_value(1);
  node.setup_driver_pin(0).set_bits(1);
  return node;
}

Node Pass_dfg::create_false_const(LGraph *g, Aux_tree *aux_tree) {
  Node node = g->create_node();
  node.set_type(U32Const_Op);
  node.set_type_const_value(0);
  node.setup_driver_pin(0).set_bits(1);
  return node;
}

Node Pass_dfg::create_node(LGraph *g, Aux_tree *aux_tree, const std::string &v) {
  I(!v.empty());
  Node node = g->create_node();
  node.setup_driver_pin(0).set_name(v);//SH:FIXME: what information do you want to store at pin(0)?
  aux_tree->set_alias(v, node);
  return node;
}

Node Pass_dfg::create_AND(LGraph *g, Aux_tree *aux_tree, Node op1, Node op2) {
  return create_binary(g, aux_tree, op1, op2, And_Op);
}

Node Pass_dfg::create_OR(LGraph *g, Aux_tree *aux_tree, Node op1, Node op2) {
  return create_binary(g, aux_tree, op1, op2, Or_Op);
}

Node Pass_dfg::create_binary(LGraph *g, Aux_tree *aux_tree, Node op1, Node op2, Node_Type_Op oper) {
  Node dfnode = g->create_node();
  dfnode.set_type(oper);
  g->add_edge(op1.setup_driver_pin(0), dfnode.setup_sink_pin(0));
  g->add_edge(op2.setup_driver_pin(0), dfnode.setup_sink_pin(0));
  return dfnode;
}

Node Pass_dfg::create_NOT(LGraph *g, Aux_tree *aux_tree, Node op1) {
  Node dfnode = g->create_node();
  dfnode.set_type(Not_Op);
  g->add_edge(op1.setup_driver_pin(0), dfnode.setup_sink_pin(0));
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
    I(false);
    return Invalid_Op;
  }
}
