// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "lnast_dfg.hpp"
#include <bits/stdint-uintn.h>
#include "pass.hpp"

Lnast_dfg::Lnast_dfg(const Eprp_var &_var, std::string_view _module_name) :
  Pass("inou.lnast_dfg", _var), eprp_var(_var), module_name(_module_name) {
  setup_lnast_to_lgraph_primitive_type_mapping();
}


std::vector<LGraph *> Lnast_dfg::do_tolg(std::shared_ptr<Lnast> ln, const Lnast_nid &top_stmts) {
    lnast = ln;
    LGraph *dfg;
    if (lnast->get_source() != "") {
      dfg = LGraph::create(path, module_name, lnast->get_source());
    } else {
      dfg = LGraph::create(path, module_name, module_name);
    }

    std::vector<LGraph *> lgs;
    top_stmts2lgraph(dfg, top_stmts);
    lgs.push_back(dfg);

    return lgs;
}


void Lnast_dfg::top_stmts2lgraph(LGraph *dfg, const Lnast_nid &lnidx_stmts) {
  fmt::print("============================= Phase-1: LNAST->LGraph Start ===============================================\n");
  process_ast_stmts(dfg, lnidx_stmts);

  fmt::print("============================= Phase-2: Adding final Module Outputs and Final Dpin Name ===================\n");
  setup_lgraph_outputs_and_final_var_name(dfg);

  dfg->ref_self_sub_node()->populate_graph_pos();
}



