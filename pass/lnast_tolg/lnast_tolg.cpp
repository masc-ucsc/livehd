// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_tolg.hpp"
#include "pass.hpp"
#include "cprop.hpp"

Lnast_tolg::Lnast_tolg(std::string_view _module_name, std::string_view _path) : module_name(_module_name), path(_path) {
  setup_lnast_to_lgraph_primitive_type_mapping();
}


std::vector<LGraph *> Lnast_tolg::do_tolg(std::shared_ptr<Lnast> ln, const Lnast_nid &top_stmts) {
    lnast = ln;
    LGraph *lg;
    std::string src{lnast->get_source()};
    if (src.empty())
      src = "-";
    lg = LGraph::create(path, module_name, src);
    name2dpin["$"] = lg->add_graph_input("$", Port_invalid, 0);
    /* name2dpin["$"] = lg->add_graph_input("$", 0, 0); */
    setup_tuple_ref(lg, "%");
    std::vector<LGraph *> lgs;
    top_stmts2lgraph(lg, top_stmts);
    lgs.push_back(lg);

    return lgs;
}


void Lnast_tolg::top_stmts2lgraph(LGraph *lg, const Lnast_nid &lnidx_stmts) {
  fmt::print("======== Phase-1: LNAST->LGraph Start ================================\n");
  process_ast_stmts(lg, lnidx_stmts);

  fmt::print("======== Phase-2: Adding final Module IOs and Final Dpin Name ========\n");
  setup_lgraph_ios_and_final_var_name(lg);

  lg->ref_self_sub_node()->populate_graph_pos();
}



