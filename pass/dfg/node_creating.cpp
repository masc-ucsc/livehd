#include "pass_dfg.hpp"
#include <string>

Index_ID Pass_dfg::create_register(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->node_type_set(nid, Flop_Op);
  state->add_register(var_name, nid);

  return nid;
}

Index_ID Pass_dfg::create_input(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->add_graph_input(var_name.c_str(), nid);

  return nid;
}

Index_ID Pass_dfg::create_output(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->add_graph_output(var_name.c_str(), nid);
  
  return nid;
}

Index_ID Pass_dfg::create_private(LGraph *g, CF2DF_State *state, const std::string &var_name) {
  Index_ID nid = create_node(g, state, var_name);
  g->node_type_set(nid, CfgAssign_Op);

  g->add_edge(Node_Pin(nid, 0, false), Node_Pin(default_constant(g, state), 0, true));
 
  return nid;
}

Index_ID Pass_dfg::default_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 0);

  return nid;
}

Index_ID Pass_dfg::true_constant(LGraph *g, CF2DF_State *state) {
  Index_ID nid = g->create_node().get_nid();
  g->node_type_set(nid, U32Const_Op);
  g->node_u32type_set(nid, 1);

  std::string var_name = temp();
  g->set_node_wirename(nid, var_name.c_str());
  state->symbol_table().add(var_name, Type::create_logical());

  return nid;
}

Index_ID Pass_dfg::create_node(LGraph *g, CF2DF_State *state, const std::string &v) {
  Index_ID nid = g->create_node().get_nid();
  state->update_reference(v, nid);
  g->set_node_wirename(nid, v.c_str());

  return nid;
}

Index_ID Pass_dfg::create_AND(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2) {
  return create_binary(g, state, op1, op2, LOGICAL_AND_OP);
}

Index_ID Pass_dfg::create_OR(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2) {
  return create_binary(g, state, op1, op2, LOGICAL_OR_OP);
}

Index_ID Pass_dfg::create_binary(LGraph *g, CF2DF_State *state, Index_ID op1, Index_ID op2, const char *oper) {
  auto target = temp();
  Index_ID dfnode = create_node(g, state, target);

  g->node_type_set(dfnode, CfgAssign_Op);
  //g->set_node_instance(dfnode, oper);

  g->add_edge(Node_Pin(op1, 0, false), Node_Pin(dfnode, 0, true));
  g->add_edge(Node_Pin(op2, 0, false), Node_Pin(dfnode, 0, true));

  return dfnode;
}

Index_ID Pass_dfg::create_NOT(LGraph *g, CF2DF_State *state, Index_ID op1) {
  auto target = temp();
  Index_ID dfnode = create_node(g, state, target);

  g->node_type_set(dfnode, CfgAssign_Op);
  //g->set_node_instance(dfnode, LOGICAL_NOT_OP);

  g->add_edge(Node_Pin(op1, 0, false), Node_Pin(dfnode, 0, true));

  return dfnode;
}

