// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inou_lnast_dfg.hpp"


void setup_inou_lnast_dfg() { Inou_lnast_dfg::setup(); }

void Inou_lnast_dfg::setup() {

  Eprp_method m1("inou.lnast_dfg.tolg", " front-end language lnast -> lgraph", &Inou_lnast_dfg::tolg);
  m1.add_label_optional("path", "path to output the lgraph[s] to", "lgdb");
  register_pass(m1);


  Eprp_method m2("inou.lnast_dfg.reduced_or_elimination", "reduced_or_op elimination for clear algorithm", &Inou_lnast_dfg::reduced_or_elimination);
  m2.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m2.add_label_optional("odir", "output directory for generated verilog files", ".");
  register_inou("lnast_dfg",m2);

  Eprp_method m3("inou.lnast_dfg.resolve_tuples", "resolve tuple chains for flattened lgraph", &Inou_lnast_dfg::resolve_tuples);
  m3.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m3.add_label_optional("odir", "output directory for generated verilog files", ".");
  register_inou("lnast_dfg",m3);
}

Inou_lnast_dfg::Inou_lnast_dfg(const Eprp_var &var) : Pass("inou.lnast_dfg", var) {
  setup_lnast_to_lgraph_primitive_type_mapping();
}


void Inou_lnast_dfg::tolg(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;
  for (const auto &ln : var.lnasts) {
    lgs = p.do_tolg(ln);
  }


  if (lgs.empty()) {
    error("failed to generate any lgraph from lnast");
  } else {
    var.add(lgs);
  }
}


std::vector<LGraph *> Inou_lnast_dfg::do_tolg(std::shared_ptr<Lnast> ln) {
    Lbench b("inou.lnast_dfg.tolg");

    lnast = ln;
    LGraph *dfg = LGraph::create(path, lnast->get_top_module_name(), "inou.lnast_dfg.tolg");
    std::vector<LGraph *> lgs;
    lnast->ssa_trans();
    lnast2lgraph(dfg);
    lgs.push_back(dfg);

    return lgs;
}



void Inou_lnast_dfg::lnast2lgraph(LGraph *dfg) {
  const auto top   = lnast->get_root();
  const auto stmts = lnast->get_first_child(top);
  process_ast_stmts(dfg, stmts);
}

void Inou_lnast_dfg::process_ast_stmts(LGraph *dfg, const Lnast_nid &lnidx_stmts) {
  for (const auto &lnidx : lnast->children(lnidx_stmts)) {
    const auto ntype = lnast->get_data(lnidx).type;
    // FIXME->sh: how to use switch to gain performance?
    if (ntype.is_assign()) {
      process_ast_assign_op(dfg, lnidx);
    } else if (ntype.is_nary_op()) {
      process_ast_nary_op(dfg, lnidx);
    } else if (ntype.is_unary_op()) {
      process_ast_nary_op(dfg, lnidx); // could be handled like unary
    } else if (ntype.is_tuple_add()) {
      process_ast_tuple_add_op(dfg, lnidx);
    } else if (ntype.is_tuple_get()) {
      process_ast_tuple_get_op(dfg, lnidx);
    } else if (ntype.is_tuple_phi_add()) {
      process_ast_tuple_phi_add_op(dfg, lnidx);
    } else if (ntype.is_dot()) {
      I(false); // should has been converted to tuple chain 
    } else if (ntype.is_select()) {
      I(false); // should has been converted to tuple chain
    } else if (ntype.is_logical_op()) {
      process_ast_logical_op(dfg, lnidx);
    } else if (ntype.is_as()) {
      process_ast_as_op(dfg, lnidx);
    } else if (ntype.is_label()) {
      process_ast_label_op(dfg, lnidx);
    } else if (ntype.is_dp_assign()) {
      process_ast_dp_assign_op(dfg, lnidx);
    } else if (ntype.is_tuple()) {
      process_ast_tuple_struct(dfg, lnidx);
    } else if (ntype.is_tuple_concat()) {
      process_ast_concat_op(dfg, lnidx);
    } else if (ntype.is_if()) {
      process_ast_if_op(dfg, lnidx);
    } else if (ntype.is_uif()) {
      process_ast_uif_op(dfg, lnidx);
    } else if (ntype.is_func_call()) {
      process_ast_func_call_op(dfg, lnidx);
    } else if (ntype.is_func_def()) {
      process_ast_func_def_op(dfg, lnidx);
    } else if (ntype.is_for()) {
      process_ast_for_op(dfg, lnidx);
    } else if (ntype.is_while()) {
      process_ast_while_op(dfg, lnidx);
    } else if (ntype.is_invalid()) { // FIXME->sh: add ignore type in LNAST?
      continue;
    } else if (ntype.is_const()) {
      I(lnast->get_name(lnidx) == "default_const");
      continue;
    } else if (ntype.is_reg_fwd()) {
      I(lnast->get_name(lnidx) == "register_forwarding");
      continue;
    } else if (ntype.is_err_flag()) {
      I(lnast->get_name(lnidx) == "err_var_undefined");
      continue;
    } else {
      I(false);
      return;
    }
  }
}


