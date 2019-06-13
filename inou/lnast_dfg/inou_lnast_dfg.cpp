//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "inou_lnast_dfg.hpp"
#include "lgraph.hpp"


void setup_inou_lnast_dfg() {
  Inou_lnast_dfg p;
  p.setup();
}


void Inou_lnast_dfg::setup() {

  Eprp_method m1("inou.lnast_dfg.tolg", "parse cfg_text -> build lnast -> generate lgraph", &Inou_lnast_dfg::tolg);
  m1.add_label_required("file",  "cfg_text files to process (comma separated)");
  m1.add_label_optional("path",  "path to build the lgraph[s]", "lgdb");

  register_inou(m1);  
}


void Inou_lnast_dfg::tolg(Eprp_var &var){
  
  Inou_lnast_dfg p;

  p.opack.file  = var.get("file");
  p.opack.path  = var.get("path");

  if (p.opack.file.empty()) {
    error(fmt::format("inou.lnast_dfg.tolg needs an input cfg_text!"));
    I(false);
    return;
  }

  //parse and construct lnast

  
  std::vector<LGraph *> lgs = p.do_tolg();

  if(lgs.empty()) {
    error(fmt::format("fail to generate lgraph from lnast"));
    I(false);
  } else {
    var.add(lgs[0]);
  }
}


std::vector<LGraph *> Inou_lnast_dfg::do_tolg() {
  
  I(!opack.file.empty());
  I(!opack.path.empty());
  LGraph *dfg = LGraph::create(opack.path, opack.file, "inou_lnast_dfg");
  
  //implement lnast to dfg here

  dfg->sync();

  std::vector<LGraph *> lgs;
  lgs.push_back(dfg);

  return lgs;
}



