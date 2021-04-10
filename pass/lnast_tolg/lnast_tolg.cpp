// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_tolg.hpp"

#include <bits/stdint-intn.h>

#include <cctype>

#include "cprop.hpp"
#include "pass.hpp"

Lnast_tolg::Lnast_tolg(std::string_view _module_name, std::string_view _path) : module_name(_module_name), path(_path) {
  setup_lnast_to_lgraph_primitive_type_mapping();
  tuple_assign_str = "tuple_assign";
}

std::vector<Lgraph *> Lnast_tolg::do_tolg(std::shared_ptr<Lnast> ln, const Lnast_nid &top_stmts) {
  Lbench b("pass.lnast_tolg");
  lnast = ln;
  Lgraph *    lg;
  std::string src{lnast->get_source()};
  if (src.empty())
    src = "-";

  // move %/$ generation at the pb->lnast
  lg             = Lgraph::create(path, module_name, src);
  name2dpin["$"] = lg->add_graph_input("$", Port_invalid, 0);
  setup_tuple_ref(lg, "%");
  std::vector<Lgraph *> lgs;
  top_stmts2lgraph(lg, top_stmts);
  lgs.push_back(lg);

  return lgs;
}

void Lnast_tolg::top_stmts2lgraph(Lgraph *lg, const Lnast_nid &lnidx_stmts) {
  /* fmt::print("======== Phase-1: LNAST->Lgraph Start ================================\n"); */
  process_ast_stmts(lg, lnidx_stmts);

  /* fmt::print("======== Phase-2: Adding final Module IO/Reg and Final Dpin Name =====\n"); */
  setup_lgraph_ios_and_final_var_name(lg);
}

void Lnast_tolg::process_ast_stmts(Lgraph *lg, const Lnast_nid &lnidx_stmts) {
  for (const auto &lnidx : lnast->children(lnidx_stmts)) {
    const auto ntype = lnast->get_data(lnidx).type;
    if (ntype.is_assign()) {
      process_ast_assign_op(lg, lnidx);
    } else if (ntype.is_dp_assign()) {
      process_ast_dp_assign_op(lg, lnidx);
    } else if (ntype.is_logical_op()) {
      process_ast_logical_op(lg, lnidx);
    } else if (ntype.is_direct_lgraph_op()) {
      process_ast_nary_op(lg, lnidx);
    } else if (ntype.is_attr_set()) {
      process_ast_attr_set_op(lg, lnidx);
    } else if (ntype.is_attr_get()) {
      process_ast_attr_get_op(lg, lnidx);
    } else if (ntype.is_tuple_add()) {
      process_ast_tuple_add_op(lg, lnidx);
    } else if (ntype.is_tuple_get()) {
      process_ast_tuple_get_op(lg, lnidx);
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
      I(lnast->get_name(lnidx) == "err_var_undefined");
      continue;
    } else if (ntype.is_select()) {
      I(false);  // have been converted to tuple chain
    } else if (ntype.is_err_flag()) {
      I(lnast->get_name(lnidx) == "err_var_undefined");
      continue;
    } else {
      lnast->dump();
      fmt::print("FIXME: implement op:{}\n", ntype.debug_name());
      I(false);
      return;
    }
  }
}

void Lnast_tolg::process_ast_if_op(Lgraph *lg, const Lnast_nid &lnidx_if) {
  for (const auto &if_child : lnast->children(lnidx_if)) {
    auto ntype = lnast->get_type(if_child);
    if (ntype.is_stmts()) {
      process_ast_stmts(lg, if_child);
    } else if (ntype.is_ref() || ntype.is_const()) {
      continue;
    } else if (ntype.is_phi()) {
      process_ast_phi_op(lg, if_child);
    } else if (ntype.is_tuple_add()) {
      process_ast_tuple_add_op(lg, if_child);
    } else if (ntype.is_tuple_get()) {
      process_ast_tuple_get_op(lg, if_child);
    } else if (ntype.is_attr_get()) {
      process_ast_attr_get_op(lg, if_child);
    } else {
      I(false);  // if-subtree should only contain stmts/cond/phi nodes
    }
  }
}

void Lnast_tolg::process_ast_phi_op(Lgraph *lg, const Lnast_nid &lnidx_phi) {
  auto phi_node   = lg->create_node(Ntype_op::Mux);
  auto cond_spin  = phi_node.setup_sink_pin("0");  // Y = ~S&"1" + S&"2"
  auto true_spin  = phi_node.setup_sink_pin("2");
  auto false_spin = phi_node.setup_sink_pin("1");

  auto lhs       = lnast->get_first_child(lnidx_phi);
  auto c1        = lnast->get_sibling_next(lhs);
  auto c2        = lnast->get_sibling_next(c1);
  auto c3        = lnast->get_sibling_next(c2);
  auto lhs_sname = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  auto     cond_dpin  = setup_ref_node_dpin(lg, c1);
  Node_pin true_dpin  = setup_ref_node_dpin(lg, c2, false, true);
  Node_pin false_dpin = setup_ref_node_dpin(lg, c3, false, true);

  I(!true_dpin.is_invalid());
  I(!false_dpin.is_invalid());

  lg->add_edge(cond_dpin, cond_spin);
  lg->add_edge(true_dpin, true_spin);
  lg->add_edge(false_dpin, false_spin);

  name2dpin[lhs_sname] = phi_node.setup_driver_pin();
  phi_node.setup_driver_pin().set_name(lhs_sname);

  if (!is_tmp_var(lhs_vname))
    setup_dpin_ssa(name2dpin[lhs_sname], lhs_vname, lnast->get_subs(lhs));
}

void Lnast_tolg::process_ast_concat_op(Lgraph *lg, const Lnast_nid &lnidx_concat) {
  auto lhs       = lnast->get_first_child(lnidx_concat);  // c0: target tuple name for concat.
  auto opd1      = lnast->get_sibling_next(lhs);          // c1: tuple operand1, either scalar or tuple
  auto opd2      = lnast->get_sibling_next(opd1);         // c2: tuple operand2, either scalar or tuple
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);
  auto opd1_name = lnast->get_sname(opd1);
  auto opd2_name = lnast->get_sname(opd2);
  // lhs = opd1 ++ opd2, both opd1 and opd2 could be either a scalar or a tuple

  // create TupAdd, concat both tail of opd1 and opd2, name it with old opd1_name (a = a ++ b) or new lhs_name (c = a ++ b)
  auto tup_add    = lg->create_node(Ntype_op::TupAdd);
  auto tn_spin    = tup_add.setup_sink_pin("tuple_name");  // tuple name
  auto value_spin = tup_add.setup_sink_pin("value");       // key->value

  auto tn_dpin = setup_ref_node_dpin(lg, opd1);
  lg->add_edge(tn_dpin, tn_spin);

  auto value_dpin = setup_ref_node_dpin(lg, opd2);
  lg->add_edge(value_dpin, value_spin);

  if (lhs_name == opd1_name) {
    name2dpin[opd1_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(opd1_name);
    // if (is_register(opd1_name))
    //   tuple_reg_names.insert(lnast->get_vname(opd1));
  } else {
    name2dpin[lhs_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(lhs_name);
    // if (is_register(lhs_name))
    //   tuple_reg_names.insert(lnast->get_vname(lhs));
  }

  if (!is_tmp_var(lhs_vname))
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
}

void Lnast_tolg::process_ast_nary_op(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  auto opr_node = setup_node_opr_and_lhs(lg, lnidx_opr, "");

  std::vector<Node_pin> opds;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue;  // the lhs has been handled at setup_node_opr_and_lhs();

    auto opd = setup_ref_node_dpin(lg, opr_child);
    if (opd.is_invalid())
      Pass::error("for operator node {}, undefined variable {} is used!\n", opr_node.debug_name(), lnast->get_sname(opr_child));

    opds.emplace_back(opd);
  }
  nary_node_rhs_connections(lg, opr_node, opds, lnast->get_type(lnidx_opr).is_minus());
}

void Lnast_tolg::process_ast_logical_op(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  // (1) create logical operator node and record the dpin to symbol table
  // (2) create comparator node and compare with 0 for each of the inputs
  // (3) take the result of every comparator as the inputs of logical operator inputs

  auto                  opr_node = setup_node_opr_and_lhs(lg, lnidx_opr, "");
  std::vector<Node_pin> eqs_dpins;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr))
      continue;  // the lhs has been handled at setup_node_opr_and_lhs();

    // TODO: A simple Ror is faster/better
    auto node_eq  = lg->create_node(Ntype_op::EQ);
    auto node_not = lg->create_node(Ntype_op::Not);
    node_eq.setup_driver_pin().connect_sink(node_not.setup_sink_pin("a"));
    auto ori_opd   = setup_ref_node_dpin(lg, opr_child);
    auto zero_dpin = lg->create_node_const(Lconst(0)).setup_driver_pin();

    lg->add_edge(ori_opd, node_eq.setup_sink_pin("A"));
    lg->add_edge(zero_dpin, node_eq.setup_sink_pin("A"));

    eqs_dpins.emplace_back(node_not.setup_driver_pin());
  }

  nary_node_rhs_connections(lg, opr_node, eqs_dpins, lnast->get_type(lnidx_opr).is_minus());
};

void Lnast_tolg::nary_node_rhs_connections(Lgraph *lg, Node &opr_node, const std::vector<Node_pin> &opds, bool is_subt) {
  switch (opr_node.get_type_op()) {
    case Ntype_op::Sum:
    case Ntype_op::Mult: {
      bool is_first = true;
      for (const auto &opd : opds) {
        if (is_subt & !is_first) {                          // note: Hunter -- for subtraction
          lg->add_edge(opd, opr_node.setup_sink_pin("B"));  // HERE Check this
        } else {
          lg->add_edge(opd, opr_node.setup_sink_pin("A"));
          is_first = false;
        }
      }
    } break;
    case Ntype_op::LT:
    case Ntype_op::GT: {
      I(opds.size() == 2);
      lg->add_edge(opds[0], opr_node.setup_sink_pin("A"));
      lg->add_edge(opds[1], opr_node.setup_sink_pin("B"));
    } break;
    case Ntype_op::EQ: {
      for (const auto &opd : opds) lg->add_edge(opd, opr_node.setup_sink_pin("A"));
    } break;
    case Ntype_op::Div:
    case Ntype_op::SHL:
    case Ntype_op::Sext:
    case Ntype_op::SRA: {
      I(opds.size() == 2);  // val<<amount
      lg->add_edge(opds[0], opr_node.setup_sink_pin("a"));
      lg->add_edge(opds[1], opr_node.setup_sink_pin("b"));
    } break;
    default: {
      I(opr_node.get_type_op() != Ntype_op::Mux);
      I(opr_node.get_type_op() != Ntype_op::Flop);
      for (const auto &opd : opds) {
        lg->add_edge(opd, opr_node.setup_sink_pin());
      }
    }
  }
}