void Inou_lnast_dfg::process_ast_if_op(LGraph *dfg, const Lnast_nid &lnidx_if) {
  for (const auto& if_child : lnast->children(lnidx_if)) {
    auto ntype = lnast->get_type(if_child);
    if (ntype.is_cstmts() || ntype.is_stmts()) {
      process_ast_stmts(dfg, if_child);
    } else if (ntype.is_cond()) {
      continue;
    } else if (ntype.is_phi()) {
      process_ast_phi_op(dfg, if_child);
    } else {
      I(false); // if-subtree should only contain cstmts/stmts/cond/phi nodes
    }
  }
}

void Inou_lnast_dfg::process_ast_phi_op(LGraph *dfg, const Lnast_nid &lnidx_phi) {
  auto phi_node   = dfg->create_node(Mux_Op);
  auto cond_spin  = phi_node.setup_sink_pin("S"); // Y = ~SA + SB
  auto true_spin  = phi_node.setup_sink_pin("B");
  auto false_spin = phi_node.setup_sink_pin("A");

  auto c0 = lnast->get_first_child(lnidx_phi);
  auto c1 = lnast->get_sibling_next(c0);
  auto c2 = lnast->get_sibling_next(c1);
  auto c3 = lnast->get_sibling_next(c2);
  auto lhs_name = lnast->get_sname(c0);
  auto c2_name = lnast->get_sname(c2);
  auto c3_name = lnast->get_sname(c3);

  fmt::print("c2_name:{}\n", c2_name);
  fmt::print("c3_name:{}\n", c3_name);

  auto cond_dpin   = setup_ref_node_dpin(dfg, c1);
  Node_pin true_dpin;
  Node_pin false_dpin;
  if (c2_name == "register_forwarding_0") {
    // referece sibling's info to get the root reg qpin
    auto reg_name = c3_name.substr();
    std::string reg_dpin_name = c3_name.substr(0,c3_name.size()-2) + "_0";
    I(name2dpin[reg_dpin_name] != Node_pin());
    auto reg_qpin = name2dpin[reg_dpin_name];
    true_dpin = reg_qpin;
  } else {
    true_dpin = setup_ref_node_dpin(dfg, c2);
  }

  if (c3_name == "register_forwarding_0") {
    // referece sibling's info to get the root reg qpin
    auto reg_name = c2_name.substr();
    std::string reg_dpin_name = c2_name.substr(0,c2_name.size()-2) + "_0";
    fmt::print("reg_dpin_name:{}\n", reg_dpin_name);
    I(name2dpin[reg_dpin_name] != Node_pin());
    auto reg_qpin = name2dpin[reg_dpin_name];
    false_dpin = reg_qpin;
  } else {
    false_dpin  = setup_ref_node_dpin(dfg, c3);
  }

  dfg->add_edge(cond_dpin,  cond_spin);
  dfg->add_edge(true_dpin,  true_spin);
  dfg->add_edge(false_dpin, false_spin);


  if (is_register(lhs_name)){
    // (1) find the corresponding #reg and its qpin, #reg_0 
    std::string reg_dpin_name = lhs_name.substr(0,lhs_name.size()-2) + "_0";
    I(name2dpin[reg_dpin_name] != Node_pin());
    auto reg_qpin = name2dpin[reg_dpin_name];

    // (2) remove the previous D-pin edge from the #reg
    auto reg_node = reg_qpin.get_node();
    I(reg_node.setup_sink_pin("D").inp_edges().size() <= 1);
    if (reg_node.setup_sink_pin("D").inp_edges().size() == 1) {
      reg_node.setup_sink_pin("D").inp_edges().begin()->del_edge();
    }
     
    // (3) actively connect the new created #reg_N to the #reg D-pin
    auto dpin = phi_node.setup_driver_pin(0);
    auto spin = reg_node.setup_sink_pin("D");
    dfg->add_edge(dpin, spin);
  }

  name2dpin[lhs_name] = phi_node.setup_driver_pin();
  phi_node.setup_driver_pin().set_name(lhs_name);

}


