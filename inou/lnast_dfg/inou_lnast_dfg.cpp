//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <variant>
#include "inou_lnast_dfg.hpp"

#include "lbench.hpp"
#include "lgraph.hpp"
#include "lnast_parser.hpp"


void setup_inou_lnast_dfg() { Inou_lnast_dfg::setup(); }

void Inou_lnast_dfg::setup() {
  Eprp_method m1("inou.lnast_dfg.tolg", "parse cfg_text -> build lnast -> generate lgraph", &Inou_lnast_dfg::tolg);
  m1.add_label_required("files", "cfg_text files to process (comma separated)");
  m1.add_label_optional("path", "path to put the lgraph[s]", "lgdb");
  register_inou("lnast_dfg", m1);

  Eprp_method m2("inou.lnast_dfg.reduced_or_elimination", "reduced_or_op elimination for clear algorithm", &Inou_lnast_dfg::reduced_or_elimination);
  m2.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m2.add_label_optional("odir", "output directory for generated verilog files", ".");
  register_inou("lnast_dfg",m2);

  Eprp_method m3("inou.lnast_dfg.resolve_tuples", "resolve tuple chains for flattened lgraph", &Inou_lnast_dfg::resolve_tuples);
  m3.add_label_optional("path", "path to read the lgraph[s]", "lgdb");
  m3.add_label_optional("odir", "output directory for generated verilog files", ".");
  register_inou("lnast_dfg",m3);

  Eprp_method m4("inou.lnast_dfg.gen_temp_lg", "create temp lgraph for bitwidth", &Inou_lnast_dfg::gen_temp_lg);
  m4.add_label_optional("path", "path to put the lgraph[s]", "lgdb");
  register_inou("lnast_dfg", m4);
}

Inou_lnast_dfg::Inou_lnast_dfg(const Eprp_var &var) : Pass("inou.lnast_dfg", var), lginp_cnt(1), lgout_cnt(0) {
  setup_lnast_to_lgraph_primitive_type_mapping();
}


void Inou_lnast_dfg::resolve_tuples(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;

  for (const auto &l : var.lgs) {
    p.do_resolve_tuples(l);
  }
}

void Inou_lnast_dfg::do_resolve_tuples(LGraph *dfg) {

  //resolve TupGet source and destination
  absl::flat_hash_set<Node::Compact> to_be_deleted;
  for (const auto &node : dfg->fast()) {
    if (node.get_type().op == TupAdd_Op) {
      I(node.get_sink_pin(0).inp_edges().size() == 1);
      I(node.get_sink_pin(3).inp_edges().size() == 1);

      to_be_deleted.insert(node.get_compact());

      // handle special case: bits attribute
      if (is_bit_attr_tuple_add(node)) {
        //FIXME->sh: now I assume value pin is connected to constant node directly, but here is another copy propagation problem
        auto bits = node.get_sink_pin(3).inp_edges().begin()->driver.get_node().get_type_const_value();
        auto target_dpin = Node_pin::find_driver_pin(dfg, node.get_driver_pin().get_name());
        target_dpin.ref_bitwidth()->e.set_ubits(bits);
      }
    } else if (node.get_type().op == TupGet_Op and tuple_get_has_key_name(node)) {
      to_be_deleted.insert(node.get_compact());
      auto tup_get_target = node.get_sink_pin(1).inp_edges().begin()->driver.get_name();
      auto chain_itr = node.get_sink_pin(0).inp_edges().begin()->driver.get_node();
      while (chain_itr.get_type().op != TupRef_Op) {
        I(chain_itr.get_type().op == TupAdd_Op);
        if (chain_itr.setup_sink_pin(1).is_connected() and is_tup_get_target(chain_itr, tup_get_target)) {
          auto value_dpin = chain_itr.setup_sink_pin(3).inp_edges().begin()->driver;
          auto value_spin = node.get_sink_pin(0).out_edges().begin()->sink;
          dfg->add_edge(value_dpin, value_spin);
        }
        auto next_itr = chain_itr.setup_sink_pin(0).inp_edges().begin()->driver.get_node();
        chain_itr = next_itr;
      }

    } else if (node.get_type().op == TupGet_Op and tuple_get_has_key_pos(node)) {
      to_be_deleted.insert(node.get_compact());
      auto tup_get_target = node.get_sink_pin(2).inp_edges().begin()->driver.get_node().get_type_const_value();
      fmt::print("tup_get_target:{}\n", tup_get_target);
      auto chain_itr = node.get_sink_pin(0).inp_edges().begin()->driver.get_node();
      while (chain_itr.get_type().op != TupRef_Op) {
        I(chain_itr.get_type().op == TupAdd_Op);
        if (chain_itr.setup_sink_pin(2).is_connected() and is_tup_get_target(chain_itr, tup_get_target)) {
          auto value_dpin = chain_itr.setup_sink_pin(3).inp_edges().begin()->driver;
          auto value_spin = node.get_sink_pin(0).out_edges().begin()->sink;
          dfg->add_edge(value_dpin, value_spin);
        }
        auto next_itr = chain_itr.setup_sink_pin(0).inp_edges().begin()->driver.get_node();
        chain_itr = next_itr;
      }
    }
  }

  for (auto &itr : to_be_deleted) {
    fmt::print("delete {}\n", itr.get_node(dfg).debug_name());
    itr.get_node(dfg).del_node();
  }
}

bool Inou_lnast_dfg::tuple_get_has_key_name(const Node &tup_get) {
  return tup_get.get_sink_pin(1).is_connected();
}

bool Inou_lnast_dfg::tuple_get_has_key_pos(const Node &tup_get) {
  return tup_get.get_sink_pin(2).is_connected();
}

bool Inou_lnast_dfg::is_tup_get_target(const Node &tup_add, std::string_view tup_get_target) {
  auto tup_add_key_name = tup_add.get_sink_pin(1).inp_edges().begin()->driver.get_name();
  return (tup_add_key_name == tup_get_target);
}


bool Inou_lnast_dfg::is_tup_get_target(const Node &tup_add, uint32_t tup_get_target) {
  auto tup_add_key_pos = tup_add.get_sink_pin(2).inp_edges().begin()->driver.get_node().get_type_const_value();
  return (tup_add_key_pos == tup_get_target);
}

void Inou_lnast_dfg::reduced_or_elimination(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;

  for (const auto &l : var.lgs) {
    p.do_reduced_or_elimination(l);
  }
}


void Inou_lnast_dfg::do_reduced_or_elimination(LGraph *dfg) {
  absl::flat_hash_set<Node::Compact> to_be_deleted;
  for (const auto &node : dfg->fast()) {
    if (node.get_type().op == Or_Op) {
      bool is_reduced_or = node.out_edges().begin()->driver.get_pid() == 1;
      if (is_reduced_or) {
        I(node.inp_edges().size() == 1);
        for (auto &out : node.out_edges()) {
          auto dpin = node.inp_edges().begin()->driver;
          dpin.set_name(node.get_driver_pin(1).get_name());
          auto spin = out.sink;
          dfg->add_edge(dpin, spin);
        }
        to_be_deleted.insert(node.get_compact());
      }
    }
  }

  for (auto &itr : to_be_deleted) {
    fmt::print("delete {}\n", itr.get_node(dfg).debug_name());
    itr.get_node(dfg).del_node();
  }
}