void Lnast_dfg::process_ast_stmts(LGraph *dfg, const Lnast_nid &lnidx_stmts) {
  for (const auto &lnidx : lnast->children(lnidx_stmts)) {
    const auto ntype = lnast->get_data(lnidx).type;
    if (ntype.is_assign()) {
      process_ast_assign_op(dfg, lnidx);
    } else if (ntype.is_dp_assign()) {
      process_ast_dp_assign_op(dfg, lnidx);
    } else if (ntype.is_nary_op() || ntype.is_unary_op()) {
      process_ast_nary_op(dfg, lnidx);
    } else if (ntype.is_attr_set()) {
      process_ast_attr_set_op(dfg, lnidx);
    } else if (ntype.is_attr_get()) {
      process_ast_attr_get_op(dfg, lnidx);
    } else if (ntype.is_tuple_add()) {
      process_ast_tuple_add_op(dfg, lnidx);
    } else if (ntype.is_tuple_get()) {
      process_ast_tuple_get_op(dfg, lnidx);
    } else if (ntype.is_tuple_phi_add()) {
      process_ast_tuple_phi_add_op(dfg, lnidx);
    } else if (ntype.is_logical_op()) {
      process_ast_logical_op(dfg, lnidx);
    } else if (ntype.is_as()) {
      process_ast_as_op(dfg, lnidx);
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
    } else if (ntype.is_invalid()) {
      continue;
    } else if (ntype.is_const()) {
      I(lnast->get_name(lnidx) == "default_const");
      continue;
    } else if (ntype.is_dot() || ntype.is_select()) {
      I(false); // have been converted to tuple chain
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


void Lnast_dfg::process_ast_if_op(LGraph *dfg, const Lnast_nid &lnidx_if) {
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

void Lnast_dfg::process_ast_phi_op(LGraph *dfg, const Lnast_nid &lnidx_phi) {
  auto phi_node   = dfg->create_node(Mux_Op);
  auto cond_spin  = phi_node.setup_sink_pin("S"); // Y = ~SA + SB
  auto true_spin  = phi_node.setup_sink_pin("B");
  auto false_spin = phi_node.setup_sink_pin("A");

  auto lhs = lnast->get_first_child(lnidx_phi);
  auto c1 = lnast->get_sibling_next(lhs);
  auto c2 = lnast->get_sibling_next(c1);
  auto c3 = lnast->get_sibling_next(c2);
  auto lhs_name = lnast->get_sname(lhs);
  auto c2_name = lnast->get_sname(c2);
  auto c3_name = lnast->get_sname(c3);

  auto cond_dpin   = setup_ref_node_dpin(dfg, c1);
  Node_pin true_dpin;
  Node_pin false_dpin;
  if (c2_name == "register_forwarding_0") {
    // referece sibling's info to get the root reg qpin
    auto reg_name = c3_name.substr();

    auto pos = reg_name.find_last_of('_');
    auto reg_qpin_name = lhs_name.substr(0,pos);

    I(name2dpin[reg_qpin_name] != Node_pin());
    auto reg_qpin = name2dpin[reg_qpin_name];
    true_dpin = reg_qpin;
  } else {
    true_dpin = setup_ref_node_dpin(dfg, c2, true);
  }

  if (c3_name == "register_forwarding_0") {
    // referece sibling's info to get the root reg qpin
    auto reg_name = c2_name.substr();
    auto pos = reg_name.find_last_of('_');
    auto reg_qpin_name = lhs_name.substr(0,pos);
    I(name2dpin[reg_qpin_name] != Node_pin());
    auto reg_qpin = name2dpin[reg_qpin_name];
    false_dpin = reg_qpin;
  } else {
    false_dpin  = setup_ref_node_dpin(dfg, c3, true);
  }

  dfg->add_edge(cond_dpin,  cond_spin);
  dfg->add_edge(true_dpin,  true_spin);
  dfg->add_edge(false_dpin, false_spin);


  if (is_register(lhs_name)){
    // (1) find the corresponding #reg and its qpin, wname = #reg
    auto pos = lhs_name.find_last_of('_');
    auto reg_qpin_name = lhs_name.substr(0,pos);
    I(name2dpin[reg_qpin_name] != Node_pin());
    auto reg_qpin = name2dpin[reg_qpin_name];

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
  auto lhs_name_no_ssa = lnast->get_vname(lhs);
  setup_dpin_ssa(name2dpin[lhs_name], lhs_name_no_ssa, lnast->get_subs(lhs));
}


void Lnast_dfg::process_ast_concat_op(LGraph *dfg, const Lnast_nid &lnidx_concat) {
  //FIXME->sh: how to support hierarchical tuple???

  auto lhs  = lnast->get_first_child(lnidx_concat); //c0: target tuple name for concat.
  auto opd1 = lnast->get_sibling_next(lhs);         //c1: tuple operand1, either scalar or tuple
  auto opd2 = lnast->get_sibling_next(opd1);        //c2: tuple operand2, either scalar or tuple
  auto lhs_name  = lnast->get_sname(lhs);
  auto opd1_name = lnast->get_sname(opd1);
  auto opd2_name = lnast->get_sname(opd2);
  //lhs = opd1 ++ opd2, both opd1 and opd2 could be either a scalar or a tuple


  //create TupAdd, concat both tail of opd1 and opd2, name it with old opd1_name (a = a ++ b) or new lhs_name (c = a ++ b)
    auto tup_add    = dfg->create_node(TupAdd_Op);
    auto tn_spin    = tup_add.setup_sink_pin("TN"); // tuple name
    auto value_spin = tup_add.setup_sink_pin("KV"); // key->value

    auto tn_dpin    = setup_ref_node_dpin(dfg, opd1, false, true);
    dfg->add_edge(tn_dpin, tn_spin);

    auto value_dpin = setup_ref_node_dpin(dfg, opd2, false, true);
    dfg->add_edge(value_dpin, value_spin);


  if (lhs_name == opd1_name) {
    name2dpin[opd1_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(opd1_name);
  } else {
    name2dpin[lhs_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(lhs_name);
  }

  setup_dpin_ssa(name2dpin[lhs_name], lnast->get_vname(lhs), lnast->get_subs(lhs));
}



void Lnast_dfg::process_ast_nary_op(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto opr_node = setup_node_opr_and_lhs(dfg, lnidx_opr);

  std::vector<Node_pin> opds;
  Node_pin opd;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue; // the lhs has been handled at setup_node_opr_and_lhs();

    opd = setup_ref_node_dpin(dfg, opr_child);
    opds.emplace_back(opd);
  }
  nary_node_rhs_connections(dfg, opr_node, opds, lnast->get_type(lnidx_opr).is_minus());
}


void Lnast_dfg::process_ast_logical_op(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  // (1) create logical operator node and record the dpin to symbol table
  // (2) create comparator node and compare with 0 for each of the inputs
  // (3) take the result of every comparator as the inputs of logical operator inputs

  auto opr_node = setup_node_opr_and_lhs(dfg, lnidx_opr);
  std::vector<Node_pin> eqs_dpins;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue; // the lhs has been handled at setup_node_opr_and_lhs();

    auto node_eq = dfg->create_node(Equals_Op);
    auto ori_opd = setup_ref_node_dpin(dfg, opr_child);
    auto zero_dpin = dfg->create_node_const(Lconst(0, 1)).setup_driver_pin();

    dfg->add_edge(ori_opd, node_eq.setup_sink_pin(1));
    dfg->add_edge(zero_dpin, node_eq.setup_sink_pin(1));

    eqs_dpins.emplace_back(node_eq.setup_driver_pin());
  }

  nary_node_rhs_connections(dfg, opr_node, eqs_dpins, lnast->get_type(lnidx_opr).is_minus());
};



void Lnast_dfg::nary_node_rhs_connections(LGraph *dfg, Node &opr_node, const std::vector<Node_pin> &opds, bool is_subt) {
  // FIXME->sh: need to think about signed number handling and signed number copy-propagation analysis for now, assuming everything is unsigned number
  switch(opr_node.get_type().op) {
    case Sum_Op:
    case Mult_Op: { // FIXME: add could be + a b c (same mult)
      bool is_first = true;
      for (const auto &opd : opds) {
        if (is_subt & !is_first) { //note: Hunter -- for subtraction
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
      I(opds.size()==2); // FIXME: comparator can have many inputs (a<b<c<d)
      dfg->add_edge(opds[0], opr_node.setup_sink_pin(1));
      dfg->add_edge(opds[1], opr_node.setup_sink_pin(3));  // why not sign?
      break;
    }
    case ShiftLeft_Op:
    case ShiftRight_Op: {
      I(opds.size()==2); // val<<amount
      dfg->add_edge(opds[0], opr_node.setup_sink_pin(0));
      dfg->add_edge(opds[1], opr_node.setup_sink_pin(1));
      break;
    }
    default: {
      for (const auto &opd : opds) {
        dfg->add_edge(opd, opr_node.setup_sink_pin(0));
      }
    }
  }
}

Node Lnast_dfg::process_ast_assign_op(LGraph *dfg, const Lnast_nid &lnidx_assign) {
  auto c0 = lnast->get_first_child(lnidx_assign);
  auto c1 = lnast->get_sibling_next(c0);

  Node_pin opd1 = setup_ref_node_dpin(dfg, c1, false, false, false, true);
  Node     opd1_node = opd1.get_node();

  Node_pin opr_spin;
  if (opd1_node.get_type_op() == TupAdd_Op) {
    opr_spin  = setup_tuple_assignment(dfg, lnidx_assign);
  } else {
    opr_spin  = setup_node_assign_and_lhs(dfg, lnidx_assign);
  }

  dfg->add_edge(opd1, opr_spin);
  return opr_spin.get_node();
}


void Lnast_dfg::process_ast_dp_assign_op(LGraph *dfg, const Lnast_nid &lnidx_dp_assign) {
  auto aset_node = dfg->create_node(AttrSet_Op);
  auto vn_spin   = aset_node.setup_sink_pin("VN"); // variable name
  auto an_spin   = aset_node.setup_sink_pin("AN"); // attribute name
  auto av_spin   = aset_node.setup_sink_pin("AV"); // attribute value

  auto c0_dp       = lnast->get_first_child(lnidx_dp_assign);
  auto c1_dp       = lnast->get_sibling_next(c0_dp);
  auto c0_dp_name  = lnast->get_sname(c0_dp);  // ssa name
  auto attr_vname  = "__dp_assign";  // no-ssa name
  auto c0_dp_vname = lnast->get_vname(c0_dp);  // no-ssa name

  auto vn_dpin = setup_ref_node_dpin(dfg, c1_dp);
  dfg->add_edge(vn_dpin, vn_spin);
  auto an_dpin = setup_key_dpin(dfg, attr_vname);
  dfg->add_edge(an_dpin, an_spin);


  auto dp_ancestor_subs  = lnast->get_data(c0_dp).subs - 1;
  auto dp_ancestor_name = std::string(c0_dp_vname) + "_" + std::to_string(dp_ancestor_subs);
  /* fmt::print("aset ancestor name:{}\n", dp_ancestor_name); */
  I(name2dpin.find(dp_ancestor_name) != name2dpin.end());

  auto av_dpin = name2dpin[dp_ancestor_name];
  dfg->add_edge(av_dpin, av_spin);

  aset_node.setup_driver_pin(0).set_name(c0_dp_name);
  name2dpin[c0_dp_name] = aset_node.get_driver_pin(0);
  setup_dpin_ssa(name2dpin[c0_dp_name], c0_dp_vname, lnast->get_subs(c0_dp));

  if (is_register(c0_dp_name)) {
    auto reg_node = name2dpin[c0_dp_vname].get_node();
    I(reg_node.get_type().op == SFlop_Op);
    I(reg_node.setup_sink_pin("D").inp_edges().size() <= 1);
    if (reg_node.setup_sink_pin("D").inp_edges().size() == 1) {
      reg_node.setup_sink_pin("D").inp_edges().begin()->del_edge();
    }
    auto dpin = aset_node.setup_driver_pin(0);
    auto spin = reg_node.setup_sink_pin("D");
    dfg->add_edge(dpin, spin);
  }
}



void Lnast_dfg::process_ast_tuple_struct(LGraph *dfg, const Lnast_nid &lnidx_tup) {
  std::string tup_name;
  std::string_view tup_vname;
  int8_t subs;
  uint16_t kp = 0;

  // note: each new tuple element will be the new tuple chain tail and inherit the tuple name
  for (const auto &tup_child : lnast->children(lnidx_tup)) {
    if (tup_child == lnast->get_first_child(lnidx_tup)) {
      tup_name = lnast->get_sname(tup_child);
      tup_vname = lnast->get_vname(tup_child);
      subs = lnast->get_subs(tup_child);
      setup_tuple_ref(dfg, tup_name, true);
      continue;
    }

    // the case with key name well-defined
    if (lnast->get_type(tup_child).is_assign()) {
      auto c0       = lnast->get_first_child(tup_child);
      auto c1       = lnast->get_sibling_next(c0);
      auto key_name = lnast->get_sname(c0);

      auto tn_dpin    = setup_tuple_ref(dfg, tup_name, true);
      auto kp_dnode   = dfg->create_node_const(Lconst(kp));
      auto kp_dpin    = kp_dnode.setup_driver_pin();
      auto value_dpin = setup_ref_node_dpin(dfg, c1, 0, 0, 1);

      auto tup_add    = dfg->create_node(TupAdd_Op);
      auto tn_spin    = tup_add.setup_sink_pin("TN"); // tuple name
      auto kp_spin    = tup_add.setup_sink_pin("KP"); // key position is unknown before tuple resolving
      auto value_spin = tup_add.setup_sink_pin("KV"); // value

      if (key_name.substr(0,4) != "null") {
        auto kn_dpin    = setup_key_dpin(dfg, key_name);
        auto kn_spin    = tup_add.setup_sink_pin("KN"); // key name
        dfg->add_edge(kn_dpin, kn_spin);
      }

      dfg->add_edge(tn_dpin, tn_spin);
      dfg->add_edge(kp_dpin, kp_spin);
      dfg->add_edge(value_dpin, value_spin);

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name);
      setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);

      kp++;
      continue;
    }

    auto tn_dpin    = setup_tuple_ref(dfg, tup_name, true);
    auto kp_dnode   = dfg->create_node_const(Lconst(kp));
    auto kp_dpin    = kp_dnode.setup_driver_pin();
    auto value_dpin = setup_ref_node_dpin(dfg, tup_child, 0, 0, 1);

    auto tup_add    = dfg->create_node(TupAdd_Op);
    auto tn_spin    = tup_add.setup_sink_pin("TN"); // tuple name
    auto kp_spin    = tup_add.setup_sink_pin("KP"); // key position is unknown before tuple resolving
    auto value_spin = tup_add.setup_sink_pin("KV"); // value

    dfg->add_edge(tn_dpin, tn_spin);
    dfg->add_edge(kp_dpin, kp_spin);
    dfg->add_edge(value_dpin, value_spin);

    name2dpin[tup_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(tup_name);
    setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);
    kp++;

  }
}


void Lnast_dfg::process_ast_tuple_phi_add_op(LGraph *dfg, const Lnast_nid &lnidx_tpa) {
  auto c0_tpa = lnast->get_first_child(lnidx_tpa);
  auto c1_tpa = lnast->get_sibling_next(c0_tpa);
  process_ast_phi_op(dfg, c0_tpa);
  process_ast_tuple_add_op(dfg, c1_tpa);
}

void Lnast_dfg::process_ast_tuple_get_op(LGraph *dfg, const Lnast_nid &lnidx_tg) {
  int i = 0;
  absl::flat_hash_map<int, Node> tg_map;
  /* absl::flat_hash_map<int, std::string_view> tg_name; */
  std::string c0_tg_name;
  std::string_view c0_tg_vname;
  int8_t           c0_tg_subs;

  for (const auto &child : lnast->children(lnidx_tg)) {
    if (i == 0) {
      const auto &c0_tg = child;
      c0_tg_name  = lnast->get_sname(c0_tg);
      c0_tg_vname = lnast->get_vname(c0_tg);
      c0_tg_subs  = lnast->get_subs(c0_tg);
      i++;
      continue;
    }

    if (i == 1) {
      const auto &c1_tg = child;
      auto tup_get = dfg->create_node(TupGet_Op);
      tg_map[i] = tup_get;

      auto tn_spin = tup_get.setup_sink_pin("TN");

      Node_pin tn_dpin;
      if (is_register(lnast->get_vname(c1_tg)))
        tn_dpin = setup_ref_node_dpin(dfg, c1_tg);
      else
        tn_dpin = setup_tuple_ref(dfg, lnast->get_sname(c1_tg));

      dfg->add_edge(tn_dpin, tn_spin);
      i++;
      continue;
    }

    // i >= 2
    if (child == lnast->get_last_child(lnidx_tg)) {
      const auto& cn_tg = child;
      auto cn_tg_name = lnast->get_sname(cn_tg);
      auto tup_get = tg_map[i-1];
      auto kn_spin = tup_get.setup_sink_pin("KN");
      auto kp_spin = tup_get.setup_sink_pin("KP");

      if (is_const(cn_tg_name)) {
        auto kp_dpin = setup_ref_node_dpin(dfg, cn_tg);
        dfg->add_edge(kp_dpin, kp_spin);
      } else {
        auto kn_dpin = setup_key_dpin(dfg, cn_tg_name);
        dfg->add_edge(kn_dpin, kn_spin);
      }

      if (vname2attr_dpin.find(c0_tg_vname) != vname2attr_dpin.end()) {
        auto aset_node = dfg->create_node(AttrSet_Op);
        auto aset_aci_spin = aset_node.setup_sink_pin("ACI");
        auto aset_ancestor_dpin = vname2attr_dpin[c0_tg_vname];
        dfg->add_edge(aset_ancestor_dpin, aset_aci_spin);

        auto aset_vn_spin = aset_node.setup_sink_pin("VN");
        auto aset_vn_dpin = tup_get.get_driver_pin(0);
        dfg->add_edge(aset_vn_dpin, aset_vn_spin);

        name2dpin[c0_tg_name] = aset_node.setup_driver_pin(0); // dummy_attr_set node now represent the latest variable
        aset_node.get_driver_pin(0).set_name(c0_tg_name);
        setup_dpin_ssa(name2dpin[c0_tg_name], c0_tg_vname, c0_tg_subs);
        vname2attr_dpin[c0_tg_vname] = aset_node.setup_driver_pin(1);
        return;
      }

      name2dpin[c0_tg_name] = tup_get.setup_driver_pin();
      tup_get.setup_driver_pin().set_name(c0_tg_name);
      setup_dpin_ssa(name2dpin[c0_tg_name], c0_tg_vname, c0_tg_subs);

    } else { //not the last child
      auto new_tup_get = dfg->create_node(TupGet_Op);
      tg_map[i] = new_tup_get;
      auto tn_spin = new_tup_get.setup_sink_pin("TN");


      const auto& cn_tg = child;
      auto cn_tg_name = lnast->get_sname(cn_tg);
      auto prev_tup_get = tg_map[i-1];
      auto kn_spin = prev_tup_get.setup_sink_pin("KN");
      auto kp_spin = prev_tup_get.setup_sink_pin("KP");

      if (is_const(cn_tg_name)) {
        auto kp_dpin = setup_ref_node_dpin(dfg, cn_tg);
        dfg->add_edge(kp_dpin, kp_spin);
      } else {
        auto kn_dpin = setup_key_dpin(dfg, cn_tg_name);
        dfg->add_edge(kn_dpin, kn_spin);
      }

      auto tn_dpin = prev_tup_get.setup_driver_pin();
      dfg->add_edge(tn_dpin, tn_spin);

      i++;
      continue;
    }
  }
}


void Lnast_dfg::process_ast_tuple_add_op(LGraph *dfg, const Lnast_nid &lnidx_ta) {
  int i = 0;
  absl::flat_hash_map<int, Node> ta_map;
  absl::flat_hash_map<int, std::string_view> ta_name;
  for (const auto & child : lnast->children(lnidx_ta)) {
    if (i == 0) {
      const auto &c0_ta = child;
      auto tup_add  = dfg->create_node(TupAdd_Op);
      auto tn_spin  = tup_add.setup_sink_pin("TN");
      auto tup_name = lnast->get_sname(c0_ta);
      auto tn_dpin = setup_tuple_ref(dfg, tup_name, true);

      // exclude invalid scalar->tuple cases
      auto tn_node  = tn_dpin.get_node();
      auto tn_ntype = tn_node.get_type().op;
      bool is_scalar =  tn_ntype != TupAdd_Op && tn_ntype != TupRef_Op;
      auto key_name = lnast->get_sname(lnast->get_sibling_next(c0_ta)); // peep for key_name ...
      if (is_scalar && key_name != "0")
	    	Pass::error("try to modify a non-exist tuple key field:{} in tuple:{}\n", key_name, tup_name);

      dfg->add_edge(tn_dpin, tn_spin);

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name); // tuple ref semantically move to here
      setup_dpin_ssa(name2dpin[tup_name], lnast->get_vname(c0_ta), lnast->get_subs(c0_ta));

      ta_map[i]  = tup_add;
      ta_name[i] = tup_name;
      i++ ;
      continue;
    }

    if (i == 1) {
      const auto &c1_ta = child;
      auto tup_add = ta_map[i-1];
      auto kn_spin = tup_add.setup_sink_pin("KN"); //key name
      auto kp_spin = tup_add.setup_sink_pin("KP"); //key name
      auto key_name = lnast->get_sname(c1_ta);
      Node_pin kn_dpin;
      if (is_const(key_name)) { // it is a key_pos, not a key_name
        auto kp_dpin = dfg->create_node_const(Lconst(key_name)).setup_driver_pin();
        dfg->add_edge(kp_dpin, kp_spin);
      } else if (key_name.substr(0,4) != "null") {// it is a pure key_name
        kn_dpin = setup_key_dpin(dfg, key_name);
        dfg->add_edge(kn_dpin, kn_spin);
      }
      ta_name[i] = lnast->get_sname(c1_ta);
      i++ ;
      continue;
    }

    // i >= 2
    if (child == lnast->get_last_child(lnidx_ta)) {
      // non-hier tuple case
      const auto & c2_ta = child;
      auto tup_add = ta_map[i-2];
      auto value_spin = tup_add.setup_sink_pin("KV"); //value
      auto value_dpin = setup_ref_node_dpin(dfg, c2_ta);
      dfg->add_edge(value_dpin, value_spin);
      i++ ;
    } else {
      // hier tuple case
      // create a new tuple chain
      const auto &cn_ta = child;
      auto tup_add  = dfg->create_node(TupAdd_Op);
      ta_map[i-1]   = tup_add;
      auto tn_spin  = tup_add.setup_sink_pin("TN");
      auto tup_name = ta_name[i-1];
      auto tn_dpin  = setup_tuple_ref(dfg, tup_name, true);
      dfg->add_edge(tn_dpin, tn_spin);

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name); // tuple ref semantically move to here
      setup_dpin_ssa(name2dpin[tup_name], lnast->get_vname(cn_ta), lnast->get_subs(cn_ta));

      // take the new tuple-chain as the original tuple-chain value-dpin -> hierarchical tuple now!
      auto tup_add_parent = ta_map[i-2];
      auto value_spin_parent = tup_add_parent.setup_sink_pin("KV");
      auto value_dpin_parent = tup_add.setup_driver_pin();
      dfg->add_edge(value_dpin_parent, value_spin_parent);

      // setup key for the new tuple chain head
      auto kn_spin = tup_add.setup_sink_pin("KN"); //key name
      auto kp_spin = tup_add.setup_sink_pin("KP"); //key name
      auto key_name = lnast->get_sname(cn_ta);
      Node_pin kn_dpin;
      if (is_const(key_name)) { // it is a key_pos, not a key_name
        auto kp_dpin = dfg->create_node_const(Lconst(key_name)).setup_driver_pin();
        dfg->add_edge(kp_dpin, kp_spin);
      } else if (key_name.substr(0,4) != "null") {// it is a pure key_name
        kn_dpin = setup_key_dpin(dfg, key_name);
        dfg->add_edge(kn_dpin, kn_spin);
      }
      ta_name[i] = lnast->get_sname(cn_ta);
      i++ ;
    }
  }
}



//either tuple root or tuple key(str) fit in this case
Node_pin Lnast_dfg::setup_tuple_ref(LGraph *dfg, std::string_view ref_name, bool for_tuple_add) {
  auto it = name2dpin.find(ref_name);

  if (it != name2dpin.end())
    return it->second;

  auto dpin = dfg->create_node(TupRef_Op).setup_driver_pin();
  dpin.set_name(ref_name);
  name2dpin[ref_name] = dpin;

  return dpin;
}

Node_pin Lnast_dfg::setup_key_dpin(LGraph *dfg, std::string_view key_name) {
  // FIXME->sh: create new table:key2dpin 2020/8/1
  auto it = key2dpin.find(key_name);
  if (it != key2dpin.end()) {
    return it->second;
  }

  auto dpin = dfg->create_node(TupKey_Op).setup_driver_pin();
  dpin.set_name(key_name);
  key2dpin[key_name] = dpin;

  return dpin;
}

bool Lnast_dfg::check_new_var_chain(const Lnast_nid &lnidx_opr) {
  std::string_view vname_1st_child;
  bool one_of_rhs_is_lhs = false;

  for (const auto &itr_ch : lnast->children(lnidx_opr)) {
    if (itr_ch == lnast->get_first_child(lnidx_opr)) {
      vname_1st_child = lnast->get_vname(itr_ch);
      if (vname_1st_child.substr(0,3) == "___")
        return false;
      if (vname2attr_dpin.find(vname_1st_child) == vname2attr_dpin.end())
        return false; // this variable never been assigned with an attribute

      continue;
    }

    if (lnast->get_vname(itr_ch) == vname_1st_child)
      one_of_rhs_is_lhs = true;
  }
  return !one_of_rhs_is_lhs;
}


// for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node Lnast_dfg::setup_node_opr_and_lhs(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  auto lg_ntype_op = decode_lnast_op(lnidx_opr);
  auto lg_opr_node = dfg->create_node(lg_ntype_op);
  bool is_new_var_chain = check_new_var_chain(lnidx_opr);

  //when #reg_0 at lhs, the register has not been created before, create it
  Node_pin reg_data_pin;
  Node_pin reg_qpin;
  if (is_register(lhs_vname)) {
    if (lnast->get_data(lhs).subs == 0)
      reg_qpin = setup_ref_node_dpin(dfg, lhs);
    else
      reg_qpin = name2dpin[lhs_vname];

    reg_data_pin = reg_qpin.get_node().setup_sink_pin("D");
  }


  if (!is_new_var_chain) {
    name2dpin[lhs_name] = lg_opr_node.setup_driver_pin(0);
    lg_opr_node.get_driver_pin(0).set_name(lhs_name);
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));

    if (is_register(lhs_name))
      dfg->add_edge(name2dpin[lhs_name], reg_data_pin);

    return lg_opr_node;
  }

  if (is_new_var_chain && vname2attr_dpin.find(lhs_vname) != vname2attr_dpin.end()) {
    auto aset_node = dfg->create_node(AttrSet_Op);
    auto aset_aci_spin = aset_node.setup_sink_pin("ACI");
    auto aset_ancestor_dpin = vname2attr_dpin[lhs_vname];
    dfg->add_edge(aset_ancestor_dpin, aset_aci_spin);

    auto aset_vn_spin = aset_node.setup_sink_pin("VN");
    auto aset_vn_dpin = lg_opr_node.get_driver_pin(0);
    dfg->add_edge(aset_vn_dpin, aset_vn_spin);

    name2dpin[lhs_name] = aset_node.setup_driver_pin(0); // dummy_attr_set node now represent the latest variable
    lg_opr_node.get_driver_pin(0).set_name(lhs_name);
    aset_node.get_driver_pin(0).set_name(lhs_name);
    //aset_node.get_driver_pin(1).set_name(lhs_name); // for debug purpose
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
    vname2attr_dpin[lhs_vname] = aset_node.setup_driver_pin(1);
    if (is_register(lhs_name))
      dfg->add_edge(name2dpin[lhs_name], reg_data_pin);
  }
  return lg_opr_node;
}


