//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_lnast_dfg.hpp"
#include "lgraph.hpp"
#include "lbench.hpp"

void setup_inou_lnast_dfg() {
  Inou_lnast_dfg p;
  p.setup();
}


void Inou_lnast_dfg::setup() {

  Eprp_method m1("inou.lnast_dfg.tolg", "parse cfg_text -> build lnast -> generate lgraph", &Inou_lnast_dfg::tolg);
  m1.add_label_required("files",  "cfg_text files to process (comma separated)");
  m1.add_label_optional("path",   "path to put the lgraph[s]", "lgdb");
  register_inou(m1);

  Eprp_method m2("inou.lnast_dfg.gen_temp_lg", "create temp lgraph for bitwidth", &Inou_lnast_dfg::gen_temp_lg);
  m2.add_label_optional("path",   "path to put the lgraph[s]", "lgdb");
  register_inou(m2);
}


void Inou_lnast_dfg::tolg(Eprp_var &var){
  Inou_lnast_dfg p;

  build_lnast(p, var);

  p.lnast = p.lnast_parser.get_ast().get(); //unique_ptr lend its ownership
  //p.lnast->ssa_trans();

  //lnast to lgraph
  std::vector<LGraph *> lgs = p.do_tolg();

  if(lgs.empty()) {
    error(fmt::format("fail to generate lgraph from lnast"));
    I(false);
  } else {
    var.add(lgs[0]);
  }
}

void Inou_lnast_dfg::build_lnast(Inou_lnast_dfg &p, Eprp_var &var) {
  Lbench b("inou.lnast_dfg.build_lnast");
  p.opack.files = var.get("files");
  p.opack.path  = var.get("path");

  if (p.opack.files.empty()) {
    error(fmt::format("inou.lnast_dfg.tolg needs an input cfg_text!"));
    I(false);
    return;
  }

  //cfg_text to lnast
  p.setup_memblock();
  p.lnast_parser.parse("lnast", p.memblock, p.token_list);
}

std::vector<LGraph *> Inou_lnast_dfg::do_tolg() {
  Lbench b("inou.lnast_dfg.do_tolg");
  I(!opack.files.empty());
  I(!opack.path.empty());
  auto pos = opack.files.rfind('/');
  std::string basename;
  if (pos != std::string::npos) {
    basename = opack.files.substr(pos+1);
  } else {
    basename = opack.files;
  }
  auto pos2 = basename.rfind('.');
  if (pos2 != std::string::npos)
    basename = basename.substr(0,pos2);

  LGraph *dfg = LGraph::create(opack.path, basename, opack.files);

  /*
  for (const auto &it: lnast->depth_preorder(lnast->get_root())) {
    const auto& node_data = lnast->get_data(it);
    std::string name(node_data.token.get_text(memblock));  //str_view to string
    std::string type = lnast_parser.ntype_dbg(node_data.type);
    auto node_scope = node_data.scope;
    fmt::print("name:{}, type:{}, scope:{}\n", name, type, node_scope);
  }
  */

  //lnast to dfg
  process_ast_top(dfg);

  std::vector<LGraph *> lgs;
  lgs.push_back(dfg);

  return lgs;
}

void Inou_lnast_dfg::process_ast_top(LGraph *dfg){
  const auto top = lnast->get_root();
  const auto stmt_parent = lnast->get_first_child(top);
  process_ast_statements(dfg, stmt_parent);
}

