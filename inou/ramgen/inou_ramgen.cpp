//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
//

#include <fstream>
#include <atomic>

#include "inou_ramgen.hpp"

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"

void Inou_ramgen_options::set(const std::string &key, const std::string &value) {

  try {
    if ( is_opt(key,"odir") ) {
      odir = value;
    }else{
      set_val(key,value);
    }
  } catch (const std::invalid_argument& ia) {
    fmt::print("ERROR: key {} has an invalid argument {}\n",key);
  }

  console->info("inou_ramgen odir:{}", odir);
}

Inou_ramgen::Inou_ramgen() {
}

Inou_ramgen::~Inou_ramgen() {
}

std::vector<LGraph *> Inou_ramgen::tolg() {
  assert(false); // generates SRAMs from a lgraph, not

  static std::vector<LGraph *> empty;
  return empty;
}

void Inou_ramgen::fromlg(std::vector<const LGraph *> &lgs) {

  assert(!opack.odir.empty());

  LGBench b;

#if 0
  //std::atomic<int> total = 0;
  int total = 0;
  for(const auto g : lgs) {
    for(auto idx : g->fast()) {
      total++;
    }
  }
  fmt::print("1. total {}\n",total);
  b.sample("warmup");

  total = 0;
  for(const auto g : lgs) {
    for(auto idx : g->fast()) {
      total++;
    }
  }
  fmt::print("3. total {}\n",total);
  b.sample("for   ");

#else

  for(const auto g : lgs) {
    //fmt::print("digraph {\n");
    std::cout << "digraph {\n";
    g->each_master_root_fast([g](Index_ID src_nid) {
      const auto &node = g->node_type_get(src_nid);
      fmt::print(" {} [label=\"{}:{}\"];\n", src_nid, src_nid, node.get_name());
    });

    g->each_output_edge_fast([](Index_ID src_nid, Port_ID src_pid, Index_ID dst_nid, Port_ID dst_pid) {
      fmt::print(" {} -> {}[label=\"{}:{}\"];\n", src_nid, dst_nid, src_pid, dst_pid);
    });
    std::cout << "}\n";
  }
#endif

}
