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

  Eprp_method m2("inou.last_dfg.tocgf", "parse cfg_text -> build lnast -> generate cfg_text", &Inou_lnast_dfg::tocfg);
  m2.add_label_required("files", "cfg_text files to process (comma separated)");
  m2.add_label_optional("path", "path to put the cgf[s]", "lgdb");

  register_inou(m2);
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
  p.lnast->ssa_trans();
  //lnast to lgraph
  std::vector<LGraph *> lgs = p.do_tolg();

  if(lgs.empty()) {
    error(fmt::format("fail to generate lgraph from lnast"));
    I(false);
  } else {
    var.add(lgs[0]);
  }
}

void Inou_lnast_dfg::tocfg(Eprp_var &var){
  error(fmt::format("method not created yet"));
  I(false);

  Inou_lnast_dfg p;

  p.opack.files = var.get("files");
  p.opack.path = var.get("path");

  if (p.opack.files.empty()) {
    error(fmt::format("inou.lnast_dft.tolg needs an input cfg_text!"));
    I(false);
    return;
  }

  // cfg_text to lnast
  p.memblock = p.setup_memblock();
  p.lnast_parser.parse("lnast", p.memblock);

  p.lnast = p.lnast_parser.get_ast().get();
  p.lnast->ssa_trans();

  // lnast to cfg_text
  for (const auto &it: lnast->depth_preorder(lnast->get_root())) {
    const auto& node_data = lnast->get_data(it);
    std::string name(node_data.token.get_text(memblock)); //str_view to string
    std::string type = lnast_parser.ntype_dbg(node_data.type);
    auto node_scope = node_data.scope;
    fmt::print(""name:{}, type{}, scope:{}\n", name, type, node_scope);
  }
  return lgs;
}


void Inou_lnast_dfg::process_ast_top(LGraph *dfg){
  const auto& top = lnast->get_root();
  const auto& statement  = lnast->get_child(top);
  const auto& statements = lnast->get_children(statement);
  process_ast_statements(dfg, statements);
}

void Inou_lnast_dfg::process_ast_statements(LGraph *dfg, const std::vector<Tree_index> &sts){
  for (const auto& ast_idx : sts) {
    const auto& op = lnast->get_data(ast_idx).type;
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


void  Inou_lnast_dfg::process_ast_pure_assign_op (LGraph *dfg, const Tree_index &ast_idx){;};
void  Inou_lnast_dfg::process_ast_binary_op      (LGraph *dfg, const Tree_index &ast_idx){;};
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
  fprintf(stderr, "Content of memblock: \n%s\n", memblock);
  if(memblock == MAP_FAILED) {
    fprintf(stderr, "error, mmap failed\n");
    exit(-3);
  }
  return memblock;
}