void Lnast_tolg::process_ast_stmts(LGraph *lg, const Lnast_nid &lnidx_stmts) {
  for (const auto &lnidx : lnast->children(lnidx_stmts)) {
    const auto ntype = lnast->get_data(lnidx).type;
    if (ntype.is_assign()) {
      process_ast_assign_op(lg, lnidx);
    } else if (ntype.is_dp_assign()) {
      process_ast_dp_assign_op(lg, lnidx);
    } else if (ntype.is_nary_op() || ntype.is_unary_op()) {
      process_ast_nary_op(lg, lnidx);
    } else if (ntype.is_attr_set()) {
      process_ast_attr_set_op(lg, lnidx);
    } else if (ntype.is_attr_get()) {
      process_ast_attr_get_op(lg, lnidx);
    } else if (ntype.is_tuple_add()) {
      process_ast_tuple_add_op(lg, lnidx);
    } else if (ntype.is_tuple_get()) {
      process_ast_tuple_get_op(lg, lnidx);
    } else if (ntype.is_tuple_phi_add()) {
      process_ast_tuple_phi_add_op(lg, lnidx);
    } else if (ntype.is_logical_op()) {
      process_ast_logical_op(lg, lnidx);
    } else if (ntype.is_as()) {
      process_ast_as_op(lg, lnidx);
    } else if (ntype.is_tuple()) {
      process_ast_tuple_struct(lg, lnidx);
    } else if (ntype.is_tuple_concat()) {
      process_ast_concat_op(lg, lnidx);
    } else if (ntype.is_if()) {
      process_ast_if_op(lg, lnidx);
    } else if (ntype.is_uif()) {
      process_ast_uif_op(lg, lnidx);
    } else if (ntype.is_func_call()) {
      process_ast_func_call_op(lg, lnidx);
    } else if (ntype.is_func_def()) {
      process_ast_func_def_op(lg, lnidx);
    } else if (ntype.is_for()) {
      process_ast_for_op(lg, lnidx);
    } else if (ntype.is_while()) {
      process_ast_while_op(lg, lnidx);
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


void Lnast_tolg::process_ast_if_op(LGraph *lg, const Lnast_nid &lnidx_if) {
  for (const auto& if_child : lnast->children(lnidx_if)) {
    auto ntype = lnast->get_type(if_child);
    if (ntype.is_cstmts() || ntype.is_stmts()) {
      process_ast_stmts(lg, if_child);
    } else if (ntype.is_cond()) {
      continue;
    } else if (ntype.is_phi()) {
      process_ast_phi_op(lg, if_child);
    } else {
      I(false); // if-subtree should only contain cstmts/stmts/cond/phi nodes
    }
  }
}

void Lnast_tolg::process_ast_phi_op(LGraph *lg, const Lnast_nid &lnidx_phi) {
  auto phi_node   = lg->create_node(Ntype_op::Mux);
  auto cond_spin  = phi_node.setup_sink_pin("0"); // Y = ~SA + SB
  auto true_spin  = phi_node.setup_sink_pin("1");
  auto false_spin = phi_node.setup_sink_pin("2");

  auto lhs = lnast->get_first_child(lnidx_phi);
  auto c1 = lnast->get_sibling_next(lhs);
  auto c2 = lnast->get_sibling_next(c1);
  auto c3 = lnast->get_sibling_next(c2);
  auto lhs_name = lnast->get_sname(lhs);
  auto c2_name = lnast->get_sname(c2);
  auto c3_name = lnast->get_sname(c3);

  auto cond_dpin   = setup_ref_node_dpin(lg, c1);
  Node_pin true_dpin;
  Node_pin false_dpin;
  if (c2_name == "register_forwarding_0") {
    // referece sibling's info to get the root reg qpin
    auto reg_name = c3_name.substr();

    auto pos = reg_name.find_last_of('_');
    auto reg_qpin_name = lhs_name.substr(0,pos);

    I(name2dpin.count(reg_qpin_name)!=0);
    auto reg_qpin = name2dpin[reg_qpin_name];
    true_dpin = reg_qpin;
  } else {
    true_dpin = setup_ref_node_dpin(lg, c2, true);
  }

  if (c3_name == "register_forwarding_0") {
    // referece sibling's info to get the root reg qpin
    auto reg_name = c2_name.substr();
    auto pos = reg_name.find_last_of('_');
    auto reg_qpin_name = lhs_name.substr(0,pos);
    I(name2dpin.count(reg_qpin_name)!=0);
    auto reg_qpin = name2dpin[reg_qpin_name];
    false_dpin = reg_qpin;
  } else {
    false_dpin  = setup_ref_node_dpin(lg, c3, true);
  }

  lg->add_edge(cond_dpin,  cond_spin);
  lg->add_edge(true_dpin,  true_spin);
  lg->add_edge(false_dpin, false_spin);

  if (is_register(lhs_name)){
    // (1) find the corresponding #reg and its qpin, wname = #reg
    auto pos = lhs_name.find_last_of('_');
    auto reg_qpin_name = lhs_name.substr(0,pos);
    I(name2dpin.count(reg_qpin_name));
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
    lg->add_edge(dpin, spin);
  }

  name2dpin[lhs_name] = phi_node.setup_driver_pin();
  phi_node.setup_driver_pin().set_name(lhs_name);
  auto lhs_name_no_ssa = lnast->get_vname(lhs);
  setup_dpin_ssa(name2dpin[lhs_name], lhs_name_no_ssa, lnast->get_subs(lhs));
}


void Lnast_tolg::process_ast_concat_op(LGraph *lg, const Lnast_nid &lnidx_concat) {

  auto lhs  = lnast->get_first_child(lnidx_concat); //c0: target tuple name for concat.
  auto opd1 = lnast->get_sibling_next(lhs);         //c1: tuple operand1, either scalar or tuple
  auto opd2 = lnast->get_sibling_next(opd1);        //c2: tuple operand2, either scalar or tuple
  auto lhs_name  = lnast->get_sname(lhs);
  auto opd1_name = lnast->get_sname(opd1);
  auto opd2_name = lnast->get_sname(opd2);
  //lhs = opd1 ++ opd2, both opd1 and opd2 could be either a scalar or a tuple


  //create TupAdd, concat both tail of opd1 and opd2, name it with old opd1_name (a = a ++ b) or new lhs_name (c = a ++ b)
    auto tup_add    = lg->create_node(Ntype_op::TupAdd);
    auto tn_spin    = tup_add.setup_sink_pin("tuple_name"); // tuple name
    auto value_spin = tup_add.setup_sink_pin("value"); // key->value

    auto tn_dpin    = setup_ref_node_dpin(lg, opd1, false, true);
    lg->add_edge(tn_dpin, tn_spin);

    auto value_dpin = setup_ref_node_dpin(lg, opd2, false, true);
    lg->add_edge(value_dpin, value_spin);


  if (lhs_name == opd1_name) {
    name2dpin[opd1_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(opd1_name);
  } else {
    name2dpin[lhs_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(lhs_name);
  }

  setup_dpin_ssa(name2dpin[lhs_name], lnast->get_vname(lhs), lnast->get_subs(lhs));
}



void Lnast_tolg::process_ast_nary_op(LGraph *lg, const Lnast_nid &lnidx_opr) {
  auto opr_node = setup_node_opr_and_lhs(lg, lnidx_opr);

  std::vector<Node_pin> opds;
  Node_pin opd;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue; // the lhs has been handled at setup_node_opr_and_lhs();

    opd = setup_ref_node_dpin(lg, opr_child);
    opds.emplace_back(opd);
  }
  nary_node_rhs_connections(lg, opr_node, opds, lnast->get_type(lnidx_opr).is_minus());
}


void Lnast_tolg::process_ast_logical_op(LGraph *lg, const Lnast_nid &lnidx_opr) {
  // (1) create logical operator node and record the dpin to symbol table
  // (2) create comparator node and compare with 0 for each of the inputs
  // (3) take the result of every comparator as the inputs of logical operator inputs

  auto opr_node = setup_node_opr_and_lhs(lg, lnidx_opr);
  std::vector<Node_pin> eqs_dpins;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue; // the lhs has been handled at setup_node_opr_and_lhs();

    auto node_eq = lg->create_node(Ntype_op::EQ);
    auto ori_opd = setup_ref_node_dpin(lg, opr_child);
    auto zero_dpin = lg->create_node_const(Lconst(0, 1)).setup_driver_pin();

    lg->add_edge(ori_opd, node_eq.setup_sink_pin("A"));
    lg->add_edge(zero_dpin, node_eq.setup_sink_pin("A"));

    eqs_dpins.emplace_back(node_eq.setup_driver_pin());
  }

  nary_node_rhs_connections(lg, opr_node, eqs_dpins, lnast->get_type(lnidx_opr).is_minus());
};



void Lnast_tolg::nary_node_rhs_connections(LGraph *lg, Node &opr_node, const std::vector<Node_pin> &opds, bool is_subt) {
  switch(opr_node.get_type_op()) {
    case Ntype_op::Sum:
    case Ntype_op::Mult: { // FIXME: add could be + a b c (same mult)
      bool is_first = true;
      for (const auto &opd : opds) {
        if (is_subt & !is_first) { //note: Hunter -- for subtraction
          lg->add_edge(opd, opr_node.setup_sink_pin("B")); // HERE Check this
        } else {
          lg->add_edge(opd, opr_node.setup_sink_pin("A"));
          is_first = false;
        }
      }
    }
    break;
    case Ntype_op::LT:
    case Ntype_op::GT: {
      I(opds.size()==2); // FIXME: comparator can have many inputs (a<b<c<d)
      lg->add_edge(opds[0], opr_node.setup_sink_pin("A"));
      lg->add_edge(opds[1], opr_node.setup_sink_pin("B"));  
    }
    break;
    case Ntype_op::Div:
    case Ntype_op::SHL:
    case Ntype_op::SRA: {
      I(opds.size()==2); // val<<amount
      lg->add_edge(opds[0], opr_node.setup_sink_pin("a"));
      lg->add_edge(opds[1], opr_node.setup_sink_pin("b"));
    }
    break;
    default: {
      I(opr_node.get_type_op()!=Ntype_op::Mux);
      I(opr_node.get_type_op()!=Ntype_op::Sflop);
      for (const auto &opd : opds) {
        lg->add_edge(opd, opr_node.setup_sink_pin());
      }
    }
  }
}

Node Lnast_tolg::process_ast_assign_op(LGraph *lg, const Lnast_nid &lnidx_assign) {
  auto c0 = lnast->get_first_child(lnidx_assign);
  auto c1 = lnast->get_sibling_next(c0);

  Node_pin opd1 = setup_ref_node_dpin(lg, c1, false, false, false, true);
  Node     opd1_node = opd1.get_node();

  Node_pin opr_spin;
  if (opd1_node.get_type_op() == Ntype_op::TupAdd) {
    opr_spin  = setup_tuple_assignment(lg, lnidx_assign);
  } else if (is_input(opd1.get_name())) {
    opr_spin  = setup_tuple_assignment(lg, lnidx_assign);
  } else {
    opr_spin  = setup_node_assign_and_lhs(lg, lnidx_assign);
  }

  lg->add_edge(opd1, opr_spin);
  return opr_spin.get_node();
}


void Lnast_tolg::process_ast_dp_assign_op(LGraph *lg, const Lnast_nid &lnidx_dp_assign) {
  auto aset_node = lg->create_node(Ntype_op::AttrSet);
  auto vn_spin   = aset_node.setup_sink_pin("name");     // variable name
  auto af_spin   = aset_node.setup_sink_pin("field");    // attribute field
  auto av_spin   = aset_node.setup_sink_pin("value");    // attribute value

  auto c0_dp       = lnast->get_first_child(lnidx_dp_assign);
  auto c1_dp       = lnast->get_sibling_next(c0_dp);
  auto c0_dp_name  = lnast->get_sname(c0_dp);  // ssa name
  auto attr_vname  = "__dp_assign";            // no-ssa name
  auto c0_dp_vname = lnast->get_vname(c0_dp);  // no-ssa name

  auto vn_dpin = setup_ref_node_dpin(lg, c1_dp);
  lg->add_edge(vn_dpin, vn_spin);
  auto an_dpin = setup_field_dpin(lg, attr_vname);
  lg->add_edge(an_dpin, af_spin);


  auto dp_ancestor_subs  = lnast->get_data(c0_dp).subs - 1;
  auto dp_ancestor_name = std::string(c0_dp_vname) + "_" + std::to_string(dp_ancestor_subs);
  /* fmt::print("aset ancestor name:{}\n", dp_ancestor_name); */
  I(name2dpin.find(dp_ancestor_name) != name2dpin.end());

  auto av_dpin = name2dpin[dp_ancestor_name];
  lg->add_edge(av_dpin, av_spin);

  aset_node.setup_driver_pin("Y").set_name(c0_dp_name); //check
  name2dpin[c0_dp_name] = aset_node.get_driver_pin("Y");
  setup_dpin_ssa(name2dpin[c0_dp_name], c0_dp_vname, lnast->get_subs(c0_dp));

  if (is_register(c0_dp_name)) {
    auto reg_node = name2dpin[c0_dp_vname].get_node();
    I(reg_node.get_type_op() == Ntype_op::Sflop);
    I(reg_node.setup_sink_pin("din").inp_edges().size() <= 1);
    if (reg_node.setup_sink_pin("din").inp_edges().size() == 1) {
      reg_node.setup_sink_pin("din").inp_edges().begin()->del_edge();
    }
    auto dpin = aset_node.setup_driver_pin("Y");
    auto spin = reg_node.setup_sink_pin("din");
    lg->add_edge(dpin, spin);
  }
}



void Lnast_tolg::process_ast_tuple_struct(LGraph *lg, const Lnast_nid &lnidx_tup) {
  std::string tup_name;
  std::string_view tup_vname;
  int8_t subs;
  uint16_t fp = 0; //field position

  // note: each new tuple element will be the new tuple chain tail and inherit the tuple name
  for (const auto &tup_child : lnast->children(lnidx_tup)) {
    if (tup_child == lnast->get_first_child(lnidx_tup)) {
      tup_name = lnast->get_sname(tup_child);
      tup_vname = lnast->get_vname(tup_child);
      subs = lnast->get_subs(tup_child);
      setup_tuple_ref(lg, tup_name);
      continue;
    }

    // the case with key name well-defined
    if (lnast->get_type(tup_child).is_assign()) {
      auto c0       = lnast->get_first_child(tup_child);
      auto c1       = lnast->get_sibling_next(c0);
      auto field_name = lnast->get_sname(c0);

      auto tn_dpin    = setup_tuple_ref(lg, tup_name);
      auto fp_dnode   = lg->create_node_const(Lconst(fp));
      auto field_pos_dpin    = fp_dnode.setup_driver_pin();
      auto value_dpin = setup_ref_node_dpin(lg, c1, 0, 0, 1);

      auto tup_add    = lg->create_node(Ntype_op::TupAdd);
      auto tn_spin    = tup_add.setup_sink_pin("tuple_name"); // tuple name
      auto field_pos_spin    = tup_add.setup_sink_pin("position"); // key position is unknown before tuple resolving
      auto value_spin = tup_add.setup_sink_pin("value"); // value

      if (field_name.substr(0,4) != "null") {
        auto field_dpin    = setup_field_dpin(lg, field_name);
        auto field_spin    = tup_add.setup_sink_pin("field"); // key name
        lg->add_edge(field_dpin, field_spin);
      }

      lg->add_edge(tn_dpin, tn_spin);
      lg->add_edge(field_pos_dpin, field_pos_spin);
      lg->add_edge(value_dpin, value_spin);

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name);
      setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);

      fp++;
      continue;
    }

    auto tn_dpin    = setup_tuple_ref(lg, tup_name);
    auto fp_dnode   = lg->create_node_const(Lconst(fp));
    auto field_pos_dpin    = fp_dnode.setup_driver_pin();
    auto value_dpin = setup_ref_node_dpin(lg, tup_child, 0, 0, 1);

    auto tup_add    = lg->create_node(Ntype_op::TupAdd);
    auto tn_spin    = tup_add.setup_sink_pin("tuple_name"); // tuple name
    auto field_pos_spin    = tup_add.setup_sink_pin("position");   // field position is unknown before tuple resolving
    auto value_spin = tup_add.setup_sink_pin("value");      // value

    lg->add_edge(tn_dpin, tn_spin);
    lg->add_edge(field_pos_dpin, field_pos_spin);
    lg->add_edge(value_dpin, value_spin);

    name2dpin[tup_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(tup_name);
    setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);
    fp++;

  }
}


void Lnast_tolg::process_ast_tuple_phi_add_op(LGraph *lg, const Lnast_nid &lnidx_tpa) {
  auto c0_tpa = lnast->get_first_child(lnidx_tpa);
  auto c1_tpa = lnast->get_sibling_next(c0_tpa);
  process_ast_phi_op(lg, c0_tpa);
  process_ast_tuple_add_op(lg, c1_tpa);
}

Node_pin Lnast_tolg::create_inp_tg(LGraph *lg, std::string_view input_field) {
  fmt::print("DBG: input_field:{}\n", input_field);
  auto it = name2dpin.find(input_field);
  if (it != name2dpin.end())
    return it->second;

  auto tup_get_inp = lg->create_node(Ntype_op::TupGet);
  auto tn_spin = tup_get_inp.setup_sink_pin("tuple_name");
  auto tn_dpin = name2dpin["$"];
  tn_dpin.connect_sink(tn_spin);

  auto field_spin = tup_get_inp.setup_sink_pin("field");
  auto field_dpin = setup_field_dpin(lg, input_field.substr(1, input_field.size()-1));
  field_dpin.connect_sink(field_spin);
  auto tg_dpin = tup_get_inp.setup_driver_pin();

#ifndef NDEBUG
  tg_dpin.set_name(input_field);
#endif
  
  name2dpin[input_field] = tg_dpin;
  return tg_dpin;
}


void Lnast_tolg::process_ast_tuple_get_op(LGraph *lg, const Lnast_nid &lnidx_tg) {
  int i = 0;
  absl::flat_hash_map<int, Node> tg_map;
  std::string      c0_tg_name;
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
      auto c1_tg_name  = lnast->get_sname(c1_tg);

      auto tup_get = lg->create_node(Ntype_op::TupGet);
      tg_map.insert_or_assign(i, tup_get);

      auto tn_spin = tup_get.setup_sink_pin("tuple_name");

      Node_pin tn_dpin;
      
      if (is_input(c1_tg_name)) {
        tn_dpin = create_inp_tg(lg, lnast->get_vname(c1_tg));
      } else if (is_register(c1_tg_name)) {
        tn_dpin = setup_ref_node_dpin(lg, c1_tg);
      } else {
        tn_dpin = setup_tuple_ref(lg, c1_tg_name);
      }

      lg->add_edge(tn_dpin, tn_spin);
      i++;
      continue;
    }

    // i >= 2
    if (child == lnast->get_last_child(lnidx_tg)) {
      const auto& cn_tg = child;
      auto cn_tg_name = lnast->get_sname(cn_tg);
      auto tup_get = tg_map[i-1];
      auto field_spin = tup_get.setup_sink_pin("field");
      auto field_pos_spin = tup_get.setup_sink_pin("position");

      if (is_const(cn_tg_name)) {
        auto field_pos_dpin = setup_ref_node_dpin(lg, cn_tg);
        lg->add_edge(field_pos_dpin, field_pos_spin);
      } else {
        auto field_dpin = setup_field_dpin(lg, cn_tg_name);
        lg->add_edge(field_dpin, field_spin);
      }

      if (vname2attr_dpin.find(c0_tg_vname) != vname2attr_dpin.end()) {
        auto aset_node = lg->create_node(Ntype_op::AttrSet);
        auto aset_aci_spin = aset_node.setup_sink_pin("chain");
        auto aset_ancestor_dpin = vname2attr_dpin[c0_tg_vname];
        lg->add_edge(aset_ancestor_dpin, aset_aci_spin);

        auto aset_vn_spin = aset_node.setup_sink_pin("name");
        auto aset_vn_dpin = tup_get.get_driver_pin();
        lg->add_edge(aset_vn_dpin, aset_vn_spin);

        name2dpin[c0_tg_name] = aset_node.setup_driver_pin("Y"); // dummy_attr_set node now represent the latest variable
        aset_node.get_driver_pin("Y").set_name(c0_tg_name);
        setup_dpin_ssa(name2dpin[c0_tg_name], c0_tg_vname, c0_tg_subs);
        vname2attr_dpin[c0_tg_vname] = aset_node.setup_driver_pin("chain");
        return;
      }

      name2dpin[c0_tg_name] = tup_get.setup_driver_pin();
      tup_get.setup_driver_pin().set_name(c0_tg_name);
      setup_dpin_ssa(name2dpin[c0_tg_name], c0_tg_vname, c0_tg_subs);

    } else { //not the last child
      auto new_tup_get = lg->create_node(Ntype_op::TupGet);
      tg_map.insert_or_assign(i, new_tup_get);
      auto tn_spin = new_tup_get.setup_sink_pin("tuple_name");


      const auto& cn_tg = child;
      auto cn_tg_name = lnast->get_sname(cn_tg);
      auto prev_tup_get = tg_map[i-1];
      auto field_spin = prev_tup_get.setup_sink_pin("field");
      auto field_pos_spin = prev_tup_get.setup_sink_pin("position");

      if (is_const(cn_tg_name)) {
        auto field_pos_dpin = setup_ref_node_dpin(lg, cn_tg);
        lg->add_edge(field_pos_dpin, field_pos_spin);
      } else {
        auto field_dpin = setup_field_dpin(lg, cn_tg_name);
        lg->add_edge(field_dpin, field_spin);
      }

      auto tn_dpin = prev_tup_get.setup_driver_pin();
      lg->add_edge(tn_dpin, tn_spin);

      i++;
      continue;
    }
  }
}


