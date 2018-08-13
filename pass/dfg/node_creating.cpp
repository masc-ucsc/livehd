#include "pass_dfg.hpp"
#include "nodetype.hpp"
#include <string>

Index_ID Pass_dfg::create_register(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  fmt::print("create node nid:{}\n", nid);
  g->node_type_set(nid, Flop_Op);
  state->add_register(var_name, nid);

  return nid;
}

Index_ID Pass_dfg::create_input(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->add_graph_input(var_name.substr(1).c_str(), nid, 1/*bit*/); //get rid of $mark

  return nid;
}

Index_ID Pass_dfg::create_output(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->add_graph_output(var_name.substr(1).c_str(), nid, 1/*bit*/); //get rid of $mark

  return nid;
}

Index_ID Pass_dfg::create_private(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->node_type_set(nid, Or_Op);

  //sh marks down, currently not used for any patterns
  //g->add_edge(Node_Pin(nid, 0, false), Node_Pin(default_constant(g, state), 0, true));
  //g->add_edge(Node_Pin(nid, 0, false), Node_Pin(false_constant(g, state), 0, true));

  return nid;
}

Index_ID Pass_dfg::default_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  fmt::print("create node for default_constant:{}, nid:{}\n", nid); //sh dbg
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);

  return nid;
}

Index_ID Pass_dfg::true_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  fmt::print("create node for true_const:{}, nid:{}\n", nid); //sh dbg
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 1);

  std::string var_name = temp();
  g->set_node_wirename(nid, var_name.c_str());

  return nid;
}

Index_ID Pass_dfg::false_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  fmt::print("create node for false_const:{}, nid:{}\n", nid); //sh dbg
  fmt::print("create node nid:{}\n", nid);
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);

  std::string var_name = temp();
  g->set_node_wirename(nid, var_name.c_str());

  return nid;
}

Index_ID Pass_dfg::create_node(LGraph *g, CF2DF_State *state, const std::string &v) {
  Index_ID nid = g->create_node().get_nid();
  state->update_reference(v, nid);
  g->set_node_wirename(nid, v.c_str());
  g->set_bits(nid,1);//sh dbg
  return nid;
}

Index_ID Pass_dfg::create_AND(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2) {
  return create_binary(g, state, op1, op2, And_Op);
}

Index_ID Pass_dfg::create_OR(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2) {
  return create_binary(g, state, op1, op2, Or_Op);
}

Index_ID Pass_dfg::create_binary(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2, Node_Type_Op oper) {
  auto target = temp();
  Index_ID dfnode = create_node(g, state, target);

  g->node_type_set(dfnode, oper);

  g->add_edge(Node_Pin(op1, 0, false), Node_Pin(dfnode, 0, true));
  g->add_edge(Node_Pin(op2, 0, false), Node_Pin(dfnode, 0, true));

  return dfnode;
}

Index_ID Pass_dfg::create_NOT(LGraph *g, CF2DF_State *state, Index_ID op1) {
  auto target = temp();
  Index_ID dfnode = create_node(g, state, target);

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