void Inou_lnast_dfg::process_ast_concat_op(LGraph *dfg, const Lnast_nid &lnidx_concat) {
  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto tn_spin    = tup_add.setup_sink_pin(TN); //tuple name
  auto kn_spin    = tup_add.setup_sink_pin(KN); //key name, unknown when concatenating
  auto kp_spin    = tup_add.setup_sink_pin(KP); //key pos
  auto value_spin = tup_add.setup_sink_pin(KV); //key->value

  auto c0_concat = lnast->get_first_child(lnidx_concat); //c0: target name for concat.
  auto c1_concat = lnast->get_sibling_next(c0_concat);   //c1: tuple name
  auto c2_concat = lnast->get_sibling_next(c1_concat);   //c2: tuple value

  auto tn_dpin = setup_tuple_ref(dfg, lnast->get_sname(c1_concat));
  dfg->add_edge(tn_dpin, tn_spin);

  auto kp_dpin = setup_tuple_chain_new_max_pos(dfg, tn_dpin);
  dfg->add_edge(kp_dpin, kp_spin);

  auto value_dpin = setup_ref_node_dpin(dfg, c2_concat);
  dfg->add_edge(value_dpin, value_spin);

  name2dpin[lnast->get_sname(c0_concat)] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name(lnast->get_sname(c0_concat));
}


Node_pin Inou_lnast_dfg::setup_tuple_chain_new_max_pos(LGraph *dfg, const Node_pin &tn_dpin) {
  uint32_t max = 0;
  auto chain_itr = tn_dpin.get_node();
  while (chain_itr.get_type().op != TupRef_Op) {
    if (chain_itr.get_type().op == TupAdd_Op) {
      I(chain_itr.setup_sink_pin(TN).is_connected());
      I(chain_itr.setup_sink_pin(KP).is_connected());
      auto dnode_of_kp_spin = chain_itr.setup_sink_pin(KP).inp_edges().begin()->driver.get_node();
      //FIXME->sh: constant propagation problem again!? now assume the dnode of kp_spin is always a well-defined constant
      if (dnode_of_kp_spin.get_type_const_value() > max)
        max = dnode_of_kp_spin.get_type_const_value();
      auto next_itr = chain_itr.setup_sink_pin(TN).inp_edges().begin()->driver.get_node();
      chain_itr = next_itr;
    } else if (chain_itr.get_type().op == Or_Op) {
      I(chain_itr.setup_sink_pin(TN).inp_edges().size() == 1);
      auto next_itr = chain_itr.setup_sink_pin(TN).inp_edges().begin()->driver.get_node();
      chain_itr = next_itr;
    } else {
      I(false, "Compile Error: tuple chain must only contains TupAdd_Op or Or_Op"); 
    }
  }

  auto new_pos_str = "0d" + std::to_string(max + 1);
  return resolve_constant(dfg, new_pos_str).setup_driver_pin();
}