/* void Lnast_tolg::create_hier_inp_tup_add(LGraph *lg, const Lnast_nid &c1_tg) { */
/*   auto c2_tg       = lnast->get_sibling_next(c1_tg); */
/*   auto c2_tg_sname = lnast->get_sname(c2_tg); */
/*   auto c2_tg_vname = lnast->get_vname(c2_tg); */
/*   auto tup_name    = lnast->get_sname(c1_tg); */
/*   auto tup_add     = lg->create_node(TupAdd_Op); */

/*   // create new graph input */
/*   auto new_inp_name = absl::StrCat(tup_name.substr(1, tup_name.size()-3), "." ,c2_tg_vname); */
/*   auto inp_dpin     = lg->add_graph_input(new_inp_name, Port_invalid, 0); */
/*   name2dpin[new_inp_name] = inp_dpin; */


/*   // construct the tup_add node */
/*   auto tn_spin = tup_add.setup_sink_pin("TN"); */
/*   auto tn_dpin = setup_tuple_ref(lg, tup_name); */
/*   tn_dpin.connect_sink(tn_spin); */

/*   auto kn_spin = tup_add.setup_sink_pin("KN"); //key name */
/*   auto kp_spin = tup_add.setup_sink_pin("KP"); //key name */
/*   auto field_name = c2_tg_sname; */
/*   Node_pin kn_dpin; */
/*   if (is_const(field_name)) { // it is a key_pos, not a field_name */
/*     auto kp_dpin = lg->create_node_const(Lconst(field_name)).setup_driver_pin(); */
/*     kp_dpin.connect_sink(kp_spin); */
/*   } else if (field_name.substr(0,4) != "null") {// it is a pure field_name */
/*     kn_dpin = setup_field_dpin(lg, field_name); */
/*     kn_dpin.connect_sink(kn_spin); */
/*   } */

/*   auto value_spin = tup_add.setup_sink_pin("KV"); //value */
/*   auto value_dpin = inp_dpin; */
/*   value_dpin.connect_sink(value_spin); */

/*   name2dpin[tup_name] = tup_add.setup_driver_pin(); */
/*   tup_add.setup_driver_pin().set_name(tup_name); // tuple ref semantically move to here */
/*   setup_dpin_ssa(name2dpin[tup_name], tup_name, 0); */
/* } */



