// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lnast_tolg.hpp"

#include <cctype>
#include <format>
#include <iostream>

#include "absl/strings/match.h"
#include "cprop.hpp"
#include "pass.hpp"
#include "perf_tracing.hpp"

Lnast_tolg::Lnast_tolg(std::string_view _module_name, std::string_view _path) : module_name(_module_name), path(_path) {
  setup_lnast_to_lgraph_primitive_type_mapping();
  tuple_assign_str = "tuple_assign";
}

std::vector<Lgraph *> Lnast_tolg::do_tolg(const std::shared_ptr<Lnast> &ln, const Lnast_nid &top_stmts) {
  TRACE_EVENT("pass", nullptr, [&ln](perfetto::EventContext ctx) {
    std::string converted_str{(char)('A' + (trace_module_cnt++ % 25))};
    ctx.event()->set_name(converted_str + std::string{ln->get_top_module_name()});
  });

  lnast    = ln;
  auto src = ln->get_source();
  if (src.empty()) {
    src = "-";
  }

  std::vector<Lgraph *> lgs;

  auto *lib = Graph_library::instance(path);
  if (lib == nullptr) {
    Pass::error("lnast_tolg: unable to open graph_library {}", path);
    return lgs;
  }
  auto *lg = lib->create_lgraph(module_name, src);

  name2dpin["$"] = lg->get_graph_input("$");
  I(!lg->get_graph_input("$").is_invalid());
  I(!lg->get_graph_output("%").is_invalid());

  top_stmts2lgraph(lg, top_stmts);
  lgs.emplace_back(lg);
  return lgs;
}

void Lnast_tolg::top_stmts2lgraph(Lgraph *lg, const Lnast_nid &lnidx_stmts) {
  process_ast_stmts(lg, lnidx_stmts);
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
    } else if (ntype.is_ne() || ntype.is_ge() || ntype.is_le()) {
      process_ast_nary_op_one2n_map(lg, lnidx);
    } else if (ntype.is_direct_lgraph_op()) {
      process_ast_nary_op_direct_map(lg, lnidx);
    } else if (ntype.is_attr_set()) {
      process_ast_attr_set_op(lg, lnidx);
    } else if (ntype.is_attr_get()) {
      process_ast_attr_get_op(lg, lnidx);
    } else if (ntype.is_tuple_add() || ntype.is_tuple_set()) {
      process_ast_tuple_add_op(lg, lnidx);
    } else if (ntype.is_tuple_get()) {
      process_ast_tuple_get_op(lg, lnidx);
    } else if (ntype.is_if()) {
      process_ast_if_op(lg, lnidx);
    } else if (ntype.is_func_call()) {
      process_ast_func_call_op(lg, lnidx);
    } else if (ntype.is_func_def()) {
      process_ast_func_def_op(lg, lnidx);
    } else if (ntype.is_tuple_concat()) {
      process_ast_concat_op(lg, lnidx);
    } else if (ntype.is_for()) {
      process_ast_for_op(lg, lnidx);
    } else if (ntype.is_while()) {
      process_ast_while_op(lg, lnidx);
    } else if (ntype.is_uif()) {
      process_ast_uif_op(lg, lnidx);
    } else if (ntype.is_invalid()) {
      continue;
    } else if (ntype.is_const()) {
      I(lnast->get_name(lnidx) == "err_var_undefined");
      continue;
    } else if (ntype.is_err_flag()) {
      I(lnast->get_name(lnidx) == "err_var_undefined");
      continue;
    } else {
      lnast->dump();
      std::print("FIXME: implement op:{}\n", ntype.debug_name());
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
    } else if (ntype.is_tuple_add() || ntype.is_tuple_set()) {
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
  auto        phi_node   = lg->create_node(Ntype_op::Mux);
  auto        cond_spin  = phi_node.setup_sink_pin("0");  // Y = ~S&"1" + S&"2"
  auto        true_spin  = phi_node.setup_sink_pin("2");
  auto        false_spin = phi_node.setup_sink_pin("1");
  const auto &tok        = lnast->get_token(lnidx_phi);
  phi_node.set_loc(tok.pos1, tok.pos2);
  phi_node.set_source(tok.fname);
  // std::print("Hello!! {} {} {}",pos1, pos2, phi_node.get_nid()  );

  auto lhs       = lnast->get_first_child(lnidx_phi);
  auto c1        = lnast->get_sibling_next(lhs);
  auto c2        = lnast->get_sibling_next(c1);
  auto c3        = lnast->get_sibling_next(c2);
  auto lhs_sname = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  auto cond_dpin_pre = setup_ref_node_dpin(lg, c1);

  auto        ror_node = lg->create_node(Ntype_op::Ror);
  const auto &tok1     = lnast->get_token(lnidx_phi);
  ror_node.set_loc(tok1.pos1, tok1.pos2);
  ror_node.set_source(tok1.fname);
  cond_dpin_pre.connect_sink(ror_node.setup_sink_pin("A"));
  auto cond_dpin = ror_node.setup_driver_pin();

  Node_pin true_dpin  = setup_ref_node_dpin(lg, c2, false, true);
  Node_pin false_dpin = setup_ref_node_dpin(lg, c3, false, true);

  I(!true_dpin.is_invalid());
  I(!false_dpin.is_invalid());

  lg->add_edge(cond_dpin, cond_spin);
  lg->add_edge(true_dpin, true_spin);
  lg->add_edge(false_dpin, false_spin);

  name2dpin[lhs_sname] = phi_node.setup_driver_pin();
  phi_node.setup_driver_pin().set_name(lhs_sname);

  if (!is_tmp_var(lhs_vname)) {
    setup_dpin_ssa(name2dpin[lhs_sname], lhs_vname, lnast->get_subs(lhs));
  }
}

void Lnast_tolg::process_ast_concat_op(Lgraph *lg, const Lnast_nid &lnidx_concat) {
  auto lhs       = lnast->get_first_child(lnidx_concat);  // c0: target tuple name for concat.
  auto opd1      = lnast->get_sibling_next(lhs);          // c1: tuple operand1, either scalar or tuple
  auto opd2      = lnast->get_sibling_next(opd1);         // c2: tuple operand2, either scalar or tuple
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);
  auto opd1_name = lnast->get_sname(opd1);
  // lhs = opd1 ++ opd2, both opd1 and opd2 could be either a scalar or a tuple

  // create TupAdd, concat both tail of opd1 and opd2, name it with old opd1_name (a = a ++ b) or new lhs_name (c = a ++ b)
  auto        tup_add = lg->create_node(Ntype_op::TupAdd);
  const auto &tok     = lnast->get_token(lnidx_concat);
  tup_add.set_loc(tok.pos1, tok.pos2);
  tup_add.set_source(tok.fname);
  auto tup_add_dpin = tup_add.setup_driver_pin();
  auto tn_spin      = tup_add.setup_sink_pin("parent");  // tuple name
  auto value_spin   = tup_add.setup_sink_pin("value");   // key->value

  auto tn_dpin = setup_ref_node_dpin(lg, opd1);
  lg->add_edge(tn_dpin, tn_spin);

  auto value_dpin = setup_ref_node_dpin(lg, opd2);
  lg->add_edge(value_dpin, value_spin);

  if (lhs_name == opd1_name) {
    name2dpin[opd1_name] = tup_add_dpin;
    tup_add_dpin.set_name(opd1_name);
  } else {
    name2dpin[lhs_name] = tup_add_dpin;
    tup_add_dpin.set_name(lhs_name);
  }

  if (!is_tmp_var(lhs_vname)) {
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
  }
}

void Lnast_tolg::process_ast_nary_op_one2n_map(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  I(lnast->get_type(lnidx_opr).is_ne() || lnast->get_type(lnidx_opr).is_ge() || lnast->get_type(lnidx_opr).is_le());
  auto exit_node = setup_node_opr_and_lhs(lg, lnidx_opr, "");
  auto opr_node  = exit_node.setup_sink_pin("a").get_driver_node();

  std::vector<Node_pin> opds;
  for (const auto &child : lnast->children(lnidx_opr)) {
    if (child == lnast->get_first_child(lnidx_opr)) {
      continue;  // the lhs has been handled at setup_node_opr_and_lhs();
    }

    auto opd = setup_ref_node_dpin(lg, child);
    if (opd.is_invalid()) {
      Pass::error("for operator node {}, undefined variable {} is used!\n", opr_node.debug_name(), lnast->get_sname(child));
    }

    opds.emplace_back(opd);
  }
  nary_node_rhs_connections(lg, opr_node, opds, lnast->get_type(lnidx_opr).is_minus());
}

void Lnast_tolg::process_ast_nary_op_direct_map(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  std::vector<Node_pin> opds;
  for (const auto &child : lnast->children(lnidx_opr)) {
    if (child == lnast->get_first_child(lnidx_opr)) {
      continue;  // the lhs has been handled at setup_node_opr_and_lhs();
    }

    auto opd = setup_ref_node_dpin(lg, child);
    if (opd.is_invalid()) {
      Pass::error("for operator node undefined variable {} is used!\n", lnast->get_sname(child));
    }

    opds.emplace_back(opd);
  }

  auto opr_node = setup_node_opr_and_lhs(lg, lnidx_opr, "");
  nary_node_rhs_connections(lg, opr_node, opds, lnast->get_type(lnidx_opr).is_minus());
}

