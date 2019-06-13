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
  m1.add_label_optional("path",  "path to build the lgraph[s]", "lgdb");

  register_inou(m1);  
}


void Inou_lnast_dfg::tolg(Eprp_var &var){
  Inou_lnast_dfg p;

  p.opack.files  = var.get("files");
  p.opack.path  = var.get("path");

  if (p.opack.files.empty()) {
    error(fmt::format("inou.lnast_dfg.tolg needs an input cfg_text!"));
    I(false);
    return;
  }

  //cfg_text to lnast
  p.memblock = p.setup_memblock();
  p.lnast_parser.parse("lnast", p.memblock);

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
  auto lnast = lnast_parser.get_ast().get(); //unique_ptr lend its ownership

  //implement lnast to dfg here
  for (const auto &it: lnast->depth_preorder(lnast->get_root()) ) {

    const auto& node_data = lnast->get_data(it);
    std::string node_name(node_data.node_token.get_text(memblock)); //str_view to string
    std::string node_type  = lnast_parser.ntype_dbg(node_data.node_type);
    auto        node_scope = node_data.scope;
    fmt::print("name:{}, type:{}, scope:{}\n", node_name, node_type, node_scope);
  }

  dfg->sync();

  std::vector<LGraph *> lgs;
  lgs.push_back(dfg);

  return lgs;
}

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