void Lnast_tolg::process_ast_tuple_add_op(LGraph *lg, const Lnast_nid &lnidx_ta) {
  int i = 0;
  absl::flat_hash_map<int, Node> ta_map;
  absl::flat_hash_map<int, std::string_view> ta_name;
  for (const auto & child : lnast->children(lnidx_ta)) {
    if (i == 0) {
      const auto &c0_ta = child;
      auto tup_name = lnast->get_sname(c0_ta);
      if (is_input(tup_name)) {
        create_inp_tg(lg, tup_name); 
      } 
      

      auto tup_add  = lg->create_node(Ntype_op::TupAdd);
      auto tn_spin  = tup_add.setup_sink_pin("tuple_name");
      auto tn_dpin  = setup_tuple_ref(lg, tup_name);
      tn_dpin.connect_sink(tn_spin);

      // exclude invalid scalar->tuple cases
      auto field_name = lnast->get_sname(lnast->get_sibling_next(c0_ta)); // peep for field_name ...
      /* bool is_scalar =  tn_ntype != TupAdd_Op && tn_ntype != TupRef_Op; */
      /* if (is_scalar && field_name != "0") */
	    	/* Pass::error("try to modify a non-exist tuple key field:{} in tuple:{}\n", field_name, tup_name); */

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name); // tuple ref semantically move to here
      setup_dpin_ssa(name2dpin[tup_name], lnast->get_vname(c0_ta), lnast->get_subs(c0_ta));

      ta_map.insert_or_assign(i,tup_add);
      ta_name.insert_or_assign(i,tup_name);
      i++ ;

      continue;
    }

    if (i == 1) {
      const auto &c1_ta = child;
      auto tup_add = ta_map[i-1];
      auto field_spin = tup_add.setup_sink_pin("field"); //field name
      auto field_pos_spin = tup_add.setup_sink_pin("position"); //field pos
      auto field_name = lnast->get_sname(c1_ta);
      Node_pin field_dpin;
      if (is_const(field_name)) { // it is a key_pos, not a field_name
        auto field_pos_dpin = lg->create_node_const(Lconst(field_name)).setup_driver_pin();
        lg->add_edge(field_pos_dpin, field_pos_spin);
      } else if (field_name.substr(0,4) != "null") {// it is a pure field_name
        field_dpin = setup_field_dpin(lg, field_name);
        lg->add_edge(field_dpin, field_spin);
      }
      ta_name.insert_or_assign(i, lnast->get_sname(c1_ta));
      i++ ;
      continue;
    }

    // i >= 2
    if (child == lnast->get_last_child(lnidx_ta)) {
      // non-hier tuple case
      const auto & c2_ta = child;
      auto tup_add = ta_map[i-2];
      auto value_spin = tup_add.setup_sink_pin("value"); //value
      auto value_dpin = setup_ref_node_dpin(lg, c2_ta);
      lg->add_edge(value_dpin, value_spin);
      i++ ;
    } else {
      // hier tuple case
      // create a new tuple chain
      const auto &cn_ta = child;
      auto tup_add  = lg->create_node(Ntype_op::TupAdd);
      ta_map.insert_or_assign(i-1,tup_add);
      auto tn_spin  = tup_add.setup_sink_pin("tuple_name");
      auto tup_name = ta_name[i-1];
      auto tn_dpin  = setup_tuple_ref(lg, tup_name);
      lg->add_edge(tn_dpin, tn_spin);

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name); // tuple ref semantically move to here
      setup_dpin_ssa(name2dpin[tup_name], lnast->get_vname(cn_ta), lnast->get_subs(cn_ta));

      // take the new tuple-chain as the original tuple-chain value-dpin -> hierarchical tuple now!
      auto tup_add_parent = ta_map[i-2];
      // note: no need to handle val_dpin for input_tg cases
      if (tup_add_parent.get_type_op() == Ntype_op::TupAdd) {
        auto value_spin_parent = tup_add_parent.setup_sink_pin("value");
        auto value_dpin_parent = tup_add.setup_driver_pin();
        lg->add_edge(value_dpin_parent, value_spin_parent);
      }

      // setup key for the new tuple chain head
      auto field_spin = tup_add.setup_sink_pin("field");    //field name
      auto field_pos_spin = tup_add.setup_sink_pin("position"); //field position
      auto field_name = lnast->get_sname(cn_ta);
      Node_pin field_dpin;
      if (is_const(field_name)) { // it is a key_pos, not a field_name
        auto field_pos_dpin = lg->create_node_const(Lconst(field_name)).setup_driver_pin();
        lg->add_edge(field_pos_dpin, field_pos_spin);
      } else if (field_name.substr(0,4) != "null") {// it is a pure field_name
        field_dpin = setup_field_dpin(lg, field_name);
        lg->add_edge(field_dpin, field_spin);
      }
      ta_name.insert_or_assign(i, lnast->get_sname(cn_ta));
      i++ ;
    }
  }
}



//either tuple root or tuple key(str) fit in this case
Node_pin Lnast_tolg::setup_tuple_ref(LGraph *lg, std::string_view ref_name) {
  auto it = name2dpin.find(ref_name);

  if (it != name2dpin.end())
    return it->second;

  if (is_input(ref_name)) 
    return create_inp_tg(lg, ref_name);

  auto dpin = lg->create_node(Ntype_op::TupRef).setup_driver_pin();
  dpin.set_name(ref_name);
  name2dpin[ref_name] = dpin;
  return dpin;
}

Node_pin Lnast_tolg::setup_field_dpin(LGraph *lg, std::string_view field_name) {
  // FIXME->sh: create new table:field2dpin 2020/8/1
  auto it = field2dpin.find(field_name);
  if (it != field2dpin.end()) {
    return it->second;
  }

  auto dpin = lg->create_node(Ntype_op::TupKey).setup_driver_pin();
  dpin.set_name(field_name);
  field2dpin[field_name] = dpin;

  return dpin;
}

bool Lnast_tolg::check_new_var_chain(const Lnast_nid &lnidx_opr) {
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
Node Lnast_tolg::setup_node_opr_and_lhs(LGraph *lg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  auto lg_ntype_op = decode_lnast_op(lnidx_opr);
  auto lg_opr_node = lg->create_node(lg_ntype_op);
  bool is_new_var_chain = check_new_var_chain(lnidx_opr);

  //when #reg_0 at lhs, the register has not been created before, create it
  Node_pin reg_data_pin;
  Node_pin reg_qpin;
  if (is_register(lhs_vname)) {
    if (lnast->get_data(lhs).subs == 0)
      reg_qpin = setup_ref_node_dpin(lg, lhs);
    else
      reg_qpin = name2dpin[lhs_vname];

    reg_data_pin = reg_qpin.get_node().setup_sink_pin("din");
  }


  if (!is_new_var_chain) {
    name2dpin[lhs_name] = lg_opr_node.setup_driver_pin();
    lg_opr_node.get_driver_pin().set_name(lhs_name);
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));

    if (is_register(lhs_name))
      lg->add_edge(name2dpin[lhs_name], reg_data_pin);

    return lg_opr_node;
  }

  if (is_new_var_chain && vname2attr_dpin.find(lhs_vname) != vname2attr_dpin.end()) {
    auto aset_node = lg->create_node(Ntype_op::AttrSet);
    auto aset_chain_spin = aset_node.setup_sink_pin("chain");
    auto aset_ancestor_dpin = vname2attr_dpin[lhs_vname];
    lg->add_edge(aset_ancestor_dpin, aset_chain_spin);

    auto aset_vn_spin = aset_node.setup_sink_pin("name");
    auto aset_vn_dpin = lg_opr_node.get_driver_pin();
    lg->add_edge(aset_vn_dpin, aset_vn_spin);

    name2dpin[lhs_name] = aset_node.setup_driver_pin("Y"); // dummy_attr_set node now represent the latest variable
    lg_opr_node.get_driver_pin().set_name(lhs_name);
    aset_node.get_driver_pin("Y").set_name(lhs_name);
    //aset_node.get_driver_pin(1).set_name(lhs_name); // for debug purpose
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
    vname2attr_dpin[lhs_vname] = aset_node.setup_driver_pin("chain");
    if (is_register(lhs_name))
      lg->add_edge(name2dpin[lhs_name], reg_data_pin);
  }
  return lg_opr_node;
}


Node_pin Lnast_tolg::setup_tuple_assignment(LGraph *lg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto tup_name  = lnast->get_sname(lhs);
  auto tup_vname = lnast->get_vname(lhs);

  auto tup_add =  lg->create_node(Ntype_op::TupAdd);

  name2dpin[tup_name] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name(tup_name);
  setup_dpin_ssa(name2dpin[tup_name], tup_vname, lnast->get_subs(lhs));

  return tup_add.setup_sink_pin("tuple_name");

}

