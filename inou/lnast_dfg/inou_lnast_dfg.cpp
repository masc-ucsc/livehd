//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

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

  Eprp_method m2("inou.lnast_dfg.gen_temp_lg", "create temp lgraph for bitwidth", &Inou_lnast_dfg::gen_temp_lg);
  m2.add_label_optional("path", "path to put the lgraph[s]", "lgdb");
  register_inou("lnast_dfg", m2);
}

Inou_lnast_dfg::Inou_lnast_dfg(const Eprp_var &var) : Pass("inou.lnast_dfg", var), lginp_cnt(1), lgout_cnt(0) {
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

  for (const auto &f : absl::StrSplit(files, ',')) {
    Lnast_parser lnast_parser(f);

    lnast = lnast_parser.ref_lnast();
    lnast->ssa_trans();

    auto        pos = f.rfind('/');
    std::string basename;
    if (pos != std::string::npos) {
      basename = f.substr(pos + 1);
    } else {
      basename = f;
    }
    auto pos2 = basename.rfind('.');
    if (pos2 != std::string::npos) basename = basename.substr(0, pos2);

    LGraph *dfg = LGraph::create(path, basename, f);

    // lnast to dfg
    process_ast_top(dfg);

    lgs.push_back(dfg);
  }

  return lgs;
}

void Inou_lnast_dfg::process_ast_top(LGraph *dfg) {
  const auto top   = lnast->get_root();
  const auto stmts = lnast->get_first_child(top);
  process_ast_stmts(dfg, stmts);
}

void Inou_lnast_dfg::process_ast_stmts(LGraph *dfg, const mmap_lib::Tree_index &stmts) {
  for (const auto &ast_idx : lnast->children(stmts)) {
    const auto ntype = lnast->get_data(ast_idx).type;
    if (ntype.is_assign()) {
      process_ast_assign_op(dfg, ast_idx);
    } else if (ntype.is_binary_op()) {
      process_ast_binary_op(dfg, ast_idx);
    } else if (ntype.is_unary_op()) {
      process_ast_unary_op(dfg, ast_idx);
    } else if (ntype.is_logical_op()) {
      process_ast_logical_op(dfg, ast_idx);
    } else if (ntype.is_as()) {
      process_ast_as_op(dfg, ast_idx);
    } else if (ntype.is_label()) {
      process_ast_label_op(dfg, ast_idx);
    } else if (ntype.is_dp_assign()) {
      process_ast_dp_assign_op(dfg, ast_idx);
    } else if (ntype.is_if()) {
      process_ast_if_op(dfg, ast_idx);
    } else if (ntype.is_uif()) {
      process_ast_uif_op(dfg, ast_idx);
    } else if (ntype.is_func_call()) {
      process_ast_func_call_op(dfg, ast_idx);
    } else if (ntype.is_func_def()) {
      process_ast_func_def_op(dfg, ast_idx);
    } else if (ntype.is_for()) {
      process_ast_for_op(dfg, ast_idx);
    } else if (ntype.is_while()) {
      process_ast_while_op(dfg, ast_idx);
    } else {
      I(false);
      return;
    }
  }
}

void Inou_lnast_dfg::process_ast_assign_op(LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  // fmt::print("purse_assign\n");
  const Node_pin opr    = setup_node_assign_and_target(dfg, lnast_op_idx);
  const auto     child0 = lnast->get_first_child(lnast_op_idx);
  const auto     child1 = lnast->get_sibling_next(child0);
  const Node_pin opd1   = setup_node_operand(dfg, child1);

  dfg->add_edge(opd1, opr, 1);
}

void Inou_lnast_dfg::process_ast_binary_op(LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  const Node_pin opr = setup_node_operator_and_target(dfg, lnast_op_idx);

  const auto child0 = lnast->get_first_child(lnast_op_idx);
  const auto child1 = lnast->get_sibling_next(child0);
  const auto child2 = lnast->get_sibling_next(child1);
  I(lnast->get_sibling_next(child2).is_invalid());

  const Node_pin opd1 = setup_node_operand(dfg, child1);
  const Node_pin opd2 = setup_node_operand(dfg, child2);
  // I(opd1 != opd2);
  // sh_fixme: the sink_pin should be determined by the functionality, not just zero

  dfg->add_edge(opd1, opr.get_node().setup_sink_pin(0), 1);
  dfg->add_edge(opd2, opr.get_node().setup_sink_pin(0), 1);
};

// note: for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node_pin Inou_lnast_dfg::setup_node_operator_and_target(LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  const auto eldest_child = lnast->get_first_child(lnast_op_idx);
  const auto name         = lnast->get_data(eldest_child).token.get_text();
  if (name.at(0) == '%') return setup_node_operand(dfg, eldest_child);

  const auto lg_ntype_op = decode_lnast_op(lnast_op_idx);
  const auto node_dpin   = dfg->create_node(lg_ntype_op, 1).setup_driver_pin(0);
  name2dpin[name]        = node_dpin;
  return node_dpin;
}

Node_pin Inou_lnast_dfg::setup_node_assign_and_target(LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  const auto eldest_child = lnast->get_first_child(lnast_op_idx);
  const auto name         = lnast->get_data(eldest_child).token.get_text();
  if (name.at(0) == '%') {
    setup_node_operand(dfg, eldest_child);
    return dfg->get_graph_output(name.substr(1));
  }

  // maybe driver_pin 1, try and error
  //FIXME: sh: setup name of driver_pin?
  //FIXME: sh: setup name2pin on this dummy Or_Op?
  return dfg->create_node(Or_Op, 1).setup_sink_pin(0);
}

// note: for operand, except the new io and reg, the node and dpin should already be in
// the table as the operand comes from existing operator output
// FIXME: sh: what about the constant node?
Node_pin Inou_lnast_dfg::setup_node_operand(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) {
  // fmt::print("operand name:{}\n", name);

  auto name = lnast->get_data(ast_idx).token.get_text();
  assert(!name.empty());

  const auto it = name2dpin.find(name);
  if (it != name2dpin.end()) {
    return it->second;
  }

  Node_pin node_dpin;
  char     first_char = name[0];

  if (first_char == '%') {
    dfg->add_graph_output(name.substr(1), Port_invalid, 1);  // Port_invalid pos, means I do not care about position
    node_dpin = dfg->get_graph_output_driver(name.substr(1));
  } else if (first_char == '$') {
    node_dpin = dfg->add_graph_input(name.substr(1), Port_invalid, 1);
  } else if (first_char == '@') {
    node_dpin = dfg->create_node(FFlop_Op).setup_driver_pin();
    // FIXME: set flop name
  } else {
    I(false);
  }

  name2dpin[name] = node_dpin;  // note: for io, the %$ identifier also recorded
  return node_dpin;
}

Node_Type_Op Inou_lnast_dfg::decode_lnast_op(const mmap_lib::Tree_index &ast_op_idx) {
  const auto raw_ntype = lnast->get_data(ast_op_idx).type.get_raw_ntype();
  return primitive_type_lnast2lg[raw_ntype];
}

void Inou_lnast_dfg::process_ast_unary_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_logical_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_as_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_label_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_if_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_uif_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_func_call_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_func_def_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_sub_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_for_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_while_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };
void Inou_lnast_dfg::process_ast_dp_assign_op(LGraph *dfg, const mmap_lib::Tree_index &ast_idx) { ; };

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