Node_pin Lnast_dfg::setup_tuple_assignment(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto tup_name  = lnast->get_sname(lhs);
  auto tup_vname = lnast->get_vname(lhs);

  auto tup_add =  dfg->create_node(TupAdd_Op);

  name2dpin[tup_name] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name(tup_name);
  setup_dpin_ssa(name2dpin[tup_name], tup_vname, lnast->get_subs(lhs));

  return tup_add.setup_sink_pin(0);

}

Node_pin Lnast_dfg::setup_node_assign_and_lhs(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  //handle register as lhs
  if (is_register(lhs_name)) {
    //when #reg_0 at lhs, the register has not been created before
    if (lhs_name.substr(lhs_name.size()-2) == "_0") {
      auto reg_qpin = setup_ref_node_dpin(dfg, lhs);
      auto reg_data_pin = reg_qpin.get_node().setup_sink_pin("D");

      auto assign_node = dfg->create_node(Or_Op);
      //create an extra-Or_Op for #reg_0, return #reg_0 sink pin for rhs connection
      name2dpin[lhs_name] = assign_node.setup_driver_pin(0); //or as assign
      name2dpin[lhs_name].set_name(lhs_name);
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));


      //connect #reg_0 dpin -> #reg.data_pin
      dfg->add_edge(name2dpin[lhs_name], reg_data_pin);

      return assign_node.setup_sink_pin(0);
    } else {
      // (1) create Or_Op to represent #reg_N
      auto assign_node =  dfg->create_node(Or_Op);
      name2dpin[lhs_name] = assign_node.setup_driver_pin(0); //or as assign
      name2dpin[lhs_name].set_name(lhs_name);
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));

      // (2) find the corresponding #reg by its qpin_name, #reg
      I(name2dpin[lhs_vname] != Node_pin());
      auto reg_qpin = name2dpin[lhs_vname];

      // (3) remove the previous D-pin edge from the #reg
      auto reg_node = reg_qpin.get_node();
      I(reg_node.get_type().op == SFlop_Op);
      I(reg_node.setup_sink_pin("D").inp_edges().size() <= 1);
      if (reg_node.setup_sink_pin("D").inp_edges().size() == 1) {
        reg_node.setup_sink_pin("D").inp_edges().begin()->del_edge();
      }

      // (4) actively connect the new created #reg_N to the #reg D-pin
      auto dpin = assign_node.setup_driver_pin(0); //or as assign
      auto spin = reg_node.setup_sink_pin("D");
      dfg->add_edge(dpin, spin);

      // (5) return the spin of Or_Op node to be drived by rhs of assign node in lnast
      return assign_node.setup_sink_pin(0);
    }
  }

  // create "or as assign" for lhs of assign
  auto assign_node =  dfg->create_node(Or_Op);

  bool is_new_var_chain = check_new_var_chain(lnidx_opr);
  /* fmt::print("is_new_var_chain:{}\n", is_new_var_chain); */

  if (!is_new_var_chain) {
    name2dpin[lhs_name] = assign_node.setup_driver_pin(0); //or as assign
    name2dpin[lhs_name].set_name(lhs_name);
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
    return assign_node.setup_sink_pin(0);
  }

  if (is_new_var_chain && vname2attr_dpin.find(lhs_vname) != vname2attr_dpin.end()) {
    auto aset_node = dfg->create_node(AttrSet_Op);
    auto aset_aci_spin = aset_node.setup_sink_pin("ACI");
    auto aset_ancestor_dpin = vname2attr_dpin[lhs_vname];
    dfg->add_edge(aset_ancestor_dpin, aset_aci_spin);

    auto aset_vn_spin = aset_node.setup_sink_pin("VN");
    auto aset_vn_dpin = assign_node.get_driver_pin(0);
    dfg->add_edge(aset_vn_dpin, aset_vn_spin);

    name2dpin[lhs_name] = aset_node.setup_driver_pin(0); // dummy_attr_set node now represent the latest variable
    assign_node.get_driver_pin(0).set_name("");
    aset_node.get_driver_pin(0).set_name(lhs_name);
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));

    vname2attr_dpin[lhs_vname] = aset_node.setup_driver_pin(1);
  }

  return assign_node.setup_sink_pin(0);

}