Node Lnast_tolg::process_ast_assign_op(Lgraph *lg, const Lnast_nid &lnidx_assign) {
  auto c0 = lnast->get_first_child(lnidx_assign);
  auto c1 = lnast->get_sibling_next(c0);

  bool is_tup_asg = lnast->get_name(lnidx_assign) == tuple_assign_str;
  auto opd1       = setup_ref_node_dpin(lg, c1, is_tup_asg);
  auto opd1_node  = opd1.get_node();
  auto opd1_ntype = opd1_node.get_type_op();

  Node_pin opr_spin;
  if (is_tup_asg || opd1_ntype == Ntype_op::TupAdd || opd1_ntype == Ntype_op::TupRef) {
    opr_spin = setup_tuple_assignment(lg, lnidx_assign);
  } else if (opd1_ntype == Ntype_op::AttrSet) {
    opr_spin = setup_node_assign_and_lhs(lg, lnidx_assign);
  } else if (opd1.has_name() && is_input(opd1.get_name())) {
    opr_spin = setup_tuple_assignment(lg, lnidx_assign);
  } else {
    opr_spin = setup_node_assign_and_lhs(lg, lnidx_assign);
  }

  lg->add_edge(opd1, opr_spin);
  return opr_spin.get_node();
}

void Lnast_tolg::process_ast_dp_assign_op(Lgraph *lg, const Lnast_nid &lnidx_dp_assign) {
  auto c0_dp       = lnast->get_first_child(lnidx_dp_assign);
  auto lhs_dp      = lnast->get_sibling_next(c0_dp);
  auto rhs_dp      = lnast->get_sibling_next(lhs_dp);
  auto rhs_dp_name = lnast->get_sname(rhs_dp);
  auto c0_dp_name  = lnast->get_sname(c0_dp);  // ssa name
  auto c0_dp_vname = lnast->get_vname(c0_dp);  // no-ssa name
  auto attr_vname  = "__dp_assign";

  if (name2dpin.find(rhs_dp_name) == name2dpin.end()) {
    process_ast_assign_op(lg, lnidx_dp_assign);
    return;
  }

  auto aset_node = lg->create_node(Ntype_op::AttrSet);
  auto vn_spin   = aset_node.setup_sink_pin("name");   // variable name (lhs)
  auto af_spin   = aset_node.setup_sink_pin("field");  // attribute field
  auto av_spin   = aset_node.setup_sink_pin("value");  // attribute value (rhs)

  auto af_dpin = setup_field_dpin(lg, attr_vname);
  lg->add_edge(af_dpin, af_spin);

  auto vn_dpin = setup_ref_node_dpin(lg, rhs_dp);
  lg->add_edge(vn_dpin, vn_spin);

  auto av_dpin = setup_ref_node_dpin(lg, lhs_dp);
  lg->add_edge(av_dpin, av_spin);

  aset_node.setup_driver_pin("Y").set_name(c0_dp_name);
  name2dpin[c0_dp_name] = aset_node.get_driver_pin("Y");
  if (!is_tmp_var(c0_dp_vname))
    setup_dpin_ssa(name2dpin[c0_dp_name], c0_dp_vname, lnast->get_subs(c0_dp));
}

void Lnast_tolg::process_ast_tuple_struct(Lgraph *lg, const Lnast_nid &lnidx_tup) {
  std::string      tup_name;
  std::string_view tup_vname;
  auto             c0_tup       = lnast->get_first_child(lnidx_tup);
  auto             c1_tup       = lnast->get_sibling_next(c0_tup);
  auto             c1_tup_vname = lnast->get_vname(c1_tup);
  int8_t           subs         = 0;
  uint16_t         fp           = 0;  // field position

  // note: each new tuple element will be the new tuple chain tail and inherit the tuple name
  for (const auto &tup_child : lnast->children(lnidx_tup)) {
    if (tup_child == lnast->get_first_child(lnidx_tup)) {
      tup_name  = lnast->get_sname(tup_child);
      tup_vname = lnast->get_vname(tup_child);
      subs      = lnast->get_subs(tup_child);
      // if (is_register(tup_vname))
      //   tuple_reg_names.insert(tup_vname);
      continue;
    }

    if (lnast->get_type(tup_child).is_invalid())
      continue;

    // the cases with key name well-defined
    if (lnast->get_type(tup_child).is_assign()) {
      auto c0         = lnast->get_first_child(tup_child);
      auto c1         = lnast->get_sibling_next(c0);
      auto field_name = lnast->get_vname(c0);

      Node_pin tn_dpin;
      if (fp == 0) {
        tn_dpin = setup_ta_ref_previous_ssa(lg, tup_vname, subs);
      } else {
        tn_dpin = setup_tuple_ref(lg, tup_name);
      }
      Lconst pos_const;
      if (field_name.substr(0, 4) != "null") {
        pos_const = Lconst(absl::StrCat(":", std::to_string(fp), ":", field_name));
      } else {
        pos_const = Lconst(fp);
      }

      auto fp_dnode       = lg->create_node_const(pos_const);
      auto field_pos_dpin = fp_dnode.setup_driver_pin();
      auto value_dpin     = setup_ref_node_dpin(lg, c1);

      auto tup_add        = lg->create_node(Ntype_op::TupAdd);
      auto field_pos_spin = tup_add.setup_sink_pin("position");    // key position is unknown before tuple resolving
      auto value_spin     = tup_add.setup_sink_pin("value");       // value

      if (!tn_dpin.is_invalid()) {
        auto tn_spin = tup_add.setup_sink_pin("tuple_name");  // tuple name
        lg->add_edge(tn_dpin, tn_spin);
      }
      lg->add_edge(field_pos_dpin, field_pos_spin);
      lg->add_edge(value_dpin, value_spin);

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name);

      if (!is_tmp_var(tup_vname))
        setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);

      fp++;
      continue;
    }

    if (is_err_var_undefined(c1_tup_vname)) {
      setup_tuple_ref(lg, tup_name);
      return;
    }

    auto tup_add        = lg->create_node(Ntype_op::TupAdd);
    auto fp_dnode       = lg->create_node_const(Lconst(fp));
    auto field_pos_dpin = fp_dnode.setup_driver_pin();
    auto value_dpin     = setup_ref_node_dpin(lg, tup_child);

    auto field_pos_spin = tup_add.setup_sink_pin("position");    // field position is unknown before tuple resolving
    auto value_spin     = tup_add.setup_sink_pin("value");       // value

    auto tn_dpin        = setup_tuple_ref(lg, tup_name);
    if (!tn_dpin.is_invalid()) {
      auto tn_spin        = tup_add.setup_sink_pin("tuple_name");  // tuple name
      lg->add_edge(tn_dpin, tn_spin);
    }
    lg->add_edge(field_pos_dpin, field_pos_spin);
    lg->add_edge(value_dpin, value_spin);

    name2dpin[tup_name] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(tup_name);

    if (!is_tmp_var(tup_vname))
      setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);
    fp++;
  }
}

Node_pin Lnast_tolg::create_inp_tg(Lgraph *lg, std::string_view input_field) {
  auto tup_get_inp = lg->create_node(Ntype_op::TupGet);
  auto tn_spin     = tup_get_inp.setup_sink_pin("tuple_name");
  auto tn_dpin     = name2dpin["$"];
  tn_dpin.connect_sink(tn_spin);

  auto pos_spin = tup_get_inp.setup_sink_pin("position");
  auto subname  = input_field.substr(1, input_field.size() - 1);

  auto pos_dpin = lg->create_node_const(Lconst(subname)).setup_driver_pin();
  lg->add_edge(pos_dpin, pos_spin);

  auto tg_dpin = tup_get_inp.setup_driver_pin();
  tg_dpin.set_name(input_field);

  name2dpin[input_field] = tg_dpin;
  return tg_dpin;
}

void Lnast_tolg::process_ast_tuple_get_op(Lgraph *lg, const Lnast_nid &lnidx_tg) {
  int                            i = 0;
  absl::flat_hash_map<int, Node> tg_map;
  std::string                    c0_tg_name;
  std::string_view               c0_tg_vname;
  int8_t                         c0_tg_subs = 0;

  for (const auto &child : lnast->children(lnidx_tg)) {
    if (i == 0) {
      const auto &c0_tg = child;
      c0_tg_name        = lnast->get_sname(c0_tg);
      c0_tg_vname       = lnast->get_vname(c0_tg);
      c0_tg_subs        = lnast->get_subs(c0_tg);
      i++;
      continue;
    }

    if (i == 1) {
      const auto &c1_tg      = child;
      auto        c1_tg_name = lnast->get_sname(c1_tg);
      auto        tup_get    = lg->create_node(Ntype_op::TupGet);
      tg_map.insert_or_assign(i, tup_get);


      Node_pin tn_dpin;

      if (is_input(c1_tg_name)) {
        tn_dpin = create_inp_tg(lg, lnast->get_vname(c1_tg));
      // } else if (is_register(c1_tg_name)) {
      //   tn_dpin = setup_ref_node_dpin(lg, c1_tg);
      } else {
        tn_dpin = setup_tuple_ref(lg, c1_tg_name);
      }

      if (!tn_dpin.is_invalid()) {
        auto tn_spin = tup_get.setup_sink_pin("tuple_name");
        lg->add_edge(tn_dpin, tn_spin);
      }
      i++;
      continue;
    }

    // i >= 2
    if (child == lnast->get_last_child(lnidx_tg)) {
      const auto &cn_tg      = child;
      auto        cn_tg_name = lnast->get_vname(cn_tg);
      auto        tup_get    = tg_map[i - 1];
      auto        pos_spin   = tup_get.setup_sink_pin("position");
      auto        lntype     = lnast->get_type(cn_tg);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, cn_tg);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst(cn_tg_name)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);
      }

      if (vname2attr_dpin.find(c0_tg_vname) != vname2attr_dpin.end()) {
        auto aset_node          = lg->create_node(Ntype_op::AttrSet);
        auto aset_aci_spin      = aset_node.setup_sink_pin("chain");
        auto aset_ancestor_dpin = vname2attr_dpin[c0_tg_vname];
        lg->add_edge(aset_ancestor_dpin, aset_aci_spin);

        auto aset_vn_spin = aset_node.setup_sink_pin("name");
        auto aset_vn_dpin = tup_get.get_driver_pin();
        lg->add_edge(aset_vn_dpin, aset_vn_spin);

        name2dpin[c0_tg_name] = aset_node.setup_driver_pin("Y");  // dummy_attr_set node now represent the latest variable
        aset_node.get_driver_pin("Y").set_name(c0_tg_name);

        if (!is_tmp_var(c0_tg_vname))
          setup_dpin_ssa(name2dpin[c0_tg_name], c0_tg_vname, c0_tg_subs);
        vname2attr_dpin[c0_tg_vname] = aset_node.setup_driver_pin("chain");
        return;
      }

      name2dpin[c0_tg_name] = tup_get.setup_driver_pin();

      tup_get.setup_driver_pin().set_name(c0_tg_name);
      if (!is_tmp_var(c0_tg_vname))
        setup_dpin_ssa(name2dpin[c0_tg_name], c0_tg_vname, c0_tg_subs);

    } else {  // not the last child
      auto new_tup_get = lg->create_node(Ntype_op::TupGet);
      tg_map.insert_or_assign(i, new_tup_get);
      auto tn_spin = new_tup_get.setup_sink_pin("tuple_name");

      const auto &cn_tg        = child;
      auto        cn_tg_name   = lnast->get_vname(cn_tg);
      auto        prev_tup_get = tg_map[i - 1];
      auto        pos_spin     = prev_tup_get.setup_sink_pin("position");
      auto        lntype       = lnast->get_type(cn_tg);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, cn_tg);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst(cn_tg_name)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);
      }

      auto tn_dpin = prev_tup_get.setup_driver_pin();
      lg->add_edge(tn_dpin, tn_spin);

      i++;
      continue;
    }
  }
}