void Inou_lnast_dfg::process_ast_nary_op(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto opr_node = setup_node_opr_and_lhs(dfg, lnidx_opr).get_node();

  std::vector<Node_pin> opds;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    Node_pin opd;
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue; // the lhs has been handled at setup_node_opr_and_lhs();

    opd = setup_ref_node_dpin(dfg, opr_child);
    opds.emplace_back(opd);
  }

  nary_node_rhs_connections(dfg, opr_node, opds, lnast->get_type(lnidx_opr).is_minus());
}


void Inou_lnast_dfg::process_ast_logical_op(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  // (1) create logical operator node and record the dpin to symbol table
  // (2) create comparator node and compare with 0 for each of the inputs 
  // (3) take the result of every comparator as the inputs of logical operator inputs

  auto opr_node = setup_node_opr_and_lhs(dfg, lnidx_opr).get_node();
  std::vector<Node_pin> eqs_dpins;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue; // the lhs has been handled at setup_node_opr_and_lhs();

    auto node_eq = dfg->create_node(Equals_Op);
    auto ori_opd = setup_ref_node_dpin(dfg, opr_child);
    auto zero_dpin = dfg->create_node_const(0, 1).setup_driver_pin();
    dfg->add_edge(ori_opd, node_eq.setup_sink_pin(1));
    dfg->add_edge(zero_dpin, node_eq.setup_sink_pin(1));

    eqs_dpins.emplace_back(node_eq.setup_driver_pin());
  }

  nary_node_rhs_connections(dfg, opr_node, eqs_dpins, lnast->get_type(lnidx_opr).is_minus());
};



void Inou_lnast_dfg::nary_node_rhs_connections(LGraph *dfg, Node &opr_node, const std::vector<Node_pin> &opds, bool is_subt) {
  // FIXME->sh: need to think about signed number handling and signed number copy-propagation analysis
  // for now, assuming everything is unsigned number
  switch(opr_node.get_type().op){
    case Sum_Op:
    case Mult_Op: {
      bool is_first = true;
      for (const auto &opd : opds) {
        if (is_subt & !is_first) { //note: hunter -- for subtraction
          dfg->add_edge(opd, opr_node.setup_sink_pin(3));
        } else {
          dfg->add_edge(opd, opr_node.setup_sink_pin(1));
          is_first = false;
        }
      }
      break;
    }
    case LessThan_Op:
    case LessEqualThan_Op:
    case GreaterThan_Op:
    case GreaterEqualThan_Op: {
      auto i = 0;
      for (const auto &opd : opds) {
        if (i == 0) {
          dfg->add_edge(opd, opr_node.setup_sink_pin(1));  
        } else {
          dfg->add_edge(opd, opr_node.setup_sink_pin(3));  
        }
        i++;
        I(i <= 2); 
      }
      break;
    }
    default: {
      for (const auto &opd : opds) {
        dfg->add_edge(opd, opr_node.setup_sink_pin(0));  
      }
    }
  }
}



void Inou_lnast_dfg::process_ast_assign_op(LGraph *dfg, const Lnast_nid &lnidx_assign) {
  auto c0 = lnast->get_first_child(lnidx_assign);
  auto c1 = lnast->get_sibling_next(c0);
  auto c0_name = lnast->get_sname(c0);
  auto c1_name = lnast->get_sname(c1);

  Node_pin opr  = setup_node_assign_and_lhs(dfg, lnidx_assign);
  Node_pin opd1 = setup_ref_node_dpin(dfg, c1);
  GI(opd1.get_node().get_type().op != U32Const_Op, opd1.get_bits() == 0);

  dfg->add_edge(opd1, opr);

}