// for both target and operands, except the new io, reg, and const, the node and its dpin
// should already be in the table as the operand comes from existing operator output
Node_pin Lnast_dfg::setup_ref_node_dpin(LGraph *dfg, const Lnast_nid &lnidx_opd, 
                                                     bool from_phi,     bool from_concat, 
                                                     bool from_tupstrc, bool from_assign) {
  auto name  = lnast->get_sname(lnidx_opd); //name = ssa_name
  auto vname = lnast->get_vname(lnidx_opd);
  auto subs  = lnast->get_subs(lnidx_opd);
  I(!name.empty());

  // special case for register, when #x_-1 in rhs, return the reg_qpin, wname #x. Note this is not true for a phi-node.
  bool reg_existed = name2dpin.find(vname) != name2dpin.end();
  if (is_register(name) && !from_phi && reg_existed) {
    if (subs == -1) {
      return name2dpin.find(vname)->second;
    } else if (subs != 0){
      return name2dpin.find(name)->second;
    } else if (subs == 0 && name2dpin.find(name) != name2dpin.end()){
      return name2dpin.find(name)->second;
    } else {
      ; //the subs == 0 and this must be in lhs case, handle later in this function
    }
  }


  const auto it = name2dpin.find(name);
  if (it != name2dpin.end()) {
    auto node = it->second.get_node();
    auto op = it->second.get_node().get_type().op;

    // it's a scalar variable, just return the node pin
    if (op != TupAdd_Op)
      return it->second;

    if (op == TupAdd_Op && from_concat)
      return it->second;

    if (op == TupAdd_Op && from_tupstrc)
      return it->second;

    // when it's a tuple chain with multiple field -> not a scalar -> return the tuple chain for it
    if (op == TupAdd_Op && from_assign) {
      if (node.setup_sink_pin(0).get_driver_node().get_type_op() == TupAdd_Op)
        return it->second;
    }

    // return a connected TupGet if the ref node is a TupAdd but also a tuple-chain of a scalar
    auto tup_get = dfg->create_node(TupGet_Op);
    auto tn_spin = tup_get.setup_sink_pin("TN"); // tuple name
    auto kp_spin = tup_get.setup_sink_pin("KP"); // key pos

    auto tn_dpin = it->second;
    auto kp_dpin = dfg->create_node_const(Lconst(0)).setup_driver_pin(); //must be pos 0 as the case is "bar = a + 1", implicitly get a.0
    dfg->add_edge(tn_dpin, tn_spin);
    dfg->add_edge(kp_dpin, kp_spin);

    return tup_get.setup_driver_pin();
  }

  Node_pin node_dpin;
  if (is_output(name)) {
    ;
  } else if (is_input(name)) {
    node_dpin = dfg->add_graph_input(name.substr(1, name.size()-3), Port_invalid, 0);
  } else if (is_const(name)) {
    node_dpin = dfg->create_node_const(Lconst(name)).setup_driver_pin();
  } else if (is_register(name)) {
    auto reg_node = dfg->create_node(SFlop_Op);
    node_dpin = reg_node.setup_driver_pin();
    setup_dpin_ssa(node_dpin, vname, -1);
    node_dpin.set_name(vname); //record #reg instead of #reg_0
    name2dpin[vname] = node_dpin;

    setup_clk(dfg, reg_node);

    return node_dpin;
  } else if (is_err_var_undefined(name)) {
    node_dpin = dfg->create_node(CompileErr_Op).setup_driver_pin();
  } else if (is_bool_true(name)) {
    node_dpin = dfg->create_node_const(Lconst(1)).setup_driver_pin();
  } else if (is_bool_false(name)) {
    node_dpin = dfg->create_node_const(Lconst(0)).setup_driver_pin();
  } else {
    return node_dpin; //return empty node_pin and trigger compile error
  }


  name2dpin[name] = node_dpin;  // for io and reg, the %$# identifier are still used in symbol table
  return node_dpin;
}

