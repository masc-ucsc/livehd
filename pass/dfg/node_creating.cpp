//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <string>

#include "lgraph.hpp"
#include "pass_dfg.hpp"

//SH:FIXME: need to think the pyrope reference syntax design
Node_pin Pass_dfg::create_reference(LGraph *g, Aux_tree *aux_tree, std::string_view var_name) {
  var_name.remove_prefix(1);
  Node_pin pin = create_node_and_pin(g, aux_tree, var_name);
  pin.get_node().set_type(DfgRef_Op);
  return pin;
}

//SH:FIXME: need to figure out how to attach other pin other than the Q pin (pin0)
Node_pin Pass_dfg::create_register(LGraph *g, Aux_tree *aux_tree, std::string_view var_name) {
  Node_pin pin = create_node_and_pin(g, aux_tree, var_name);
  pin.get_node().set_type(SFlop_Op);
  return pin;
}

Node_pin Pass_dfg::create_input(LGraph *g, Aux_tree *aux_tree, std::string_view var_name, uint16_t bits) {
  var_name.remove_prefix(1); //get rid of $mark
  auto pin = g->add_graph_input(var_name, bits, 0);
  return pin;
}

Node_pin Pass_dfg::create_output(LGraph *g, Aux_tree *aux_tree, std::string_view var_name, uint16_t bits) {
  //add_graph_output will return the sink pin of graph out
  //we need it's twin driver node_pin to represent %out
  var_name.remove_prefix(1); //get rid of %mark
  g->add_graph_output(var_name, bits, 0); // get rid of %mark
  auto pin = g->get_graph_output_driver(var_name);
  return pin;
}

Node_pin Pass_dfg::create_private(LGraph *g, Aux_tree *aux_tree, std::string_view var_name) {
  Node_pin pin = create_node_and_pin(g, aux_tree, var_name);
  pin.get_node().set_type(Or_Op);
  return pin;
}

Node_pin Pass_dfg::create_const32(LGraph *g, uint32_t val, uint16_t node_bit_width, bool is_signed) {
  Node_pin pin = create_const32_node(g, std::to_string(val), node_bit_width, is_signed).setup_driver_pin(0);
  return pin;
}

Node_pin Pass_dfg::create_default_const(LGraph *g) {
  Node_pin pin = g->create_node_const(0,1).setup_driver_pin();
  if(!pin.has_name())
    pin.set_name("default_const");
  return pin;
}

Node_pin Pass_dfg::create_true_const(LGraph *g) {
  Node_pin pin = g->create_node_const(1,1).setup_driver_pin();
  if(!pin.has_name())
    pin.set_name("true");
  return pin;
}

Node_pin Pass_dfg::create_false_const(LGraph *g) {
  Node_pin pin = g->create_node_const(0,1).setup_driver_pin();
  if(!pin.has_name())
    pin.set_name("false");
  return pin;
}

Node_pin Pass_dfg::create_node_and_pin(LGraph *g, Aux_tree *aux_tree, std::string_view v) {
  I(!v.empty());
  Node node = g->create_node();
  node.setup_driver_pin(0).set_name(v);
  //aux_tree->set_alias(v, node.get_driver_pin(0));
  return node.get_driver_pin(0);
}

Node_pin Pass_dfg::create_AND(LGraph *g, Aux_tree *aux_tree, Node_pin op1, Node_pin op2) {
  return create_binary(g, aux_tree, op1, op2, And_Op);
}

Node_pin Pass_dfg::create_OR(LGraph *g, Aux_tree *aux_tree, Node_pin op1, Node_pin op2) {
  return create_binary(g, aux_tree, op1, op2, Or_Op);
}
Node_pin Pass_dfg::create_binary(LGraph *g, Aux_tree *aux_tree, Node_pin op1, Node_pin op2, Node_Type_Op oper) {
  Node sink_node = g->create_node();
  sink_node.set_type(oper);
  g->add_edge(op1, sink_node.setup_sink_pin(0));
  g->add_edge(op2, sink_node.setup_sink_pin(0));
  //SH:FIXME: need to extend to multi-output-pin Node Type
  return sink_node.setup_driver_pin(0);
}

Node_pin Pass_dfg::create_NOT(LGraph *g, Aux_tree *aux_tree, Node_pin op1) {
  Node sink_node = g->create_node();
  sink_node.set_type(Not_Op);
  g->add_edge(op1, sink_node.setup_sink_pin());
  return sink_node.setup_driver_pin();
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
  } else if(operator_text == "and") {
    return And_Op;
  } else if(operator_text == "or") {
    return Or_Op;
  } else if(operator_text == "=" || operator_text == "as" || operator_text == ":" ) {
    return Or_Op; // reduction or
  } else if (operator_text == "()"){
    return Or_Op; // SH:FIXME: tuple not implemented yet
  } else if(operator_text == "+" || operator_text == "-") {
    return Sum_Op;
  } else if(operator_text == "*") {
    return Mult_Op;
  } else if(operator_text == "/") {
    return Div_Op;
  } else if(operator_text == "!" || operator_text == "~"){
    return Not_Op;
  } else if(operator_text == "&") {
    return And_Op;
  } else if(operator_text == "^") {
    return Xor_Op;
  } else if(operator_text == "|") {
    return Or_Op;
  } else if(operator_text == ".()") {
    // SH:FIXME: might be fake funcall, set Invalid for now?
    // SH:FIXME: or change the target text and of into node_type_from_text()?
    return Invalid_Op;
  }else {
    fmt::print("Operator: {}\n", operator_text);
    return Invalid_Op;
  }
}
