//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <atomic>
#include <fstream>

#include "eprp_utils.hpp"
#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

#include "inou_graphviz.hpp"

void setup_inou_graphviz() {
  Inou_graphviz p;
  p.setup();
}

void Inou_graphviz::setup() {

  Eprp_method m2("inou.graphviz", "export lgraph to graphviz dot format", &Inou_graphviz::fromlg);

  m2.add_label_optional("bits", "dump bits (true/false)", "false");
  m2.add_label_optional("verbose", "dump bits and wirename (true/false)", "false");

  register_inou(m2);
}

Inou_graphviz::Inou_graphviz()
    : Pass("graphviz") {

  bits = false;
  verbose = false;
}

void Inou_graphviz::fromlg(Eprp_var &var) {

  Inou_graphviz p;

  p.odir = var.get("odir");
  p.bits = var.get("bits") == "true";
  p.verbose = var.get("verbose") == "true";

  bool ok = p.setup_directory(p.odir);
  if(!ok)
    return;

  std::vector<LGraph *> lgs;
  for(const auto &l : var.lgs) {
    lgs.push_back(l);
  }

  p.do_fromlg(lgs);
}

void Inou_graphviz::do_fromlg(std::vector<LGraph *> &lgs) {

  for(const auto g : lgs) {
    const auto hier = g->get_hierarchy();
    fmt::print("hierarchy for {}\n",g->get_name());
    for(auto &[name,lgid]:hier) {
      fmt::print("  {} {}\n",name,lgid);
    }
  }
  for(const auto lg_parent : lgs) {
    populate_data(lg_parent);
    lg_parent->each_sub_graph_fast([lg_parent,this](Index_ID idx, Lg_type_id lgid, std::string_view iname){
      fmt::print("subgraph lgid:{}\n", lgid);
      LGraph *lg_child = LGraph::open(lg_parent->get_path(), lgid);
      populate_data(lg_child);
    });
  }
}

void Inou_graphviz::populate_data(LGraph* g){
  std::string data = "digraph {\n";

  g->each_master_root_fast([this, g, &data](Index_ID src_nid) {
    const auto &ntype = g->node_type_get(src_nid);
    std::string bits_str = std::to_string(g->get_bits(src_nid));
    if(verbose)
      data += fmt::format(" {} [label=\"n{}, {}, {}b\n{}\"];\n", src_nid, src_nid, ntype.get_name(), bits_str, g->get_node_wirename(src_nid));
    else if(bits)
      data += fmt::format(" {} [label=\"n{}:{}:{}b\"];\n", src_nid, src_nid, ntype.get_name(), bits_str);
    else
      data += fmt::format(" {} [label=\"n{}:{}\"];\n",  src_nid, src_nid, ntype.get_name());
  });

  g->each_output_edge_fast([&data](Index_ID src_nid, Port_ID src_pid, Index_ID dst_nid, Port_ID dst_pid) {
    data += fmt::format(" {} -> {}[label=\"{}:{}\"];\n", src_nid, dst_nid, src_pid, dst_pid);
  });
  data += "}\n";

  std::string file = absl::StrCat(odir, "/", g->get_name(), ".dot");
  int         fd   = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
  if(fd < 0) {
    Pass::error("inou.graphviz unable to create {}", file);
    return;
  }
  write(fd, data.c_str(), data.size());
  close(fd);
}