void Inou_lnast_dfg::tolg(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs = p.do_tolg();

  if (lgs.empty()) {
    error("failed to generate any lgraph from lnast");
  } else {
    var.add(lgs);
  }
}


std::vector<LGraph *> Inou_lnast_dfg::do_tolg() {
  Lbench b("inou.lnast_dfg.do_tolg");
  I(!files.empty());
  I(!path.empty());

  std::vector<LGraph *> lgs;

  for (auto &itr_f : absl::StrSplit(files, ',')) {
    const auto f = std::string(itr_f);
    Lnast_parser lnast_parser(f);

    lnast = lnast_parser.ref_lnast();
    lnast->ssa_trans();

    auto        pos = f.rfind('/');
    std::string basename;
    if (pos != std::string::npos)
      basename = f.substr(pos + 1);
    else
      basename = f;

    auto pos2 = basename.rfind('.');

    if (pos2 != std::string::npos)
      basename = basename.substr(0, pos2);

    LGraph *dfg = LGraph::create(path, basename, f);

    lnast2lgraph(dfg);

    lgs.push_back(dfg);
  }

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
    if (ntype.is_assign()) {
      process_ast_assign_op(dfg, lnidx);
    } else if (ntype.is_binary_op()) {
      process_ast_binary_op(dfg, lnidx);
    } else if (ntype.is_dot()) {
      process_ast_dot_op(lnidx);
    } else if (ntype.is_select()) {
      process_ast_select_op(lnidx);
    } else if (ntype.is_unary_op()) { //FIXME->sh: to be deprecated
      process_ast_unary_op(dfg, lnidx);
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
    } else if (ntype.is_const()) {
      I(lnast->get_name(lnidx) == "default_const");
      continue;
    } else {
      I(false);
      return;
    }
  }
}


void Inou_lnast_dfg::process_ast_concat_op(LGraph *dfg, const Lnast_nid &lnidx_concat) {
  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto tn_spin    = tup_add.setup_sink_pin(0); //tuple name
  auto kn_spin    = tup_add.setup_sink_pin(1); //key name, unknown when concatenating
  auto kp_spin    = tup_add.setup_sink_pin(2); //key pos
  auto value_spin = tup_add.setup_sink_pin(3); //value

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
  fmt::print("tup_add dpin name is:{}\n", lnast->get_sname(c1_concat));
  fmt::print("dpin name from map is:{}\n", name2dpin[tup_add.get_driver_pin().get_name()].get_name());

}


Node_pin Inou_lnast_dfg::setup_tuple_chain_new_max_pos(LGraph *dfg, const Node_pin &tn_dpin) {
  uint32_t max = 0;
  auto chain_itr = tn_dpin.get_node();
  while (chain_itr.get_type().op != TupRef_Op) {
    if (chain_itr.get_type().op == TupAdd_Op) {
      I(chain_itr.setup_sink_pin(0).is_connected()); // tuple name
      I(chain_itr.setup_sink_pin(2).is_connected()); // key pos
      auto dnode_of_kp_spin = chain_itr.setup_sink_pin(2).inp_edges().begin()->driver.get_node();
      //FIXME->sh: constant propagation problem again!? now assume the dnode of kp_spin is always a well-defined constant
      if (dnode_of_kp_spin.get_type_const_value() > max)
        max = dnode_of_kp_spin.get_type_const_value();
      auto next_itr = chain_itr.setup_sink_pin(0).inp_edges().begin()->driver.get_node();
      chain_itr = next_itr;
    } else if (chain_itr.get_type().op == Or_Op) {
      I(chain_itr.setup_sink_pin(0).inp_edges().size() == 1);
      auto next_itr = chain_itr.setup_sink_pin(0).inp_edges().begin()->driver.get_node();
      chain_itr = next_itr;
    } else {
      I(false); //compile error: tuple chain must only contains TupRef_Op or Or_Op
    }
  }

  auto new_pos_str = "0d" + std::to_string(max + 1);
  return resolve_constant(dfg, new_pos_str).setup_driver_pin();
}



void Inou_lnast_dfg::process_ast_binary_op(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  auto opr_node = setup_node_operator_and_target(dfg, lnidx_opr).get_node();

  for (auto opr_child = lnast->children(lnidx_opr).begin();
       opr_child != lnast->children(lnidx_opr).end(); ++opr_child) {
    if (opr_child == lnast->children(lnidx_opr).begin())
      continue; // already handled at setup_node_operator_and_target();
    else {
      auto child_name = lnast->get_sname(*opr_child);
      Node_pin opd;
      if (name2lnidx.find(child_name) != name2lnidx.end()) {
        opd = add_tuple_get_from_dot_or_sel(dfg, name2lnidx[child_name]);
      } else {
        opd = setup_ref_node_dpin(dfg, *opr_child);
      }
      dfg->add_edge(opd, opr_node.setup_sink_pin(0));// FIXME->sh: the sink_pin should be determined by the functionality, not just zero
    }
  };
}

void Inou_lnast_dfg::process_ast_assign_op(LGraph *dfg, const Lnast_nid &lnidx_assign) {
  auto c0 = lnast->get_first_child(lnidx_assign);
  auto c1 = lnast->get_sibling_next(c0);
  auto c0_name = lnast->get_sname(c0);
  auto c1_name = lnast->get_sname(c1);


  if ((name2lnidx.find(c0_name) != name2lnidx.end()) and name2lnidx.find(c1_name) != name2lnidx.end()) {
    auto ast_opr_idx = name2lnidx[c1_name];
    //rhs example: (1)bar = tup.foo; (2) bar = tup.foo + tup[1]
    if (lnast->get_type(ast_opr_idx).is_dot() or lnast->get_type(ast_opr_idx).is_select())
      add_tuple_get_from_dot_or_sel(dfg, name2lnidx[c1_name]);

    ast_opr_idx = name2lnidx[c0_name];
    //lhs cases: (1)tup.foo = bar; (2)tup[1] = bar;
    if (lnast->get_type(ast_opr_idx).is_dot()) {
      add_tuple_add_from_dot(dfg, name2lnidx[c0_name], lnidx_assign);
    } else {
      add_tuple_add_from_sel(dfg, name2lnidx[c0_name], lnidx_assign);
    }
    return;
  }


  if (name2lnidx.find(c0_name) != name2lnidx.end()) {
    auto ast_opr_idx = name2lnidx[c0_name];
    //lhs cases: (1)tup.foo = bar; (2)tup[1] = bar;
    if (lnast->get_type(ast_opr_idx).is_dot()) {
      add_tuple_add_from_dot(dfg, name2lnidx[c0_name], lnidx_assign);
    } else {
      add_tuple_add_from_sel(dfg, name2lnidx[c0_name], lnidx_assign);
    }
    return;
  }

  if (name2lnidx.find(c1_name) != name2lnidx.end()) {
    auto ast_opr_idx = name2lnidx[c1_name];
    //rhs example: (1)bar = tup.foo; (2) bar = tup.foo + tup[1]
    if (lnast->get_type(ast_opr_idx).is_dot() or lnast->get_type(ast_opr_idx).is_select())
      add_tuple_get_from_dot_or_sel(dfg, name2lnidx[c1_name]);
  }

  Node_pin opr  = setup_node_assign_and_target(dfg, lnidx_assign);
  Node_pin opd1 = setup_ref_node_dpin(dfg, c1);
  GI(opd1.get_node().get_type().op != U32Const_Op, opd1.get_bits() == 0);

  dfg->add_edge(opd1, opr);
}