bool Lnast_tolg::is_hier_inp_bits_set(const Lnast_nid &lnidx_ta) {
  for (const auto &child : lnast->children(lnidx_ta)) {
    if (child == lnast->get_first_child(lnidx_ta)) {
      if (is_input(lnast->get_vname(child)))
        continue;
      else
        return false;
    } else {
      if (lnast->get_vname(child) == "__ubits" || lnast->get_vname(child) == "__sbits")
        return true;
    }
  }
  return false;
}

// note-I:  since it's BW setting on the hier-inp, you know it's flattened scalar -> can create lg hier-input for it
// note-II: these inputs might be access by a TG with run-time index, you have to collect these flattened graph-inputs
//          into a TA-chain for the future possible access.
void Lnast_tolg::process_hier_inp_bits_set(Lgraph *lg, const Lnast_nid &lnidx_ta) {
  std::string full_inp_hier_name;
  for (const auto &child : lnast->children(lnidx_ta)) {
    if (child == lnast->get_first_child(lnidx_ta)) {
      const auto &c0_ta = child;
      I(is_input(lnast->get_vname(c0_ta)));
      full_inp_hier_name = lnast->get_vname(c0_ta).substr(1);
    } else if (lnast->get_vname(child) != "__ubits" && lnast->get_vname(child) != "__sbits") {
      I(child != lnast->get_last_child(lnidx_ta));
      full_inp_hier_name = absl::StrCat(full_inp_hier_name, ".", lnast->get_vname(child));
    } else if (lnast->get_vname(child) == "__ubits" || lnast->get_vname(child) == "__sbits") {  // at the __bits child
      //(1) create flattened input
      Node_pin flattened_inp;
      if (!lg->has_graph_input(full_inp_hier_name))
        flattened_inp = lg->add_graph_input(full_inp_hier_name, Port_invalid, 0);
      else
        flattened_inp = name2dpin[full_inp_hier_name];

      //(2) create attr_set node for input
      //auto aset_node = lg->create_node(Ntype_op::AttrSet);
      auto aset_node = lg->create_node(Ntype_op::TupAdd);
      auto vn_spin   = aset_node.setup_sink_pin("tuple_name");   // variable name
      auto af_spin   = aset_node.setup_sink_pin("position");  // attribute field
      auto av_spin   = aset_node.setup_sink_pin("value");  // attribute value

      flattened_inp.connect_sink(vn_spin);
      auto af_dpin = setup_field_dpin(lg, lnast->get_vname(child));
      af_dpin.connect_sink(af_spin);

      auto const_lnidx = lnast->get_sibling_next(child);
      auto av_dpin     = setup_ref_node_dpin(lg, const_lnidx);
      av_dpin.connect(av_spin);
      auto aset_dpin                = aset_node.setup_driver_pin("Y");
      name2dpin[full_inp_hier_name] = aset_dpin;

      create_inp_ta4dynamic_idx(lg, aset_dpin, full_inp_hier_name);
      break;  // no need to iterate to last child
    }
  }
}

void Lnast_tolg::create_inp_ta4dynamic_idx(Lgraph *lg, const Node_pin &val_dpin, std::string_view full_inp_hier_name) {
  auto pos          = full_inp_hier_name.find_last_of('.');
  auto last_subname = full_inp_hier_name.substr(pos + 1);

  // if the last subname is not a number(not a constant), means the tuple is not array-like, it's impossilbe
  // future graph-inp will try to get the array-element from a dynamic index, so we don't need to construct the inp_ta
  if (!std::isdigit(last_subname.at(0)))
    return;

  auto tup_name  = full_inp_hier_name.substr(0, pos);
  auto ta_node   = lg->create_node(Ntype_op::TupAdd);
  auto pos_spin  = ta_node.setup_sink_pin("position");
  auto val_spin  = ta_node.setup_sink_pin("value");

  auto name_dpin = setup_tuple_ref(lg, tup_name);
  if (!name_dpin.is_invalid()) {
    auto name_spin = ta_node.setup_sink_pin("tuple_name");
    name_dpin.connect_sink(name_spin);
  }

  auto pos_dpin = lg->create_node_const(Lconst(last_subname)).setup_driver_pin();
  pos_dpin.connect_sink(pos_spin);
  val_dpin.connect(val_spin);
  auto ta_dpin = ta_node.setup_driver_pin();
  ta_dpin.set_name(tup_name);
  name2dpin[tup_name] = ta_node.setup_driver_pin();
}

void Lnast_tolg::process_ast_tuple_add_op(Lgraph *lg, const Lnast_nid &lnidx_ta) {
  if (is_hier_inp_bits_set(lnidx_ta)) {
    process_hier_inp_bits_set(lg, lnidx_ta);
    return;
  }

  absl::flat_hash_map<int, Node>        ta_map;
  absl::flat_hash_map<int, std::string> ta_name;
  int                                   i = 0;

  for (const auto &child : lnast->children(lnidx_ta)) {
    if (i == 0) {
      const auto &c0_ta     = child;
      auto        tup_sname = lnast->get_sname(c0_ta);
      auto        tup_vname = lnast->get_vname(c0_ta);
      auto        subs      = lnast->get_subs(c0_ta);

      auto tup_add = lg->create_node(Ntype_op::TupAdd);
      auto tn_dpin = setup_ta_ref_previous_ssa(lg, tup_vname, subs);
      if (!tn_dpin.is_invalid()) {
        auto tn_spin = tup_add.setup_sink_pin("tuple_name");
        tn_dpin.connect_sink(tn_spin);
      }

      // exclude invalid scalar->tuple cases
      auto field_name = lnast->get_sname(lnast->get_sibling_next(c0_ta));  // peep for field_name ...

      name2dpin[tup_sname] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_sname);
      // if (is_register(tup_vname))
      //   tuple_reg_names.insert(tup_vname);
      setup_dpin_ssa(name2dpin[tup_sname], lnast->get_vname(c0_ta), lnast->get_subs(c0_ta));

      ta_map.insert_or_assign(i, tup_add);
      ta_name.insert_or_assign(i, tup_sname);
      i++;
      continue;
    }

    if (i == 1) {
      const auto &c1_ta       = child;
      auto        tup_add     = ta_map[i - 1];
      auto        pos_spin    = tup_add.setup_sink_pin("position");
      auto        field_vname = lnast->get_vname(c1_ta);
      auto        lntype      = lnast->get_type(c1_ta);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, c1_ta);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst(field_vname)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);
      }

      ta_name.insert_or_assign(i, field_vname);
      i++;
      continue;
    }

    // i >= 2
    if (child == lnast->get_last_child(lnidx_ta)) {
      // non-hier tuple case
      const auto &c2_ta      = child;
      auto        tup_add    = ta_map[i - 2];
      auto        value_spin = tup_add.setup_sink_pin("value");  // value
      auto        value_dpin = setup_ref_node_dpin(lg, c2_ta, true);
      lg->add_edge(value_dpin, value_spin);
      i++;
    } else {
      // hier tuple case, create a new tuple chain
      const auto &cn_ta    = child;
      auto        tup_add  = lg->create_node(Ntype_op::TupAdd);
      auto        tup_name = ta_name[i - 1];

      auto tn_dpin = setup_tuple_ref(lg, tup_name);
      if (!tn_dpin.is_invalid()) {
        auto tn_spin  = tup_add.setup_sink_pin("tuple_name");
        lg->add_edge(tn_dpin, tn_spin);
      }

      ta_map.insert_or_assign(i - 1, tup_add);

      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name);  // tuple ref semantically move to here
      auto cn_ta_vname = lnast->get_vname(cn_ta);
      if (!is_tmp_var(cn_ta_vname))
        setup_dpin_ssa(name2dpin[tup_name], cn_ta_vname, lnast->get_subs(cn_ta));

      // take the new tuple-chain as the original tuple-chain value-dpin -> hierarchical tuple now!
      auto tup_add_parent = ta_map[i - 2];
      // note: no need to handle val_dpin for input_tg cases
      if (tup_add_parent.get_type_op() == Ntype_op::TupAdd) {
        auto value_spin_parent = tup_add_parent.setup_sink_pin("value");
        auto value_dpin_parent = tup_add.setup_driver_pin();
        lg->add_edge(value_dpin_parent, value_spin_parent);
      }

      // setup key for the new tuple chain head
      auto pos_spin    = tup_add.setup_sink_pin("position");  // field position
      auto field_vname = lnast->get_vname(cn_ta);
      auto lntype      = lnast->get_type(cn_ta);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, cn_ta);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst(field_vname)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);
      }

      ta_name.insert_or_assign(i, lnast->get_sname(cn_ta));
      i++;
    }
  }
}