void Lnast_tolg::process_ast_logical_op(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  // (1) create logical operator node and record the dpin to symbol table
  // (2) create comparator node and compare with 0 for each of the inputs
  // (3) take the result of every comparator as the inputs of logical operator inputs

  auto                  opr_node = setup_node_opr_and_lhs(lg, lnidx_opr, "");
  std::vector<Node_pin> eqs_dpins;
  for (const auto &opr_child : lnast->children(lnidx_opr)) {
    if (opr_child == lnast->get_first_child(lnidx_opr)) {
      continue;  // the lhs has been handled at setup_node_opr_and_lhs();
    }

    // TODO: A simple Ror is faster/better
    auto        node_eq = lg->create_node(Ntype_op::EQ);
    const auto &tok1    = lnast->get_token(lnidx_opr);
    node_eq.set_loc(tok1.pos1, tok1.pos2);
    node_eq.set_source(tok1.fname);
    auto        node_not = lg->create_node(Ntype_op::Not);
    const auto &tok2     = lnast->get_token(lnidx_opr);
    node_not.set_loc(tok2.pos1, tok2.pos2);
    node_not.set_source(tok2.fname);
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
      for (const auto &opd : opds) {
        lg->add_edge(opd, opr_node.setup_sink_pin("A"));
      }
    } break;
    case Ntype_op::Get_mask: {
      I(opds.size() > 0);
      opr_node.setup_sink_pin("a").connect_driver(opds[0]);
      if (opds.size() == 1) {
        opr_node.setup_sink_pin("mask").connect_driver(opr_node.create_const(-1));
      } else {
        I(opds.size() == 2);
        opr_node.setup_sink_pin("mask").connect_driver(opds[1]);
      }
    } break;
    case Ntype_op::Set_mask: {
      I(opds.size() == 3);
      opr_node.setup_sink_pin("a").connect_driver(opds[0]);
      opr_node.setup_sink_pin("mask").connect_driver(opds[1]);
      opr_node.setup_sink_pin("value").connect_driver(opds[2]);
    } break;
    case Ntype_op::Div:
    case Ntype_op::Sext:
    case Ntype_op::SRA: {
      I(opds.size() == 2);  // val<<amount
      lg->add_edge(opds[0], opr_node.setup_sink_pin("a"));
      lg->add_edge(opds[1], opr_node.setup_sink_pin("b"));
    } break;
    case Ntype_op::SHL: {
      I(opds.size() == 2);  // val<<amount
      lg->add_edge(opds[0], opr_node.setup_sink_pin("a"));
      lg->add_edge(opds[1], opr_node.setup_sink_pin("B"));
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
  auto opd1_ntype = opd1.get_type_op();

  Node_pin opr_spin;
  auto     cond1 = is_tup_asg;
  auto     cond2 = opd1_ntype == Ntype_op::TupAdd;
  auto     cond3 = opd1.has_name() && is_input(opd1.get_name());

  if (cond1 || cond2 || cond3) {
    opr_spin = setup_tuple_assignment(lg, lnidx_assign);
  } else {  // including condition of "opd1_ntype == Ntype_op::AttrSet"
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
  if (name2dpin.find(rhs_dp_name) == name2dpin.end()) {
    process_ast_assign_op(lg, lnidx_dp_assign);
    return;
  }

  auto c0_dp_name  = lnast->get_sname(c0_dp);  // ssa name
  auto c0_dp_vname = lnast->get_vname(c0_dp);  // no-ssa name
  auto attr_vname  = std::string("__dp_assign");

  auto        aset_node = lg->create_node(Ntype_op::AttrSet);
  const auto &tok2      = lnast->get_token(lnidx_dp_assign);
  aset_node.set_loc(tok2.pos1, tok2.pos2);
  aset_node.set_source(tok2.fname);
  auto vn_spin = aset_node.setup_sink_pin("parent");  // variable name (lhs)
  auto af_spin = aset_node.setup_sink_pin("field");   // attribute field
  auto av_spin = aset_node.setup_sink_pin("value");   // attribute value (rhs)

  auto af_dpin = setup_field_dpin(lg, attr_vname);
  lg->add_edge(af_dpin, af_spin);

  auto vn_dpin = setup_ref_node_dpin(lg, rhs_dp);
  lg->add_edge(vn_dpin, vn_spin);

  auto av_dpin = setup_ref_node_dpin(lg, lhs_dp);
  lg->add_edge(av_dpin, av_spin);

  auto aset_node_dpin = aset_node.setup_driver_pin();
  aset_node_dpin.set_name(c0_dp_name);
  name2dpin[c0_dp_name] = aset_node_dpin;
  if (!is_tmp_var(c0_dp_vname)) {
    setup_dpin_ssa(name2dpin[c0_dp_name], c0_dp_vname, lnast->get_subs(c0_dp));
  }
}

void Lnast_tolg::process_ast_tuple_struct(Lgraph *lg, const Lnast_nid &lnidx_tup) {
  auto c0_tup    = lnast->get_first_child(lnidx_tup);
  auto tup_name  = lnast->get_sname(c0_tup);
  auto tup_vname = lnast->get_vname(c0_tup);
  auto subs      = lnast->get_subs(c0_tup);

  auto c1_tup = lnast->get_sibling_next(c0_tup);
  if (c1_tup.is_invalid()) {
    // Tuple can be empty
    // 2                             tuple :
    // 3                                   ref : ___d
    // 2                          get_mask :
    // 3                                   ref : ___e
    // 3                                   ref : index
    // 3                                   ref : ___d

    auto tup_add_dpin = lg->create_node(Ntype_op::TupAdd).setup_driver_pin();
    // tup_add_dpin.set_loc(std::make_pair( (lnast->get_token(lnidx_tup)).pos1 , (lnast->get_token(lnidx_tup)).pos2 ) );
    name2dpin[tup_name] = tup_add_dpin;
    tup_add_dpin.set_name(tup_name);
    if (!is_tmp_var(tup_vname)) {
      setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);
    }

    return;
  }

  auto     c1_tup_vname = lnast->get_vname(c1_tup);
  uint16_t fp           = 0;

  for (auto tup_child = c1_tup; !tup_child.is_invalid(); tup_child = lnast->get_sibling_next(tup_child)) {
    I(tup_child != lnast->get_first_child(lnidx_tup));

    auto type = lnast->get_type(tup_child);
    if (type.is_invalid()) {
      continue;
    }

    // the cases with key name well-defined
    if (type.is_assign()) {
      auto c0         = lnast->get_first_child(tup_child);
      auto c1         = lnast->get_sibling_next(c0);
      auto field_name = lnast->get_vname(c0);

      Node_pin tn_dpin;
      if (fp == 0) {
        tn_dpin = setup_ta_ref_previous_ssa(tup_vname, subs);
      } else {
        tn_dpin = setup_tuple_ref(lg, tup_name);
      }
      Lconst pos_const;
      if (field_name.substr(0, 4) != "null") {
        // pos_const = Lconst::from_pyrope(absl::StrCat(":", std::string(fp), ":", field_name));
        pos_const = Lconst::from_pyrope(absl::StrCat(":", fp, ":", field_name));
      } else {
        pos_const = Lconst(fp);
      }

      auto fp_dnode       = lg->create_node_const(pos_const);
      auto field_pos_dpin = fp_dnode.setup_driver_pin();
      auto value_dpin     = setup_ref_node_dpin(lg, c1);

      auto        tup_add = lg->create_node(Ntype_op::TupAdd);  //<--try pos1,pos2 here
      const auto &tok2    = lnast->get_token(lnidx_tup);
      tup_add.set_loc(tok2.pos1, tok2.pos2);
      tup_add.set_source(tok2.fname);
      auto field_pos_spin = tup_add.setup_sink_pin("field");  // key field is unknown before tuple resolving
      auto value_spin     = tup_add.setup_sink_pin("value");  // value

      if (!tn_dpin.is_invalid()) {
        auto tn_spin = tup_add.setup_sink_pin("parent");  // tuple name
        lg->add_edge(tn_dpin, tn_spin);
      }
      lg->add_edge(field_pos_dpin, field_pos_spin);
      lg->add_edge(value_dpin, value_spin);

      auto tup_add_dpin   = tup_add.setup_driver_pin();
      name2dpin[tup_name] = tup_add_dpin;
      tup_add_dpin.set_name(tup_name);

      if (!is_tmp_var(tup_vname)) {
        setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);
      }

      fp++;
      continue;
    }

    if (is_err_var_undefined(c1_tup_vname)) {
      setup_tuple_ref(lg, tup_name);
      return;
    }

    auto fp_dnode       = lg->create_node_const(Lconst(fp));
    auto field_pos_dpin = fp_dnode.setup_driver_pin();
    auto value_dpin     = setup_ref_node_dpin(lg, tup_child);

    auto        tup_add = lg->create_node(Ntype_op::TupAdd);
    const auto &tok2    = lnast->get_token(lnidx_tup);
    tup_add.set_loc(tok2.pos1, tok2.pos2);
    tup_add.set_source(tok2.fname);
    auto field_pos_spin = tup_add.setup_sink_pin("field");  // field field is unknown before tuple resolving
    auto value_spin     = tup_add.setup_sink_pin("value");  // value

    auto tn_dpin = setup_tuple_ref(lg, tup_name);
    if (!tn_dpin.is_invalid()) {
      auto tn_spin = tup_add.setup_sink_pin("parent");  // tuple name
      lg->add_edge(tn_dpin, tn_spin);
    }
    lg->add_edge(field_pos_dpin, field_pos_spin);
    lg->add_edge(value_dpin, value_spin);

    auto tup_add_dpin   = tup_add.setup_driver_pin();
    name2dpin[tup_name] = tup_add_dpin;
    tup_add_dpin.set_name(tup_name);

    if (!is_tmp_var(tup_vname)) {
      setup_dpin_ssa(name2dpin[tup_name], tup_vname, subs);
    }
    fp++;
  }
}