void Inou_lnast_dfg::process_ast_tuple_struct(LGraph *dfg, const Lnast_nid &lnidx_tup) {
  std::string tup_name;
  uint16_t kp = 0;

  //note: each new tuple element will be the new tuple chain tail and inherit the tuple name
  for (auto tup_child = lnast->children(lnidx_tup).begin(); tup_child != lnast->children(lnidx_tup).end(); ++tup_child) {
    if (tup_child == lnast->children(lnidx_tup).begin()) {
      tup_name = lnast->get_sname(*tup_child);
      setup_tuple_ref(dfg, tup_name);
    } else {
      I(lnast->get_type(*tup_child).is_assign());
      auto c0      = lnast->get_first_child(*tup_child);
      auto c1      = lnast->get_sibling_next(c0);
      auto key_name = lnast->get_sname(c0);
      // auto c1_name = lnast->get_sname(c1);

      auto tn_dpin    = setup_tuple_ref(dfg, tup_name);
      auto kn_dpin    = setup_tuple_key(dfg, key_name);
      keyname2pos[key_name] = "0d" + std::to_string(kp);
      auto kp_dnode   = resolve_constant(dfg, "0d" + std::to_string(kp));
      auto kp_dpin    = kp_dnode.setup_driver_pin();
      auto value_dpin = setup_ref_node_dpin(dfg, c1);

      auto tup_add    = dfg->create_node(TupAdd_Op);
      auto tn_spin    = tup_add.setup_sink_pin(TN); //tuple name
      auto kn_spin    = tup_add.setup_sink_pin(KN); //key name
      auto kp_spin    = tup_add.setup_sink_pin(KP); //key position is unknown before tuple resolving
      auto value_spin = tup_add.setup_sink_pin(KV); //value

      dfg->add_edge(tn_dpin, tn_spin);
      /* if (kn_dpin != Node_pin()) */
      /*   dfg->add_edge(kn_dpin, kn_spin); */
      dfg->add_edge(kn_dpin, kn_spin);
      dfg->add_edge(kp_dpin, kp_spin);
      dfg->add_edge(value_dpin, value_spin);

      kp++;
      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name);
    }
  }
}



void Inou_lnast_dfg::process_ast_tuple_phi_add_op(LGraph *dfg, const Lnast_nid &lnidx_tpa) {
  auto c0_tpa = lnast->get_first_child(lnidx_tpa);
  auto c1_tpa = lnast->get_sibling_next(c0_tpa);
  process_ast_phi_op(dfg, c0_tpa);
  process_ast_tuple_add_op(dfg, c1_tpa);
}


void Inou_lnast_dfg::process_ast_tuple_get_op(LGraph *dfg, const Lnast_nid &lnidx_tg) {
  //lnidx_opr = dot or sel
  auto c0_tg = lnast->get_first_child(lnidx_tg);
  auto c1_tg = lnast->get_sibling_next(c0_tg);
  auto c2_tg = lnast->get_sibling_next(c1_tg);

  auto c2_dot_name = lnast->get_sname(c2_tg);

  auto tup_get = dfg->create_node(TupGet_Op);
  auto tn_spin = tup_get.setup_sink_pin(TN); // tuple name
  auto kn_spin = tup_get.setup_sink_pin(KN); // key name
  auto kp_spin = tup_get.setup_sink_pin(KP); // key pos

  auto tn_dpin = setup_tuple_ref(dfg, lnast->get_sname(c1_tg));
  dfg->add_edge(tn_dpin, tn_spin);

  if (is_const(c2_dot_name)) {
    auto kp_dpin = setup_ref_node_dpin(dfg, c2_tg);
    dfg->add_edge(kp_dpin, kp_spin);
  } else {
    auto kn_dpin = setup_tuple_key(dfg, lnast->get_sname(c2_tg));
    dfg->add_edge(kn_dpin, kn_spin);
  }


  name2dpin[lnast->get_sname(c0_tg)] = tup_get.setup_driver_pin();
  tup_get.setup_driver_pin().set_name(lnast->get_sname(c0_tg));
}