// either tuple root or tuple key(str) fit in this case
Node_pin Lnast_tolg::setup_tuple_ref(Lgraph *lg, std::string_view ref_name) {
  auto it = name2dpin.find(ref_name);

  if (it != name2dpin.end()) {
    return it->second;
  }

  if (is_input(ref_name))
    return create_inp_tg(lg, ref_name);

  static Node_pin invalid_dpin;
  return invalid_dpin;
#if 0
  auto dpin           = lg->create_node_const(Lconst(ref_name)).setup_driver_pin();
  name2dpin[ref_name] = dpin;
  return dpin;
#endif
}

Node_pin Lnast_tolg::setup_ta_ref_previous_ssa(Lgraph *lg, std::string_view ref_vname, int16_t subs) {
  (void)lg; // FIXME: remove arg
  if (subs == 0) {
    auto ref_name = absl::StrCat(ref_vname, "_", subs);
#if 0
    auto dpin     = lg->create_node(Ntype_op::TupRef).setup_driver_pin();
    dpin.set_name(ref_name);
#else
    Node_pin dpin; // invalid_dpin
#endif
    name2dpin[ref_name] = dpin;
    return dpin;
  }

  auto chain_tail_name = absl::StrCat(ref_vname, "_", subs - 1);  // try to concatenate after the TA(ssa-1)
  I(name2dpin.find(chain_tail_name) != name2dpin.end());
  I(name2dpin[chain_tail_name].get_name() == chain_tail_name);
  return name2dpin[chain_tail_name];
}

Node_pin Lnast_tolg::setup_field_dpin(Lgraph *lg, std::string_view field_name) {
  auto it2 = field2dpin.find(field_name);
  if (it2 != field2dpin.end()) {
    return it2->second;
  }

  auto dpin = lg->create_node_const(field_name).setup_driver_pin();
  field2dpin[field_name] = dpin;

  return dpin;
}

bool Lnast_tolg::is_new_var_chain(const Lnast_nid &lnidx_opr) {
  std::string_view vname_1st_child;
  bool             one_of_rhs_is_lhs = false;

  for (const auto &itr_ch : lnast->children(lnidx_opr)) {
    if (itr_ch == lnast->get_first_child(lnidx_opr)) {
      vname_1st_child = lnast->get_vname(itr_ch);
      if (vname_1st_child.substr(0, 3) == "___")
        return false;
      if (vname2attr_dpin.find(vname_1st_child) == vname2attr_dpin.end())
        return false;  // this variable never been assigned with an attribute

      continue;
    }

    if (lnast->get_vname(itr_ch) == vname_1st_child)
      one_of_rhs_is_lhs = true;
  }
  return !one_of_rhs_is_lhs;
}

// for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node Lnast_tolg::setup_node_opr_and_lhs(Lgraph *lg, const Lnast_nid &lnidx_opr, std::string_view fir_func_name) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  Ntype_op lg_ntype_op;
  Node     exit_node;
  if (fir_func_name.empty()) {
    lg_ntype_op = decode_lnast_op(lnidx_opr);
    exit_node   = lg->create_node(lg_ntype_op);
  } else {
    I(fir_func_name.substr(0, 2) == "__");

    auto op = Ntype::get_op(fir_func_name.substr(2));
    if (op == Ntype_op::Invalid) {
      exit_node = lg->create_node_sub(fir_func_name);
    } else {
      exit_node = lg->create_node(op);
    }
  }

  if (!is_new_var_chain(lnidx_opr)) {
    name2dpin[lhs_name] = exit_node.setup_driver_pin("Y");
    exit_node.get_driver_pin("Y").set_name(lhs_name);

    if (!is_tmp_var(lhs_vname))
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
    return exit_node;
  }

  if (is_new_var_chain(lnidx_opr) && vname2attr_dpin.find(lhs_vname) != vname2attr_dpin.end()) {
    auto aset_node          = lg->create_node(Ntype_op::AttrSet);
    auto aset_chain_spin    = aset_node.setup_sink_pin("chain");
    auto aset_ancestor_dpin = vname2attr_dpin[lhs_vname];
    lg->add_edge(aset_ancestor_dpin, aset_chain_spin);

    auto aset_vn_spin = aset_node.setup_sink_pin("name");
    auto aset_vn_dpin = exit_node.get_driver_pin("Y");
    lg->add_edge(aset_vn_dpin, aset_vn_spin);

    if (fir_func_name.empty())
      name2dpin[lhs_name] = aset_node.setup_driver_pin("Y");  // dummy_attr_set node now represent the latest variable
    else
      name2dpin[lhs_name] = exit_node.setup_driver_pin("Y");

    exit_node.get_driver_pin("Y").set_name(lhs_name);
    aset_node.get_driver_pin("Y").set_name(lhs_name);

    if (!is_tmp_var(lhs_vname))
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
    vname2attr_dpin[lhs_vname] = aset_node.setup_driver_pin("chain");
  }

  return exit_node;
}

Node_pin Lnast_tolg::setup_tuple_assignment(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto tup_name  = lnast->get_sname(lhs);
  auto tup_vname = lnast->get_vname(lhs);
  auto tup_add   = lg->create_node(Ntype_op::TupAdd);

  name2dpin[tup_name] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name(tup_name);
  // if (is_register(tup_name))
  //   tuple_reg_names.insert(tup_vname);

  if (!is_tmp_var(tup_vname))
    setup_dpin_ssa(name2dpin[tup_name], tup_vname, lnast->get_subs(lhs));

  return tup_add.setup_sink_pin("tuple_name");
}

Node_pin Lnast_tolg::setup_node_assign_and_lhs(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);
  auto rhs       = lnast->get_sibling_next(lhs);
  (void)rhs;
  auto assign_node = lg->create_node(Ntype_op::Or);

  if (!is_new_var_chain(lnidx_opr)) {
    name2dpin[lhs_name] = assign_node.setup_driver_pin();  // or as assign
    name2dpin[lhs_name].set_name(lhs_name);

    if (!is_tmp_var(lhs_vname))
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
    return assign_node.setup_sink_pin("A");
  }

  if (is_new_var_chain(lnidx_opr) && vname2attr_dpin.find(lhs_vname) != vname2attr_dpin.end()) {
    auto aset_node          = lg->create_node(Ntype_op::AttrSet);
    auto aset_chain_spin    = aset_node.setup_sink_pin("chain");
    auto aset_ancestor_dpin = vname2attr_dpin[lhs_vname];
    lg->add_edge(aset_ancestor_dpin, aset_chain_spin);

    auto aset_vn_spin = aset_node.setup_sink_pin("name");
    auto aset_vn_dpin = assign_node.get_driver_pin("Y");
    lg->add_edge(aset_vn_dpin, aset_vn_spin);

    name2dpin[lhs_name] = aset_node.setup_driver_pin("Y");  // dummy_attr_set node now represent the latest variable
    aset_node.get_driver_pin("Y").set_name(lhs_name);

    if (!is_tmp_var(lhs_vname))
      setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));

    // Do not chain, point to the last AttrSet
    // vname2attr_dpin[lhs_vname] = aset_node.setup_driver_pin("chain");
  }
  return assign_node.setup_sink_pin("A");
}

// for both lhs and rhs, except the new io, reg, and const, the node and its dpin
// should already be in the table as the operand comes from existing operator output
Node_pin Lnast_tolg::setup_ref_node_dpin(Lgraph *lg, const Lnast_nid &lnidx_opd, bool from_ta_assign, bool from_phi) {
  auto name  = lnast->get_sname(lnidx_opd);  // name = ssa_name
  auto vname = lnast->get_vname(lnidx_opd);
  I(!name.empty());

  if (lnast->get_type(lnidx_opd).is_const()) {  // High priority in search to avoid alias
    auto node_dpin  = create_const(lg, vname);
    name2dpin[name] = node_dpin;  // for io and reg, the %$# identifier are still used in symbol table
    return node_dpin;
  }

  const auto &it = name2dpin.find(name);
  if (it != name2dpin.end()) {
    auto node = it->second.get_node();
    auto op   = it->second.get_node().get_type_op();

    // if ref comes from an TA dpin
    if (op == Ntype_op::TupAdd && !from_ta_assign && !from_phi) {
      auto parent_node  = node.get_sink_pin("tuple_name");
      if (parent_node.is_invalid())
        return it->second;

      auto parent_ntype = parent_node.get_type_op();
      auto val_spin     = node.setup_sink_pin("value");

      if (parent_ntype == Ntype_op::Or) {
        return create_scalar_access_tg(lg, it->second);
      } else if (parent_ntype == Ntype_op::TupRef && val_spin.is_connected()) {
        // case: the tuple has only one pos sink pin connected and being used
        // to an operator -> it's still a scalar -> create TG to fetch field 0
        // note: if the field is connected, it cannot be viewed as scalar so
        // just return the TA chain itself, not the scalar-TG(0) optimization.
        if (val_spin.get_driver_node().get_type_op() != Ntype_op::TupAdd) {
          if (!node.setup_sink_pin("position").is_connected()) {
            return create_scalar_access_tg(lg, it->second);
          }
        }
      }
    }
    return it->second;
  }

  Node_pin node_dpin;
  if (is_output(name)) {
    ;
  } else if (is_input(name)) {
    // later the node_type should change to TupGet and connected to $
    node_dpin = lg->create_node(Ntype_op::TupRef).setup_driver_pin();
    node_dpin.set_name(name.substr(0, name.size() - 2));
    name2dpin[name] = node_dpin;
    return node_dpin;
  // } else if (is_register(name)) {
  //   // note-I: the register is first appear at the rhs! Create a floating Or to represent this reg
  //   // later, this Or should be driven by the reg q-pin at the end of program sequence
  //   // note-II: in the case that the Or is driven by a reg TA chain, change Or -> assignment TA
  //   // FIXME->sh: I think the Or node here could generate the Register directly and mimic what 
  //   // attr_get __create_flop is doing. i.e. create register and let d-pin drivern by the largest
  //   // SSA in the end of lnast_tolg pass
  //   auto wire_or_node = lg->create_node(Ntype_op::Or);
  //   node_dpin         = wire_or_node.setup_driver_pin();
  //   node_dpin.set_name(name);
  //   name2dpin[name] = node_dpin;

  //   driver_var2wire_nodes[vname].push_back(wire_or_node);
  //   if (!is_tmp_var(vname))
  //     setup_dpin_ssa(node_dpin, vname, 0);  // FIXME->sh: do we really need this?
  //   return node_dpin;
  } else if (is_err_var_undefined(name)) {
    node_dpin = lg->create_node(Ntype_op::CompileErr).setup_driver_pin("Y");
  } else if (is_bool_true(name)) {
    node_dpin = lg->create_node_const(Lconst(1)).setup_driver_pin();
  } else if (is_bool_false(name)) {
    node_dpin = lg->create_node_const(Lconst(0)).setup_driver_pin();
  } else {
    return node_dpin;  // return empty node_pin and trigger compile error
  }

  name2dpin[name] = node_dpin;  // for io and reg, the %$# identifier are still used in symbol table
  return node_dpin;
}