Node_pin Lnast_tolg::setup_node_assign_and_lhs(LGraph *lg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);
  auto rhs       = lnast->get_sibling_next(lhs);
  auto rhs_vname = lnast->get_vname(rhs);

  //handle register as lhs
  if (is_register(lhs_name)) {
    //when #reg_0 at lhs, the register has not been created before
    if (lhs_name.substr(lhs_name.size()-2) == "_0") {
      auto reg_qpin = setup_ref_node_dpin(lg, lhs);
      auto reg_data_pin = reg_qpin.get_node().setup_sink_pin("D");

      auto assign_node = lg->create_node(Ntype_op::Or);
      //create an extra-Or_Op for #reg_0, return #reg_0 sink pin for rhs connection
      name2dpin[lhs_name] = assign_node.setup_driver_pin(); //or as assign
      name2dpin[lhs_name].set_name(lhs_name);
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));


      //connect #reg_0 dpin -> #reg.data_pin
      lg->add_edge(name2dpin[lhs_name], reg_data_pin);

      return assign_node.setup_sink_pin("A");
    } else {
      // (1) create Or_Op to represent #reg_N
      auto assign_node =  lg->create_node(Ntype_op::Or);
      name2dpin[lhs_name] = assign_node.setup_driver_pin(); //or as assign
      name2dpin[lhs_name].set_name(lhs_name);
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));

      // (2) find the corresponding #reg by its qpin_name, #reg
      I(name2dpin.count(lhs_vname));
      auto reg_qpin = name2dpin[lhs_vname];

      // (3) remove the previous D-pin edge from the #reg
      auto reg_node = reg_qpin.get_node();
      I(reg_node.get_type_op() == Ntype_op::Sflop);
      I(reg_node.setup_sink_pin("din").inp_edges().size() <= 1);
      if (reg_node.setup_sink_pin("din").inp_edges().size() == 1) {
        reg_node.setup_sink_pin("din").inp_edges().begin()->del_edge();
      }

      // (4) actively connect the new created #reg_N to the #reg D-pin
      auto dpin = assign_node.setup_driver_pin(); //or as assign
      auto spin = reg_node.setup_sink_pin("din");
      lg->add_edge(dpin, spin);

      // (5) return the spin of Or_Op node to be drived by rhs of assign node in lnast
      return assign_node.setup_sink_pin("A");
    }
  }

  auto assign_node = lg->create_node(Ntype_op::Or);

  bool is_new_var_chain = check_new_var_chain(lnidx_opr);
  /* fmt::print("is_new_var_chain:{}\n", is_new_var_chain); */

  if (!is_new_var_chain) {
    name2dpin[lhs_name] = assign_node.setup_driver_pin(); //or as assign
    name2dpin[lhs_name].set_name(lhs_name);
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
    return assign_node.setup_sink_pin("A");
  }

  if (is_new_var_chain && vname2attr_dpin.find(lhs_vname) != vname2attr_dpin.end()) {
    auto aset_node = lg->create_node(Ntype_op::AttrSet);
    auto aset_chain_spin = aset_node.setup_sink_pin("chain");
    auto aset_ancestor_dpin = vname2attr_dpin[lhs_vname];
    lg->add_edge(aset_ancestor_dpin, aset_chain_spin);

    auto aset_vn_spin = aset_node.setup_sink_pin("name");
    auto aset_vn_dpin = assign_node.get_driver_pin("Y");
    lg->add_edge(aset_vn_dpin, aset_vn_spin);

    name2dpin[lhs_name] = aset_node.setup_driver_pin("Y"); // dummy_attr_set node now represent the latest variable
    aset_node.get_driver_pin("Y").set_name(lhs_name);
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));

    vname2attr_dpin[lhs_vname] = aset_node.setup_driver_pin("chain");
  }
  return assign_node.setup_sink_pin("A");
}


// for both target and operands, except the new io, reg, and const, the node and its dpin
// should already be in the table as the operand comes from existing operator output
Node_pin Lnast_tolg::setup_ref_node_dpin(LGraph *lg, const Lnast_nid &lnidx_opd,
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
    auto op = it->second.get_node().get_type_op();

    // it's a scalar variable, just return the node pin
    if (op != Ntype_op::TupAdd)
      return it->second;

    if (op == Ntype_op::TupAdd && from_concat)
      return it->second;

    if (op == Ntype_op::TupAdd && from_tupstrc)
      return it->second;

    // when it's a tuple chain with multiple field -> not a scalar -> return the tuple chain for it
    if (op == Ntype_op::TupAdd && from_assign) {
      auto parent_node = node.setup_sink_pin("tuple_name").get_driver_node();
      auto parent_ntype = parent_node.get_type_op();
      if (parent_ntype == Ntype_op::TupAdd)
        return it->second;

      // case of $input as the assignment rhs
      if (parent_ntype == Ntype_op::TupRef && is_input(parent_node.setup_driver_pin().get_name()))
        return it->second;
    }

    // return a connected TupGet if the ref node is a TupAdd but also a tuple-chain of a scalar
    auto tup_get = lg->create_node(Ntype_op::TupGet);
    auto tn_spin = tup_get.setup_sink_pin("tuple_name"); // tuple name
    auto field_pos_spin = tup_get.setup_sink_pin("position");   // field pos

    auto tn_dpin = it->second;
    auto field_pos_dpin = lg->create_node_const(Lconst(0)).setup_driver_pin(); //must be pos 0 as the case is "bar = a + 1", implicitly get a.0
    lg->add_edge(tn_dpin, tn_spin);
    lg->add_edge(field_pos_dpin, field_pos_spin);

    return tup_get.setup_driver_pin();
  }

  Node_pin node_dpin;
  if (is_output(name)) {
    ;
  } else if (is_input(name)) {
    node_dpin = lg->create_node(Ntype_op::TupRef).setup_driver_pin(); //later the node_type should change to TupGet and connected to $
    node_dpin.set_name(name.substr(0, name.size()-2));
    name2dpin[name] = node_dpin;
    return node_dpin;
  } else if (is_const(name)) {
    node_dpin = lg->create_node_const(Lconst(vname)).setup_driver_pin();
  } else if (is_register(name)) {
    auto reg_node = lg->create_node(Ntype_op::Sflop);
    node_dpin = reg_node.setup_driver_pin();
    setup_dpin_ssa(node_dpin, vname, -1);
    node_dpin.set_name(vname); //record #reg instead of #reg_0
    name2dpin[vname] = node_dpin;
    setup_clk(lg, reg_node);

    return node_dpin;
  } else if (is_err_var_undefined(name)) {
    node_dpin = lg->create_node(Ntype_op::CompileErr).setup_driver_pin();
  } else if (is_bool_true(name)) {
    node_dpin = lg->create_node_const(Lconst(1)).setup_driver_pin();
  } else if (is_bool_false(name)) {
    node_dpin = lg->create_node_const(Lconst(0)).setup_driver_pin();
  } else {
    return node_dpin; //return empty node_pin and trigger compile error
  }


  name2dpin[name] = node_dpin;  // for io and reg, the %$# identifier are still used in symbol table
  return node_dpin;
}

Ntype_op Lnast_tolg::decode_lnast_op(const Lnast_nid &lnidx_opr) {
  const auto raw_ntype = lnast->get_data(lnidx_opr).type.get_raw_ntype();
  return primitive_type_lnast2lg[raw_ntype];
}



void Lnast_tolg::process_ast_attr_set_op (LGraph *lg, const Lnast_nid &lnidx_aset) {

  auto c0_aset      = lnast->get_first_child(lnidx_aset);
  auto c1_aset      = lnast->get_sibling_next(c0_aset);
  auto c2_aset      = lnast->get_sibling_next(c1_aset);

  auto c0_aset_name = lnast->get_sname(c0_aset);  // ssa name
  auto attr_vname   = lnast->get_vname(c1_aset);  // no-ssa name
  auto vname        = lnast->get_vname(c0_aset);  // no-ssa name

  auto aset_node = lg->create_node(Ntype_op::AttrSet);
  auto vn_spin   = aset_node.setup_sink_pin("name");     // variable name
  auto av_spin   = aset_node.setup_sink_pin("value");    // attribute value
  auto af_spin   = aset_node.setup_sink_pin("field");    // attribute field

  auto aset_ancestor_subs  = lnast->get_data(c0_aset).subs - 1;
  auto aset_ancestor_name = std::string(vname) + "_" + std::to_string(aset_ancestor_subs);

  Node_pin vn_dpin;

  if (is_register(c0_aset_name)) {
    bool reg_existed = name2dpin.find(vname) != name2dpin.end();
    if (!reg_existed) {
      vn_dpin = setup_ref_node_dpin(lg, c0_aset);
      lg->add_edge(vn_dpin, vn_spin);
    } else {
      vn_dpin = name2dpin[vname];
      lg->add_edge(vn_dpin, vn_spin);
    }
  } else if (is_input(c0_aset_name)) {
    /* vn_dpin = setup_ref_node_dpin(lg, c0_aset); */
    vn_dpin = setup_tuple_ref(lg, lnast->get_name(c0_aset));
    lg->add_edge(vn_dpin, vn_spin);
  } else if (name2dpin.find(aset_ancestor_name) != name2dpin.end()) {
    vn_dpin = name2dpin[aset_ancestor_name];
    lg->add_edge(vn_dpin, vn_spin);
  }


  auto af_dpin = setup_field_dpin(lg, attr_vname);
  lg->add_edge(af_dpin, af_spin);

  auto av_dpin = setup_ref_node_dpin(lg, c2_aset);
  lg->add_edge(av_dpin, av_spin);

  aset_node.setup_driver_pin("Y").set_name(c0_aset_name);
  aset_node.setup_driver_pin("chain").set_name(c0_aset_name); // just for debug purpose
  name2dpin[c0_aset_name] = aset_node.get_driver_pin("Y");
  vname2attr_dpin[vname] = aset_node.get_driver_pin("chain");
}