Node_Type_Op Lnast_dfg::decode_lnast_op(const Lnast_nid &lnidx_opr) {
  const auto raw_ntype = lnast->get_data(lnidx_opr).type.get_raw_ntype();
  return primitive_type_lnast2lg[raw_ntype];
}



void Lnast_dfg::process_ast_attr_set_op (LGraph *dfg, const Lnast_nid &lnidx_aset) {

  auto c0_aset      = lnast->get_first_child(lnidx_aset);
  auto c1_aset      = lnast->get_sibling_next(c0_aset);
  auto c2_aset      = lnast->get_sibling_next(c1_aset);


  auto c0_aset_name = lnast->get_sname(c0_aset);  // ssa name
  /* auto c1_aset_vname = lnast->get_vname(c1_aset); // no-ssa name */
  auto attr_vname   = lnast->get_vname(c1_aset);  // no-ssa name
  auto vname        = lnast->get_vname(c0_aset);  // no-ssa name

  auto aset_node = dfg->create_node(AttrSet_Op);
  auto vn_spin   = aset_node.setup_sink_pin("VN"); // variable name
  auto an_spin   = aset_node.setup_sink_pin("AN"); // attribute name
  auto av_spin   = aset_node.setup_sink_pin("AV"); // attribute value

  auto aset_ancestor_subs  = lnast->get_data(c0_aset).subs - 1;
  auto aset_ancestor_name = std::string(vname) + "_" + std::to_string(aset_ancestor_subs);
#ifndef NDEBUG
  /* fmt::print("aset ancestor name:{}\n", aset_ancestor_name); */
#endif

  Node_pin vn_dpin;

  /* bool is_reg_or_inp = is_register(c0_aset_name) || is_input(c0_aset_name); */
  /* if (aset_ancestor_subs == -1 && is_reg_or_inp ) { //create corresponding reg and input first */
  /*   vn_dpin = setup_ref_node_dpin(dfg, c0_aset); */
  /*   dfg->add_edge(vn_dpin, vn_spin); */
  /* } else if (name2dpin.find(aset_ancestor_name) != name2dpin.end()) { */
  /*   vn_dpin = name2dpin[aset_ancestor_name]; */
  /*   dfg->add_edge(vn_dpin, vn_spin); */
  /* } */

  if (is_register(c0_aset_name)) {
    bool reg_existed = name2dpin.find(vname) != name2dpin.end();
    if (!reg_existed) {
      vn_dpin = setup_ref_node_dpin(dfg, c0_aset);
      dfg->add_edge(vn_dpin, vn_spin);
    } else {
      vn_dpin = name2dpin[vname];
      dfg->add_edge(vn_dpin, vn_spin);
    }
  } else if (is_input(c0_aset_name)) {
    vn_dpin = setup_ref_node_dpin(dfg, c0_aset);
    dfg->add_edge(vn_dpin, vn_spin);
  } else if (name2dpin.find(aset_ancestor_name) != name2dpin.end()) {
    vn_dpin = name2dpin[aset_ancestor_name];
    dfg->add_edge(vn_dpin, vn_spin);
  }



  
  auto an_dpin = setup_key_dpin(dfg, attr_vname);
  dfg->add_edge(an_dpin, an_spin);

  auto av_dpin = setup_ref_node_dpin(dfg, c2_aset);
  dfg->add_edge(av_dpin, av_spin);

  aset_node.setup_driver_pin(0).set_name(c0_aset_name);
  aset_node.setup_driver_pin(1).set_name(c0_aset_name); // just for debug purpose
  name2dpin[c0_aset_name] = aset_node.get_driver_pin(0);
  vname2attr_dpin[vname] = aset_node.get_driver_pin(1);
}