Node_pin Lnast_tolg::create_inp_tg(Lgraph *lg, std::string_view input_field) {
  auto tup_get_inp = lg->create_node(Ntype_op::TupGet);
  // FIXME: what is the lnidx_* for this?
  // tup_get_inp.set_loc(std::make_pair( (lnast->get_token(lnidx_*)).pos1 , (lnast->get_token(lnidx_*)).pos2 ) );
  auto tn_spin = tup_get_inp.setup_sink_pin("parent");
  auto tn_dpin = name2dpin["$"];
  tn_dpin.connect_sink(tn_spin);

  auto pos_spin = tup_get_inp.setup_sink_pin("field");
  I(input_field.front() == '$');
  auto subname = Lgtuple::get_all_but_first_level(input_field);

  auto pos_dpin = lg->create_node_const(Lconst::from_pyrope(subname)).setup_driver_pin();
  lg->add_edge(pos_dpin, pos_spin);

  auto tg_dpin = tup_get_inp.setup_driver_pin();
  tg_dpin.set_name(input_field);

  name2dpin[input_field] = tg_dpin;
  return tg_dpin;
}

void Lnast_tolg::process_ast_tuple_get_op(Lgraph *lg, const Lnast_nid &lnidx_tg) {
  absl::flat_hash_map<int, Node> tg_map;
  int                            i = 0;
  std::string                    c0_tg_name;
  std::string                    c0_tg_vname;
  std::string                    c1_tg_name;
  std::string                    cn_tg_name;
  auto                           c0_tg_subs = 0;

  for (const auto &child : lnast->children(lnidx_tg)) {
    if (i == 0) {
      const auto &c0_tg = child;
      c0_tg_name        = lnast->get_sname(c0_tg);
      c0_tg_vname       = lnast->get_vname(c0_tg);
      c0_tg_subs        = lnast->get_subs(c0_tg);
      i++;
      continue;
    } else if (i == 1) {
      const auto &c1_tg   = child;
      c1_tg_name          = lnast->get_sname(c1_tg);
      auto        tup_get = lg->create_node(Ntype_op::TupGet);
      const auto &tok2    = lnast->get_token(lnidx_tg);
      tup_get.set_loc(tok2.pos1, tok2.pos2);
      tup_get.set_source(tok2.fname);
      tg_map.insert_or_assign(i, tup_get);

      Node_pin tn_dpin;
      if (is_input(c1_tg_name)) {
        tn_dpin = create_inp_tg(lg, lnast->get_vname(c1_tg));
      } else {
        tn_dpin = setup_tuple_ref(lg, c1_tg_name);
      }

      I(!tn_dpin.is_invalid());
      auto tn_spin = tup_get.setup_sink_pin("parent");
      lg->add_edge(tn_dpin, tn_spin);
      i++;
      continue;
    } else if (child == lnast->get_last_child(lnidx_tg)) {
      // i >= 2
      const auto &cn_tg = child;
      cn_tg_name        = lnast->get_vname(cn_tg);
      auto tup_get      = tg_map[i - 1];
      auto pos_spin     = tup_get.setup_sink_pin("field");
      auto lntype       = lnast->get_type(cn_tg);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, cn_tg);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst::from_pyrope(cn_tg_name)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);
      }

      auto tg_dpin          = tup_get.setup_driver_pin();
      name2dpin[c0_tg_name] = tg_dpin;
      tg_dpin.set_name(c0_tg_name);
      if (!is_tmp_var(c0_tg_vname)) {
        setup_dpin_ssa(name2dpin[c0_tg_name], c0_tg_vname, c0_tg_subs);
      }

    } else {  // not the last child
      auto        new_tup_get = lg->create_node(Ntype_op::TupGet);
      const auto &tok2        = lnast->get_token(lnidx_tg);
      new_tup_get.set_loc(tok2.pos1, tok2.pos2);
      new_tup_get.set_source(tok2.fname);
      tg_map.insert_or_assign(i, new_tup_get);
      auto tn_spin = new_tup_get.setup_sink_pin("parent");

      const auto &cn_tg = child;
      cn_tg_name        = lnast->get_vname(cn_tg);
      auto prev_tup_get = tg_map[i - 1];
      auto pos_spin     = prev_tup_get.setup_sink_pin("field");
      auto lntype       = lnast->get_type(cn_tg);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, cn_tg);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst::from_pyrope(cn_tg_name)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);
      }

      auto tn_dpin = prev_tup_get.setup_driver_pin();
      lg->add_edge(tn_dpin, tn_spin);

      i++;
      continue;
    }
  }
}

bool Lnast_tolg::is_tuple_struct_ta(const Lnast_nid &lnidx_ta) {
  auto c0_name = lnast->get_vname(lnast->get_first_child(lnidx_ta));
  if (c0_name.substr(0, 3) != "___") {
    return false;
  }

  return true;
}

