// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "inou_lnast_dfg.hpp"


void setup_inou_lnast_dfg() { Inou_lnast_dfg::setup(); }

void Inou_lnast_dfg::setup() {
  //FIXME->sh: shoudl be deprecate and use pipe approach
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

  Eprp_method m4("inou.lnast_dfg.lglnast.tolg", "translate lg -> lnast -> lg (for verif. purposes)", &Inou_lnast_dfg::lglnverif_tolg);
  register_inou("lnast_dfg", m4);

  // FIXME->sh: when stable, change method name to just tolg()
  Eprp_method m5("inou.lnast_dfg.tolg_from_pipe", "Pyrope code-> build lnast -> generate lgraph", &Inou_lnast_dfg::tolg_from_pipe);
  register_inou("lnast_dfg", m5);
}

Inou_lnast_dfg::Inou_lnast_dfg(const Eprp_var &var) : Pass("inou.lnast_dfg", var) {
  setup_lnast_to_lgraph_primitive_type_mapping();
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
    // FIXME->sh: how to use switch to gain performance?
    if (ntype.is_assign()) {
      process_ast_assign_op(dfg, lnidx);
    } else if (ntype.is_nary_op()) {
      process_ast_nary_op(dfg, lnidx);
    } else if (ntype.is_dot()) {
      process_ast_dot_op(lnidx);
    } else if (ntype.is_select()) {
      process_ast_select_op(lnidx);
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
  auto phi        = dfg->create_node(Mux_Op);
  auto cond_spin  = phi.setup_sink_pin("S"); // Y = ~SA + SB
  auto true_spin  = phi.setup_sink_pin("B");
  auto false_spin = phi.setup_sink_pin("A");

  auto c0 = lnast->get_first_child(lnidx_phi);
  auto c1 = lnast->get_sibling_next(c0);
  auto c2 = lnast->get_sibling_next(c1);
  auto c3 = lnast->get_sibling_next(c2);

  auto cond_dpin   = setup_ref_node_dpin(dfg, c1);
  auto true_dpin   = setup_ref_node_dpin(dfg, c2);
  auto false_dpin  = setup_ref_node_dpin(dfg, c3);

  dfg->add_edge(cond_dpin, cond_spin);
  dfg->add_edge(true_dpin, true_spin);
  dfg->add_edge(false_dpin, false_spin);

  name2dpin[lnast->get_sname(c0)] = phi.setup_driver_pin();
  phi.setup_driver_pin().set_name(lnast->get_sname(c0));


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
      I(false); //compile error: tuple chain must only contains TupRef_Op or Or_Op
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
    else {
      auto child_name = lnast->get_sname(opr_child);
      if (name2lnidx.find(child_name) != name2lnidx.end()) {
        opd = add_tuple_get_from_dot_or_sel(dfg, name2lnidx[child_name]);
      } else {
        opd = setup_ref_node_dpin(dfg, opr_child);
      }
    }
    opds.emplace_back(opd);
  }

  nary_node_rhs_connections(dfg, opr_node, opds, lnast->get_type(lnidx_opr).is_minus());
}

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



Node_pin Inou_lnast_dfg::add_tuple_get_from_dot_or_sel(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  //lnidx_opr = dot or sel
  auto c0_dot = lnast->get_first_child(lnidx_opr);
  auto c1_dot = lnast->get_sibling_next(c0_dot);
  auto c2_dot = lnast->get_sibling_next(c1_dot);

  auto c2_dot_name = lnast->get_sname(c2_dot);

  auto tup_get = dfg->create_node(TupGet_Op);
  auto tn_spin = tup_get.setup_sink_pin(TN); // tuple name
  auto kn_spin = tup_get.setup_sink_pin(KN); // key name
  auto kp_spin = tup_get.setup_sink_pin(KP); // key pos

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

  return tup_get.get_driver_pin();
}


Node_pin Inou_lnast_dfg::add_tuple_add_from_sel(LGraph *dfg, const Lnast_nid &lnidx_sel, const Lnast_nid &lnidx_assign) {
  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto tn_spin    = tup_add.setup_sink_pin(TN); //tuple name
  auto kn_spin    = tup_add.setup_sink_pin(KN); //key name, create it but still unknown for now
  auto kp_spin    = tup_add.setup_sink_pin(KP); //key pos
  auto value_spin = tup_add.setup_sink_pin(KV); //value

  auto c0_sel = lnast->get_first_child(lnidx_sel); //c0: intermediate name for select.
  auto c1_sel = lnast->get_sibling_next(c0_sel);   //c1: tuple name
  auto c2_sel = lnast->get_sibling_next(c1_sel);   //c2: key position


  auto target_tuple_ref_name = absl::StrCat(std::string(lnast->get_name(c1_sel)), "_", lnast->get_subs(c1_sel) - 1);
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

  return tup_add.get_driver_pin();
}