void Lnast_dfg::process_ast_attr_get_op(LGraph *dfg, const Lnast_nid &lnidx_aget) {
  auto c0_aget = lnast->get_first_child(lnidx_aget);
  auto c1_aget = lnast->get_sibling_next(c0_aget);
  auto c2_aget = lnast->get_sibling_next(c1_aget);
  auto c0_aget_name  = lnast->get_sname(c0_aget);
  auto c0_aget_vname = lnast->get_vname(c0_aget);
  auto c1_aget_name  = lnast->get_sname(c1_aget);
  auto driver_vname  = lnast->get_vname(c1_aget);
  auto attr_name     = lnast->get_vname(c2_aget);

  if (attr_name == "__last_value") {
    auto wire_node = dfg->create_node(Or_Op); // might need to change to other type according to the real driver
    wire_node.get_driver_pin(0).set_name(c0_aget_name);
    name2dpin[c0_aget_name] = wire_node.setup_driver_pin(0);
    setup_dpin_ssa(name2dpin[c0_aget_name], c0_aget_vname, lnast->get_subs(c0_aget));
    driver_var2wire_nodes[driver_vname].push_back(wire_node);
    return;
  }

  auto aget_node = dfg->create_node(AttrGet_Op);
  auto vn_spin   = aget_node.setup_sink_pin("VN"); // variable name
  auto an_spin   = aget_node.setup_sink_pin("AN"); // attribute name


  /* I(name2dpin.find(c1_aget_name) != name2dpin.end()); */
  auto vn_dpin = setup_ref_node_dpin(dfg, c1_aget);
  /* auto vn_dpin = name2dpin[c1_aget_name]; */
  dfg->add_edge(vn_dpin, vn_spin);

  auto an_dpin = setup_key_dpin(dfg, attr_name);
  dfg->add_edge(an_dpin, an_spin);

  aget_node.setup_driver_pin().set_name(c0_aget_name);
  name2dpin[c0_aget_name] = aget_node.get_driver_pin();
  setup_dpin_ssa(name2dpin[c0_aget_name], c0_aget_vname, lnast->get_subs(c0_aget));
}