bool Lnast_tolg::is_hier_inp_bits_set(const Lnast_nid &lnidx_ta) {
  for (const auto &child : lnast->children(lnidx_ta)) {
    if (child == lnast->get_first_child(lnidx_ta)) {
      if (is_input(lnast->get_vname(child))) {
        continue;
      }
      return false;
    }
    if (lnast->get_vname(child) == "__ubits" || lnast->get_vname(child) == "__sbits") {
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
  std::string root_inp_sname;

  Port_ID io_pos = Port_invalid;
  for (const auto &child : lnast->children(lnidx_ta)) {
    if (child == lnast->get_first_child(lnidx_ta)) {
      const auto &c0_ta = child;
      I(is_input(lnast->get_vname(c0_ta)));
      std::tie(io_pos, full_inp_hier_name) = Lgtuple::convert_key_to_io(lnast->get_vname(c0_ta));
      root_inp_sname                       = Lgtuple::convert_key_to_io(lnast->get_sname(c0_ta)).second;

    } else if (lnast->get_vname(child) != "__ubits" && lnast->get_vname(child) != "__sbits") {
      I(child != lnast->get_last_child(lnidx_ta));
      absl::StrAppend(&full_inp_hier_name, ".", lnast->get_vname(child));

    } else if (lnast->get_vname(child) == "__ubits" || lnast->get_vname(child) == "__sbits") {  // at the __bits child
      //(1) create flattened input
      std::string bits_str = std::string{lnast->get_vname(lnast->get_sibling_next(child))};
      auto        bits     = std::stoi(bits_str);
      if (lnast->get_vname(child) == "__ubits") {
        bits++;  // WARNING: in lgraph/lnast all the bit sizes assume signed. So 1 bit unsigned is 2 bits signed
      }

      Node_pin flattened_inp;
      if (!lg->has_graph_input(full_inp_hier_name)) {
        flattened_inp = lg->add_graph_input(full_inp_hier_name, io_pos, bits);
      } else {
        I(name2dpin.find(full_inp_hier_name) != name2dpin.end());
        flattened_inp = name2dpin[full_inp_hier_name];
        flattened_inp.set_bits(bits);
      }
      if (lnast->get_vname(child) == "__ubits") {
        flattened_inp.set_unsign();
      }

      // Create pos and value before TupAdd to preserve topographical order
      auto af_dpin = setup_field_dpin(lg, lnast->get_vname(child));

      auto const_lnidx = lnast->get_sibling_next(child);
      auto av_dpin     = setup_ref_node_dpin(lg, const_lnidx);

      auto        aset_node = lg->create_node(Ntype_op::TupAdd);
      const auto &tok2      = lnast->get_token(lnidx_ta);
      aset_node.set_loc(tok2.pos1, tok2.pos2);
      aset_node.set_source(tok2.fname);
      auto vn_spin = aset_node.setup_sink_pin("parent");  // variable name
      auto af_spin = aset_node.setup_sink_pin("field");   // attribute field
      auto av_spin = aset_node.setup_sink_pin("value");   // attribute value

      flattened_inp.connect_sink(vn_spin);
      af_dpin.connect_sink(af_spin);

      av_dpin.connect(av_spin);
      auto aset_dpin            = aset_node.setup_driver_pin();
      name2dpin[root_inp_sname] = aset_dpin;
      create_inp_ta4runtime_idx(lg, aset_dpin, root_inp_sname);  // full_inp_hier_name);
      break;                                                     // no need to iterate to last child
    }
  }
}

void Lnast_tolg::create_inp_ta4runtime_idx(Lgraph *lg, const Node_pin &val_dpin, std::string_view full_inp_hier_name) {
  auto pos = full_inp_hier_name.rfind('.');
  if (pos == std::string::npos) {
    return;
  }
  auto last_subname = full_inp_hier_name.substr(pos + 1);

  // if the last subname is not a number(not a constant), means the tuple is not array-like, it's impossilbe
  // future graph-inp will try to get the array-element from a dynamic-runtime index, so we don't need to construct the inp_ta
  if (!str_tools::is_i(last_subname.substr(0, 1))) {
    return;
  }

  auto tup_name  = full_inp_hier_name.substr(0, pos);
  auto name_dpin = setup_tuple_ref(lg, tup_name);
  auto pos_dpin  = lg->create_node_const(Lconst::from_pyrope(last_subname)).setup_driver_pin();

  auto ta_node = lg->create_node(Ntype_op::TupAdd);
  // FIXME: location for this?
  auto pos_spin = ta_node.setup_sink_pin("field");
  auto val_spin = ta_node.setup_sink_pin("value");

  if (!name_dpin.is_invalid()) {
    auto name_spin = ta_node.setup_sink_pin("parent");
    name_dpin.connect_sink(name_spin);
  }

  pos_dpin.connect_sink(pos_spin);
  val_dpin.connect(val_spin);
  auto ta_dpin = ta_node.setup_driver_pin();
  ta_dpin.set_name(tup_name);
  name2dpin[tup_name] = ta_dpin;
}

void Lnast_tolg::process_ast_tuple_add_op(Lgraph *lg, const Lnast_nid &lnidx_ta) {
  // FIXME: This code breaks the topographical order in the Lgraph. It creates
  // first the TupAdd and then the constants for it. It will create more
  // efficient Lgraphs if the constants/inputs are created first, and then the
  // TupAdd is created

  if (is_hier_inp_bits_set(lnidx_ta)) {
    process_hier_inp_bits_set(lg, lnidx_ta);
    return;
  }

  if (is_tuple_struct_ta(lnidx_ta)) {
    process_ast_tuple_struct(lg, lnidx_ta);
    return;
  }

  // empty ta, handle as tuple_struct
  if (lnast->get_first_child(lnidx_ta) == lnast->get_last_child(lnidx_ta)) {
    process_ast_tuple_struct(lg, lnidx_ta);
    return;
  }

  bool        is_all_constant_key_ta = true;
  int         k                      = 0;
  std::string concatenated_field_str;
  Node_pin    val_dpin;

  for (const auto &child : lnast->children(lnidx_ta)) {
    if (k == 0) {
      k++;
      continue;
    } else if (child == lnast->get_last_child(lnidx_ta)) {
      val_dpin = setup_ref_node_dpin(lg, child, true);
      I(!val_dpin.is_invalid());
    } else {
      if (lnast->get_type(child).is_ref()) {
        is_all_constant_key_ta = false;
        break;
      }
      auto cn_name = lnast->get_vname(child);
      if (concatenated_field_str.empty()) {
        concatenated_field_str = cn_name;
      } else {
        absl::StrAppend(&concatenated_field_str, ".", cn_name);
      }

      k++;
      continue;
    }
  }

  if (is_all_constant_key_ta) {
    int j = 0;
    for (const auto &child : lnast->children(lnidx_ta)) {
      if (j == 0) {
        const auto &c0_ta       = child;
        auto        tuple_sname = lnast->get_sname(c0_ta);
        auto        tuple_vname = lnast->get_vname(c0_ta);
        auto        subs        = lnast->get_subs(c0_ta);
        auto        tup_add     = lg->create_node(Ntype_op::TupAdd);
        const auto &tok1        = lnast->get_token(lnidx_ta);
        tup_add.set_loc(tok1.pos1, tok1.pos2);
        tup_add.set_source(tok1.fname);

        auto pos_spin = tup_add.setup_sink_pin("field");
        // from_pyrope("0.__ubits");
        // from_string("0.__ubits");
        // pyrope string cannot differentiate "0.__ubits" and "0bxxx", you need need to make it from_string for Lconst
        Node_pin pos_dpin;
        // if (concatenated_field_str.find('.') != std::string::npos) {
        if (absl::StrContains(concatenated_field_str, '.')) {
          pos_dpin = lg->create_node_const(Lconst::from_string(concatenated_field_str)).setup_driver_pin();
        } else {
          pos_dpin = lg->create_node_const(Lconst::from_pyrope(concatenated_field_str)).setup_driver_pin();
        }
        pos_dpin.connect_sink(pos_spin);

        auto val_spin = tup_add.setup_sink_pin("value");
        val_dpin.connect_sink(val_spin);

        auto tn_dpin = setup_ta_ref_previous_ssa(tuple_vname, subs);
        if (!tn_dpin.is_invalid()) {
          auto tn_spin = tup_add.setup_sink_pin("parent");
          tn_dpin.connect_sink(tn_spin);
        }

        auto ta_dpin = tup_add.setup_driver_pin();
        name2dpin.insert_or_assign(tuple_sname, ta_dpin);
        ta_dpin.set_name(tuple_sname);
        setup_dpin_ssa(name2dpin[tuple_sname], lnast->get_vname(c0_ta), lnast->get_subs(c0_ta));

        auto it = vname2tuple_head.find(tuple_vname);
        if (it == vname2tuple_head.end()) {
          vname2tuple_head.insert_or_assign(tuple_vname, tup_add);
        }

        j++;
      }
    }
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

      // exclude invalid scalar->tuple cases
      // auto field_name = lnast->get_sname(lnast->get_sibling_next(c0_ta));  // peep for field_name ...

      auto        tup_add = lg->create_node(Ntype_op::TupAdd);
      const auto &tok2    = lnast->get_token(lnidx_ta);
      tup_add.set_loc(tok2.pos1, tok2.pos2);
      tup_add.set_source(tok2.fname);
      auto tn_dpin = setup_ta_ref_previous_ssa(tup_vname, subs);
      if (!tn_dpin.is_invalid()) {
        auto tn_spin = tup_add.setup_sink_pin("parent");
        tn_dpin.connect_sink(tn_spin);
      }

      auto ta_dpin = tup_add.setup_driver_pin();
      name2dpin.insert_or_assign(tup_sname, ta_dpin);
      ta_dpin.set_name(tup_sname);
      setup_dpin_ssa(name2dpin[tup_sname], lnast->get_vname(c0_ta), lnast->get_subs(c0_ta));

      auto it = vname2tuple_head.find(tup_vname);
      if (it == vname2tuple_head.end()) {
        vname2tuple_head.insert_or_assign(tup_vname, tup_add);
      }

      ta_map.insert_or_assign(i, tup_add);
      ta_name.insert_or_assign(i, tup_sname);
      i++;
      continue;
    }

    if (i == 1) {
      const auto &c1_ta       = child;
      auto        tup_add     = ta_map[i - 1];
      auto        pos_spin    = tup_add.setup_sink_pin("field");
      auto        field_vname = lnast->get_vname(c1_ta);
      auto        lntype      = lnast->get_type(c1_ta);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, c1_ta);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst::from_pyrope(field_vname)).setup_driver_pin();
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
      const auto &cn_ta   = child;
      auto        tup_add = lg->create_node(Ntype_op::TupAdd);
      const auto &tok     = lnast->get_token(lnidx_ta);
      tup_add.set_loc(tok.pos1, tok.pos2);
      tup_add.set_source(tok.fname);
      auto tup_name = ta_name[i - 1];

      auto tn_dpin = setup_tuple_ref(lg, tup_name);
      if (!tn_dpin.is_invalid() && !tn_dpin.get_node().is_type_const()) {
        auto tn_spin = tup_add.setup_sink_pin("parent");
        lg->add_edge(tn_dpin, tn_spin);
      }

      ta_map.insert_or_assign(i - 1, tup_add);

      auto ta_dpin = tup_add.setup_driver_pin();
      name2dpin.insert_or_assign(tup_name, ta_dpin);
      ta_dpin.set_name(tup_name);  // tuple ref semantically move to here
      auto cn_ta_vname = lnast->get_vname(cn_ta);
      if (!is_tmp_var(cn_ta_vname)) {
        setup_dpin_ssa(name2dpin[tup_name], cn_ta_vname, lnast->get_subs(cn_ta));
      }

      // take the new tuple-chain as the original tuple-chain value-dpin -> hierarchical tuple now!
      auto tup_add_parent = ta_map[i - 2];
      // note: no need to handle val_dpin for input_tg cases
      if (tup_add_parent.get_type_op() == Ntype_op::TupAdd) {
        auto value_spin_parent = tup_add_parent.setup_sink_pin("value");
        auto value_dpin_parent = ta_dpin;
        lg->add_edge(value_dpin_parent, value_spin_parent);
      }

      // setup key for the new tuple chain head
      auto pos_spin    = tup_add.setup_sink_pin("field");
      auto field_vname = lnast->get_vname(cn_ta);
      auto lntype      = lnast->get_type(cn_ta);

      if (lntype.is_ref()) {
        auto pos_dpin = setup_ref_node_dpin(lg, cn_ta);
        lg->add_edge(pos_dpin, pos_spin);
      } else {
        I(lntype.is_const());
        auto pos_dpin = lg->create_node_const(Lconst::from_pyrope(field_vname)).setup_driver_pin();
        lg->add_edge(pos_dpin, pos_spin);
      }

      ta_name.insert_or_assign(i, lnast->get_sname(cn_ta));
      i++;
    }
  }
}

