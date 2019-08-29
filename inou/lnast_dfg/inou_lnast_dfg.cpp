//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_lnast_dfg.hpp"
#include "lgraph.hpp"


void setup_inou_lnast_dfg() {
  Inou_lnast_dfg p;
  p.setup();
}


void Inou_lnast_dfg::setup() {

  Eprp_method m1("inou.lnast_dfg.tolg", "parse cfg_text -> build lnast -> generate lgraph", &Inou_lnast_dfg::tolg);
  m1.add_label_required("files",  "cfg_text files to process (comma separated)");
  m1.add_label_optional("path",   "path to put the lgraph[s]", "lgdb");

  register_inou(m1);
}


void Inou_lnast_dfg::tolg(Eprp_var &var){
  Inou_lnast_dfg p;

  p.opack.files = var.get("files");
  p.opack.path  = var.get("path");

  if (p.opack.files.empty()) {
    error(fmt::format("inou.lnast_dfg.tolg needs an input cfg_text!"));
    I(false);
    return;
  }

  //cfg_text to lnast
  p.memblock = p.setup_memblock();
  p.lnast_parser.parse("lnast", p.memblock);

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

std::vector<LGraph *> Inou_lnast_dfg::do_tolg() {
  I(!opack.files.empty());
  I(!opack.path.empty());
  LGraph *dfg = LGraph::create(opack.path, opack.files, "inou_lnast_dfg");

  for (const auto &it: lnast->depth_preorder(lnast->get_root())) {
    const auto& node_data = lnast->get_data(it);
    std::string name(node_data.token.get_text(memblock));  //str_view to string
    std::string type = lnast_parser.ntype_dbg(node_data.type);
    auto node_scope = node_data.scope;
    fmt::print("name:{}, type:{}, scope:{}\n", name, type, node_scope);
  }

  //lnast to dfg
  process_ast_top(dfg);

  std::vector<LGraph *> lgs;
  lgs.push_back(dfg);

  return lgs;
}

void Inou_lnast_dfg::process_ast_top(LGraph *dfg){
  const auto top = lnast->get_root();
  const auto statements = lnast->get_children(lnast->get_children(top)[0]);
  process_ast_statements(dfg, statements);
}

void Inou_lnast_dfg::process_ast_statements(LGraph *dfg, const std::vector<Tree_index> &sts){
  for (const auto& ast_idx : sts) {
    const auto op = lnast->get_data(ast_idx).type;
    if (is_pure_assign_op(op)) {
      process_ast_pure_assign_op(dfg, ast_idx);
    } else if (is_binary_op(op)) {
      process_ast_binary_op(dfg, ast_idx);
    } else if (is_unary_op(op)) {
      process_ast_unary_op(dfg, ast_idx);
    } else if (is_logical_op(op)) {
      process_ast_logical_op(dfg, ast_idx);
    } else if (is_as_op(op)) {
      process_ast_as_op(dfg, ast_idx);
    } else if (is_label_op(op)) {
      process_ast_label_op(dfg, ast_idx);
    } else if (is_dp_assign_op(op)) {
      process_ast_dp_assign_op(dfg, ast_idx);
    } else if (is_if_op(op)) {
      process_ast_if_op(dfg, ast_idx);
    } else if (is_uif_op(op)) {
      process_ast_uif_op(dfg, ast_idx);
    } else if (is_func_call_op(op)) {
      process_ast_func_call_op(dfg, ast_idx);
    } else if (is_func_def_op(op)) {
      process_ast_func_def_op(dfg, ast_idx);
    } else if (is_for_op(op)) {
      process_ast_for_op(dfg, ast_idx);
    } else if (is_while_op(op)) {
      process_ast_while_op(dfg, ast_idx);
    } else {
      I(false);
      return;
    }
  }
}

void  Inou_lnast_dfg::process_ast_pure_assign_op (LGraph *dfg, const Tree_index &lnast_op_idx) {
  fmt::print("purse_assign\n");
  const Node_pin  opr    = setup_node_operator_and_target(dfg, lnast_op_idx);
  const Node_pin  opd1   = setup_node_operand(dfg, lnast->get_children(lnast_op_idx)[1]);
  if(opr.is_graph_output()){
    dfg->add_edge(opd1, opr, 1);
  } else {
    dfg->add_edge(opd1, opr.get_node().setup_sink_pin(), 1); //maybe sink_pin 1, try and error
  }
}

void  Inou_lnast_dfg::process_ast_binary_op (LGraph *dfg, const Tree_index &lnast_op_idx) {
  const Node_pin  opr    = setup_node_operator_and_target(dfg, lnast_op_idx);
  const auto  children   = lnast->get_children(lnast_op_idx);
  I(children.size() == 3);
  //note: children[0] is the operator name and has been processed in setup_binary_operator_node
  const Node_pin  opd1   = setup_node_operand(dfg, children[1]);
  const Node_pin  opd2   = setup_node_operand(dfg, children[2]);
  I(opd1 != opd2);
  //sh_fixme: the sink_pin should be determined by the functionality, not just zero
  dfg->add_edge(opd1, opr.get_node().setup_sink_pin(0), 1);
  dfg->add_edge(opd2, opr.get_node().setup_sink_pin(0), 1);

};


//note: for operator, we must create a new node and dpin as it represents a new gate in the netlist
Node_pin Inou_lnast_dfg::setup_node_operator_and_target (LGraph *dfg, const Tree_index &lnast_op_idx) {
  const auto eldest_child = lnast->get_children(lnast_op_idx)[0];
  const auto name         = lnast->get_data(eldest_child).token.get_text(memblock);
  if (name.at(0) == '%')
    return setup_node_operand(dfg, eldest_child);

  const auto lg_ntype_op  = decode_lnast_op(lnast_op_idx);
  const auto node_dpin    = dfg->create_node(lg_ntype_op, 1).setup_driver_pin(0);
  name2dpin[name] = node_dpin;
  return node_dpin;
}


//note: for operand, except the new io and reg, the node and dpin should already be in the table as the operand comes from existing operator output
Node_pin Inou_lnast_dfg::setup_node_operand(LGraph *dfg, const Tree_index &ast_idx){
  const auto name = lnast->get_data(ast_idx).token.get_text(memblock);
  fmt::print("operand name:{}\n", name);
  Node_pin node_dpin;

  if(name2dpin.find(name) != name2dpin.end())
    return name2dpin[name];

  if (name.at(0) == '%') {
    //node_dpin = dfg->add_graph_output(name.substr(1), lgout_cnt++, 1); //ask Jose
    node_dpin = dfg->add_graph_output(name.substr(1), 100, 1);
  } else if (name.at(0) == '$') {
    node_dpin = dfg->add_graph_input(name.substr(1), lginp_cnt++, 1);
  } else if (name.at(0) == '@') {
    node_dpin = dfg->create_node(FFlop_Op).setup_driver_pin();
  } else {
    I(false);
  }

  name2dpin[name] = node_dpin; //note: for io, the %$ identifier also recorded
  return node_dpin;
}

Node_Type_Op Inou_lnast_dfg::decode_lnast_op(const Tree_index &ast_op_idx) {
  const auto op = lnast->get_data(ast_op_idx).type;
  return primitive_type_lnast2lg[op];
}


void  Inou_lnast_dfg::process_ast_unary_op       (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_logical_op     (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_as_op          (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_label_op       (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_if_op          (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_uif_op         (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_func_call_op   (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_func_def_op    (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_sub_op         (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_for_op         (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_while_op       (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_dp_assign_op   (LGraph *dfg, const Tree_index &ast_idx){;};


std::string_view Inou_lnast_dfg::setup_memblock(){
  auto file_path = opack.files;
  int fd = open(file_path.c_str(), O_RDONLY);
  if(fd < 0) {
    fprintf(stderr, "error, could not open %s\n", file_path.c_str());
    exit(-3);
  }

  struct stat sb;
  fstat(fd, &sb);
  printf("Size: %lu\n", (uint64_t)sb.st_size);

  char *memblock = (char *)mmap(NULL, sb.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
  fprintf(stderr, "Content of memblock: \n%s\n", memblock); //SH_FIXME: remove when benchmarking
  if(memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }
  return memblock;
}

void Inou_lnast_dfg::setup_lnast_to_lgraph_primitive_type_mapping(){
  primitive_type_lnast2lg [Lnast_ntype_invalid]     = Invalid_Op ;
  primitive_type_lnast2lg [Lnast_ntype_pure_assign] = Or_Op ;
  primitive_type_lnast2lg [Lnast_ntype_logical_and] = And_Op ;
  primitive_type_lnast2lg [Lnast_ntype_logical_or]  = Or_Op ;
  primitive_type_lnast2lg [Lnast_ntype_and]         = And_Op ;
  primitive_type_lnast2lg [Lnast_ntype_or]          = Or_Op ;
  primitive_type_lnast2lg [Lnast_ntype_xor]         = Xor_Op ;
  primitive_type_lnast2lg [Lnast_ntype_plus]        = Sum_Op ;
  primitive_type_lnast2lg [Lnast_ntype_minus]       = Sum_Op ;
  primitive_type_lnast2lg [Lnast_ntype_mult]        = Mult_Op ;
  primitive_type_lnast2lg [Lnast_ntype_div]         = Div_Op ;
  primitive_type_lnast2lg [Lnast_ntype_same]        = Equals_Op ;
  primitive_type_lnast2lg [Lnast_ntype_lt]          = LessThan_Op ;
  primitive_type_lnast2lg [Lnast_ntype_le]          = LessEqualThan_Op ;
  primitive_type_lnast2lg [Lnast_ntype_gt]          = GreaterThan_Op ;
  primitive_type_lnast2lg [Lnast_ntype_ge]          = GreaterEqualThan_Op ;
  //sh_fixme: to be extended ...
}