void Inou_lnast_dfg::process_ast_tuple_struct(LGraph *dfg, const Lnast_nid &lnidx_tup) {
  std::string tup_name;
  int kp = 0;

  //note: each new tuple element will be the new tuple chain tail and inherit the tuple name
  for (auto tup_child = lnast->children(lnidx_tup).begin(); tup_child != lnast->children(lnidx_tup).end(); ++tup_child) {
    if (tup_child == lnast->children(lnidx_tup).begin()) {
      tup_name = lnast->get_sname(*tup_child);
      setup_tuple_ref(dfg, tup_name);
    } else {
      I(lnast->get_type(*tup_child).is_assign());
      auto c0      = lnast->get_first_child(*tup_child);
      auto c1      = lnast->get_sibling_next(c0);
      auto c0_name = lnast->get_sname(c0);
      auto c1_name = lnast->get_sname(c1);

      auto tn_dpin    = setup_tuple_ref(dfg, tup_name);
      auto kn_dpin    = setup_tuple_key(dfg, c0_name);
      auto kp_dnode   = resolve_constant(dfg, "0d" + std::to_string(kp));
      auto kp_dpin    = kp_dnode.setup_driver_pin();
      auto value_dpin = setup_ref_node_dpin(dfg, c1);

      auto tup_add    = dfg->create_node(TupAdd_Op);
      auto tn_spin    = tup_add.setup_sink_pin(0); //tuple name
      auto kn_spin    = tup_add.setup_sink_pin(1); //key name
      auto kp_spin    = tup_add.setup_sink_pin(2); //key position is unknown before tuple resolving
      auto value_spin = tup_add.setup_sink_pin(3); //value

      dfg->add_edge(tn_dpin, tn_spin);
      if (kn_dpin != Node_pin())
        dfg->add_edge(kn_dpin, kn_spin);
      dfg->add_edge(kp_dpin, kp_spin);
      dfg->add_edge(value_dpin, value_spin);

      kp++;
      name2dpin[tup_name] = tup_add.setup_driver_pin();
      tup_add.setup_driver_pin().set_name(tup_name);
    }
  }
}

Node_pin Inou_lnast_dfg::add_tuple_get_from_dot_or_sel(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  //lnidx_opr = dot or sel
  auto c0_dot = lnast->get_first_child(lnidx_opr);
  auto c1_dot = lnast->get_sibling_next(c0_dot);
  auto c2_dot = lnast->get_sibling_next(c1_dot);

  auto c2_dot_name = lnast->get_sname(c2_dot);

  auto tup_get = dfg->create_node(TupGet_Op);
  auto tn_spin = tup_get.setup_sink_pin(0); // tuple name
  auto kn_spin = tup_get.setup_sink_pin(1); // key name
  auto kp_spin = tup_get.setup_sink_pin(2); // key pos

  auto tn_dpin = setup_tuple_ref(dfg, lnast->get_sname(c1_dot));
  dfg->add_edge(tn_dpin, tn_spin);

  if (is_const(c2_dot_name)) {
    auto kp_dpin = setup_ref_node_dpin(dfg, c2_dot);
    dfg->add_edge(kp_dpin, kp_spin);
  } else {
    auto kn_dpin = setup_tuple_key(dfg, lnast->get_sname(c2_dot));
    dfg->add_edge(kn_dpin, kn_spin);
  }


  name2dpin[lnast->get_sname(c0_dot)] = tup_get.setup_driver_pin();
  tup_get.setup_driver_pin().set_name(lnast->get_sname(c0_dot));
  fmt::print("tup_get dpin name is:{}\n", lnast->get_sname(c0_dot));
  fmt::print("dpin name from map is:{}\n", name2dpin[tup_get.get_driver_pin().get_name()].get_name());

  return tup_get.get_driver_pin();
}


Node_pin Inou_lnast_dfg::add_tuple_add_from_sel(LGraph *dfg, const Lnast_nid &lnidx_sel, const Lnast_nid &lnidx_assign) {
  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto tn_spin    = tup_add.setup_sink_pin(0); //tuple name
  auto kn_spin    = tup_add.setup_sink_pin(1); //key name, create it but still unknown for now
  auto kp_spin    = tup_add.setup_sink_pin(2); //key pos
  auto value_spin = tup_add.setup_sink_pin(3); //value

  auto c0_sel = lnast->get_first_child(lnidx_sel); //c0: intermediate name for select.
  auto c1_sel = lnast->get_sibling_next(c0_sel);   //c1: tuple name
  auto c2_sel = lnast->get_sibling_next(c1_sel);   //c2: key position


  auto target_subs = lnast->get_subs(c1_sel) == 0 ? 0 : lnast->get_subs(c1_sel) - 1 ; //FIXME->sh: need check with __bits attr.
  auto target_tuple_ref_name = absl::StrCat(std::string(lnast->get_name(c1_sel)), "_", lnast->get_subs(c1_sel)-1);
  // auto tn_dpin = setup_tuple_ref(dfg, lnast->get_sname(c1_sel));
  auto tn_dpin = setup_tuple_ref(dfg, target_tuple_ref_name);

  dfg->add_edge(tn_dpin, tn_spin);

  auto kp_dpin = setup_ref_node_dpin(dfg, c2_sel);
  dfg->add_edge(kp_dpin, kp_spin);

  auto c0_assign = lnast->get_first_child(lnidx_assign);
  auto c1_assign = lnast->get_sibling_next(c0_assign);
  auto value_dpin = setup_ref_node_dpin(dfg, c1_assign);
  dfg->add_edge(value_dpin, value_spin);

  name2dpin[lnast->get_sname(c1_sel)] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name(lnast->get_sname(c1_sel)); //note: tuple ref semantically move to here
  fmt::print("tup_add dpin name is:{}\n", lnast->get_sname(c1_sel));
  fmt::print("dpin name from map is:{}\n", name2dpin[tup_add.get_driver_pin().get_name()].get_name());

  return tup_add.get_driver_pin();
}