// either tuple root or tuple key(str) fit in this case
Node_pin Lnast_tolg::setup_tuple_ref(Lgraph *lg, std::string_view ref_name) {
  static Node_pin invalid_dpin;

  if (std::isdigit(ref_name.front())) {
    return invalid_dpin;
  }

  auto it = name2dpin.find(ref_name);
  if (it != name2dpin.end()) {
    return it->second;
  }

  if (is_input(ref_name)) {
    return create_inp_tg(lg, ref_name);
  }

  return invalid_dpin;
}

Node_pin Lnast_tolg::setup_ta_ref_previous_ssa(std::string_view ref_vname, int16_t subs) {
  if (subs == 0) {
    auto     ref_name = ref_vname;
    Node_pin invalid_dpin;
    name2dpin[ref_name] = invalid_dpin;
    return invalid_dpin;
  }

  std::string chain_tail_name;
  if (subs > 1) {
    chain_tail_name = absl::StrCat(ref_vname, "|", subs - 1);  // try to concatenate after the TA(ssa-1)
  } else {
    chain_tail_name = ref_vname;
  }

  I(name2dpin.find(chain_tail_name) != name2dpin.end());
  I(name2dpin[chain_tail_name].get_name() == chain_tail_name);
  return name2dpin[chain_tail_name];
}

Node_pin Lnast_tolg::setup_field_dpin(Lgraph *lg, std::string_view field_name) {
  auto it2 = field2dpin.find(field_name);
  if (it2 != field2dpin.end()) {
    return it2->second;
  }

  auto dpin              = lg->create_node_const(Lconst::from_pyrope(field_name)).setup_driver_pin();
  field2dpin[field_name] = dpin;

  return dpin;
}

// for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node Lnast_tolg::setup_node_opr_and_lhs(Lgraph *lg, const Lnast_nid &lnidx_opr, std::string_view fir_func_name) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  Ntype_op lg_ntype_op;
  Node     exit_node;

  if (!fir_func_name.empty()) {  // handle firrtl sub
    I(fir_func_name.substr(0, 2) == "__");

    auto op = Ntype::get_op(fir_func_name.substr(2));
    if (op == Ntype_op::Invalid) {
      exit_node = lg->create_node_sub(fir_func_name);
    } else {
      exit_node = lg->create_node(op);
    }
    const auto &tok1 = lnast->get_token(lnidx_opr);
    exit_node.set_loc(tok1.pos1, tok1.pos2);
    exit_node.set_source(tok1.fname);
  } else {  // handle normal lnast oprator
    auto lnopr_type = lnast->get_type(lnidx_opr);
    if (lnopr_type.is_ne()) {
      auto        eq_node = lg->create_node(Ntype_op::EQ);
      const auto &tok2    = lnast->get_token(lnidx_opr);
      eq_node.set_loc(tok2.pos1, tok2.pos2);
      eq_node.set_source(tok2.fname);
      exit_node        = lg->create_node(Ntype_op::Not);
      const auto &tok3 = lnast->get_token(lnidx_opr);
      exit_node.set_loc(tok3.pos1, tok3.pos2);
      exit_node.set_source(tok3.fname);
      eq_node.setup_driver_pin().connect_sink(exit_node.setup_sink_pin("a"));

    } else if (lnopr_type.is_le()) {
      auto        lt_node = lg->create_node(Ntype_op::LT);
      const auto &tok2    = lnast->get_token(lnidx_opr);
      lt_node.set_loc(tok2.pos1, tok2.pos2);
      lt_node.set_source(tok2.fname);

      exit_node        = lg->create_node(Ntype_op::Not);
      const auto &tok3 = lnast->get_token(lnidx_opr);
      exit_node.set_loc(tok3.pos1, tok3.pos2);
      exit_node.set_source(tok3.fname);
      lt_node.setup_driver_pin().connect_sink(exit_node.setup_sink_pin("a"));

    } else if (lnopr_type.is_ge()) {
      auto        gt_node = lg->create_node(Ntype_op::GT);
      const auto &tok2    = lnast->get_token(lnidx_opr);
      gt_node.set_loc(tok2.pos1, tok2.pos2);
      gt_node.set_source(tok2.fname);

      exit_node        = lg->create_node(Ntype_op::Not);
      const auto &tok3 = lnast->get_token(lnidx_opr);
      exit_node.set_loc(tok3.pos1, tok3.pos2);
      exit_node.set_source(tok3.fname);
      gt_node.setup_driver_pin().connect_sink(exit_node.setup_sink_pin("a"));

    } else {
      lg_ntype_op      = decode_lnast_op(lnidx_opr);
      exit_node        = lg->create_node(lg_ntype_op);
      const auto &tok2 = lnast->get_token(lnidx_opr);
      exit_node.set_loc(tok2.pos1, tok2.pos2);
      exit_node.set_source(tok2.fname);
      // std::print("Hello2!! {} {} {}",tok2.pos1, tok2.pos2, exit_node.get_nid()  ); // here is the sum node location!!
    }
  }

  name2dpin[lhs_name] = exit_node.setup_driver_pin("Y");
  exit_node.get_driver_pin("Y").set_name(lhs_name);

  if (!is_tmp_var(lhs_vname)) {
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
  }
  return exit_node;
}

Node_pin Lnast_tolg::setup_tuple_assignment(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  auto        lhs       = lnast->get_first_child(lnidx_opr);
  auto        tup_name  = lnast->get_sname(lhs);
  auto        tup_vname = lnast->get_vname(lhs);
  auto        tup_add   = lg->create_node(Ntype_op::TupAdd);
  const auto &tok2      = lnast->get_token(lnidx_opr);
  tup_add.set_loc(tok2.pos1, tok2.pos2);
  tup_add.set_source(tok2.fname);

  auto tup_add_dpin   = tup_add.setup_driver_pin();
  name2dpin[tup_name] = tup_add_dpin;
  tup_add_dpin.set_name(tup_name);

  if (!is_tmp_var(tup_vname)) {
    setup_dpin_ssa(name2dpin[tup_name], tup_vname, lnast->get_subs(lhs));
  }

  return tup_add.setup_sink_pin("parent");
}