Node_pin Inou_lnast_dfg::add_tuple_add_from_dot(LGraph *dfg, const Lnast_nid &lnidx_dot, const Lnast_nid &lnidx_assign) {

  auto tup_add    = dfg->create_node(TupAdd_Op);
  auto tn_spin    = tup_add.setup_sink_pin(TN); //tuple name
  auto kn_spin    = tup_add.setup_sink_pin(KN); //key name
  auto kp_spin    = tup_add.setup_sink_pin(KP); //key position of the key_name is recorded at tuple initialization
  auto value_spin = tup_add.setup_sink_pin(KV); //value
  auto c0_dot   = lnast->get_first_child(lnidx_dot); //c0: intermediate name for dot.
  auto c1_dot   = lnast->get_sibling_next(c0_dot);   //c1: tuple name
  auto c2_dot   = lnast->get_sibling_next(c1_dot);   //c2: key name
  auto key_name = lnast->get_sname(c2_dot);

  if (key_name.substr(0,6) == "__bits") {
    // no need to connect to tuple_ref when __bits, meaningless
    // instead, when it's $/%/#, you should create corresponding io/reg node
    setup_ref_node_dpin(dfg, c1_dot);
    auto kn_dpin = setup_tuple_key(dfg, key_name);
    dfg->add_edge(kn_dpin, kn_spin);

    auto c0_assign = lnast->get_first_child(lnidx_assign);
    auto c1_assign = lnast->get_sibling_next(c0_assign);
    auto value_dpin = setup_ref_node_dpin(dfg, c1_assign);
    dfg->add_edge(value_dpin, value_spin);
    tup_add.setup_driver_pin().set_name(lnast->get_sname(c1_dot)); // set name on driver_pin, but don't enter name2dpin table

  } else {
    auto target_subs = lnast->get_subs(c1_dot) == 0 ? 0 : lnast->get_subs(c1_dot) - 1 ;
    auto target_tuple_ref_name = absl::StrCat(std::string(lnast->get_name(c1_dot)), "_", target_subs);
    auto tn_dpin = setup_tuple_ref(dfg, target_tuple_ref_name);
    dfg->add_edge(tn_dpin, tn_spin);

    auto kn_dpin = setup_tuple_key(dfg, lnast->get_sname(c2_dot));
    dfg->add_edge(kn_dpin, kn_spin);

    auto kp_str = keyname2pos[key_name];
    auto kp_dpin = resolve_constant(dfg, kp_str).setup_driver_pin();
    dfg->add_edge(kp_dpin, kp_spin);

    auto c0_assign = lnast->get_first_child(lnidx_assign);
    auto c1_assign = lnast->get_sibling_next(c0_assign);
    auto value_dpin = setup_ref_node_dpin(dfg, c1_assign);
    dfg->add_edge(value_dpin, value_spin);
    name2dpin[lnast->get_sname(c1_dot)] = tup_add.setup_driver_pin();
    tup_add.setup_driver_pin().set_name(lnast->get_sname(c1_dot)); // tuple ref semantically move to here
  }

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




// for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node_pin Inou_lnast_dfg::setup_node_opr_and_lhs(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  const auto c0 = lnast->get_first_child(lnidx_opr);
  const auto c0_name = lnast->get_sname(c0);

  // generally, there won't be a case that the target node point to a output/reg directly
  // this is not allowed by LNAST.

  const auto lg_ntype_op = decode_lnast_op(lnidx_opr);
  auto node_dpin     = dfg->create_node(lg_ntype_op).setup_driver_pin(0);
  name2dpin[c0_name] = node_dpin;
  node_dpin.set_name(c0_name);
  return node_dpin;
}

Node_pin Inou_lnast_dfg::setup_node_assign_and_lhs(LGraph *dfg, const Lnast_nid &lnidx_opr) {
  const auto c0   = lnast->get_first_child(lnidx_opr);
  const auto c0_name = lnast->get_sname(c0);
  if (is_output(c0_name)) {
    if(dfg->is_graph_output(c0_name.substr(1, c0_name.size()-3))) {
      return dfg->get_graph_output(c0_name.substr(1, c0_name.size()-3));  //get rid of '%' char
    } else {
      setup_ref_node_dpin(dfg, c0);
      return dfg->get_graph_output(c0_name.substr(1, c0_name.size()-3));
    }
  } else if (c0_name.substr(0,1) == "#") {
    return setup_ref_node_dpin(dfg, c0).get_node().setup_sink_pin("D");  //FIXME->sh: check later
  }

  auto equal_node =  dfg->create_node(Or_Op);
  name2dpin[c0_name] = equal_node.setup_driver_pin(1); //check
  name2dpin[c0_name].set_name(c0_name);
  return equal_node.setup_sink_pin(0);
}