void Lnast_tolg::process_ast_attr_get_op(LGraph *lg, const Lnast_nid &lnidx_aget) {
  auto c0_aget = lnast->get_first_child(lnidx_aget);
  auto c1_aget = lnast->get_sibling_next(c0_aget);
  auto c2_aget = lnast->get_sibling_next(c1_aget);
  auto c0_aget_name  = lnast->get_sname(c0_aget);
  auto c0_aget_vname = lnast->get_vname(c0_aget);
  auto c1_aget_name  = lnast->get_sname(c1_aget);
  auto driver_vname  = lnast->get_vname(c1_aget);
  auto attr_field     = lnast->get_vname(c2_aget);

  if (attr_field == "__last_value") {
    auto wire_node = lg->create_node(Ntype_op::Or); // might need to change to other type according to the real driver
    wire_node.get_driver_pin().set_name(c0_aget_name);
    name2dpin[c0_aget_name] = wire_node.setup_driver_pin();
    setup_dpin_ssa(name2dpin[c0_aget_name], c0_aget_vname, lnast->get_subs(c0_aget));
    driver_var2wire_nodes[driver_vname].push_back(wire_node);
    return;
  }

  auto aget_node = lg->create_node(Ntype_op::AttrGet);
  auto vn_spin   = aget_node.setup_sink_pin("name");  // variable name
  auto af_spin   = aget_node.setup_sink_pin("field"); // attribute field


  /* I(name2dpin.find(c1_aget_name) != name2dpin.end()); */
  auto vn_dpin = setup_ref_node_dpin(lg, c1_aget);
  /* auto vn_dpin = name2dpin[c1_aget_name]; */
  lg->add_edge(vn_dpin, vn_spin);

  auto af_dpin = setup_field_dpin(lg, attr_field);
  lg->add_edge(af_dpin, af_spin);

  aget_node.setup_driver_pin().set_name(c0_aget_name);
  name2dpin[c0_aget_name] = aget_node.get_driver_pin();
  setup_dpin_ssa(name2dpin[c0_aget_name], c0_aget_vname, lnast->get_subs(c0_aget));
}


bool Lnast_tolg::subgraph_outp_is_tuple(Sub_node* sub) {
  uint16_t outp_cnt = 0;
  for (const auto &io_pin : sub->get_io_pins()) {
    if (io_pin.is_output())
      outp_cnt ++;

    if (outp_cnt > 1)
      return true;
  }
  return false;
}

std::vector<std::string_view> Lnast_tolg::split_hier_name(std::string_view hier_name) {
  auto start = 0u;
  auto end = hier_name.find('.');
  std::vector<std::string_view> token_vec;
  while (end != std::string_view::npos) {
    std::string_view token = hier_name.substr(start, end - start);
    token_vec.emplace_back(token);
    start = end + 1; //
    end = hier_name.find('.', start);
  }
  std::string_view token = hier_name.substr(start, end - start);
  token_vec.emplace_back(token);
  return token_vec;
}

void Lnast_tolg::subgraph_io_connection(LGraph *lg, Sub_node* sub, std::string_view arg_tup_name, std::string_view res_name, Node subg_node) {
  bool subg_outp_is_scalar = !subgraph_outp_is_tuple(sub);

  // start query subgraph io and construct TGs for connecting inputs, TAs/scalar for connecting outputs
  for (const auto &io_pin : sub->get_io_pins()) {
    // 2020/8/28: support hier-tuple-inpuputs
    if (io_pin.is_input()) {
      std::vector<Node_pin> created_tup_gets;
      auto hier_inp_subnames = split_hier_name(io_pin.name);
      for (const auto& subname : hier_inp_subnames) {
        auto tup_get = lg->create_node(Ntype_op::TupGet);
        auto tn_spin = tup_get.setup_sink_pin("tuple_name"); 
        auto field_spin = tup_get.setup_sink_pin("field"); // key name

        Node_pin tn_dpin;
        if (&subname == &hier_inp_subnames.front()) {
          tn_dpin = setup_tuple_ref(lg, arg_tup_name);
        } else {
          tn_dpin = created_tup_gets.back();
        }

        tn_dpin.connect_sink(tn_spin);
        auto subname_ssa = absl::StrCat(subname, "_0");
        auto field_dpin = setup_field_dpin(lg, subname_ssa);
        field_dpin.connect_sink(field_spin);

        // note: for scalar input, front() == back()
        if (&subname == &hier_inp_subnames.back()) {
          auto subg_spin = subg_node.setup_sink_pin(io_pin.name);
          tup_get.setup_driver_pin().connect_sink(subg_spin);
        }
        created_tup_gets.emplace_back(tup_get.get_driver_pin());
      }
      continue;
    }

    if (subg_outp_is_scalar) {
      auto io_name_ssa = absl::StrCat(io_pin.name, "_0");
      auto subg_dpin = subg_node.setup_driver_pin(io_pin.name);
      auto scalar_node = lg->create_node(Ntype_op::Or);
      auto scalar_dpin = scalar_node.setup_driver_pin();
      subg_dpin.connect_sink(scalar_node.setup_sink_pin("A"));

      name2dpin[res_name] = scalar_dpin;
      scalar_dpin.set_name(res_name);
      auto pos = res_name.find_last_of('_');
      auto res_vname = res_name.substr(0,pos);
      auto res_sub   = std::stoi(std::string(res_name.substr(pos+1)));
      setup_dpin_ssa(scalar_dpin, res_vname, 0);

      // note: the function call scalar return must be a "new_var_chain"
      if (vname2attr_dpin.find(res_vname) != vname2attr_dpin.end()) {
        auto aset_node = lg->create_node(Ntype_op::AttrSet);
        auto aset_chain_spin = aset_node.setup_sink_pin("chain");
        auto aset_ancestor_dpin = vname2attr_dpin[res_vname];
        lg->add_edge(aset_ancestor_dpin, aset_chain_spin);

        auto aset_vn_spin = aset_node.setup_sink_pin("name");
        auto aset_vn_dpin = scalar_dpin;
        lg->add_edge(aset_vn_dpin, aset_vn_spin);

        name2dpin[res_name] = aset_node.setup_driver_pin("Y"); // dummy_attr_set node now represent the latest variable
        aset_node.get_driver_pin("Y").set_name(res_name);
        setup_dpin_ssa(name2dpin[res_name], res_vname, res_sub);
        vname2attr_dpin[res_vname] = aset_node.setup_driver_pin("chain");
      }
      continue;
    }

    // subgraph output
    auto tup_add    = lg->create_node(Ntype_op::TupAdd);
    auto tn_spin    = tup_add.setup_sink_pin("tuple_name"); //tuple name
    auto field_spin    = tup_add.setup_sink_pin("field"); 
    auto value_spin = tup_add.setup_sink_pin("value"); 

    auto tn_dpin = setup_tuple_ref(lg, res_name);
    tn_dpin.connect_sink(tn_spin);

    auto io_name_ssa = absl::StrCat(io_pin.name, "_0");
    auto field_dpin = setup_field_dpin(lg, io_name_ssa);
    field_dpin.connect_sink(field_spin);

    auto subg_dpin = subg_node.setup_driver_pin(io_pin.name);
    subg_dpin.connect_sink(value_spin);

    auto ta_dpin = tup_add.setup_driver_pin();
    name2dpin[res_name] = ta_dpin;
    ta_dpin.set_name(res_name);
  }
}