Node_pin Lnast_tolg::setup_node_assign_and_lhs(Lgraph *lg, const Lnast_nid &lnidx_opr) {
  auto lhs       = lnast->get_first_child(lnidx_opr);
  auto lhs_name  = lnast->get_sname(lhs);
  auto lhs_vname = lnast->get_vname(lhs);

  auto        assign_node = lg->create_node(Ntype_op::Or);
  const auto &tok2        = lnast->get_token(lnidx_opr);
  assign_node.set_loc(tok2.pos1, tok2.pos2);
  assign_node.set_source(tok2.fname);

  name2dpin[lhs_name] = assign_node.setup_driver_pin();  // or as assign
  name2dpin[lhs_name].set_name(lhs_name);

  if (!is_tmp_var(lhs_vname)) {
    setup_dpin_ssa(name2dpin[lhs_name], lhs_vname, lnast->get_subs(lhs));
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
    auto node_dpin = create_const(lg, vname);
    I(!node_dpin.is_invalid());
    name2dpin[name] = node_dpin;  // for io and reg, the %$# identifier are still used in symbol table
    return node_dpin;
  }

  const auto &it = name2dpin.find(name);
  if (it != name2dpin.end()) {
    auto node = it->second.get_node();
    auto op   = node.get_type_op();

    // if ref comes from an TA dpin
    if (op == Ntype_op::TupAdd && !from_ta_assign && !from_phi) {
      auto parent_node = node.get_sink_pin("parent");
      if (parent_node.is_invalid()) {
        return it->second;
      }

      auto parent_ntype = parent_node.get_type_op();
      auto val_spin     = node.setup_sink_pin("value");
      auto tn_spin      = node.setup_sink_pin("parent");

      if (parent_ntype == Ntype_op::Or) {
        return create_scalar_access_tg(lg, it->second);
      } else if (!tn_spin.is_connected() && val_spin.is_connected()) {
        // it's head of tuple-chain
        // case: the tuple has only one pos sink pin connected and being used
        // to an operator -> it's still a scalar -> create TG to fetch field 0
        // note: if the field is connected, it cannot be viewed as scalar so
        // just return the TA chain itself, not the scalar-TG(0) optimization.
        if (val_spin.get_driver_node().get_type_op() != Ntype_op::TupAdd) {
          if (!node.setup_sink_pin("field").is_connected()) {
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
    // if (name2din.find(name)!= name2dpin.end()) {
    node_dpin = lg->create_node(Ntype_op::Or).setup_driver_pin();
    // node_dpin.set_loc(std::make_pair( (lnast->get_token(lnidx_opd)).pos1 , (lnast->get_token(lnidx_opd)).pos2 ) );
    node_dpin.set_name(name);
    name2dpin[name] = node_dpin;
    //}
    return node_dpin;
  } else if (is_err_var_undefined(name)) {
    node_dpin = lg->create_node(Ntype_op::CompileErr).setup_driver_pin("Y");
    // node_dpin.set_loc(std::make_pair( (lnast->get_token(lnidx_opd)).pos1 , (lnast->get_token(lnidx_opd)).pos2 ) );
  } else if (is_bool_true(name)) {
    node_dpin = lg->create_node_const(Lconst(1)).setup_driver_pin();
  } else if (is_bool_false(name)) {
    node_dpin = lg->create_node_const(Lconst(0)).setup_driver_pin();
  } else {
#ifndef NDEBUG
    Pass::warn("node_dpin creation added in this \"else\" to enable RocketTile LG generation.");
#endif
    node_dpin = lg->create_node_const(Lconst(0)).setup_driver_pin();
    return node_dpin;  // return empty node_pin and trigger compile error
  }

  name2dpin[name] = node_dpin;  // for io and reg, the %$# identifier are still used in symbol table
  return node_dpin;
}

Node_pin Lnast_tolg::create_scalar_access_tg(Lgraph *lg, const Node_pin &tg_tupname_dpin, const Node_pin &field_dpin) {
  I(false);  // delete this method
  auto tup_get = lg->create_node(Ntype_op::TupGet);
  // FIXME: loc?
  auto tn_spin    = tup_get.setup_sink_pin("parent");  // tuple name
  auto field_spin = tup_get.setup_sink_pin("field");   // field

  auto tn_dpin = tg_tupname_dpin;
  tn_dpin.connect_sink(tn_spin);
  field_dpin.connect_sink(field_spin);
  return tup_get.setup_driver_pin();
}

Node_pin Lnast_tolg::create_scalar_access_tg(Lgraph *lg, const Node_pin &tg_tupname_dpin) {
  auto tup_get = lg->create_node(Ntype_op::TupGet);
  // FIXME: loc?
  auto tn_spin        = tup_get.setup_sink_pin("parent");  // tuple name
  auto field_pos_spin = tup_get.setup_sink_pin("field");   // field pos

  auto tn_dpin = tg_tupname_dpin;

  // must be pos 0 as the case is "bar = a + 1", implicitly get a.0
  auto field_pos_dpin = lg->create_node_const(Lconst(0)).setup_driver_pin();
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
  if (str_tools::contains(const_str, "bits") && !str_tools::contains(const_str, "__")) {  // contains "bits" but not "__"
    // NOTE: FIRRTL needs bits in constants for the bitwidth inference pass.
    // TODO: It may be cleaner to create a __fir_const sub in the LNAST gen
    auto lg_fir_const_node   = lg->create_node_sub("__fir_const");
    auto fir_const_node_dpin = lg_fir_const_node.setup_driver_pin("Y");
    fir_const_node_dpin.set_name(const_str);
    return fir_const_node_dpin;
  } else {
    auto lc = Lconst::from_pyrope(const_str);
    return lg->create_node_const(lc).setup_driver_pin();
  }
}

void Lnast_tolg::process_ast_attr_set_op(Lgraph *lg, const Lnast_nid &lnidx_aset) {
  auto name_aset  = lnast->get_first_child(lnidx_aset);
  auto field_aset = lnast->get_sibling_next(name_aset);

  auto name  = lnast->get_sname(name_aset);  // ssa name
  auto vname = lnast->get_vname(name_aset);  // no-ssa name

  auto        aset_node = lg->create_node(Ntype_op::AttrSet);
  const auto &tok2      = lnast->get_token(lnidx_aset);
  aset_node.set_loc(tok2.pos1, tok2.pos2);
  aset_node.set_source(tok2.fname);

  lh::Tree_index val_aset;

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
      if (lnast->get_sibling_next(val_aset).is_invalid()) {
        break;
      }
    }
    auto av_dpin = setup_field_dpin(lg, field);
    lg->add_edge(av_dpin, af_spin);
  }

  {
    // Get the variable name with SSA/input
    auto vn_spin = aset_node.setup_sink_pin("parent");  // variable name

    auto        aset_ancestor_subs = lnast->get_data(name_aset).subs - 1;
    std::string aset_ancestor_name;
    if (aset_ancestor_subs > 0) {
      aset_ancestor_name = absl::StrCat(vname, "|", aset_ancestor_subs);
    } else {
      aset_ancestor_name = vname;
    }

    Node_pin vn_dpin;
    if (is_input(name)) {
      vn_dpin = setup_tuple_ref(lg, lnast->get_name(name_aset));
      I(!vn_dpin.is_invalid());  // inputs always succeed
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
      auto        value_nossa = lnast->get_vname(val_aset);
      auto        value_subs  = lnast->get_data(val_aset).subs - 1;
      std::string value_name;
      if (value_subs > 0) {
        value_name = absl::StrCat(value_nossa, "|", value_subs);
      } else {
        value_name = value_nossa;
      }

      const auto &it = name2dpin.find(value_name);
      if (it != name2dpin.end()) {
        val_dpin = it->second;
      }
    }
    if (val_dpin.is_invalid()) {
      val_dpin = setup_ref_node_dpin(lg, val_aset);
    }
    lg->add_edge(val_dpin, val_spin);
  }

  auto aset_node_dpin = aset_node.setup_driver_pin();
  aset_node_dpin.set_name(name);
  name2dpin[name] = aset_node_dpin;
}

void Lnast_tolg::process_ast_attr_get_op(Lgraph *lg, const Lnast_nid &lnidx_aget) {
  auto c0_aget       = lnast->get_first_child(lnidx_aget);
  auto c1_aget       = lnast->get_sibling_next(c0_aget);
  auto cn_aget       = lnast->get_last_child(lnidx_aget);
  auto c0_aget_name  = lnast->get_sname(c0_aget);
  auto c0_aget_vname = lnast->get_vname(c0_aget);

  std::string hier_fields_cat_name;
  auto        attr_vname = lnast->get_vname(cn_aget);
  auto        it_aset    = c1_aget;

  while (it_aset != cn_aget) {
    if (hier_fields_cat_name.empty()) {
      hier_fields_cat_name = lnast->get_vname(it_aset);
    } else {
      absl::StrAppend(&hier_fields_cat_name, ".", lnast->get_vname(it_aset));
    }
    it_aset = lnast->get_sibling_next(it_aset);
  }

  if (attr_vname == "__create_flop") {
    auto        flop_node = lg->create_node(Ntype_op::Flop);
    const auto &tok2      = lnast->get_token(lnidx_aget);
    flop_node.set_loc(tok2.pos1, tok2.pos2);
    flop_node.set_source(tok2.fname);
    auto flop_dpin = flop_node.setup_driver_pin();
    flop_dpin.set_name(hier_fields_cat_name);
    name2dpin[c0_aget_name] = flop_dpin;

    if (!is_tmp_var(c0_aget_vname)) {
      setup_dpin_ssa(name2dpin[c0_aget_name], c0_aget_vname, lnast->get_subs(c0_aget));
    }

    auto flop_din_driver_pin = setup_ref_node_dpin(lg, c1_aget);
    if (!flop_din_driver_pin.is_invalid()) {  // flop has some previous attribute set
      flop_din_driver_pin.connect_sink(flop_node.setup_sink_pin("din"));
      // put the head of the tuple chain as the wire_node that will be driven by the largest ssa later
      auto driver_vname = lnast->get_vname(c1_aget);
      I(vname2tuple_head.find(driver_vname) != vname2tuple_head.end());
      auto tup_head_node = vname2tuple_head[driver_vname];
      driver_vname2wire_nodes[driver_vname].emplace_back(tup_head_node);
    } else {  // if no previous attribute set, use the normal __last_value flow
      auto driver_vname = lnast->get_vname(c1_aget);
      driver_vname2wire_nodes[driver_vname].emplace_back(flop_node);
    }

    it_aset = lnast->get_sibling_next(it_aset);
    if (!it_aset.is_invalid()) {
      Pass::error("attribute {} must be the last in the entry {}\n", attr_vname, hier_fields_cat_name);
    }
    return;
  }

  if (attr_vname == "__last_value") {
    Node wire_node;
    wire_node        = lg->create_node(Ntype_op::Or);  // might need to change to other type according to the real driver
    const auto &tok2 = lnast->get_token(lnidx_aget);
    wire_node.set_loc(tok2.pos1, tok2.pos2);
    wire_node.set_source(tok2.fname);
    auto wire_dpin = wire_node.setup_driver_pin();
    wire_dpin.set_name(hier_fields_cat_name);

    name2dpin[c0_aget_name] = wire_dpin;

    if (!is_tmp_var(c0_aget_vname)) {
      setup_dpin_ssa(name2dpin[c0_aget_name], c0_aget_vname, lnast->get_subs(c0_aget));
    }

    std::string driver_vname;
    driver_vname = lnast->get_vname(c1_aget);

    driver_vname2wire_nodes[driver_vname].emplace_back(wire_node);

    it_aset = lnast->get_sibling_next(it_aset);
    if (!it_aset.is_invalid()) {
      Pass::error("attribute {} must be the last in the entry {}\n", attr_vname, hier_fields_cat_name);
    }
    return;
  }
}

void Lnast_tolg::process_ast_func_call_op(Lgraph *lg, const Lnast_nid &lnidx_fc) {
  auto c0_fc         = lnast->get_first_child(lnidx_fc);
  auto func_name_ori = lnast->get_vname(lnast->get_sibling_next(c0_fc));
  auto cn_fc         = lnast->get_last_child(lnidx_fc);
  auto cn_fc_sname   = lnast->get_sname(cn_fc);

  std::string func_name;
  auto        cond2 = func_name_ori.substr(0, 2) == "__";
  auto        cond3 = !inlined_func_names.contains(func_name_ori);
  if (cond2 || cond3) {
    func_name = func_name_ori;
  } else {
    func_name = absl::StrCat(module_name, ".", func_name_ori);
  }

  std::string arg_tup_name;
  if (is_input(cn_fc_sname)) {
    I(cn_fc_sname.front() == '$');
    arg_tup_name = Lgtuple::get_all_but_first_level(lnast->get_vname(cn_fc));
  } else {
    arg_tup_name = cn_fc_sname;
  }

  auto ret_name  = lnast->get_sname(c0_fc);
  auto subg_node = lg->create_node_sub(func_name);
  // OLD STYLE: subg_node.set_name(absl::StrCat(arg_tup_name, ":", ret_name, ":", func_name));
  subg_node.set_name(lnast->get_vname(c0_fc));

  auto subg_spin = subg_node.setup_sink_pin("$");
  auto subg_dpin = subg_node.setup_driver_pin("%");

  auto arg_tup_dpin = setup_tuple_ref(lg, arg_tup_name);
  I(!arg_tup_dpin.is_invalid());  // input is guaranteed
  arg_tup_dpin.connect_sink(subg_spin);

  // create a TA assignment for the ret
  auto        ta_ret = lg->create_node(Ntype_op::TupAdd);
  const auto &tok2   = lnast->get_token(lnidx_fc);
  ta_ret.set_loc(tok2.pos1, tok2.pos2);
  ta_ret.set_source(tok2.fname);
  auto ta_ret_dpin = ta_ret.setup_driver_pin();

  subg_dpin.connect_sink(ta_ret.setup_sink_pin("parent"));
  name2dpin[ret_name] = ta_ret_dpin;
  ta_ret_dpin.set_name(ret_name);

  if (ret_name[0] == '%') {
    auto ret_vname = lnast->get_vname(c0_fc);
    auto subs      = lnast->get_subs(c0_fc);
    setup_dpin_ssa(name2dpin[ret_name], ret_vname, subs);
  }
}

void Lnast_tolg::process_ast_func_def_op(Lgraph *lg, const Lnast_nid &lnidx) {
  auto        c0_fdef    = lnast->get_first_child(lnidx);
  auto        c1_fdef    = lnast->get_sibling_next(c0_fdef);
  auto        func_stmts = lnast->get_sibling_next(c1_fdef);
  std::string func_vname(lnast->get_vname(c0_fdef));
  auto        subg_module_name = absl::StrCat(module_name, ".", func_vname);
  Lnast_tolg  p(subg_module_name, path);

  // std::print("============================= Sub-module: LNAST->Lgraph Start ({})
  // ==============================================\n",
  //            subg_module_name);

  p.do_tolg(lnast, func_stmts);
  // std::print("============================= Sub-module: LNAST->Lgraph End   ({})
  // ==============================================\n",
  //            subg_module_name);

  // TODO: We should have a TupAdd so that the function can be passed, but this
  // code is wrong because there is no SSA at the function definition

  auto        tup_add = lg->create_node(Ntype_op::TupAdd);
  const auto &tok2    = lnast->get_token(lnidx);
  tup_add.set_loc(tok2.pos1, tok2.pos2);
  tup_add.set_source(tok2.fname);
  auto pos_spin   = tup_add.setup_sink_pin("field");  // field name
  auto value_spin = tup_add.setup_sink_pin("value");

  auto field_dpin = lg->create_node_const(Lconst::from_pyrope("__fdef")).setup_driver_pin();

  field_dpin.connect_sink(pos_spin);

  auto      *library = Graph_library::instance(path);
  Lg_type_id lgid;
  if (library->has_name(subg_module_name)) {
    lgid = library->get_lgid(subg_module_name);
  }
  // guard.unlock();

  auto value_dpin = lg->create_node_const(Lconst(lgid)).setup_driver_pin();
  value_dpin.connect_sink(value_spin);

  auto ta_dpin          = tup_add.setup_driver_pin();
  name2dpin[func_vname] = ta_dpin;  // note: record only the function_name instead of top.function_name
  ta_dpin.set_name(func_vname);
  inlined_func_names.insert(func_vname);
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
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_log_and] = Ntype_op::And;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_log_not] = Ntype_op::Not;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_log_or]  = Ntype_op::Ror;

  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_red_or] = Ntype_op::Ror;
  // No direct primive translate for red_and/red_xor

  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_assign]  = Ntype_op::Or;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_and] = Ntype_op::And;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_or]  = Ntype_op::Or;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_not] = Ntype_op::Not;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_bit_xor] = Ntype_op::Xor;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_plus]    = Ntype_op::Sum;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_minus]   = Ntype_op::Sum;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_mult]    = Ntype_op::Mult;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_div]     = Ntype_op::Div;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_eq]      = Ntype_op::EQ;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_lt]      = Ntype_op::LT;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_gt]      = Ntype_op::GT;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_sra]     = Ntype_op::SRA;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_shl]     = Ntype_op::SHL;

  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_get_mask] = Ntype_op::Get_mask;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_set_mask] = Ntype_op::Set_mask;

  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_sext] = Ntype_op::Sext;
  // FIXME->sh: to be extended ...
}