void Inou_lnast_dfg::process_ast_tuple_add_op(LGraph *dfg, const Lnast_nid &lnidx_ta) {
  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto tn_spin    = tup_add.setup_sink_pin(TN); //tuple name
  auto kn_spin    = tup_add.setup_sink_pin(KN); //key name
  auto kp_spin    = tup_add.setup_sink_pin(KP); //key position of the key_name is recorded at tuple initialization
  auto value_spin = tup_add.setup_sink_pin(KV); //value
  auto c0_ta   = lnast->get_first_child(lnidx_ta);  //c0: tuple name
  auto c1_ta   = lnast->get_sibling_next(c0_ta);   //c1: key name
  auto c2_ta   = lnast->get_sibling_next(c1_ta);   //c2: value
  auto tup_name = lnast->get_sname(c0_ta);
  auto key_name = lnast->get_sname(c1_ta);


  if (key_name.size() >= 6 && key_name.substr(0,6) == "__bits") {
    // no need to connect to tuple_ref when __bits, meaningless
    setup_ref_node_dpin(dfg, c0_ta); // create corresponding io/reg if necessary
    auto kn_dpin = setup_tuple_key(dfg, key_name);
    dfg->add_edge(kn_dpin, kn_spin);

    auto value_dpin = setup_ref_node_dpin(dfg, c2_ta);
    dfg->add_edge(value_dpin, value_spin);
    auto dpin_name = lnast->get_sname(c0_ta).substr(0, lnast->get_sname(c0_ta).size()-2); // get rid of ssa name to avoid lgraph node search confliction
    tup_add.setup_driver_pin().set_name(dpin_name); // set name on driver_pin, but don't enter name2dpin table
    /* tup_add.setup_driver_pin().set_name(lnast->get_sname(c0_ta)); // set name on driver_pin, but don't enter name2dpin table */
  } else {
    auto tn_dpin = setup_tuple_ref(dfg, tup_name);
    dfg->add_edge(tn_dpin, tn_spin);

    Node_pin kn_dpin;
    if (is_const(key_name)) {// it is a key_pos, not a key_name
       // find pos2key_name
      for (auto &i : keyname2pos) {
        if (i.second == key_name) {
          kn_dpin = setup_tuple_key(dfg, i.first);
          dfg->add_edge(kn_dpin, kn_spin);
          break;
        }
      }
    } else {// it is a key_name
      kn_dpin = setup_tuple_key(dfg, lnast->get_sname(c1_ta));
      dfg->add_edge(kn_dpin, kn_spin);
    }

    

    std::string kp_str;
    if (is_const(key_name)) { // it is a key_pos, not a key_name
      kp_str = key_name;
    } else {
      kp_str = keyname2pos[key_name];
    } 

    auto kp_dpin = resolve_constant(dfg, kp_str).setup_driver_pin();
    dfg->add_edge(kp_dpin, kp_spin);

    auto value_dpin = setup_ref_node_dpin(dfg, c2_ta);
    dfg->add_edge(value_dpin, value_spin);
    name2dpin[lnast->get_sname(c0_ta)] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(lnast->get_sname(c0_ta)); // tuple ref semantically move to here
  }
}


//either tuple root or tuple key(str) fit in this case
Node_pin Inou_lnast_dfg::setup_tuple_ref(LGraph *dfg, std::string_view ref_name) {
  if (name2dpin.find(ref_name) == name2dpin.end()) {
    auto dpin = dfg->create_node(TupRef_Op).setup_driver_pin();
    dpin.set_name(ref_name);
    name2dpin[ref_name] = dpin;
  }
  return name2dpin[ref_name];
}


Node_pin Inou_lnast_dfg::setup_tuple_key(LGraph *dfg, std::string_view key_name) {
  /* if (key_name.substr(0,4)== "null") */
  /*   return Node_pin(); */

  /* if (name2dpin.find(key_name) == name2dpin.end()) { */
  /*   auto dpin = dfg->create_node(TupKey_Op).setup_driver_pin(); */
  /*   dpin.set_name(key_name); */
  /*   name2dpin[key_name] = dpin; */
  /* } */
  /* return name2dpin[key_name]; */

  if (name2dpin.find(key_name) == name2dpin.end()) {
    auto dpin = dfg->create_node(TupKey_Op).setup_driver_pin();
    if (key_name.substr(0,4) == "null")
      dpin.set_name(absl::StrCat("null", dpin.debug_name()));
    else 
      dpin.set_name(key_name);
    name2dpin[key_name] = dpin;
  }
  return name2dpin[key_name];
}