Node_pin Inou_lnast_dfg::add_tuple_add_from_dot(LGraph *dfg, const Lnast_nid &lnidx_dot, const Lnast_nid &lnidx_assign) {
  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto tn_spin    = tup_add.setup_sink_pin(0); //tuple name
  auto kn_spin    = tup_add.setup_sink_pin(1); //key name
  auto kp_spin    = tup_add.setup_sink_pin(2); //key position is unknown before tuple resolving
  auto value_spin = tup_add.setup_sink_pin(3); //value

  auto c0_dot = lnast->get_first_child(lnidx_dot); //c0: intermediate name for dot.
  auto c1_dot = lnast->get_sibling_next(c0_dot);   //c1: tuple name
  auto c2_dot = lnast->get_sibling_next(c1_dot);   //c2: key name

  auto target_subs = lnast->get_subs(c1_dot) == 0 ? 0 : lnast->get_subs(c1_dot) - 1 ;
  auto target_tuple_ref_name = absl::StrCat(std::string(lnast->get_name(c1_dot)), "_", target_subs);
  // auto tn_dpin = setup_tuple_ref(dfg, lnast->get_sname(c1_dot));
  auto tn_dpin = setup_tuple_ref(dfg, target_tuple_ref_name);
  dfg->add_edge(tn_dpin, tn_spin);

  auto kn_dpin = setup_tuple_key(dfg, lnast->get_sname(c2_dot));
  dfg->add_edge(kn_dpin, kn_spin);

  auto c0_assign = lnast->get_first_child(lnidx_assign);
  auto c1_assign = lnast->get_sibling_next(c0_assign);
  auto value_dpin = setup_ref_node_dpin(dfg, c1_assign);
  dfg->add_edge(value_dpin, value_spin);

  name2dpin[lnast->get_sname(c1_dot)] = tup_add.setup_driver_pin();
  tup_add.setup_driver_pin().set_name(lnast->get_sname(c1_dot)); //note: tuple ref semantically move to here
  fmt::print("tup_add dpin name is:{}\n", lnast->get_sname(c1_dot));
  fmt::print("dpin name from map is:{}\n", name2dpin[tup_add.get_driver_pin().get_name()].get_name());

  return tup_add.get_driver_pin();
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
  if (key_name.substr(0,4)== "null")
    return Node_pin();

  if (name2dpin.find(key_name) == name2dpin.end()) {
    auto dpin = dfg->create_node(TupKey_Op).setup_driver_pin();
    dpin.set_name(key_name);
    name2dpin[key_name] = dpin;
  }
  return name2dpin[key_name];
}


void Inou_lnast_dfg::process_ast_dot_op(const Lnast_nid &lnidx_dot) {
  //note: the opr name is stored in 1st child in lnast
  auto c0 = lnast->get_first_child(lnidx_dot);
  name2lnidx[lnast->get_sname(c0)] = lnidx_dot;
}

void Inou_lnast_dfg::process_ast_select_op(const Lnast_nid &lnidx_sel) {
  //note: the opr name is stored in 1st child in lnast
  auto c0 = lnast->get_first_child(lnidx_sel);
  name2lnidx[lnast->get_sname(c0)] = lnidx_sel;
  fmt::print("select_op target_name:{}\n", lnast->get_sname(c0));
  fmt::print("stored select first child:{}\n", lnast->get_sname(lnast->get_first_child(name2lnidx[lnast->get_sname(c0)])));
}



// note: for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node_pin Inou_lnast_dfg::setup_node_operator_and_target(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  const auto c0 = lnast->get_first_child(lnidx_opr);
  const auto c0_name = lnast->get_sname(c0);

  //// note: generally, there won't be a case that the target node point to a output/reg directly
  //         this is not allowed by LNAST.

  const auto lg_ntype_op = decode_lnast_op(lnidx_opr);
  auto node_dpin     = dfg->create_node(lg_ntype_op).setup_driver_pin(0);
  name2dpin[c0_name] = node_dpin;
  node_dpin.set_name(c0_name);
  return node_dpin;
}

Node_pin Inou_lnast_dfg::setup_node_assign_and_target(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  const auto c0   = lnast->get_first_child(lnidx_opr);
  const auto c0_name = lnast->get_sname(c0);
  if (c0_name.substr(0,1) == "%") {
    if(dfg->is_graph_output(c0_name.substr(1))) {
      return dfg->get_graph_output(c0_name.substr(1));  //get rid of '%' char
    } else {
      setup_ref_node_dpin(dfg, c0);
      return dfg->get_graph_output(c0_name.substr(1));
    }
  } else if (c0_name.substr(0,1) == "#") {
    return setup_ref_node_dpin(dfg, c0).get_node().setup_sink_pin("D");  //FIXME->sh: check later
  }

  auto equal_node =  dfg->create_node(Or_Op);
  name2dpin[c0_name] = equal_node.setup_driver_pin(1); //check
  name2dpin[c0_name].set_name(c0_name);
  return equal_node.setup_sink_pin(0);
}


//note: for both target and operands, except the new io, reg, and const, the node and its dpin
//      should already be in the table as the operand comes from existing operator output
Node_pin Inou_lnast_dfg::setup_ref_node_dpin(LGraph *dfg, const Lnast_nid &lnidx_opd) {

  auto name = lnast->get_sname(lnidx_opd);
  assert(!name.empty());

  const auto it = name2dpin.find(name);
  if (it != name2dpin.end())
    return it->second;

  Node_pin node_dpin;

  if (is_output(name)) {
    // Port_invalid pos, means I do not care about position
    dfg->add_graph_output(name.substr(1), Port_invalid, 0);
    node_dpin = dfg->get_graph_output_driver_pin(name.substr(1));
  } else if (is_input(name)) {
    node_dpin = dfg->add_graph_input(name.substr(1), Port_invalid, 0);
  } else if (is_register(name)) {
    node_dpin = dfg->create_node(FFlop_Op).setup_driver_pin();
  } else if (is_const(name)) {
    node_dpin = resolve_constant(dfg, name).setup_driver_pin();
  } else {
    I(false);
  }

  node_dpin.set_name(name);
  name2dpin[name] = node_dpin;  // note: for io and reg, the %$# identifier also recorded
  return node_dpin;
}

Node_Type_Op Inou_lnast_dfg::decode_lnast_op(const Lnast_nid &lnidx_opr) {
  const auto raw_ntype = lnast->get_data(lnidx_opr).type.get_raw_ntype();
  return primitive_type_lnast2lg[raw_ntype];
}

void Inou_lnast_dfg::process_ast_unary_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_logical_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_as_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_label_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_if_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_uif_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_func_call_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_func_def_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_sub_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_for_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_while_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };
void Inou_lnast_dfg::process_ast_dp_assign_op(LGraph *dfg, const Lnast_nid &lnidx) { ; };

void Inou_lnast_dfg::setup_lnast_to_lgraph_primitive_type_mapping() {
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_invalid]     = Invalid_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_assign]      = Or_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_and] = And_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_logical_or]  = Or_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_and]         = And_Op;
  primitive_type_lnast2lg[Lnast_ntype::Lnast_ntype_or]          = Or_Op;
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

void Inou_lnast_dfg::gen_temp_lg(Eprp_var &var) {
  Inou_lnast_dfg p(var);

  // lnast to lgraph
  std::vector<LGraph *> lgs = p.do_gen_temp_lg();

  if (lgs.empty()) {
    error(fmt::format("fail to generate lgraph from lnast"));
    I(false);
  } else {
    var.add(lgs[0]);
  }
}

//FIXME: Move these later, just for isolating tests for now.

//#define basic_tests
//#define join_test //PASSING w/ exception of constants
//#define mux_test //PASSING
//#define comparison_test //PASSING
#define generic_overflow_tests //FIXME: FAILING ADD and SUB last tests (see FIXME in section)

