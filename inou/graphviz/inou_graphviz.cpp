//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <fstream>
#include <atomic>

#include "inou_graphviz.hpp"

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

void Inou_graphviz_options::set(const std::string &key, const std::string &value) {

  try {
    if ( is_opt(key,"odir") ) {
      odir = value;
    }else{
      set_val(key,value);
    }
  } catch (const std::invalid_argument& ia) {
    fmt::print("ERROR: key {} has an invalid argument {}\n",key);
  }

  console->info("inou_graphviz odir:{}", odir);
}

Inou_graphviz::Inou_graphviz() {
}

Inou_graphviz::~Inou_graphviz() {
}

std::vector<LGraph *> Inou_graphviz::tolg() {
  assert(false); // generates SRAMs from a lgraph, not

  static std::vector<LGraph *> empty;
  return empty;
}

void Inou_graphviz::fromlg(std::vector<const LGraph *> &lgs) {

  assert(!opack.odir.empty());

  mkdir(opack.odir.c_str(),0755);

  for(const auto g : lgs) {

    std::string data;

    //fmt::print("digraph {\n");
    data = "digraph {\n";
    g->each_master_root_fast([g,&data](Index_ID src_nid) {
      const auto &node = g->node_type_get(src_nid);
      data += fmt::format(" {} [label=\"{}:{}\"];\n", src_nid, src_nid, node.get_name());
    });

    g->each_output_edge_fast([&data](Index_ID src_nid, Port_ID src_pid, Index_ID dst_nid, Port_ID dst_pid) {
      data += fmt::format(" {} -> {}[label=\"{}:{}\"];\n", src_nid, dst_nid, src_pid, dst_pid);
    });
    data += "}\n";

    std::string file = opack.odir + "/" + g->get_name() + ".dot";
    int fd = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd>=0) {
      write(fd,data.c_str(),data.size());
      close(fd);
    }
  }
}