void Lnast_tolg::setup_scalar_reg_clkrst(Lgraph *lg, Node &reg_node) {
  Node_pin clk_dpin;
  if (!lg->has_graph_input("clock_pin")) {
    clk_dpin = lg->add_graph_input("clock_pin", Port_invalid, 1);
  } else {
    clk_dpin = lg->get_graph_input("clock_pin");
  }

  auto clk_spin = reg_node.setup_sink_pin("clock_pin");
  lg->add_edge(clk_dpin, clk_spin);

  /////////////////////////////////

  Node_pin rst_dpin;
  if (!lg->has_graph_input("reset_pin")) {
    rst_dpin = lg->add_graph_input("reset_pin", Port_invalid, 1);
  } else {
    rst_dpin = lg->get_graph_input("reset_pin");
  }

  auto rst_spin = reg_node.setup_sink_pin("reset_pin");
  lg->add_edge(rst_dpin, rst_spin);
}

void Lnast_tolg::setup_dpin_ssa(Node_pin &dpin, std::string_view var_name, uint16_t subs) {
  ssa_info_map.insert_or_assign(dpin.get_compact_class_driver(), Ssa_info(var_name, subs));
}

void Lnast_tolg::create_out_ta(Lgraph *lg, std::string_view field_name, Node_pin &val_dpin) {
  auto tup_add = lg->create_node(Ntype_op::TupAdd);
  // FIXME: loc?
  auto tn_dpin = setup_tuple_ref(lg, "%");  // might come from TupAdd
  if (!tn_dpin.is_invalid()) {
    auto tn_spin = tup_add.setup_sink_pin("parent");
    tn_dpin.connect_sink(tn_spin);
  }

  auto pos_spin = tup_add.setup_sink_pin("field");

  std::string tup_name{"%"};
  if (!field_name.empty()) {
    absl::StrAppend(&tup_name, ".", field_name);
  }

  auto pos_dpin = lg->create_node_const(Lconst::from_string(tup_name)).setup_driver_pin();
  pos_dpin.connect_sink(pos_spin);

  auto val_spin = tup_add.setup_sink_pin("value");
  val_dpin.connect_sink(val_spin);

  auto ta_dpin   = tup_add.setup_driver_pin();
  name2dpin["%"] = ta_dpin;
  I(!name2dpin["%"].is_invalid());
  ta_dpin.set_name("%");  // tuple ref semantically moves to here
}