std::vector<LGraph *> Inou_lnast_dfg::do_gen_temp_lg() {
  Lbench b("inou.gen_temp_lg.do_tolg");

  LGraph *top = LGraph::create("lgdb", "temp_bitwidth_graph", "nosource");

  //------------ construct your lgraph start-------------------

  int pos = 0;

#ifdef basic_tests

  // IO
  auto top_a = top->add_graph_input("a", pos++, 4);
  auto top_b = top->add_graph_input("b", pos++, 3);
  auto top_c = top->add_graph_input("c", pos++, 2);
  auto top_d = top->add_graph_input("d", pos++, 2);
  auto top_e = top->add_graph_input("e", pos++, 10);

  auto top_z = top->add_graph_output("z", pos++, 4);
  auto top_y = top->add_graph_output("y", pos++, 5);
  auto top_x = top->add_graph_output("x", pos++, 4);
  auto top_w = top->add_graph_output("w", pos++, 4);
  auto top_v = top->add_graph_output("v", pos++, 10);
  auto top_u = top->add_graph_output("u", pos++, 11);
  auto top_t = top->add_graph_output("t", pos++, 1);

  // Sum_Op (Sub): A:4 - B:3 = Z<15,-7>
  auto sub          = top->create_node(Sum_Op);
  auto sub_sink_1   = sub.setup_sink_pin("AU");
  auto sub_sink_2   = sub.setup_sink_pin("BU");
  auto sub_driver_1 = sub.setup_driver_pin("Y");
  top->add_edge(top_a, sub_sink_1);
  top->add_edge(top_b, sub_sink_2);
  top->add_edge(sub_driver_1, top_z);

  // Sum_Op (Add): A:4 + B:2 = Z<18,0>
  auto add          = top->create_node(Sum_Op);
  auto add_sink_1   = add.setup_sink_pin("AU");
  auto add_sink_2   = add.setup_sink_pin("AU");
  auto add_driver_1 = add.setup_driver_pin("Y");
  top->add_edge(top_a, add_sink_1);
  top->add_edge(top_c, add_sink_2);
  top->add_edge(add_driver_1, top_y);

  // Div_Op: A:4 / C:2
  auto div          = top->create_node(Div_Op);
  auto div_sink_1   = div.setup_sink_pin("AU");
  auto div_sink_2   = div.setup_sink_pin("BU");
  auto div_driver_1 = div.setup_driver_pin("Y");
  top->add_edge(top_a, div_sink_1);
  top->add_edge(top_c, div_sink_2);
  top->add_edge(div_driver_1, top_x);

  // Mult_Op: C:2 * D:2
  auto mult          = top->create_node(Mult_Op);
  auto mult_sink_1   = mult.setup_sink_pin("AU");
  auto mult_sink_2   = mult.setup_sink_pin("AU");
  auto mult_driver_1 = mult.setup_driver_pin("Y");
  top->add_edge(top_c, mult_sink_1);
  top->add_edge(top_d, mult_sink_2);
  top->add_edge(mult_driver_1, top_w);

  // Mod_Op: E:10 % D:2
  auto mod          = top->create_node(Mod_Op);
  auto mod_sink_1   = mod.setup_sink_pin("AU");
  auto mod_sink_2   = mod.setup_sink_pin("BU");
  auto mod_driver_1 = mod.setup_driver_pin("Y");
  top->add_edge(top_e, mod_sink_1);
  top->add_edge(top_d, mod_sink_2);
  top->add_edge(mod_driver_1, top_v);

  auto andN         = top->create_node(And_Op);
  auto and_sink_1   = andN.setup_sink_pin("A");
  auto and_sink_2   = andN.setup_sink_pin("A");
  auto and_sink_3   = andN.setup_sink_pin("A");
  auto and_driver_1 = andN.setup_driver_pin("Y");
  top->add_edge(top_c, and_sink_1);
  top->add_edge(top_d, and_sink_2);
  top->add_edge(top_e, and_sink_3);
  top->add_edge(and_driver_1, top_y);

  // Equals_Op
  auto const_eq_node1          = top->create_node_const(10);
  auto const_eq_node2          = top->create_node_const(10);
  auto const_eq_node1_driver_1 = const_eq_node1.setup_driver_pin("Y");
  auto const_eq_node2_driver_1 = const_eq_node2.setup_driver_pin("Y");

  auto equal          = top->create_node(Equals_Op);
  auto equal_sink_1   = equal.setup_sink_pin("AU");
  auto equal_sink_2   = equal.setup_sink_pin("AU");
  auto equal_driver_1 = equal.setup_driver_pin("Y");
  top->add_edge(const_eq_node1_driver_1, equal_sink_1);
  top->add_edge(const_eq_node2_driver_1, equal_sink_2);
  top->add_edge(equal_driver_1, top_t);

#endif

#ifdef generic_overflow_tests

  //#define add_overflow_tests //FIXME FOR SHENG: When I have an inp of 63bits, the explicit max is wrong. (max = -9223372036854775808)
  #define subt_overflow_tests

  #ifdef add_overflow_tests
  auto top_addo_a = top->add_graph_input("addo_a", pos++, 65);
  auto top_addo_b = top->add_graph_input("addo_b", pos++, 71);
  auto top_addo_c = top->add_graph_input("addo_c", pos++, 4);
  auto top_addo_d = top->add_graph_input("addo_d", pos++, 63);

  auto top_addo_w = top->add_graph_output("addo_w", pos++, 65);
  auto top_addo_x = top->add_graph_output("addo_x", pos++, 69);
  auto top_addo_y = top->add_graph_output("addo_y", pos++, 68);
  auto top_addo_z = top->add_graph_output("addo_z", pos++, 100);

  // Sum_Op (Add): A:65 + B:71 = Z:72
  auto addo          = top->create_node(Sum_Op);
  auto addo_sink_1   = addo.setup_sink_pin("AU");
  auto addo_sink_2   = addo.setup_sink_pin("AU");
  auto addo_driver_1 = addo.setup_driver_pin("Y");
  top->add_edge(top_addo_a, addo_sink_1);
  top->add_edge(top_addo_b, addo_sink_2);
  top->add_edge(addo_driver_1, top_addo_z);

  // Sum_Op (Add): A:4 + B:4 + C:65 = Z:66
  auto addo1          = top->create_node(Sum_Op);
  auto addo1_sink_1   = addo1.setup_sink_pin("AU");
  auto addo1_sink_2   = addo1.setup_sink_pin("AU");
  auto addo1_sink_3   = addo1.setup_sink_pin("AU");
  auto addo1_driver_1 = addo1.setup_driver_pin("Y");
  top->add_edge(top_addo_c, addo1_sink_1);
  top->add_edge(top_addo_c, addo1_sink_2);
  top->add_edge(top_addo_a, addo1_sink_3);
  top->add_edge(addo1_driver_1, top_addo_y);

  // Sum_Op (Add): A:65 + B:4 + C:65 = Z:67
  auto addo2          = top->create_node(Sum_Op);
  auto addo2_sink_1   = addo2.setup_sink_pin("AU");
  auto addo2_sink_2   = addo2.setup_sink_pin("AU");
  auto addo2_sink_3   = addo2.setup_sink_pin("AU");
  auto addo2_driver_1 = addo2.setup_driver_pin("Y");
  top->add_edge(top_addo_a, addo2_sink_1);
  top->add_edge(top_addo_c, addo2_sink_2);
  top->add_edge(top_addo_a, addo2_sink_3);
  top->add_edge(addo2_driver_1, top_addo_x);

  // Sum_Op (Add): A:63 + B:63 = Z:64 (this test checks adding 2 non-ovfl and getting ovfl result)
  auto addo3          = top->create_node(Sum_Op);
  auto addo3_sink_1   = addo3.setup_sink_pin("AU");
  auto addo3_sink_2   = addo3.setup_sink_pin("AU");
  auto addo3_sink_3   = addo3.setup_sink_pin("AU");
  auto addo3_driver_1 = addo3.setup_driver_pin("Y");
  top->add_edge(top_addo_d, addo3_sink_1);
  top->add_edge(top_addo_d, addo3_sink_2);
  top->add_edge(top_addo_d, addo3_sink_3);
  top->add_edge(addo3_driver_1, top_addo_w);
  #endif

  #ifdef subt_overflow_tests
  auto top_subo_a = top->add_graph_input("subo_a", pos++, 65);
  auto top_subo_b = top->add_graph_input("subo_b", pos++, 66);
  auto top_subo_c = top->add_graph_input("subo_c", pos++, 71);
  auto top_subo_d = top->add_graph_input("subo_d", pos++, 4);
  auto top_subo_e = top->add_graph_input("subo_e", pos++, 63);

  auto top_subo_w = top->add_graph_output("subo_w", pos++, 65);
  auto top_subo_x = top->add_graph_output("subo_x", pos++, 75);
  auto top_subo_y = top->add_graph_output("subo_y", pos++, 74);
  auto top_subo_z = top->add_graph_output("subo_z", pos++, 70);

  // Sum_Op (Sub): A:65 - B:71 = Z:72
  auto subo          = top->create_node(Sum_Op);
  auto subo_sink_1   = subo.setup_sink_pin("AU");
  auto subo_sink_2   = subo.setup_sink_pin("BU");
  auto subo_driver_1 = subo.setup_driver_pin("Y");
  top->add_edge(top_subo_a, subo_sink_1);
  top->add_edge(top_subo_c, subo_sink_2);
  top->add_edge(subo_driver_1, top_subo_x);

  // Sum_Op (Sub): A:71 - B:4 - C:65 = Z:73
  auto subo1          = top->create_node(Sum_Op);
  auto subo1_sink_1   = subo1.setup_sink_pin("AU");
  auto subo1_sink_2   = subo1.setup_sink_pin("BU");
  auto subo1_sink_3   = subo1.setup_sink_pin("BU");
  auto subo1_driver_1 = subo1.setup_driver_pin("Y");
  top->add_edge(top_subo_c, subo1_sink_1);
  top->add_edge(top_subo_d, subo1_sink_2);
  top->add_edge(top_subo_a, subo1_sink_3);
  top->add_edge(subo1_driver_1, top_subo_y);

  // Sum_Op (Sub): A:4 - B:65 = Z:66
  auto subo2          = top->create_node(Sum_Op);
  auto subo2_sink_1   = subo2.setup_sink_pin("AU");
  auto subo2_sink_2   = subo2.setup_sink_pin("BU");
  auto subo2_driver_1 = subo2.setup_driver_pin("Y");
  top->add_edge(top_subo_d, subo2_sink_1);
  top->add_edge(top_subo_a, subo2_sink_2);
  top->add_edge(subo2_driver_1, top_subo_z);

  // Sum_Op (Sub): A:63 - B:63 = Z:64 (this test checks subing 2 non-ovfl and getting ovfl result)
  auto subo3          = top->create_node(Sum_Op);
  auto subo3_sink_1   = subo3.setup_sink_pin("AU");
  auto subo3_sink_2   = subo3.setup_sink_pin("BU");
  auto subo3_driver_1 = subo3.setup_driver_pin("Y");
  top->add_edge(top_subo_e, subo3_sink_1);
  top->add_edge(top_subo_e, subo3_sink_2);
  top->add_edge(subo3_driver_1, top_subo_w);
  #endif

#endif

#ifdef mux_test
  //Simple 4-to-1 mux. BW should be 0 to (2^5 - 1).
  auto top_mux_a = top->add_graph_input("mux_a", pos++, 2);
  auto top_mux_b = top->add_graph_input("mux_b", pos++, 2);
  auto top_mux_c = top->add_graph_input("mux_c", pos++, 3);
  auto top_mux_d = top->add_graph_input("mux_d", pos++, 4);
  auto top_mux_e = top->add_graph_input("mux_e", pos++, 5);
  auto top_mux_z = top->add_graph_output("mux_z", pos++, 6);

  auto mux          = top->create_node(Mux_Op);
  auto mux_sink_1   = mux.setup_sink_pin("S");
  auto mux_sink_2   = mux.setup_sink_pin("A");
  auto mux_sink_3   = mux.setup_sink_pin("B");
  auto mux_sink_4   = mux.setup_sink_pin("C");
  auto mux_sink_5   = mux.setup_sink_pin("D");
  auto mux_driver_1 = mux.setup_driver_pin("Y");
  top->add_edge(top_mux_a, mux_sink_1);
  top->add_edge(top_mux_b, mux_sink_2);
  top->add_edge(top_mux_c, mux_sink_3);
  top->add_edge(top_mux_d, mux_sink_4);
  top->add_edge(top_mux_e, mux_sink_5);
  top->add_edge(mux_driver_1, top_mux_z);

  //Mux that has an input in overflow mode.
  auto top_mux_f = top->add_graph_input("mux_f", pos++, 1);
  auto top_mux_g = top->add_graph_input("mux_g", pos++, 10);
  auto top_mux_h = top->add_graph_input("mux_h", pos++, 66);
  auto top_mux_y = top->add_graph_output("mux_y", pos++, 70);

  auto muxo         = top->create_node(Mux_Op);
  auto muxo_sink_1   = muxo.setup_sink_pin("S");
  auto muxo_sink_2   = muxo.setup_sink_pin("A");
  auto muxo_sink_3   = muxo.setup_sink_pin("B");
  auto muxo_driver_1 = muxo.setup_driver_pin("Y");
  top->add_edge(top_mux_f, muxo_sink_1);
  top->add_edge(top_mux_g, muxo_sink_2);
  top->add_edge(top_mux_h, muxo_sink_3);
  top->add_edge(muxo_driver_1, top_mux_y);
#endif

#ifdef comparison_test
  //Defining all constants used.
  auto const_node1 = top->create_node_const(10);
  auto const_node2 = top->create_node_const(20);
  auto const_node3 = top->create_node_const(7);
  auto const_node4 = top->create_node_const(1);
  auto const_node5 = top->create_node_const(100);
  auto const_node1_driver = const_node1.setup_driver_pin("Y");
  auto const_node2_driver = const_node2.setup_driver_pin("Y");
  auto const_node3_driver = const_node3.setup_driver_pin("Y");
  auto const_node4_driver = const_node4.setup_driver_pin("Y");
  auto const_node5_driver = const_node5.setup_driver_pin("Y");

  //LT: 10 < 20 & 10 < 7 ... always 0.
  auto top_comp_z = top->add_graph_output("comp_z", pos++, 4);

  auto lt          = top->create_node(LessThan_Op);
  auto lt_sink_1   = lt.setup_sink_pin("AU");
  auto lt_sink_2   = lt.setup_sink_pin("BU");
  auto lt_sink_3   = lt.setup_sink_pin("BU");
  auto lt_driver_1 = lt.setup_driver_pin("Y");
  top->add_edge(const_node1_driver, lt_sink_1);
  top->add_edge(const_node2_driver, lt_sink_2);
  top->add_edge(const_node3_driver, lt_sink_3);

  top->add_edge(lt_driver_1, top_comp_z);

  //LT: 1 < 20 & 1 < 7 ... always 1.
  auto top_comp_y = top->add_graph_output("comp_y", pos++, 1);

  auto lt2          = top->create_node(LessThan_Op);
  auto lt2_sink_1   = lt2.setup_sink_pin("AU");
  auto lt2_sink_2   = lt2.setup_sink_pin("BU");
  auto lt2_sink_3   = lt2.setup_sink_pin("BU");
  auto lt2_driver_1 = lt2.setup_driver_pin("Y");
  top->add_edge(const_node4_driver, lt2_sink_1);
  top->add_edge(const_node2_driver, lt2_sink_2);
  top->add_edge(const_node3_driver, lt2_sink_3);
  top->add_edge(lt2_driver_1, top_comp_y);

  //LT: [0,7] < [0,15] & [0,7] < [0,3] ... result = [0,1].
  auto top_comp_a = top->add_graph_input("comp_a", pos++, 3);
  auto top_comp_b = top->add_graph_input("comp_b", pos++, 4);
  auto top_comp_c = top->add_graph_input("comp_c", pos++, 2);
  auto top_comp_x = top->add_graph_output("comp_x", pos++, 1);

  auto lt3          = top->create_node(LessThan_Op);
  auto lt3_sink_1   = lt3.setup_sink_pin("AU");
  auto lt3_sink_2   = lt3.setup_sink_pin("BU");
  auto lt3_sink_3   = lt3.setup_sink_pin("BU");
  auto lt3_driver_1 = lt3.setup_driver_pin("Y");
  top->add_edge(top_comp_a, lt3_sink_1);
  top->add_edge(top_comp_b, lt3_sink_2);
  top->add_edge(top_comp_c, lt3_sink_3);
  top->add_edge(lt3_driver_1, top_comp_x);

  //GT: 10 > 20 & 10 > 7 ... always 0.
  auto top_comp_w = top->add_graph_output("comp_w", pos++, 4);

  auto gt          = top->create_node(GreaterThan_Op);
  auto gt_sink_1   = gt.setup_sink_pin("AU");
  auto gt_sink_2   = gt.setup_sink_pin("BU");
  auto gt_sink_3   = gt.setup_sink_pin("BU");
  auto gt_driver_1 = gt.setup_driver_pin("Y");
  top->add_edge(const_node1_driver, gt_sink_1);
  top->add_edge(const_node2_driver, gt_sink_2);
  top->add_edge(const_node3_driver, gt_sink_3);
  top->add_edge(gt_driver_1, top_comp_w);

  //GT: 100 > 20 & 100 > 7 ... always 1.
  auto top_comp_v = top->add_graph_output("comp_v", pos++, 4);

  auto gt2          = top->create_node(GreaterThan_Op);
  auto gt2_sink_1   = gt2.setup_sink_pin("AU");
  auto gt2_sink_2   = gt2.setup_sink_pin("BU");
  auto gt2_sink_3   = gt2.setup_sink_pin("BU");
  auto gt2_driver_1 = gt2.setup_driver_pin("Y");
  top->add_edge(const_node5_driver, gt2_sink_1);
  top->add_edge(const_node2_driver, gt2_sink_2);
  top->add_edge(const_node3_driver, gt2_sink_3);
  top->add_edge(gt2_driver_1, top_comp_v);

  //GT: [0,7] < [0,15] & [0,7] < [0,3] ... result = [0,1].
  auto top_comp_d = top->add_graph_input("comp_d", pos++, 3);
  auto top_comp_e = top->add_graph_input("comp_e", pos++, 4);
  auto top_comp_f = top->add_graph_input("comp_f", pos++, 2);
  auto top_comp_u = top->add_graph_output("comp_u", pos++, 1);

  auto gt3          = top->create_node(GreaterThan_Op);
  auto gt3_sink_1   = gt3.setup_sink_pin("AU");
  auto gt3_sink_2   = gt3.setup_sink_pin("BU");
  auto gt3_sink_3   = gt3.setup_sink_pin("BU");
  auto gt3_driver_1 = gt3.setup_driver_pin("Y");
  top->add_edge(top_comp_d, gt3_sink_1);
  top->add_edge(top_comp_e, gt3_sink_2);
  top->add_edge(top_comp_f, gt3_sink_3);
  top->add_edge(gt3_driver_1, top_comp_u);

  //LTE: 10 <= 20 & 10 <= 7 ... always 0.
  auto top_comp_t = top->add_graph_output("comp_t", pos++, 4);

  auto lte          = top->create_node(LessEqualThan_Op);
  auto lte_sink_1   = lte.setup_sink_pin("AU");
  auto lte_sink_2   = lte.setup_sink_pin("BU");
  auto lte_sink_3   = lte.setup_sink_pin("BU");
  auto lte_driver_1 = lte.setup_driver_pin("Y");
  top->add_edge(const_node1_driver, lte_sink_1);
  top->add_edge(const_node2_driver, lte_sink_2);
  top->add_edge(const_node3_driver, lte_sink_3);

  top->add_edge(lte_driver_1, top_comp_t);

  //LTE: 1 <= 20 & 1 <= 1 ... always 1.
  auto top_comp_s = top->add_graph_output("comp_s", pos++, 1);

  auto lte2          = top->create_node(LessEqualThan_Op);
  auto lte2_sink_1   = lte2.setup_sink_pin("AU");
  auto lte2_sink_2   = lte2.setup_sink_pin("BU");
  auto lte2_sink_3   = lte2.setup_sink_pin("BU");
  auto lte2_driver_1 = lte2.setup_driver_pin("Y");
  top->add_edge(const_node4_driver, lte2_sink_1);
  top->add_edge(const_node2_driver, lte2_sink_2);
  top->add_edge(const_node4_driver, lte2_sink_3);
  top->add_edge(lte2_driver_1, top_comp_s);

  //LTE: [0,7] < [0,15] & [0,7] < [0,3] ... result = [0,1].
  auto top_comp_g = top->add_graph_input("comp_g", pos++, 3);
  auto top_comp_h = top->add_graph_input("comp_h", pos++, 4);
  auto top_comp_i = top->add_graph_input("comp_i", pos++, 2);
  auto top_comp_r = top->add_graph_output("comp_r", pos++, 1);

  auto lte3          = top->create_node(LessEqualThan_Op);
  auto lte3_sink_1   = lte3.setup_sink_pin("AU");
  auto lte3_sink_2   = lte3.setup_sink_pin("BU");
  auto lte3_sink_3   = lte3.setup_sink_pin("BU");
  auto lte3_driver_1 = lte3.setup_driver_pin("Y");
  top->add_edge(top_comp_g, lte3_sink_1);
  top->add_edge(top_comp_h, lte3_sink_2);
  top->add_edge(top_comp_i, lte3_sink_3);
  top->add_edge(lte3_driver_1, top_comp_r);

  //GTE: 10 >= 20 & 10 >= 7 ... always 0.
  auto top_comp_q = top->add_graph_output("comp_q", pos++, 4);

  auto gte          = top->create_node(GreaterEqualThan_Op);
  auto gte_sink_1   = gte.setup_sink_pin("AU");
  auto gte_sink_2   = gte.setup_sink_pin("BU");
  auto gte_sink_3   = gte.setup_sink_pin("BU");
  auto gte_driver_1 = gte.setup_driver_pin("Y");
  top->add_edge(const_node1_driver, gte_sink_1);
  top->add_edge(const_node2_driver, gte_sink_2);
  top->add_edge(const_node3_driver, gte_sink_3);

  top->add_edge(gte_driver_1, top_comp_q);

  //GTE: 1 <= 20 & 1 <= 1 ... always 1.
  auto top_comp_p = top->add_graph_output("comp_p", pos++, 1);

  auto gte2          = top->create_node(GreaterEqualThan_Op);
  auto gte2_sink_1   = gte2.setup_sink_pin("AU");
  auto gte2_sink_2   = gte2.setup_sink_pin("BU");
  auto gte2_sink_3   = gte2.setup_sink_pin("BU");
  auto gte2_driver_1 = gte2.setup_driver_pin("Y");
  top->add_edge(const_node5_driver, gte2_sink_1);
  top->add_edge(const_node2_driver, gte2_sink_2);
  top->add_edge(const_node5_driver, gte2_sink_3);
  top->add_edge(gte2_driver_1, top_comp_p);

  //GTE: [0,7] < [0,15] & [0,7] < [0,3] ... result = [0,1].
  auto top_comp_j = top->add_graph_input("comp_j", pos++, 3);
  auto top_comp_k = top->add_graph_input("comp_k", pos++, 4);
  auto top_comp_l = top->add_graph_input("comp_l", pos++, 2);
  auto top_comp_o = top->add_graph_output("comp_o", pos++, 1);

  auto gte3          = top->create_node(GreaterEqualThan_Op);
  auto gte3_sink_1   = gte3.setup_sink_pin("AU");
  auto gte3_sink_2   = gte3.setup_sink_pin("BU");
  auto gte3_sink_3   = gte3.setup_sink_pin("BU");
  auto gte3_driver_1 = gte3.setup_driver_pin("Y");
  top->add_edge(top_comp_j, gte3_sink_1);
  top->add_edge(top_comp_k, gte3_sink_2);
  top->add_edge(top_comp_l, gte3_sink_3);
  top->add_edge(gte3_driver_1, top_comp_o);
#endif

#ifdef shift_test
  //FIXME: Add tests in (try to do before designing pass).
#endif

#ifdef join_test
  //Join two inputs (no overflow).
  auto top_join_a = top->add_graph_input("join_a", pos++, 1);
  auto top_join_b = top->add_graph_input("join_b", pos++, 3);
  auto top_join_z = top->add_graph_output("join_z", pos++, 4);

  auto join2i          = top->create_node(Join_Op);
  auto join2i_sink_1   = join2i.setup_sink_pin(0);
  auto join2i_sink_2   = join2i.setup_sink_pin(1);
  auto join2i_driver_1 = join2i.setup_driver_pin("Y");
  top->add_edge(top_join_a, join2i_sink_1);
  top->add_edge(top_join_b, join2i_sink_2);
  top->add_edge(join2i_driver_1, top_join_z);

  //Join two constants.
  /*auto top_join_y = top->add_graph_output("join_y", pos++, 12);

  //FIXME: This won't work since how I get const value is wrong.
  auto const_j2c_node1 = top->create_node_const(10);
  auto const_j2c_node2 = top->create_node_const(20);
  auto const_j2c_node3 = top->create_node_const(7);
  auto const_j2c_node1_driver = const_j2c_node1.setup_driver_pin("Y");
  auto const_j2c_node2_driver = const_j2c_node2.setup_driver_pin("Y");
  auto const_j2c_node3_driver = const_j2c_node3.setup_driver_pin("Y");

  auto join2c          = top->create_node(Join_Op);
  auto join2c_sink_1   = join2c.setup_sink_pin(0);
  auto join2c_sink_2   = join2c.setup_sink_pin(1);
  auto join2c_sink_3   = join2c.setup_sink_pin(2);
  auto join2c_driver_1 = join2c.setup_driver_pin("Y");
  top->add_edge(const_j2c_node1_driver, join2c_sink_1);
  top->add_edge(const_j2c_node2_driver, join2c_sink_2);
  top->add_edge(const_j2c_node3_driver, join2c_sink_3);
  top->add_edge(join2c_driver_1, top_join_y);*/


  //Join two inputs (overflow on 1 input).
  auto top_join_c = top->add_graph_input("join_c", pos++, 65);
  auto top_join_d = top->add_graph_input("join_d", pos++, 5);
  auto top_join_x = top->add_graph_output("join_x", pos++, 75);

  auto join2io          = top->create_node(Join_Op);
  auto join2io_sink_1   = join2io.setup_sink_pin(0);
  auto join2io_sink_2   = join2io.setup_sink_pin(1);
  auto join2io_driver_1 = join2io.setup_driver_pin("Y");
  top->add_edge(top_join_c, join2io_sink_1);
  top->add_edge(top_join_d, join2io_sink_2);
  top->add_edge(join2io_driver_1, top_join_x);

  //Join multiple inputs (joining causes overflow).
  auto top_join_e = top->add_graph_input("join_e", pos++, 10);
  auto top_join_f = top->add_graph_input("join_f", pos++, 20);
  auto top_join_g = top->add_graph_input("join_g", pos++, 30);
  auto top_join_h = top->add_graph_input("join_h", pos++, 39);
  auto top_join_w = top->add_graph_output("join_w", pos++, 100);

  auto join2o          = top->create_node(Join_Op);
  auto join2o_sink_1   = join2o.setup_sink_pin(0);
  auto join2o_sink_2   = join2o.setup_sink_pin(1);
  auto join2o_sink_3   = join2o.setup_sink_pin(2);
  auto join2o_sink_4   = join2o.setup_sink_pin(3);
  auto join2o_driver_1 = join2o.setup_driver_pin("Y");
  top->add_edge(top_join_e, join2o_sink_1);
  top->add_edge(top_join_f, join2o_sink_2);
  top->add_edge(top_join_g, join2o_sink_3);
  top->add_edge(top_join_h, join2o_sink_4);
  top->add_edge(join2o_driver_1, top_join_w);

#endif

  //BW cases designed but no test cases written yet: shift (LogicRight, Left)

  //------------ construct your lgraph end-------------------

  std::vector<LGraph *> lgs;
  lgs.push_back(top);
  return lgs;
}