Node_pin Lnast_tolg::create_scalar_access_tg(Lgraph *lg, const Node_pin &tg_tupname_dpin, const Node_pin &field_dpin) {
  I(false);  // delete this method
  auto tup_get    = lg->create_node(Ntype_op::TupGet);
  auto tn_spin    = tup_get.setup_sink_pin("tuple_name");  // tuple name
  auto field_spin = tup_get.setup_sink_pin("field");       // field

  auto tn_dpin = tg_tupname_dpin;
  tn_dpin.connect_sink(tn_spin);
  field_dpin.connect_sink(field_spin);
  return tup_get.setup_driver_pin();
}

Node_pin Lnast_tolg::create_scalar_access_tg(Lgraph *lg, const Node_pin &tg_tupname_dpin) {
  auto tup_get        = lg->create_node(Ntype_op::TupGet);
  auto tn_spin        = tup_get.setup_sink_pin("tuple_name");  // tuple name
  auto field_pos_spin = tup_get.setup_sink_pin("position");    // field pos

  auto tn_dpin = tg_tupname_dpin;
  auto field_pos_dpin
      = lg->create_node_const(Lconst(0)).setup_driver_pin();  // must be pos 0 as the case is "bar = a + 1", implicitly get a.0
  tn_dpin.connect_sink(tn_spin);
  field_pos_dpin.connect_sink(field_pos_spin);
  return tup_get.setup_driver_pin();
}

Ntype_op Lnast_tolg::decode_lnast_op(const Lnast_nid &lnidx_opr) {
  const auto raw_ntype = lnast->get_data(lnidx_opr).type.get_raw_ntype();
  const auto it        = primitive_type_lnast2lg.find(raw_ntype);
  I(it != primitive_type_lnast2lg.end());
  return it->second;
}

Node_pin Lnast_tolg::create_const(Lgraph *lg, std::string_view const_str) {
  if (const_str.find("bits") == std::string_view::npos)
    return lg->create_node_const(Lconst(const_str)).setup_driver_pin();

  // NOTE: FIRRTL needs bits in constants for the bitwidth inference pass.
  // TODO: It  may be cleaner to create a __fir_const sub in the LNAST gen
  auto lg_fir_const_node = lg->create_node_sub("__fir_const");
  lg_fir_const_node.setup_driver_pin("Y").set_name(const_str);
  return lg_fir_const_node.setup_driver_pin("Y");
}

void Lnast_tolg::process_ast_attr_set_op(Lgraph *lg, const Lnast_nid &lnidx_aset) {
  auto name_aset  = lnast->get_first_child(lnidx_aset);
  auto field_aset = lnast->get_sibling_next(name_aset);

  auto name  = lnast->get_sname(name_aset);  // ssa name
  auto vname = lnast->get_vname(name_aset);  // no-ssa name

  auto aset_node = lg->create_node(Ntype_op::AttrSet);

  mmap_lib::Tree_index val_aset;

  {
    // Get the field[s] foo.bar.xxx.__tree = 3 -> field = "bar.xxx.__tree"
    auto        af_spin = aset_node.setup_sink_pin("field");  // attribute field
    std::string field;

    auto it_aset = field_aset;

    while (!it_aset.is_invalid()) {
      I(lnast->get_type(it_aset).is_const());  // How can it be a ref?? foo.a.b.c.__xxx (a/b/c/__xxx must be consts)
      auto vname2 = lnast->get_vname(it_aset);
      if (field.empty()) {
        field = vname2;
      } else {
        absl::StrAppend(&field, ".", vname2);
      }
      it_aset  = lnast->get_sibling_next(it_aset);
      val_aset = it_aset;
      if (lnast->get_sibling_next(val_aset).is_invalid())
        break;
    }
    auto av_dpin = setup_field_dpin(lg, field);
    // auto av_dpin = lg->create_node_const(Lconst(field)).setup_driver_pin();
    lg->add_edge(av_dpin, af_spin);
  }

  {
    // Get the variable name with SSA/input
    auto vn_spin = aset_node.setup_sink_pin("name");  // variable name

    auto aset_ancestor_subs = lnast->get_data(name_aset).subs - 1;
    auto aset_ancestor_name = absl::StrCat(vname, "_", aset_ancestor_subs);

    Node_pin vn_dpin;
    if (is_input(name)) {
      vn_dpin = setup_tuple_ref(lg, lnast->get_name(name_aset));
      I(!vn_dpin.is_invalid()); // inputs always succeed
      lg->add_edge(vn_dpin, vn_spin);
    } else if (name2dpin.find(aset_ancestor_name) != name2dpin.end()) {
      vn_dpin = name2dpin[aset_ancestor_name];
      lg->add_edge(vn_dpin, vn_spin);
    } else if (name2dpin.find(name) != name2dpin.end()) {
      vn_dpin = name2dpin[name];
      lg->add_edge(vn_dpin, vn_spin);
    }
  }

  {
    // Get the value
    auto val_spin = aset_node.setup_sink_pin("value");  // attribute value

    Node_pin val_dpin;
    if (lnast->get_type(val_aset).is_ref()) {  // setup_ref_node_dpin does not handle SSA for LHS
      auto        value_subs  = lnast->get_data(val_aset).subs - 1;
      auto        value_nossa = lnast->get_vname(val_aset);
      auto        value_name  = absl::StrCat(value_nossa, "_", value_subs);
      const auto &it          = name2dpin.find(value_name);
      if (it != name2dpin.end()) {
        val_dpin = it->second;
      }
    }
    if (val_dpin.is_invalid()) {
      val_dpin = setup_ref_node_dpin(lg, val_aset);
    }
    lg->add_edge(val_dpin, val_spin);
  }

  aset_node.setup_driver_pin("Y").set_name(name);
  aset_node.setup_driver_pin("chain").set_name(name);  // just for debug purpose
  name2dpin[name]        = aset_node.get_driver_pin("Y");
  vname2attr_dpin[vname] = aset_node.get_driver_pin("chain");
}

void Lnast_tolg::process_ast_attr_get_op(Lgraph *lg, const Lnast_nid &lnidx_aget) {
  auto c0_aget       = lnast->get_first_child(lnidx_aget);
  auto c1_aget       = lnast->get_sibling_next(c0_aget);
  auto c2_aget       = lnast->get_sibling_next(c1_aget);
  auto c0_aget_name  = lnast->get_sname(c0_aget);
  auto c0_aget_vname = lnast->get_vname(c0_aget);

  std::string attr_field;
  {
    auto it_aset = c2_aget;

    while (!it_aset.is_invalid()) {
      I(lnast->get_type(it_aset).is_const());  // How can it be a ref?? foo.a.b.c.__xxx (a/b/c/__xxx must be consts)
      auto vname2 = lnast->get_vname(it_aset);
      if (attr_field.empty()) {
        attr_field = vname2;
      } else {
        absl::StrAppend(&attr_field, ".", vname2);
      }

      if (vname2 == "__create_flop" || vname2 == "__last_value") {
        Node wire_node;
        if (vname2 == "__create_flop") {
          wire_node = lg->create_node(Ntype_op::Flop);
        } else {
          wire_node = lg->create_node(Ntype_op::Or);  // might need to change to other type according to the real driver
        }
        wire_node.get_driver_pin().set_name(lnast->get_vname(c1_aget));
        name2dpin[c0_aget_name] = wire_node.setup_driver_pin();

        if (!is_tmp_var(c0_aget_vname))
          setup_dpin_ssa(name2dpin[c0_aget_name], c0_aget_vname, lnast->get_subs(c0_aget));

        auto driver_vname = lnast->get_vname(c1_aget);
        driver_var2wire_nodes[driver_vname].push_back(wire_node);

        it_aset = lnast->get_sibling_next(it_aset);
        if (!it_aset.is_invalid()) {
          Pass::error("attribute {} must be the last in the entry {}\n", vname2, attr_field);
        }
        return;
      }

      it_aset = lnast->get_sibling_next(it_aset);
    }
  }

  Node node = lg->create_node(Ntype_op::AttrGet);

  {
    // set outputs
    auto node_dpin = node.setup_driver_pin();
    node_dpin.set_name(c0_aget_name);
    name2dpin[c0_aget_name] = node_dpin;

    if (!is_tmp_var(c0_aget_vname))
      setup_dpin_ssa(node_dpin, c0_aget_vname, lnast->get_subs(c0_aget));
  }

  {
    auto vn_spin = node.setup_sink_pin("name");  // variable name

    Node_pin vn_dpin = setup_ref_node_dpin(lg, c1_aget);
    lg->add_edge(vn_dpin, vn_spin);
  }

  auto af_spin = node.setup_sink_pin("field");  // attribute field
  auto af_dpin = setup_field_dpin(lg, attr_field);
  lg->add_edge(af_dpin, af_spin);
}

bool Lnast_tolg::subgraph_outp_is_tuple(Sub_node *sub) {
  uint16_t outp_cnt = 0;
  for (const auto *io_pin : sub->get_io_pins()) {
    if (io_pin->is_output()) {
      outp_cnt++;
    }

    if (outp_cnt > 1)
      return true;
  }
  return false;
}

void Lnast_tolg::split_hier_name(std::string_view full_name, std::vector<std::string_view> &hier_io_subnames) {
  auto start = 0u;
  auto end   = full_name.find('.');
  while (end != std::string_view::npos) {
    std::string_view token = full_name.substr(start, end - start);
    hier_io_subnames.emplace_back(token);
    start = end + 1;  //
    end   = full_name.find('.', start);
  }
  std::string_view token = full_name.substr(start, end - start);
  hier_io_subnames.emplace_back(token);
}