bool Lnast_dfg::subgraph_outp_is_tuple(Sub_node* sub) {
  uint16_t outp_cnt = 0;
  for (const auto &io_pin : sub->get_io_pins()) { 
    if (io_pin.is_output()) 
      outp_cnt ++;

    if (outp_cnt > 1) 
      return true;
  }
  return false;
}

void Lnast_dfg::subgraph_io_connection(LGraph *dfg, Sub_node* sub, std::string_view arg_tup_name, std::string_view res_name, Node subg_node) {
  bool subg_outp_is_scalar = !subgraph_outp_is_tuple(sub);

  // start query subgraph io and construct TGs for connecting inputs, TAs/scalar for connecting outputs
  for (const auto &io_pin : sub->get_io_pins()) {
    Port_ID pos_pid = sub->get_graph_pos(io_pin.name);
    /* fmt::print("io_name:{}, pos_pid:{}\n", io_pin.name, pos_pid); */
    if (io_pin.is_input()) {
      auto tup_get = dfg->create_node(TupGet_Op);
      auto tn_spin = tup_get.setup_sink_pin("TN"); // tuple name
      auto kn_spin = tup_get.setup_sink_pin("KN"); // key name

      auto tn_dpin = setup_tuple_ref(dfg, arg_tup_name);
      tn_dpin.connect_sink(tn_spin);

      auto io_name_ssa = absl::StrCat(io_pin.name, "_0");
      auto kn_dpin = setup_key_dpin(dfg, io_name_ssa);
      kn_dpin.connect_sink(kn_spin);

      auto subg_spin = subg_node.setup_sink_pin(io_pin.name);
      tup_get.setup_driver_pin().connect_sink(subg_spin);

    } else if (subg_outp_is_scalar) {
      auto io_name_ssa = absl::StrCat(io_pin.name, "_0");
      auto subg_dpin = subg_node.setup_driver_pin(io_pin.name);
      auto scalar_node = dfg->create_node(Or_Op);
      auto scalar_dpin = scalar_node.setup_driver_pin(0);
      subg_dpin.connect_sink(scalar_node.setup_sink_pin(0));

      name2dpin[res_name] = scalar_dpin;  
      scalar_dpin.set_name(res_name);
      auto pos = res_name.find_last_of('_');
      auto res_vname = res_name.substr(0,pos);
      auto res_sub   = std::stoi(std::string(res_name.substr(pos+1)));
      setup_dpin_ssa(scalar_dpin, res_vname, 0);

      // note: the function call scalar return must be a "new_var_chain"
      if (vname2attr_dpin.find(res_vname) != vname2attr_dpin.end()) {
        auto aset_node = dfg->create_node(AttrSet_Op);
        auto aset_aci_spin = aset_node.setup_sink_pin("ACI");
        auto aset_ancestor_dpin = vname2attr_dpin[res_vname];
        dfg->add_edge(aset_ancestor_dpin, aset_aci_spin);

        auto aset_vn_spin = aset_node.setup_sink_pin("VN");
        auto aset_vn_dpin = scalar_dpin;
        dfg->add_edge(aset_vn_dpin, aset_vn_spin);

        name2dpin[res_name] = aset_node.setup_driver_pin(0); // dummy_attr_set node now represent the latest variable
        aset_node.get_driver_pin(0).set_name(res_name);
        setup_dpin_ssa(name2dpin[res_name], res_vname, res_sub);
        vname2attr_dpin[res_vname] = aset_node.setup_driver_pin(1);
      }

    } else {
      auto tup_add    = dfg->create_node(TupAdd_Op);
      auto tn_spin    = tup_add.setup_sink_pin("TN"); //tuple name
      auto kn_spin    = tup_add.setup_sink_pin("KN"); //key name
      auto value_spin = tup_add.setup_sink_pin("KV"); //value

      auto tn_dpin = setup_tuple_ref(dfg, res_name, true);
      tn_dpin.connect_sink(tn_spin);

      auto io_name_ssa = absl::StrCat(io_pin.name, "_0");
      auto kn_dpin = setup_key_dpin(dfg, io_name_ssa);
      kn_dpin.connect_sink(kn_spin);

      auto subg_dpin = subg_node.setup_driver_pin(io_pin.name);
      subg_dpin.connect_sink(value_spin);
      
      auto ta_dpin = tup_add.setup_driver_pin();
      name2dpin[res_name] = ta_dpin;
      ta_dpin.set_name(res_name);
    }
  }
}