// for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node_pin Inou_lnast_dfg::setup_node_opr_and_lhs(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto c0 = lnast->get_first_child(lnidx_opr);
  auto c0_name = lnast->get_sname(c0);

  // generally, there won't be a case that the target node point to a output/reg directly
  // this is not allowed by LNAST.
  auto lg_ntype_op   = decode_lnast_op(lnidx_opr);
  auto node_dpin     = dfg->create_node(lg_ntype_op).setup_driver_pin(0);
  name2dpin[c0_name] = node_dpin;
  node_dpin.set_name(c0_name);
  return node_dpin;
}




Node_pin Inou_lnast_dfg::setup_node_assign_and_lhs(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto lhs      = lnast->get_first_child(lnidx_opr);
  auto lhs_name = lnast->get_sname(lhs);
  //handle output as lhs
  if (is_output(lhs_name)) {
    if(dfg->is_graph_output(lhs_name.substr(1, lhs_name.size()-3))) {
      return dfg->get_graph_output(lhs_name.substr(1, lhs_name.size()-3));  //get rid of '%' char
    } else {
      setup_ref_node_dpin(dfg, lhs);
      return dfg->get_graph_output(lhs_name.substr(1, lhs_name.size()-3));
    }
  } 

  //handle register as lhs
  if (is_register(lhs_name)) {
    //when #reg_0 at lhs, the register has not been created before
    fmt::print("lhs_name:{}\n", lhs_name);
    if (lhs_name.substr(lhs_name.size()-2) == "_0") {
      fmt::print("hello!!!\n");
      auto reg_qpin = setup_ref_node_dpin(dfg, lhs);
      return reg_qpin.get_node().setup_sink_pin("D");
    } else {
      // (1) create Or_Op to represent #reg_N except #reg_0, which has been represented as the #reg qpin
      auto equal_node =  dfg->create_node(Or_Op);
      name2dpin[lhs_name] = equal_node.setup_driver_pin(1); //reduced or output
      name2dpin[lhs_name].set_name(lhs_name);

      // (2) find the corresponding #reg and its qpin, #reg_0 
      std::string reg_dpin_name = lhs_name.substr(0,lhs_name.size()-2) + "_0";
      I(name2dpin[reg_dpin_name] != Node_pin());
      auto reg_qpin = name2dpin[reg_dpin_name];

      // (3) remove the previous D-pin edge from the #reg
      auto reg_node = reg_qpin.get_node();
      fmt::print("reg_node debug_name:{}\n", reg_node.debug_name());
      I(reg_node.get_type().op == SFlop_Op);
      I(reg_node.setup_sink_pin("D").inp_edges().size() <= 1);
      if (reg_node.setup_sink_pin("D").inp_edges().size() == 1) {
        reg_node.setup_sink_pin("D").inp_edges().begin()->del_edge();
      }
       
      // (4) actively connect the new created #reg_N to the #reg D-pin
      auto dpin = equal_node.setup_driver_pin(1);
      auto spin = reg_node.setup_sink_pin("D");
      dfg->add_edge(dpin, spin);

      // (5) return the spin of Or_Op node to be drived by rhs of assign node in lnast
      return equal_node.setup_sink_pin(0);
    }
  } 

  // handle lhs from normal opr
  auto equal_node =  dfg->create_node(Or_Op);
  name2dpin[lhs_name] = equal_node.setup_driver_pin(1); //reduced or output
  name2dpin[lhs_name].set_name(lhs_name);
  return equal_node.setup_sink_pin(0);
}