void Lnast_tolg::process_ast_func_call_op(LGraph *lg, const Lnast_nid &lnidx_fc) {
  auto c0_fc        = lnast->get_first_child(lnidx_fc);
  auto res_name     = lnast->get_sname(c0_fc);
  auto func_name    = lnast->get_vname(lnast->get_sibling_next(c0_fc));
  auto arg_tup_name = lnast->get_sname(lnast->get_last_child(lnidx_fc));

  auto *library = Graph_library::instance(path);
  if (name2dpin.find(func_name) == name2dpin.end()) {
    fmt::print("function {} defined in separated prp file, query lgdb\n", func_name);
    Lg_type_id lgid;
    if (library->has_name(func_name))
      lgid = library->get_lgid(func_name);

    auto subg_node = lg->create_node_sub(lgid);
    auto *sub = library->ref_sub(lgid);

    subg_node.set_name(absl::StrCat(res_name, ":", func_name));
    fmt::print("subg node_name:{}\n", subg_node.get_name());
    subgraph_io_connection(lg, sub, arg_tup_name, res_name, subg_node);
    return;
  }

  fmt::print("function {} defined in same prp file, query lgdb\n", func_name);
  auto ta_func_def = name2dpin[func_name].get_node();
  I(ta_func_def.get_type_op() == Ntype_op::TupAdd);
  I(ta_func_def.setup_sink_pin("KV").get_driver_node().get_type_op() == Ntype_op::Const);
  Lg_type_id lgid = ta_func_def.setup_sink_pin("KV").get_driver_node().get_type_const().to_i();

  auto subg_node = lg->create_node_sub(lgid);
  auto *sub = library->ref_sub(lgid);

  subg_node.set_name(absl::StrCat(res_name, ":", func_name));
  fmt::print("subg node_name:{}\n", subg_node.get_name());
  subgraph_io_connection(lg, sub, arg_tup_name, res_name, subg_node);
};

void Lnast_tolg::process_ast_func_def_op (LGraph *lg, const Lnast_nid &lnidx) {
  auto c0_fdef = lnast->get_first_child(lnidx);
  auto c1_fdef = lnast->get_sibling_next(c0_fdef);
  auto func_stmts = lnast->get_sibling_next(c1_fdef);
  auto func_name = lnast->get_vname(c0_fdef);
  auto subg_module_name = absl::StrCat(module_name, ":", func_name);
  Lnast_tolg p(subg_module_name, path);

  fmt::print("============================= Sub-module: LNAST->LGraph Start ===============================================\n");
  p.do_tolg(lnast, func_stmts);
  fmt::print("============================= Sub-module: LNAST->LGraph End ===============================================\n");

  auto tup_add    = lg->create_node(Ntype_op::TupAdd);
  auto field_spin    = tup_add.setup_sink_pin("field"); //field name
  auto value_spin = tup_add.setup_sink_pin("value"); 


  auto field_dpin = setup_field_dpin(lg, "__function_call");
  field_dpin.connect_sink(field_spin);

  auto *library = Graph_library::instance(path);
  Lg_type_id lgid;
  if (library->has_name(subg_module_name)) {
    lgid = library->get_lgid(subg_module_name);
  }

  auto value_dpin = lg->create_node_const(Lconst(lgid)).setup_driver_pin();
  value_dpin.connect_sink(value_spin);

  name2dpin[func_name] = tup_add.setup_driver_pin(); //note: record only the function_name instead of top.function_name
  tup_add.setup_driver_pin().set_name(func_name);
};

void Lnast_tolg::process_ast_as_op       (LGraph *lg, const Lnast_nid &lnidx) { ; };
void Lnast_tolg::process_ast_uif_op      (LGraph *lg, const Lnast_nid &lnidx) { ; };
void Lnast_tolg::process_ast_for_op      (LGraph *lg, const Lnast_nid &lnidx) { ; };
void Lnast_tolg::process_ast_while_op    (LGraph *lg, const Lnast_nid &lnidx) { ; };

void Lnast_tolg::setup_lnast_to_lgraph_primitive_type_mapping() {
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_invalid]     = Ntype_op::Invalid;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_assign]      = Ntype_op::Or;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_and] = Ntype_op::And;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_or]  = Ntype_op::Or;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_not] = Ntype_op::Not;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_and]         = Ntype_op::And;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_or]          = Ntype_op::Or;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_not]         = Ntype_op::Not;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_xor]         = Ntype_op::Xor;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_plus]        = Ntype_op::Sum;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_minus]       = Ntype_op::Sum;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_mult]        = Ntype_op::Mult;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_div]         = Ntype_op::Div;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_same]        = Ntype_op::EQ;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_lt]          = Ntype_op::LT;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_gt]          = Ntype_op::GT;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_shift_right] = Ntype_op::SRA;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_shift_left]  = Ntype_op::SHL;
  // FIXME->sh: to be extended ...
}

void Lnast_tolg::setup_clk(LGraph *lg, Node &reg_node) {
  Node_pin clk_dpin;
  if (!lg->is_graph_input("clock")) {
    clk_dpin = lg->add_graph_input("clock", Port_invalid, 1);
  } else {
    clk_dpin = lg->get_graph_input("clock");
  }

  auto clk_spin = reg_node.setup_sink_pin("clock");
  lg->add_edge (clk_dpin, clk_spin);
}


void Lnast_tolg::setup_dpin_ssa(Node_pin &dpin, std::string_view var_name, uint16_t subs) {
  dpin.ref_ssa()->set_ssa(subs);
  dpin.set_prp_vname(var_name);
}




void Lnast_tolg::create_out_ta(LGraph *lg, std::string_view field_name, Node_pin &val_dpin) {
  auto tup_add  = lg->create_node(Ntype_op::TupAdd);
  auto tn_spin  = tup_add.setup_sink_pin("tuple_name");
  auto tn_dpin  = setup_tuple_ref(lg, "%"); // might come from TupRef or TupAdd
  tn_dpin.connect_sink(tn_spin);

  auto field_spin  = tup_add.setup_sink_pin("field");
  auto field_dpin  = setup_field_dpin(lg, field_name);
  field_dpin.connect_sink(field_spin);

  auto val_spin = tup_add.setup_sink_pin("value");
  val_dpin.connect_sink(val_spin);


  name2dpin["%"] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name("%"); // tuple ref semantically moves to here
}