void Lnast_tolg::setup_lgraph_ios_and_final_var_name(Lgraph *lg) {
  absl::flat_hash_map<std::string, Node_pin> vname2ssa_dpin;  // pyrope variable -> dpin with the largest ssa var subscription
  for (auto node : lg->forward()) {
    auto ntype = node.get_type_op();

    if (ntype == Ntype_op::Sub) {
      // Skip non-FIRRTL function nodes (since FIRRTL is removed, skip all Sub nodes that don't match expected pattern)
      if (node.get_type_sub_node().get_name().substr(0, 6) != "__fir_") {
        continue;
      }
    }

    auto dpin = node.get_driver_pin("Y");

    // connect hier-tuple-inputs and scalar input from the unified input $
    if (ntype == Ntype_op::Or && is_input(dpin.get_name())) {
      node.set_type(Ntype_op::TupGet);  // change node semantic: Or -> TupGet
      auto tn_spin = node.setup_sink_pin("parent");
      auto tn_dpin = name2dpin["$"];
      tn_dpin.connect_sink(tn_spin);

      auto pos_spin = node.setup_sink_pin("field");
      I(dpin.get_name().front() == '$');
      auto dpin_subname = Lgtuple::get_all_but_first_level(dpin.get_name());
      auto pos_dpin     = lg->create_node_const(Lconst::from_string(dpin_subname)).setup_driver_pin();
      pos_dpin.connect_sink(pos_spin);
      continue;
    }

    // collect vname table info
    auto ssa_it = ssa_info_map.find(dpin.get_compact_class_driver());
    if (ssa_it != ssa_info_map.end()) {
      auto it2 = vname2ssa_dpin.find(ssa_it->second.var_name);
      if (it2 != vname2ssa_dpin.end()) {
        auto ssa_it2 = ssa_info_map.find(it2->second.get_compact_class_driver());
        if (ssa_it2 != ssa_info_map.end() && ssa_it2->second.subs < ssa_it->second.subs) {
          vname2ssa_dpin.insert_or_assign(ssa_it->second.var_name, dpin);
        }
      } else {
        vname2ssa_dpin.insert_or_assign(ssa_it->second.var_name, dpin);
      }
    }
  }  // end of lg->forward()

  // create scalar graph outputs or set the final variable name based on vname table
  for (const auto &[vname, dpin_largest_ssa] : vname2ssa_dpin) {
    if (is_output(vname)) {
      auto edible_dpin = dpin_largest_ssa;
      I(vname.front() == '%');
      create_out_ta(lg, Lgtuple::get_all_but_first_level(vname), edible_dpin);  // don't pass first char % as the key name
      // continue;
    }

    auto it = driver_vname2wire_nodes.find(vname);
    if (it == driver_vname2wire_nodes.end()) {
      continue;
    }

    auto driver_ntype = dpin_largest_ssa.get_node().get_type_op();
    for (auto &wire_node : it->second) {
      Node_pin wire_spin;
      if (driver_ntype == Ntype_op::TupAdd) {
        if (wire_node.is_type(Ntype_op::Or)) {
          auto dpin_name = wire_node.get_driver_pin().get_name();
          auto found     = dpin_name.find('.');
          if (found == std::string::npos) {
            // it's a TA assignment
            wire_node.set_type(Ntype_op::TupAdd);  // change wire_node type from Or_Op to dummy TupAdd_Op
            wire_spin = wire_node.setup_sink_pin("parent");
          } else {
            // FIXME->sh: check the wire_node name, if it is a hier_name like _T.a.b, then you should replace the or with TG
            wire_node.set_type(Ntype_op::TupGet);
            auto field_spin = wire_node.setup_sink_pin("field");
            auto field_dpin = lg->create_node_const(Lconst::from_string(dpin_name.substr(found + 1))).setup_driver_pin();
            field_dpin.connect_sink(field_spin);
            wire_spin = wire_node.setup_sink_pin("parent");
          }
        } else if (wire_node.is_type(Ntype_op::TupAdd)) {
          wire_spin = wire_node.setup_sink_pin("parent");
        } else {
          I(wire_node.is_type(Ntype_op::Flop));
          wire_spin = wire_node.setup_sink_pin("din");
        }
      } else {
        I(wire_node != dpin_largest_ssa.get_node());
        if (wire_node.is_type(Ntype_op::Or)) {
          wire_spin = wire_node.setup_sink_pin("A");
        } else if (wire_node.is_type(Ntype_op::Flop)) {
          wire_spin = wire_node.setup_sink_pin("din");
        } else {
          I(wire_node.is_type(Ntype_op::TupAdd));
          wire_spin = wire_node.setup_sink_pin("parent");
        }
      }
      dpin_largest_ssa.connect_sink(wire_spin);
    }
  }

  // connect output tuple-chain to output pin "%"
  auto unified_out_dpin = name2dpin["%"];             // TA node
  auto unified_out_spin = lg->get_graph_output("%");  // Must be created before
  I(!unified_out_spin.is_invalid());
  if (!unified_out_dpin.is_invalid()) {
    unified_out_dpin.connect_sink(unified_out_spin);
  }

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

void Lnast_tolg::post_process_ginp_attr_connections(Lgraph *lg) {
  // final process to reconnect ginp-> normal_node as ginp -> attr_set_node -> normal_node if any
  lg->each_graph_input([](Node_pin &ginp) {
    if (ginp.get_name() == "%") {
      return;
    }

    // identtify the attr_set node of this ginp if any
    Node_pin attr_set_dpin;

    for (auto &e : ginp.out_edges()) {
      auto sink_node  = e.sink.get_node();
      auto sink_ntype = sink_node.get_type_op();
      if (sink_ntype == Ntype_op::AttrSet && ginp.out_edges().size() == 1) {
        return;
      }

      if (sink_ntype == Ntype_op::AttrSet) {
        attr_set_dpin = sink_node.setup_driver_pin();
      }
    }

    if (attr_set_dpin.is_invalid()) {
      return;
    }

    for (auto &e : ginp.out_edges()) {
      auto sink_node  = e.sink.get_node();
      auto sink_ntype = sink_node.get_type_op();
      if (sink_ntype == Ntype_op::AttrSet) {
        continue;
      }

      attr_set_dpin.connect_sink(e.sink);
      e.del_edge();
    }
  });
}

void Lnast_tolg::try_create_flattened_inp(Lgraph *lg) {
  auto uinp = lg->get_graph_input("$").get_non_hierarchical();  // hierarchical not needed, it should be faster this way

  for (auto &e : uinp.out_edges()) {
    auto tg = e.sink.get_node();
    I(tg.get_type_op() == Ntype_op::TupGet || tg.get_type_op() == Ntype_op::TupAdd);

    inp_artifacts[tg.get_compact()].insert(tg);  // insert the head of the chain

    auto tg_name = tg.get_driver_pin().get_name();

    I(tg_name.substr(0, 1) == "$");
    auto hier_name = Lgtuple::get_all_but_first_level(tg_name);  // get rid of "$" in "$foo"
    if (hier_name.empty()) {
      continue;
    }

    for (auto &tg_out : tg.out_edges()) {
      dfs_try_create_flattened_inp(lg, tg_out.sink, hier_name, tg);
    }

    for (auto itr : inp_artifacts[tg.get_compact()]) {
      if (!itr.is_invalid()) {
        itr.del_node();
      }
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
  auto tn_spin = cur_tg.setup_sink_pin("parent");
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

  // (3) create graph_input and connect to cur_tg field sink pin
  Node_pin ginp;
  if (!lg->has_graph_input(hier_name)) {
    ginp = lg->add_graph_input(hier_name, Port_invalid, 0);
  } else if (name2dpin.find(hier_name) != name2dpin.end()) {
    ginp = name2dpin[hier_name];
  } else {
    ginp = lg->get_graph_input(hier_name);
  }

  auto pos_spin = cur_tg.setup_sink_pin("field");
  ginp.connect_sink(pos_spin);
  return;
}

void Lnast_tolg::dfs_try_create_flattened_inp(Lgraph *lg, Node_pin &cur_node_spin, std::string_view hier_name_sv,
                                              Node &chain_head) {
  std::string hier_name(hier_name_sv);

  auto cur_node  = cur_node_spin.get_node();
  auto cur_ntype = cur_node.get_type_op();
  bool is_leaf   = false;

  if (cur_ntype == Ntype_op::TupGet && cur_node_spin == cur_node.setup_sink_pin("field")) {
    auto pos_spin = cur_node.setup_sink_pin("field");
    if (pos_spin.is_connected() && pos_spin.get_driver_node().get_type_op() == Ntype_op::TupGet) {
      create_ginp_as_runtime_idx(lg, hier_name, chain_head, cur_node);
      return;
    }
  } else if (cur_ntype == Ntype_op::AttrSet) {
    if (cur_node.is_sink_connected("field")) {
      // auto field_txt = cur_node.get_sink_pin("field").get_driver_pin().get_type_const().to_str();
      auto field_txt = cur_node.get_sink_pin("field").get_driver_pin().get_type_const().to_field();  // low intuitive api
      if (!Lgtuple::is_root_attribute(field_txt)) {
        auto non_attr_field = Lgtuple::get_all_but_last_level(field_txt);
        absl::StrAppend(&hier_name, ".", non_attr_field);
      }
    }
    is_leaf = true;
  } else if (cur_ntype == Ntype_op::TupGet && cur_node_spin == cur_node.setup_sink_pin("parent")) {
    auto pos_spin = cur_node.setup_sink_pin("field");
    if (pos_spin.is_connected() && pos_spin.get_driver_node().get_type_op() != Ntype_op::Const) {
      handle_inp_tg_runtime_idx(hier_name, chain_head, cur_node);
      return;
    }

    inp_artifacts[chain_head.get_compact()].insert(cur_node);  // only remove the artifact tup_gets

    std::string new_hier_name = hier_name;
    if (cur_node.is_sink_connected("field")) {
      auto field_node = cur_node.get_sink_pin("field").get_driver_node();
      if (field_node.is_type_const()) {
        // auto field_name = field_node.get_type_const().to_str();
        auto field_name = field_node.get_type_const().to_field();  // low intuitive api
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
    if (name2dpin.find(hier_name) != name2dpin.end()) {
      ginp = name2dpin[hier_name];
    } else if (!lg->has_graph_input(hier_name)) {
      ginp = lg->add_graph_input(hier_name, Port_invalid, 0);
    } else {
      ginp = lg->get_graph_input(hier_name);
    }

    inp2leaf_tg_spins[ginp].emplace_back(cur_node_spin);

    // if can reach leaf, the hierarchy is ended as a scalar, all path
    // assignments (either Or or TA) should be removed later
  }
}

void Lnast_tolg::dump() const {
  std::cout << "name2dpin:\n";
  for (auto &it : name2dpin) {
    std::print("  name:{} dpin:{}\n", it.first, it.second.debug_name());
  }
}