void Inou_lnast_dfg::process_ast_statements(LGraph *dfg, const mmap_lib::Tree_index &stmt_parent) {
  for (const auto& ast_idx : lnast->children(stmt_parent)) {
    const auto ntype = lnast->get_data(ast_idx).type;
    if (ntype.is_pure_assign()) {
      process_ast_pure_assign_op(dfg, ast_idx);
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

void  Inou_lnast_dfg::process_ast_pure_assign_op (LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  //fmt::print("purse_assign\n");
  const Node_pin  opr    = setup_node_pure_assign_and_target(dfg, lnast_op_idx);
  const auto child0 = lnast->get_first_child(lnast_op_idx);
  const auto child1 = lnast->get_sibling_next(child0);
  const Node_pin  opd1   = setup_node_operand(dfg, child1);

  dfg->add_edge(opd1, opr, 1);
}

void  Inou_lnast_dfg::process_ast_binary_op (LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  const Node_pin  opr    = setup_node_operator_and_target(dfg, lnast_op_idx);

  const auto child0 = lnast->get_first_child(lnast_op_idx);
  const auto child1 = lnast->get_sibling_next(child0);
  const auto child2 = lnast->get_sibling_next(child1);
  I(lnast->get_sibling_next(child2).is_invalid());

  const Node_pin  opd1   = setup_node_operand(dfg, child1);
  const Node_pin  opd2   = setup_node_operand(dfg, child2);
  //I(opd1 != opd2);
  //sh_fixme: the sink_pin should be determined by the functionality, not just zero

  dfg->add_edge(opd1, opr.get_node().setup_sink_pin(0), 1);
  dfg->add_edge(opd2, opr.get_node().setup_sink_pin(0), 1);
};


//note: for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node_pin Inou_lnast_dfg::setup_node_operator_and_target (LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  const auto eldest_child = lnast->get_first_child(lnast_op_idx);
  const auto name         = lnast->get_data(eldest_child).token.get_text(memblock);
  if (name.at(0) == '%')
    return setup_node_operand(dfg, eldest_child);

  const auto lg_ntype_op  = decode_lnast_op(lnast_op_idx);
  const auto node_dpin    = dfg->create_node(lg_ntype_op, 1).setup_driver_pin(0);
  name2dpin[name] = node_dpin;
  return node_dpin;
}

Node_pin Inou_lnast_dfg::setup_node_pure_assign_and_target (LGraph *dfg, const mmap_lib::Tree_index &lnast_op_idx) {
  const auto eldest_child = lnast->get_first_child(lnast_op_idx);
  const auto name         = lnast->get_data(eldest_child).token.get_text(memblock);
  if (name.at(0) == '%') {
    setup_node_operand(dfg, eldest_child);
    return dfg->get_graph_output(name.substr(1));
  }

  //maybe driver_pin 1, try and error
  return dfg->create_node(Or_Op, 1).setup_sink_pin(0);
}

//note: for operand, except the new io and reg, the node and dpin should already be in the table as the operand comes from existing operator output
Node_pin Inou_lnast_dfg::setup_node_operand(LGraph *dfg, const mmap_lib::Tree_index &ast_idx){
  //fmt::print("operand name:{}\n", name);

  auto       name = lnast->get_data(ast_idx).token.get_text(memblock);
  assert(!name.empty());

  const auto it = name2dpin.find(name);
  if(it != name2dpin.end()) {
    return it->second;
  }

  Node_pin node_dpin;
  char first_char = name[0];

  if (first_char == '%') {
    dfg->add_graph_output(name.substr(1), Port_invalid, 1); // Port_invalid pos, means I do not care about position
    node_dpin = dfg->get_graph_output_driver(name.substr(1));
  } else if (first_char == '$') {
    node_dpin = dfg->add_graph_input(name.substr(1), Port_invalid, 1);
  } else if (first_char == '@') {
    node_dpin = dfg->create_node(FFlop_Op).setup_driver_pin();
    // FIXME: set flop name
  } else {
    I(false);
  }

  name2dpin[name] = node_dpin; //note: for io, the %$ identifier also recorded
  return node_dpin;
}

Node_Type_Op Inou_lnast_dfg::decode_lnast_op(const mmap_lib::Tree_index &ast_op_idx) {
  const auto raw_ntype = lnast->get_data(ast_op_idx).type.get_raw_ntype();
  return primitive_type_lnast2lg[raw_ntype];
}


void  Inou_lnast_dfg::process_ast_unary_op       (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_logical_op     (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_as_op          (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_label_op       (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_if_op          (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_uif_op         (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_func_call_op   (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_func_def_op    (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_sub_op         (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_for_op         (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_while_op       (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_dp_assign_op   (LGraph *dfg, const mmap_lib::Tree_index &ast_idx){;};


void Inou_lnast_dfg::setup_memblock(){
  auto file_path = opack.files;
  int fd = open(file_path.c_str(), O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "error, could not open %s\n", file_path.c_str());
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);
  printf("Size: %lu FIXME: munmap at the end of the pass or memory leak\n", (uint64_t)sb.st_size);

  char *mem = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  //fprintf(stderr, "Content of memblock: \n%s\n", memblock); //SH_FIXME: remove when benchmarking
  if(mem == MAP_FAILED) {
    Pass::error("inou_lnast_dfg: mmap failed for file:{} and size:{}", file_path, sb.st_size);
  }
  memblock = mem;
}

void Inou_lnast_dfg::setup_lnast_to_lgraph_primitive_type_mapping(){
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_invalid]     = Invalid_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_pure_assign] = Or_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_logical_and] = And_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_logical_or]  = Or_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_and]         = And_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_or]          = Or_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_xor]         = Xor_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_plus]        = Sum_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_minus]       = Sum_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_mult]        = Mult_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_div]         = Div_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_same]        = Equals_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_lt]          = LessThan_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_le]          = LessEqualThan_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_gt]          = GreaterThan_Op ;
  primitive_type_lnast2lg [Lnast_ntype::Lnast_ntype_ge]          = GreaterEqualThan_Op ;
  //sh_fixme: to be extended ...
}