void Lnast_tolg::subgraph_io_connection(Lgraph *lg, Sub_node *sub, std::string_view arg_tup_name, std::string_view ret_name,
                                        Node subg_node) {
  bool subg_outp_is_scalar = !subgraph_outp_is_tuple(sub);

  // start query subgraph io and construct TGs for connecting inputs, TAs/scalar for connecting outputs
  for (const auto *io_pin : sub->get_io_pins()) {
    I(!io_pin->is_invalid());
    // I. io_pin is_input
    if (io_pin->is_input()) {
      std::vector<Node_pin>         created_tup_gets;
      std::vector<std::string_view> hier_inp_subnames;

      split_hier_name(io_pin->name, hier_inp_subnames);
      for (const auto &subname : hier_inp_subnames) {
        auto tup_get  = lg->create_node(Ntype_op::TupGet);
        auto pos_spin = tup_get.setup_sink_pin("position");  // key pos

        Node_pin tn_dpin;
        if (&subname == &hier_inp_subnames.front()) {
          tn_dpin = setup_tuple_ref(lg, arg_tup_name);
        } else {
          tn_dpin = created_tup_gets.back();
        }

        if (!tn_dpin.is_invalid()) {
          auto tn_spin  = tup_get.setup_sink_pin("tuple_name");
          tn_dpin.connect_sink(tn_spin);
        }

        auto pos_dpin = lg->create_node_const(Lconst(subname)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);

        // note: for scalar input, front() == back()
        if (&subname == &hier_inp_subnames.back()) {
          auto subg_spin = subg_node.setup_sink_pin(io_pin->name);
          tup_get.setup_driver_pin().connect_sink(subg_spin);
        }
        created_tup_gets.emplace_back(tup_get.get_driver_pin());
      }
      continue;
    }

    // II. io_pin is_output and is scalar
    if (subg_outp_is_scalar) {
      auto subg_dpin   = subg_node.setup_driver_pin(io_pin->name);
      auto scalar_node = lg->create_node(Ntype_op::Or);
      auto scalar_dpin = scalar_node.setup_driver_pin();
      subg_dpin.connect_sink(scalar_node.setup_sink_pin("A"));

      name2dpin[ret_name] = scalar_dpin;
      scalar_dpin.set_name(ret_name);
      auto pos       = ret_name.find_last_of('_');
      auto res_vname = ret_name.substr(0, pos);
      auto res_sub   = std::stoi(std::string(ret_name.substr(pos + 1)));

      if (!is_tmp_var(res_vname))
        setup_dpin_ssa(scalar_dpin, res_vname, 0);

      // note: the function call scalar return must be a "new_var_chain"
      if (vname2attr_dpin.find(res_vname) != vname2attr_dpin.end()) {
        auto aset_node          = lg->create_node(Ntype_op::AttrSet);
        auto aset_chain_spin    = aset_node.setup_sink_pin("chain");
        auto aset_ancestor_dpin = vname2attr_dpin[res_vname];
        lg->add_edge(aset_ancestor_dpin, aset_chain_spin);

        auto aset_vn_spin = aset_node.setup_sink_pin("name");
        auto aset_vn_dpin = scalar_dpin;
        lg->add_edge(aset_vn_dpin, aset_vn_spin);

        name2dpin[ret_name] = aset_node.setup_driver_pin("Y");  // dummy_attr_set node now represent the latest variable

        aset_node.get_driver_pin("Y").set_name(ret_name);
        if (!is_tmp_var(res_vname))
          setup_dpin_ssa(name2dpin[ret_name], res_vname, res_sub);
        vname2attr_dpin[res_vname] = aset_node.setup_driver_pin("chain");
      }
      continue;
    }

    // III. hier-subgraph-output
    std::vector<std::string_view> hier_out_subnames;
    split_hier_name(io_pin->name, hier_out_subnames);
    auto i = 0;
    for (const auto &subname : hier_out_subnames) {
      if (i == 0) {
        // handle the TA of function call return and the TA of first subname
        auto ta_ret      = lg->create_node(Ntype_op::TupAdd);
        auto ta_ret_dpin = ta_ret.setup_driver_pin();

        auto ta_ret_tn_dpin = setup_tuple_ref(lg, ret_name);
        if (!ta_ret_tn_dpin.is_invalid()) {
          ta_ret_tn_dpin.connect_sink(ta_ret.setup_sink_pin("tuple_name"));
        }

        auto ta_ret_field_dpin = lg->create_node_const(Lconst(subname)).setup_driver_pin();
        ta_ret_field_dpin.connect_sink(ta_ret.setup_sink_pin("position"));

        name2dpin[ret_name] = ta_ret_dpin;
        ta_ret_dpin.set_name(ret_name);

        if (hier_out_subnames.size() == 1) {
          auto subg_dpin = subg_node.setup_driver_pin(io_pin->name);
          subg_dpin.connect_sink(ta_ret.setup_sink_pin("value"));
          break;
        } else {
          I(hier_out_subnames.size() > 1);
          auto ta_subname      = lg->create_node(Ntype_op::TupAdd);
          auto ta_subname_dpin = ta_subname.setup_driver_pin();

          auto ta_subname_tn_dpin = setup_tuple_ref(lg, subname);
          if (!ta_subname_tn_dpin.is_invalid()) {
            ta_subname_tn_dpin.connect_sink(ta_subname.setup_sink_pin("tuple_name"));
          }
          // note: we don't know the field and value for ta_subname yet till next subname

          ta_subname_dpin.connect_sink(ta_ret.setup_sink_pin("value"));  // connect to parent value_dpin
          name2dpin[subname] = ta_subname_dpin;
          ta_subname_dpin.set_name(subname);
          i++;
        }
      } else if (i == (int)(hier_out_subnames.size() - 1)) {
        auto parent_field_dpin = lg->create_node_const(Lconst(subname)).setup_driver_pin();
        auto parent_subname    = hier_out_subnames[i - 1];
        auto ta_hier_parent    = name2dpin[parent_subname].get_node();
        parent_field_dpin.connect_sink(ta_hier_parent.setup_sink_pin("position"));
        auto subg_dpin = subg_node.setup_driver_pin(io_pin->name);
        subg_dpin.connect_sink(ta_hier_parent.setup_sink_pin("value"));

      } else {  // the middle ones, if any
        I(hier_out_subnames.size() >= 3);
        auto ta_subname        = lg->create_node(Ntype_op::TupAdd);
        auto parent_subname    = hier_out_subnames[i - 1];
        auto ta_hier_parent    = name2dpin[parent_subname].get_node();
        auto parent_field_dpin = lg->create_node_const(Lconst(subname)).setup_driver_pin();

        auto ta_subname_tn_dpin = setup_tuple_ref(lg, subname);
        if (!ta_subname_tn_dpin.is_invalid()) {
          ta_subname_tn_dpin.connect_sink(ta_subname.setup_sink_pin("tuple_name"));
        }
        // note: we don't know the field and value for ta_subname yet till next subname

        auto ta_subname_dpin = ta_subname.setup_driver_pin();
        ta_subname_dpin.connect_sink(ta_hier_parent.setup_sink_pin("value"));
        parent_field_dpin.connect_sink(ta_hier_parent.setup_sink_pin("position"));
        name2dpin[subname] = ta_subname_dpin;
        ta_subname_dpin.set_name(subname);
        i++;
      }
    }
  }
}

void Lnast_tolg::process_direct_op_connection(Lgraph *lg, const Lnast_nid &lnidx_fc) {
  Node fc_node;
  int  i = 0;
  for (const auto &child : lnast->children(lnidx_fc)) {
    if (i == 0) {  // lhs
      ++i;
      continue;
    }
    if (i == 1) {  // func_name
      auto fir_func_name = lnast->get_vname(child);

      fc_node = setup_node_opr_and_lhs(lg, lnidx_fc, fir_func_name);
      i++;
      continue;
    }

    // TODO: If the connection changes to Ntype cell, we can do:
    // out = __div(4,3)  should work too
    auto ref_dpin = setup_ref_node_dpin(lg, child);
    switch (i) {
      case 2: ref_dpin.connect_sink(fc_node.setup_sink_pin("e1")); break;
      case 3: ref_dpin.connect_sink(fc_node.setup_sink_pin("e2")); break;
      case 4: ref_dpin.connect_sink(fc_node.setup_sink_pin("e3")); break;
      default: I(false, "firrtl_primitive_op should have 3 input edges at most!");
    }
    i++;
  }
}

void Lnast_tolg::process_ast_func_call_op(Lgraph *lg, const Lnast_nid &lnidx_fc) {
  auto c0_fc     = lnast->get_first_child(lnidx_fc);
  auto func_name = lnast->get_vname(lnast->get_sibling_next(c0_fc));
  if (func_name.substr(0, 6) == "__fir_") {  // TODO: Can we do this generic, not FIRRTL specific?
    process_direct_op_connection(lg, lnidx_fc);
    return;
  }

  auto ret_name     = lnast->get_sname(c0_fc);
  auto arg_tup_name = lnast->get_sname(lnast->get_last_child(lnidx_fc));

#if 0
  std::string func_name = (std::string)func_name_tmp;
  if (lg->get_name().find("__firrtl_") != std::string::npos) {
    func_name = absl::StrCat("__firrtl_", func_name_tmp);
  }
#endif

  std::unique_lock<std::mutex> guard(lgs_mutex);
  auto *                       library = Graph_library::instance(path);
  if (name2dpin.find(func_name) == name2dpin.end()) {
#ifndef NDEBUG
    fmt::print("function {} defined in separated prp file, query lgdb\n", func_name);
#endif
    Node      subg_node;
    Sub_node *sub;
    if (library->has_name(func_name)) {
      auto lgid = library->get_lgid(func_name);
      subg_node = lg->create_node_sub(lgid);
      sub       = library->ref_sub(lgid);

    } else {
      subg_node = lg->create_node_sub(func_name);
      sub       = library->ref_sub(func_name);
    }
    guard.unlock();

    subg_node.set_name(absl::StrCat(arg_tup_name, ":", ret_name, ":", func_name));

    // just connect to $ and %, handle the rest at global io connection
    Node_pin subg_spin;
    Node_pin subg_dpin;
    if (sub->has_pin("$")) {  // sum.prp -> top.prp
      subg_spin = subg_node.setup_sink_pin("$");
    } else {  // top.prp -> sum.prp
      sub->add_input_pin("$", Port_invalid);
      subg_spin = subg_node.setup_sink_pin("$");
    }
    auto arg_tup_dpin = setup_tuple_ref(lg, arg_tup_name);
    I(!arg_tup_dpin.is_invalid()); // input is guaranteed
    arg_tup_dpin.connect_sink(subg_spin);

    if (sub->has_pin("%")) {
      subg_dpin = subg_node.setup_driver_pin("%");
    } else {
      sub->add_output_pin("%", Port_invalid);
      subg_dpin = subg_node.setup_driver_pin("%");
    }

    // create a TA assignment for the ret
    auto ta_ret      = lg->create_node(Ntype_op::TupAdd);
    auto ta_ret_dpin = ta_ret.setup_driver_pin();

    subg_dpin.connect_sink(ta_ret.setup_sink_pin("tuple_name"));
    name2dpin[ret_name] = ta_ret_dpin;
    ta_ret_dpin.set_name(ret_name);
    return;
  }

  // FIXME->sh: for the inlined function, we should also just connect to %/$ only?
  fmt::print("function {} defined in same prp file, query lgdb\n", func_name);
  auto ta_func_def = name2dpin[func_name].get_node();
  I(ta_func_def.get_type_op() == Ntype_op::TupAdd);
  I(ta_func_def.setup_sink_pin("value").get_driver_node().get_type_op() == Ntype_op::Const);
  Lg_type_id lgid = ta_func_def.setup_sink_pin("value").get_driver_node().get_type_const().to_i();

  auto  subg_node = lg->create_node_sub(lgid);
  auto *sub       = library->ref_sub(lgid);

  subg_node.set_name(absl::StrCat(ret_name, ":", func_name));
  subgraph_io_connection(lg, sub, arg_tup_name, ret_name, subg_node);
}