// for both target and operands, except the new io, reg, and const, the node and its dpin
// should already be in the table as the operand comes from existing operator output
Node_pin Inou_lnast_dfg::setup_ref_node_dpin(LGraph *dfg, const Lnast_nid &lnidx_opd) {

  auto name = lnast->get_sname(lnidx_opd);
  assert(!name.empty());

  const auto it = name2dpin.find(name);
  if (it != name2dpin.end())
    return it->second;

  Node_pin node_dpin;

  if (is_output(name)) {
    dfg->add_graph_output(name.substr(1, name.size()-3), Port_invalid, 0); // Port_invalid pos means do not care about position
    fmt::print("add graph out:{}\n", name.substr(1, name.size()-3));       // -3 means get rid of %, _0(ssa subscript)
    node_dpin = dfg->get_graph_output_driver_pin(name.substr(1, name.size()-3));
  } else if (is_input(name)) {
    node_dpin = dfg->add_graph_input(name.substr(1, name.size()-3), Port_invalid, 0);
    fmt::print("add graph inp:{}\n", name.substr(1, name.size()-3));
  } else if (is_register(name)) {
    //FIXME->sh: need to extend to Fluid_flop, Async_flop etc...
    node_dpin = dfg->create_node(SFlop_Op).setup_driver_pin();
  } else if (is_const(name)) {
    node_dpin = resolve_constant(dfg, name).setup_driver_pin();
  } else if (is_default_const(name)) {
    node_dpin = resolve_constant(dfg, "0d0").setup_driver_pin();
  } else if (is_err_var_undefined(name)) {
    node_dpin = dfg->create_node(CompileErr_Op).setup_driver_pin();
  } else {
    return node_dpin; //return empty node_pin
  }

  if (is_output(name) || is_input(name) || is_register(name)) {
    /* node_dpin.set_name(name.substr(1)); */
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



void Inou_lnast_dfg::process_ast_logical_op  (LGraph *dfg, const Lnast_nid &lnidx) { ; };
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

void Inou_lnast_dfg::lglnverif_tolg(Eprp_var &var) {
  //Assumption: Pipe some LG[s] into this command.

  //Take LG[s], translate them to into LNAST[s]
  Pass_lgraph_to_lnast p(var);
  p.trans(var);

  //For each new LNAST, translate them to LG[s]
  Inou_lnast_dfg i(var);

  std::vector<LGraph *> lgs;// = i.do_lglnverif_tolg(var);
  for (const auto &l : var.lnasts) {
    lgs.push_back(i.do_lglnverif_tolg(l));
  }
  var.add(lgs);
}

LGraph* Inou_lnast_dfg::do_lglnverif_tolg(std::shared_ptr<Lnast> llnast) {
  Lbench b("inou.lnast_dfg.do_lglnverif_tolg");

  std::string module_name = static_cast<std::string>(llnast->get_top_module_name().size(), llnast->get_top_module_name().data());
  LGraph *dfg = LGraph::create("lgdb2", module_name, "my_test");

  lnast = llnast;
  lnast->ssa_trans();
  lnast2lgraph(dfg);
  return dfg;
}

void Inou_lnast_dfg::tolg_from_pipe(Eprp_var &var) {
  Inou_lnast_dfg p(var);
  std::vector<LGraph *> lgs;
  for (const auto &ln : var.lnasts) {
    lgs = p.do_tolg_from_pipe(ln);
  }


  if (lgs.empty()) {
    error("failed to generate any lgraph from lnast");
  } else {
    var.add(lgs);
  }
}


std::vector<LGraph *> Inou_lnast_dfg::do_tolg_from_pipe(std::shared_ptr<Lnast> ln) {
    Lbench b("inou.lnast_dfg.do_tolg_from_pipe");
    lnast = ln; //FIXME->sh: should use ln directly? redesign when integrating all front-end
    LGraph *dfg = LGraph::create(path, lnast->get_top_module_name(), "from_front_end_lnast_pipe");
    std::vector<LGraph *> lgs;
    lnast->ssa_trans();
    lnast2lgraph(dfg);
    lgs.push_back(dfg);

    return lgs;
}