void Lnast_tolg::setup_lgraph_ios_and_final_var_name(LGraph *lg) {
  absl::flat_hash_map<std::string_view, Node_pin> vname2dpin; // pyrope variable -> dpin with the largest ssa var subscription
  for (auto node: lg->forward()) {
    auto dpin  = node.get_driver_pin("Y");
    auto ntype = node.get_type_op();

    // connect hier-tuple-inputs and scalar input from unified input $
    if (ntype == Ntype_op::TupRef && is_input(dpin.get_name())) {
      node.set_type(Ntype_op::TupGet); //change node semantic: TupRef -> TupGet
      auto tn_spin = node.setup_sink_pin("tuple_name");
      auto tn_dpin = name2dpin["$"];
      tn_dpin.connect_sink(tn_spin);

      auto field_spin = node.setup_sink_pin("field");
      auto field_dpin  = setup_field_dpin(lg, dpin.get_name().substr(1, dpin.get_name().size()-1));
      field_dpin.connect_sink(field_spin);
      /* inp_artifacts.insert(node); */
      /* auto hier_name_base = std::string(dpin.get_name().substr(1, dpin.get_name().size()-3)); */
      /* for (auto &out : node.out_edges()) { */
      /*   auto spin = out.sink; */
      /*   dfs_try_create_flattened_inp(lg, spin, hier_name_base, inp_artifacts); */
      /* } */
      continue;
    }

    if (ntype == Ntype_op::TupAdd && !node.has_outputs() && is_output(dpin.get_name())) {
      create_out_ta(lg, dpin.get_name(), dpin);
      
      continue;
      // construct hier-tuple-outputs
      // backward Dynamic Programming DFS traversal (memoization)
      /* auto hier_name_base = std::string(dpin.get_name().substr(1, dpin.get_name().size()-3)); */
      /* absl::flat_hash_set<Node::Compact> memo; */
      /* dp_create_hier_outputs(lg, node, hier_name_base, memo); */
    }

    // collect vname table info
    if (dpin.has_ssa() && dpin.has_prp_vname()) {
      auto vname = dpin.get_prp_vname();
      auto subs  = dpin.ref_ssa()->get_subs();

      if(vname2dpin.find(vname) == vname2dpin.end()) {
        /* fmt::print("add new vname2dpin:{}\n", vname); */
        auto [it,inserted] = vname2dpin.insert({vname, dpin});
        assert(inserted);
        continue;
      }

      if (subs >= vname2dpin[vname].get_ssa().get_subs()) {
        vname2dpin.insert_or_assign(vname, dpin);
      }
    }
  }

  //create scalar graph outputs or set the final variable name based on vname table
  for (auto const&[vname, vname_dpin] : vname2dpin) {
    auto dpin_name = vname_dpin.get_name();
    if (is_output(dpin_name)) {
      auto edible_dpin = vname_dpin;
      create_out_ta(lg, dpin_name, edible_dpin);
      continue;
    }

    if (driver_var2wire_nodes.find(vname) != driver_var2wire_nodes.end()) {
      auto driver_ntype = vname_dpin.get_node().get_type_op();
      for (auto &it : driver_var2wire_nodes[vname]) {
        if (driver_ntype == Ntype_op::TupAdd) {
          it.set_type(Ntype_op::TupAdd); // change wire_node type from Or_Op to dummy TupAdd_Op
          auto attr_key_dpin = setup_field_dpin(lg, "__last_value");
          auto attr_key_spin = it.setup_sink_pin("field");
          lg->add_edge(attr_key_dpin, attr_key_spin);
          auto wire_spin = it.get_sink_pin("tuple_name");
          lg->add_edge(vname_dpin, wire_spin);
        } else {
          I(it != vname_dpin.get_node());
          auto wire_spin = it.get_sink_pin("A"); //check
          lg->add_edge(vname_dpin, wire_spin);
        }
      }
      continue;
    }
  }

  // connect output tuple-chain to output pin "%"
  auto unified_out_dpin = name2dpin["%"]; // TA node
  auto unified_out_spin = lg->add_graph_output("%", Port_invalid, 0);
  unified_out_dpin.connect_sink(unified_out_spin);

  absl::flat_hash_set<Node> inp_artifacts;
  lg->each_graph_input ([&] (Node_pin &uinp) {
      for (auto &e : uinp.out_edges()) {
        auto tg = e.sink.get_node();
        I(tg.get_type_op() == Ntype_op::TupGet);
        std::string hier_name = (std::string)tg.get_driver_pin().get_name();

        for (auto &tg_out : tg.out_edges()) {
          inp_artifacts.insert(tg); 
          dfs_try_create_flattened_inp(lg, tg_out.sink, hier_name, inp_artifacts);
        }
      }
    /* try_create_flattened_inp(lg, pin); */
  });

  lg->each_graph_output ([&] (Node_pin &pin) {
    /* try_create_flattened_out(lg, pin); */
    ;
  });


  // connect graph inputs to leaf_artifact fanout
  for (auto &itr : inp2leaf_tg_spins) {
    Node_pin ginp = itr.first;

    for (auto &spin : itr.second) {
      ginp.connect_sink(spin);
    }
  }


  // clean up tmp hierarchical input artifacts
  for (auto itr : inp_artifacts) {
    itr.del_node();
  }

  // if all inputs could be resolved, delete the unified $
  auto unified_inp = lg->get_graph_input("$");
  /* if (unified_inp.out_edges().size() == 0) */
    /* unified_inp.del_driver(Node_pin &dst) */

}

/* void Lnast_tolg::try_create_flattened_inp (LGraph *lg, Node_pin &uinp) { */

/*   for (auto &out : uinp.out_edges()) { */
/*     auto sink_node = out.sink.get_node(); */
/*     if (sink_node.get_type_op() == Ntype_op::TupGet) { */
/*       std::string base_name = out */
      
/*     } */
    
/*   } */
  
/* } */



/* void Lnast_tolg::dp_create_hier_outputs(LGraph *lg, Node &cur_node, std::string hier_name, absl::flat_hash_set<Node::Compact> &memo) { */
/*   auto cur_type = cur_node.get_type_op(); */
/*   if (cur_type == TupRef_Op || cur_type != TupAdd_Op) */
/*     return; */

/*   if (memo.find(cur_node.get_compact())!= memo.end()) */
/*     return; */

/*   memo.insert(cur_node.get_compact()); */

/*   auto parent_dpin = cur_node.get_sink_pin(0).get_driver_pin(); */
/*   auto parent_node = parent_dpin.get_node(); */

/*   if (!cur_node.has_sink_pin_connected(1) && !cur_node.has_sink_pin_connected(2)) { */
/*     dp_create_hier_outputs(lg, parent_node, hier_name, memo); */
/*     return; */
/*   } */

/*   auto [tup_name, field_name, key_pos] = Cprop::get_tuple_name_key(cur_node); */
/*   std::string new_hier_name; */
/*   if (!field_name.empty()) { */
/*     new_hier_name = absl::StrCat(hier_name, ".", field_name.substr(0, field_name.size()-2)); */
/*   } else { */
/*     new_hier_name = absl::StrCat(hier_name, ".", key_pos); */
/*   } */

/*   Node_pin val_dpin; */
/*   if (cur_node.has_sink_pin_connected(3)) */
/*     val_dpin = cur_node.get_sink_pin(3).get_driver_pin(); */

/*   auto val_dnode      = val_dpin.get_node(); */
/*   auto val_dnode_type = val_dnode.get_type_op(); */
/*   if (val_dnode_type == TupAdd_Op) { */
/*     dp_create_hier_outputs(lg, val_dnode,   new_hier_name, memo); */
/*     dp_create_hier_outputs(lg, parent_node, hier_name, memo); */
/*   } else { */
/*     if (val_dnode_type == TupRef_Op && is_input(val_dpin.get_name())) { */
/*       auto val_dpin_name  = val_dpin.get_name(); */
/*       auto ginp_name      = val_dpin_name.substr(1, val_dpin_name.size()-3); */

/*       Node_pin ginp; */
/*       if (!lg->is_graph_input(ginp_name)) */
/*         ginp = lg->add_graph_input(ginp_name, Port_invalid, 0); */
/*       else */
/*         ginp = lg->get_graph_input(ginp_name); */

/*       auto out_spin = lg->add_graph_output(new_hier_name, Port_invalid, 0); */
/*       inp2leaf_tg_spins[ginp].emplace_back(out_spin); */
/*     } else { */
/*       auto out_spin = lg->add_graph_output(new_hier_name, Port_invalid, 0); */
/*       val_dpin.connect(out_spin); */
/*     } */
/*   } */
/*   dp_create_hier_outputs(lg, parent_node, hier_name, memo); */
/*   return; */
/* } */

void Lnast_tolg::dfs_try_create_flattened_inp(LGraph *lg, Node_pin &cur_node_spin, std::string hier_name,
                                               absl::flat_hash_set<Node> &inp_artifacts)
{
  auto cur_node  = cur_node_spin.get_node();
  auto cur_ntype = cur_node.get_type_op();
  bool is_leaf = false;
  auto new_hier_name = hier_name;
  if (cur_ntype == Ntype_op::TupGet) {
    inp_artifacts.insert(cur_node); // only remove the artifact tup_gets
    auto [tup_name, field_name, key_pos] = Cprop::get_tuple_name_key(cur_node);
    if (!field_name.empty()) {
      new_hier_name = absl::StrCat(new_hier_name, ".", field_name.substr(0, field_name.size()-2));
    } else {
      new_hier_name = absl::StrCat(new_hier_name, ".", key_pos);
    }
    for (auto &e : cur_node.out_edges()) {
      dfs_try_create_flattened_inp(lg, e.sink, new_hier_name, inp_artifacts);
    }
  } else if (cur_ntype == Ntype_op::Or && cur_node.inp_edges().size() == 1) {
    for (auto &e : cur_node.out_edges()) {
      dfs_try_create_flattened_inp(lg, e.sink, new_hier_name, inp_artifacts);
    }
  } else if (cur_ntype == Ntype_op::TupAdd && check_is_attrset_ta(cur_node)) {
    /* is_leaf = true; */
    /* auto [tup_name, field_name, key_pos] = Cprop::get_tuple_name_key(cur_node); */
    /* if (!field_name.empty()) { */
    /*   new_hier_name = absl::StrCat(new_hier_name, ".", field_name.substr(0, field_name.size()-2)); */
    /* } else { */
    /*   new_hier_name = absl::StrCat(new_hier_name, ".", key_pos); */
    /* } */

    /* // reconstruct attribute setting, sort of flatten it */
  } else if (cur_ntype == Ntype_op::TupAdd ) {
    /* // cannot handle now, you don't know it's tuple or scalar */
    /* return; */
  } else {
    // normal operation and attribute set
    is_leaf = true;
  }


  if (is_leaf) {
    Node_pin ginp;
    if (!lg->is_graph_input(new_hier_name))
      ginp = lg->add_graph_input(new_hier_name, Port_invalid, 0);
    else
      ginp = lg->get_graph_input(new_hier_name);

    inp2leaf_tg_spins[ginp].emplace_back(cur_node_spin);
    return;
  }
}


bool Lnast_tolg::check_is_attrset_ta (Node &node) {
  return false;
}