// for both target and operands, except the new io, reg, and const, the node and its dpin
// should already be in the table as the operand comes from existing operator output
Node_pin Inou_lnast_dfg::setup_ref_node_dpin(LGraph *dfg, const Lnast_nid &lnidx_opd) {

  auto name = lnast->get_sname(lnidx_opd);
  assert(!name.empty());

  const auto it = name2dpin.find(name);
  if (it != name2dpin.end()){
    return it->second;
  }

  Node_pin node_dpin;

  if (is_output(name)) {
    dfg->add_graph_output(name.substr(1, name.size()-3), Port_invalid, 0); // Port_invalid pos means do not care about position
    fmt::print("add graph out:{}\n", name.substr(1, name.size()-3));       // -3 means get rid of %, _0(ssa subscript)
    node_dpin = dfg->get_graph_output_driver_pin(name.substr(1, name.size()-3));
  } else if (is_input(name)) {
    node_dpin = dfg->add_graph_input(name.substr(1, name.size()-3), Port_invalid, 0);
    fmt::print("add graph inp:{}\n", name.substr(1, name.size()-3));
  } else if (is_const(name)) {
    node_dpin = resolve_constant(dfg, name).setup_driver_pin();
  } else if (is_default_const(name)) {
    node_dpin = resolve_constant(dfg, "0d0").setup_driver_pin();
  } else if (is_register(name)) {
    node_dpin = dfg->create_node(SFlop_Op).setup_driver_pin();

    Node_pin clk_dpin;
    if (!dfg->is_graph_input("clk")) {
      clk_dpin = dfg->add_graph_input("clk", Port_invalid, 0);
      clk_dpin.ref_bitwidth()->e.set_ubits(1);
    } else {
      clk_dpin = dfg->get_graph_input("clk");
    }

    auto clk_spin = node_dpin.get_node().setup_sink_pin("C");
    dfg->add_edge (clk_dpin, clk_spin);

    // FIXME->sh: reset pin not supported in SFlop yet !!?
    /* Node_pin rst_dpin; */
    /* if (!dfg->is_graph_input("rst")) { */
    /*   rst_dpin = dfg->add_graph_input("rst", Port_invalid, 0); */
    /*   rst_dpin.ref_bitwidth()->e.set_ubits(1); */
    /* } else { */
    /*   rst_dpin = dfg->get_graph_input("rst"); */
    /* } */
    /* auto rst_spin = node_dpin.get_node().setup_sink_pin("CLR"); */
    /* dfg->add_edge (rst_dpin, rst_spin); */


  } else if (is_err_var_undefined(name)) {
    node_dpin = dfg->create_node(CompileErr_Op).setup_driver_pin();
  } else {
    return node_dpin; //return empty node_pin and trigger compile error
  }


  if (is_output(name) || is_input(name)) {
    ;
  } else {
    node_dpin.set_name(name);
  }

  name2dpin[name] = node_dpin;  // for io and reg, the %$# identifier are still used in symbol table
  return node_dpin;
}

Node_Type_Op Inou_lnast_dfg::decode_lnast_op(const Lnast_nid &lnidx_opr) {
  const auto raw_ntype = lnast->get_data(lnidx_opr).type.get_raw_ntype();
  return primitive_type_lnast2lg[raw_ntype];
}


void Inou_lnast_dfg::process_ast_as_op       (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_label_op    (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_uif_op      (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_func_call_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_func_def_op (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_sub_op      (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_for_op      (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_while_op    (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_dp_assign_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };

void Inou_lnast_dfg::setup_lnast_to_lgraph_primitive_type_mapping() {
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_invalid]     = Invalid_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_assign]      = Or_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_and] = And_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_or]  = Or_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_not] = Not_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_and]         = And_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_or]          = Or_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_not]         = Not_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_xor]         = Xor_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_plus]        = Sum_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_minus]       = Sum_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_mult]        = Mult_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_div]         = Div_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_same]        = Equals_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_lt]          = LessThan_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_le]          = LessEqualThan_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_gt]          = GreaterThan_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_ge]          = GreaterEqualThan_Op;
  // sh_fixme: to be extended ...
}