void Lnast_tolg::process_ast_func_def_op(Lgraph *lg, const Lnast_nid &lnidx) {
  auto       c0_fdef          = lnast->get_first_child(lnidx);
  auto       c1_fdef          = lnast->get_sibling_next(c0_fdef);
  auto       func_stmts       = lnast->get_sibling_next(c1_fdef);
  auto       func_name        = lnast->get_vname(c0_fdef);
  auto       subg_module_name = absl::StrCat(module_name, ":", func_name);
  Lnast_tolg p(subg_module_name, path);

  fmt::print("============================= Sub-module: LNAST->Lgraph Start ===============================================\n");
  p.do_tolg(lnast, func_stmts);
  fmt::print("============================= Sub-module: LNAST->Lgraph End ===============================================\n");

  auto tup_add    = lg->create_node(Ntype_op::TupAdd);
  auto pos_spin   = tup_add.setup_sink_pin("position");  // field name
  auto value_spin = tup_add.setup_sink_pin("value");

  auto field_dpin = lg->create_node_const(Lconst("__function_call")).setup_driver_pin();
  field_dpin.connect_sink(pos_spin);

  std::unique_lock<std::mutex> guard(lgs_mutex);
  auto *                       library = Graph_library::instance(path);
  Lg_type_id                   lgid;
  if (library->has_name(subg_module_name)) {
    lgid = library->get_lgid(subg_module_name);
  }
  guard.unlock();

  auto value_dpin = lg->create_node_const(Lconst(lgid)).setup_driver_pin();
  value_dpin.connect_sink(value_spin);

  name2dpin[func_name] = tup_add.setup_driver_pin();  // note: record only the function_name instead of top.function_name
  tup_add.setup_driver_pin().set_name(func_name);
};

void Lnast_tolg::process_ast_uif_op(Lgraph *lg, const Lnast_nid &lnidx) {
  (void)lg;
  (void)lnidx;
};

void Lnast_tolg::process_ast_for_op(Lgraph *lg, const Lnast_nid &lnidx) {
  (void)lg;
  (void)lnidx;
};

void Lnast_tolg::process_ast_while_op(Lgraph *lg, const Lnast_nid &lnidx) {
  (void)lg;
  (void)lnidx;
};

void Lnast_tolg::setup_lnast_to_lgraph_primitive_type_mapping() {
  // Logical handled in a separate step
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_and] = Ntype_op::And;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_not] = Ntype_op::Not;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_or]  = Ntype_op::Ror;

  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_reduce_or] = Ntype_op::Ror;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_assign]    = Ntype_op::Or;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_and]   = Ntype_op::And;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_or]    = Ntype_op::Or;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_not]   = Ntype_op::Not;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_xor]   = Ntype_op::Xor;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_plus]      = Ntype_op::Sum;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_minus]     = Ntype_op::Sum;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_mult]      = Ntype_op::Mult;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_div]       = Ntype_op::Div;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_eq]        = Ntype_op::EQ;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_lt]        = Ntype_op::LT;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_gt]        = Ntype_op::GT;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_sra]       = Ntype_op::SRA;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_shr]       = Ntype_op::SRA;  // FIXME: it should be sra(tposs(a),b)
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_shl]       = Ntype_op::SHL;

  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_sext] = Ntype_op::Sext;
  // FIXME->sh: to be extended ...
}

void Lnast_tolg::setup_scalar_reg_clkrst(Lgraph *lg, Node &reg_node) {
  Node_pin clk_dpin;
  if (!lg->has_graph_input("clock")) {
    clk_dpin = lg->add_graph_input("clock", Port_invalid, 1);
  } else {
    clk_dpin = lg->get_graph_input("clock");
  }

  auto clk_spin = reg_node.setup_sink_pin("clock");
  lg->add_edge(clk_dpin, clk_spin);

  /////////////////////////////////

  Node_pin rst_dpin;
  if (!lg->has_graph_input("reset")) {
    rst_dpin = lg->add_graph_input("reset", Port_invalid, 1);
  } else {
    rst_dpin = lg->get_graph_input("reset");
  }

  auto rst_spin = reg_node.setup_sink_pin("reset");
  lg->add_edge(rst_dpin, rst_spin);

}

void Lnast_tolg::setup_dpin_ssa(Node_pin &dpin, std::string_view var_name, uint16_t subs) {
  dpin.ref_ssa()->set_ssa(subs);
  dpin.set_prp_vname(var_name);
}

void Lnast_tolg::create_out_ta(Lgraph *lg, std::string_view field_name, Node_pin &val_dpin) {
  auto tup_add = lg->create_node(Ntype_op::TupAdd);
  auto tn_dpin = setup_tuple_ref(lg, "%");  // might come from TupRef or TupAdd
  if (!tn_dpin.is_invalid()) {
    auto tn_spin = tup_add.setup_sink_pin("tuple_name");
    tn_dpin.connect_sink(tn_spin);
  }

  auto pos_spin = tup_add.setup_sink_pin("position");

  std::string tup_name{"%"};
  if (!field_name.empty())
    absl::StrAppend(&tup_name, ".", field_name);

  auto pos_dpin = lg->create_node_const(Lconst(tup_name)).setup_driver_pin();
  pos_dpin.connect_sink(pos_spin);

  auto val_spin = tup_add.setup_sink_pin("value");
  val_dpin.connect_sink(val_spin);

  name2dpin["%"] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name("%");  // tuple ref semantically moves to here
}

void Lnast_tolg::setup_lgraph_ios_and_final_var_name(Lgraph *lg) {
  absl::flat_hash_map<std::string_view, Node_pin> vname2ssa_dpin;  // pyrope variable -> dpin with the largest ssa var subscription
  for (auto node : lg->forward()) {
    auto ntype = node.get_type_op();

    if (ntype == Ntype_op::Sub) {
      // TODO: can we rid of this to make it more generic?
      if (node.get_type_sub_node().get_name().substr(0, 9) == "__firrtl_")
        continue;
      if (node.get_type_sub_node().get_name().substr(0, 6) != "__fir_")
        continue;
    }

    auto dpin = node.get_driver_pin("Y");

    // connect hier-tuple-inputs and scalar input from the unified input $
    if (ntype == Ntype_op::TupRef && is_input(dpin.get_name())) {
      node.set_type(Ntype_op::TupGet);  // change node semantic: TupRef -> TupGet
      auto tn_spin = node.setup_sink_pin("tuple_name");
      auto tn_dpin = name2dpin["$"];
      tn_dpin.connect_sink(tn_spin);

      auto pos_spin = node.setup_sink_pin("position");
      auto pos_dpin = lg->create_node_const(Lconst(dpin.get_name().substr(1, dpin.get_name().size() - 1))).setup_driver_pin();
      pos_dpin.connect_sink(pos_spin);
      continue;
    }

    // collect vname table info
    if (dpin.has_ssa() && dpin.has_prp_vname()) {
      auto vname = dpin.get_prp_vname();
      auto subs  = dpin.ref_ssa()->get_subs();

      auto it = vname2ssa_dpin.find(vname);
      if (it == vname2ssa_dpin.end()) {
        vname2ssa_dpin.insert_or_assign(vname, dpin);
      } else if (subs >= it->second.get_ssa().get_subs()) {
        vname2ssa_dpin.insert_or_assign(vname, dpin);
      }
    }
  }

  // create scalar graph outputs or set the final variable name based on vname table
  for (const auto &[vname, dpin_largest_ssa] : vname2ssa_dpin) {
    if (is_output(vname)) {
      auto edible_dpin = dpin_largest_ssa;
      create_out_ta(lg, vname.substr(1), edible_dpin);  // don't pass first char % as the key name
      continue;
    }

    auto it = driver_var2wire_nodes.find(vname);
    if (it == driver_var2wire_nodes.end())
      continue;

    auto driver_ntype = dpin_largest_ssa.get_node().get_type_op();
    for (auto &wire_node : it->second) {
      Node_pin wire_spin;
      if (driver_ntype == Ntype_op::TupAdd) {
        if (wire_node.is_type(Ntype_op::Or)) {
          wire_node.set_type(Ntype_op::TupAdd);  // change wire_node type from Or_Op to dummy TupAdd_Op
          wire_spin = wire_node.setup_sink_pin("tuple_name");
        } else {
          I(wire_node.is_type(Ntype_op::Flop));
          wire_spin = wire_node.setup_sink_pin("din");
        }
      } else {
        I(wire_node != dpin_largest_ssa.get_node());
        if (wire_node.is_type(Ntype_op::Or)) {
          wire_spin = wire_node.setup_sink_pin("A");  // check
        } else {
          I(wire_node.is_type(Ntype_op::Flop));
          wire_spin = wire_node.setup_sink_pin("din");  // check
        }
      }
      dpin_largest_ssa.connect_sink(wire_spin);
    }
  }

  // FIXME->sh: could be deprecated if get rid of __q_pin
  // note: you have to wait the outputs have been connected to the unified output % TA so that the
  // attr_get.__q_pin has a real sink output pin (val_dpin of the TA %).
  // for (auto const &[vname, dpin_largest_ssa] : vname2ssa_dpin) {
  //   if (is_register(vname)) {
  //     setup_final_register(lg, vname, dpin_largest_ssa);
  //     continue;
  //   }
  // }

  // connect output tuple-chain to output pin "%"
  auto unified_out_dpin = name2dpin["%"];  // TA node
  auto unified_out_spin = lg->add_graph_output("%", Port_invalid, 0);
  unified_out_dpin.connect_sink(unified_out_spin);

  // try to create flattened inputs
  try_create_flattened_inp(lg);

  // connect graph inputs to leaf_artifact fanout
  for (auto &itr : inp2leaf_tg_spins) {
    Node_pin ginp = itr.first;
    for (auto &spin : itr.second) {
      ginp.connect_sink(spin);
    }
  }

  post_process_ginp_attr_connections(lg);
}