void Lnast_dfg::process_ast_func_call_op(LGraph *dfg, const Lnast_nid &lnidx_fc) {
  auto c0_fc = lnast->get_first_child(lnidx_fc);
  auto res_name  = lnast->get_sname(c0_fc);
  auto func_name     = lnast->get_vname(lnast->get_sibling_next(c0_fc));
  auto arg_tup_name  = lnast->get_sname(lnast->get_last_child(lnidx_fc));

  auto *library = Graph_library::instance(path);
  if (name2dpin.find(func_name) == name2dpin.end()) {
    fmt::print("function {} defined in separated prp file, query lgdb\n", func_name);
    /* auto path = dfg->get_path(); */
    Lg_type_id lgid;
    if (library->has_name(func_name)) 
      lgid = library->get_lgid(func_name);

    auto *sub = library->ref_sub(lgid);
    auto subg_node = dfg->create_node_sub(lgid);

    subgraph_io_connection(dfg, sub, arg_tup_name, res_name, subg_node);
    return;
  } 

  fmt::print("function {} defined in same prp file, query lgdb\n", func_name);
  auto ta_func_def = name2dpin[func_name].get_node();
  I(ta_func_def.get_type_op() == TupAdd_Op);
  I(ta_func_def.setup_sink_pin("KV").get_driver_node().get_type_op() == Const_Op);
  Lg_type_id lgid = ta_func_def.setup_sink_pin("KV").get_driver_node().get_type_const().to_i();

  auto *sub = library->ref_sub(lgid);
  auto subg_node = dfg->create_node_sub(lgid);

  subgraph_io_connection(dfg, sub, arg_tup_name, res_name, subg_node);
};

void Lnast_dfg::process_ast_func_def_op (LGraph *dfg, const Lnast_nid &lnidx) {
  auto c0_fdef = lnast->get_first_child(lnidx);
  auto c1_fdef = lnast->get_sibling_next(c0_fdef);
  auto func_stmts = lnast->get_sibling_next(c1_fdef);
  auto func_name = lnast->get_vname(c0_fdef);
  // auto subg_module_name = absl::StrCat(module_name, ".", func_name); FIXME->sh: graphviz compains about "." hierarchy
  auto subg_module_name = absl::StrCat(module_name, ":", func_name);
  Lnast_dfg p(eprp_var, subg_module_name);

  fmt::print("============================= Sub-module: LNAST->LGraph Start ===============================================\n");
  p.do_tolg(lnast, func_stmts);
  fmt::print("============================= Sub-module: LNAST->LGraph End ===============================================\n");

  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto kn_spin    = tup_add.setup_sink_pin("KN"); //key name
  auto value_spin = tup_add.setup_sink_pin("KV"); //value


  auto kn_dpin = setup_key_dpin(dfg, "__function_call");
  kn_dpin.connect_sink(kn_spin);

  auto *library = Graph_library::instance(path);
  Lg_type_id lgid;
  if (library->has_name(subg_module_name)) {
    lgid = library->get_lgid(subg_module_name);
  }

  auto value_dpin = dfg->create_node_const(Lconst(lgid)).setup_driver_pin();
  value_dpin.connect_sink(value_spin);

  name2dpin[func_name] = tup_add.setup_driver_pin(); //note: record only the function_name instead of top.function_name
  tup_add.setup_driver_pin().set_name(func_name);
  /* setup_dpin_ssa(name2dpin[tup_name], lnast->get_vname(c0_ta), lnast->get_subs(c0_ta)); */
};

void Lnast_dfg::process_ast_as_op       (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Lnast_dfg::process_ast_label_op    (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Lnast_dfg::process_ast_uif_op      (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Lnast_dfg::process_ast_sub_op      (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Lnast_dfg::process_ast_for_op      (LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Lnast_dfg::process_ast_while_op    (LGraph *dfg, const Lnast_nid &lnidx) { ; };

void Lnast_dfg::setup_lnast_to_lgraph_primitive_type_mapping() {
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
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_shift_right] = ShiftRight_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_shift_left]  = ShiftLeft_Op;
  // FIXME->sh: to be extended ...
}

void Lnast_dfg::setup_clk(LGraph *dfg, Node &reg_node) {
  Node_pin clk_dpin;
  if (!dfg->is_graph_input("clock")) {
    clk_dpin = dfg->add_graph_input("clock", Port_invalid, 1);
  } else {
    clk_dpin = dfg->get_graph_input("clock");
  }

  auto clk_spin = reg_node.setup_sink_pin("CLK");
  dfg->add_edge (clk_dpin, clk_spin);
}


void Lnast_dfg::setup_dpin_ssa(Node_pin &dpin, std::string_view var_name, uint16_t subs) {
  dpin.ref_ssa()->set_ssa(subs);
  dpin.set_prp_vname(var_name);
}


void Lnast_dfg::setup_lgraph_outputs_and_final_var_name(LGraph *dfg) {
  absl::flat_hash_map<std::string_view, Node_pin> vname2dpin; //Pyrope variable -> dpin with the largest ssa var subscription
  for (auto node: dfg->forward()) {
    auto dpin = node.get_driver_pin(0);
    if (dpin.has_ssa() && dpin.has_prp_vname()) {
      auto vname = dpin.get_prp_vname();
      auto subs  = dpin.ref_ssa()->get_subs();

      if(vname2dpin.find(vname) == vname2dpin.end()) {
        /* fmt::print("add new vname2dpin:{}\n", vname); */
        vname2dpin[vname] = dpin;
        continue;
      }

      if (subs >= vname2dpin[vname].get_ssa().get_subs())
        vname2dpin[vname] = dpin;
    }
  }

  //based on the table, create graph outputs or set the final variable name
  for (auto const&[vname, vname_dpin] : vname2dpin) {
    auto dpin_name = vname_dpin.get_name();
    if (is_output(dpin_name)) {
      auto out_spin = dfg->add_graph_output(dpin_name.substr(1, dpin_name.size() - 3), Port_invalid, 0); // Port_invalid pos means do not care about position
      /* fmt::print("add graph out:{}\n", dpin_name.substr(1, dpin_name.size() - 3));                       // -3 means get rid of %, _0(ssa subscript) */
      dfg->add_edge(vname_dpin, out_spin);
      continue;
    }

    if (driver_var2wire_nodes.find(vname) != driver_var2wire_nodes.end()) {
      auto driver_ntype = vname_dpin.get_node().get_type().op;
      for (auto &it : driver_var2wire_nodes[vname]) {
        if (driver_ntype == TupAdd_Op) {
          it.set_type(TupAdd_Op); // change wire_node type from Or_Op to dummy TupAdd_Op
          auto attr_key_dpin = setup_key_dpin(dfg, "__last_value");
          auto attr_key_spin = it.setup_sink_pin(1);
          dfg->add_edge(attr_key_dpin, attr_key_spin);
          auto wire_spin = it.get_sink_pin(0);
          dfg->add_edge(vname_dpin, wire_spin);
        } else {
          I(it != vname_dpin.get_node());
          auto wire_spin = it.get_sink_pin(0);
          dfg->add_edge(vname_dpin, wire_spin);
        }
      }
      continue;
    }

    /* else { */
    /*   //normal variable, but without lnast index, don't know how to get rid of the subs substr from the dpin name now */
    /*   //one solution is to record string_view of variable in SSA annotation instead of vid, a interger. */
    /*   ; */
    /* } */
  }
}