void Inou_lnast_dfg::gen_temp_lg(Eprp_var &var){
  Inou_lnast_dfg p;

  //lnast to lgraph
  std::vector<LGraph *> lgs = p.do_gen_temp_lg();

  if(lgs.empty()) {
    error(fmt::format("fail to generate lgraph from lnast"));
    I(false);
  } else {
    var.add(lgs[0]);
  }
}


std::vector<LGraph *> Inou_lnast_dfg::do_gen_temp_lg() {
  LGBench b("inou.gen_temp_lg.do_tolg");

  LGraph *top = LGraph::create("lgdb", "temp_bitwidth_graph", "nosource");

  //------------ construct your lgraph start-------------------

  int pos = 0;
  auto top_a = top->add_graph_input("a", pos++, 1);
  auto top_b = top->add_graph_input("b", pos++, 1);
  auto top_z = top->add_graph_output("z", pos++, 1);

  auto sum = top->create_node(Sum_Op);

  auto sum_sink_1 = sum.setup_sink_pin("AU");
  auto sum_sink_2 = sum.setup_sink_pin("BU");
  auto sum_driver_1 = sum.setup_driver_pin("Y");

  top->add_edge(top_a,sum_sink_1);
  top->add_edge(top_b,sum_sink_2);
  //top->add_edge(sum_driver_1,top_z);
  sum_driver_1.connect(top_z);
  sum_driver_1.set_bits(2);

  for (const auto &inp_edge : sum.inp_edges()) {
    auto dpin = inp_edge.driver;
    auto spin = inp_edge.sink;
    fmt::print("\tEDGE: {} -> {}\n", dpin.debug_name(), spin.debug_name());
  }
  for (const auto &out_edge : sum.out_edges()) {
    auto dpin = out_edge.driver;
    auto spin = out_edge.sink;
    fmt::print("\tEDGE: {} -> {} {}\n", dpin.debug_name(), spin.debug_name(), spin.get_pid());
  }

  //------------ construct your lgraph end-------------------

  std::vector<LGraph *> lgs;
  lgs.push_back(top);
  return lgs;
}