void Lnast_tolg::setup_final_register(Lgraph *lg, std::string_view vname, const Node_pin &dpin_largest_ssa) {
  if (tuple_reg_names.find(vname) != tuple_reg_names.end()) {
    // TODO: maybe connect the tuple-reg tail to the TG that is fetching un-existed tuple-reg chain
    return;
  }


  auto reg_node = lg->create_node(Ntype_op::Flop);
  auto reg_din  = reg_node.setup_sink_pin("din");
  dpin_largest_ssa.connect_sink(reg_din);
  setup_scalar_reg_clkrst(lg, reg_node);
  auto     smallest_ssa_name = absl::StrCat(vname, "_0");
  auto     node_smallest_ssa = name2dpin[smallest_ssa_name].get_node();
  Node_pin spin;
  auto     type = node_smallest_ssa.get_type_op();
  if (type == Ntype_op::AttrSet) {
    auto spin_name = node_smallest_ssa.setup_sink_pin("name");
    if (spin_name.is_connected()) {
      node_smallest_ssa = node_smallest_ssa.setup_sink_pin("name").get_driver_node();
      I(node_smallest_ssa.get_type_op() == Ntype_op::Or);
      spin = node_smallest_ssa.setup_sink_pin("A");
    } else {
      spin = node_smallest_ssa.setup_sink_pin("name");
    }
  } else if (type == Ntype_op::Or && !node_smallest_ssa.has_inputs()) {
    spin = node_smallest_ssa.setup_sink_pin("A");
  }

  auto reg_qpin = reg_node.setup_driver_pin();
  reg_qpin.set_name(smallest_ssa_name);
  name2dpin[smallest_ssa_name] = reg_qpin;

  if (spin != Node_pin())
    reg_qpin.connect_sink(spin);

  // if (reg_vname2attr_get_qpin_nodes.find(vname) != reg_vname2attr_get_qpin_nodes.end()) {
  //   for (auto attr_get_node : reg_vname2attr_get_qpin_nodes[vname]) {
  //     for (auto &e : attr_get_node.out_edges()) {
  //       reg_qpin.connect_sink(e.sink);
  //     }
  //     attr_get_node.del_node();
  //   }
  // }
}

void Lnast_tolg::post_process_ginp_attr_connections(Lgraph *lg) {
  // final process to reconnect ginp-> normal_node as ginp -> attr_set_node -> normal_node if any
  lg->each_graph_input([](Node_pin &ginp) {
    if (ginp.get_name() == "%")
      return;

    // identtify the attr_set node of this ginp if any
    Node_pin attr_set_dpin;

    for (auto &e : ginp.out_edges()) {
      auto sink_node  = e.sink.get_node();
      auto sink_ntype = sink_node.get_type_op();
      if (sink_ntype == Ntype_op::AttrSet && ginp.out_edges().size() == 1)
        return;

      if (sink_ntype == Ntype_op::AttrSet)
        attr_set_dpin = sink_node.setup_driver_pin("Y");
    }

    if (attr_set_dpin.is_invalid())
      return;

    for (auto &e : ginp.out_edges()) {
      auto sink_node  = e.sink.get_node();
      auto sink_ntype = sink_node.get_type_op();
      if (sink_ntype == Ntype_op::AttrSet)
        continue;

      attr_set_dpin.connect_sink(e.sink);
      e.del_edge();
    }
  });
}

void Lnast_tolg::try_create_flattened_inp(Lgraph *lg) {
  auto uinp = lg->get_graph_input("$");

  for (auto &e : uinp.out_edges()) {
    auto tg = e.sink.get_node();
    I(tg.get_type_op() == Ntype_op::TupGet);

    inp_artifacts[tg.get_compact()].insert(tg);  // insert the head of the chain

    I(tg.get_driver_pin().get_name().substr(0, 1) == "$");
    std::string hier_name{tg.get_driver_pin().get_name().substr(1)};  // get rid of "$" in "$foo"

    for (auto &tg_out : tg.out_edges()) {
      dfs_try_create_flattened_inp(lg, tg_out.sink, hier_name, tg);
    }

    for (auto itr : inp_artifacts[tg.get_compact()]) {
      if (!itr.is_invalid())
        itr.del_node();
    }
  }
}

void Lnast_tolg::handle_inp_tg_runtime_idx(std::string_view hier_name, Node &chain_head, Node &cur_tg) {
  // (1) iterate and remove previous TGs in the same chain
  for (auto itr : inp_artifacts[chain_head.get_compact()]) {
    if (!itr.is_invalid() && itr != cur_tg) {
      itr.del_node();
    }
  }

  // (2) erase the table
  inp_artifacts.erase(chain_head.get_compact());

  // (3) connect to the pre-constructed TA (constructed when hier-inputs bits are set)
  auto tn_spin = cur_tg.setup_sink_pin("tuple_name");
  auto tn_dpin = name2dpin[hier_name];
  tn_dpin.connect_sink(tn_spin);

  return;
}

void Lnast_tolg::create_ginp_as_runtime_idx(Lgraph *lg, std::string_view hier_name, Node &chain_head, Node &cur_tg) {
  // (1) iterate and remove previous TGs in the same chain
  for (auto itr : inp_artifacts[chain_head.get_compact()]) {
    if (!itr.is_invalid() && itr != cur_tg) {
      itr.del_node();
    }
  }

  // (2) erase the table
  inp_artifacts.erase(chain_head.get_compact());

  // (3) create graph_input and connect to cur_tg position sink pin
  Node_pin ginp;
  if (!lg->has_graph_input(hier_name))
    ginp = lg->add_graph_input(hier_name, Port_invalid, 0);
  else if (name2dpin.find(hier_name) != name2dpin.end())
    ginp = name2dpin[hier_name];
  else
    ginp = lg->get_graph_input(hier_name);

  auto pos_spin = cur_tg.setup_sink_pin("position");
  ginp.connect_sink(pos_spin);
  return;
}

void Lnast_tolg::dfs_try_create_flattened_inp(Lgraph *lg, Node_pin &cur_node_spin, std::string hier_name, Node &chain_head) {
  auto cur_node  = cur_node_spin.get_node();
  auto cur_ntype = cur_node.get_type_op();
  bool is_leaf   = false;

  if (cur_ntype == Ntype_op::TupGet && cur_node_spin == cur_node.setup_sink_pin("position")) {
    auto pos_spin = cur_node.setup_sink_pin("position");
    if (pos_spin.is_connected() && pos_spin.get_driver_node().get_type_op() == Ntype_op::TupGet) {
      create_ginp_as_runtime_idx(lg, hier_name, chain_head, cur_node);
      return;
    }
  } else if (cur_ntype == Ntype_op::AttrSet) {
    if (cur_node.is_sink_connected("field")) {
      auto field_txt = cur_node.get_sink_pin("field").get_driver_pin().get_type_const().to_string();
      if (!Lgtuple::is_root_attribute(field_txt)) {
        auto non_attr_field = Lgtuple::get_all_but_last_level(field_txt);
        absl::StrAppend(&hier_name, ".", non_attr_field);
      }
    }
    is_leaf = true;
  } else if (cur_ntype == Ntype_op::TupGet && cur_node_spin == cur_node.setup_sink_pin("tuple_name")) {
    auto pos_spin = cur_node.setup_sink_pin("position");
    if (pos_spin.is_connected() && pos_spin.get_driver_node().get_type_op() != Ntype_op::Const) {
      handle_inp_tg_runtime_idx(hier_name, chain_head, cur_node);
      return;
    }

    inp_artifacts[chain_head.get_compact()].insert(cur_node);  // only remove the artifact tup_gets

    // auto [tup_name, field_name] = Cprop::get_tuple_name_key(cur_node);
    std::string new_hier_name{hier_name};
    if (cur_node.is_sink_connected("position")) {
      auto field_node = cur_node.get_sink_pin("position").get_driver_node();
      if (field_node.is_type_const()) {
        auto field_name = field_node.get_type_const().to_string();
        new_hier_name   = absl::StrCat(hier_name, ".", field_name);
      }
    }
    for (auto &e : cur_node.out_edges()) {
      dfs_try_create_flattened_inp(lg, e.sink, new_hier_name, chain_head);
    }
  } else if (false && cur_ntype == Ntype_op::Or && cur_node.inp_edges().size() == 1) {
    for (auto &e : cur_node.out_edges()) {
      dfs_try_create_flattened_inp(lg, e.sink, hier_name, chain_head);
    }
  } else if (cur_ntype == Ntype_op::TupAdd && check_is_tup_assign(cur_node)) {
    inp_artifacts[chain_head.get_compact()].insert(cur_node);
    for (auto &e : cur_node.out_edges()) {
      dfs_try_create_flattened_inp(lg, e.sink, hier_name, chain_head);
    }
  } else {
    // normal operation and pure scalar attribute set
    is_leaf = true;
  }

  if (is_leaf) {
    Node_pin ginp;
    if (!lg->has_graph_input(hier_name))
      ginp = lg->add_graph_input(hier_name, Port_invalid, 0);
    else if (name2dpin.find(hier_name) != name2dpin.end())
      ginp = name2dpin[hier_name];
    else
      ginp = lg->get_graph_input(hier_name);

    inp2leaf_tg_spins[ginp].emplace_back(cur_node_spin);

    // if can reach leaf, the hierarchy is ended as a scalar, all path
    // assignments (either Or or TA) should be removed later
  }
}

void Lnast_tolg::dump() const {
  fmt::print("name2dpin:\n");
  for (auto &it : name2dpin) {
    fmt::print("  name:{} dpin:{}\n", it.first, it.second.debug_name());
  }
}
