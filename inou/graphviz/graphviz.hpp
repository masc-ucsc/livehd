// This file is distributed under the BSD 3-Clause License. See LICENSE for details.

// goal:
//   make inou_graphviz only act as the interface with pass/inou 
// how:
//   inou_graphviz only interface with Eprp_var and extract the need infomation
//   e.g. path/files/odir etc
//   create a graphviz object to take in the extracted info and do the jobs
// why:
//   the pass_compiler could call the do_from_lnast/lgraph directly without pass Eprp_var all around

#pragma once

#include "lgraph.hpp"
#include "lgedgeiter.hpp"
#include "lgraphbase.hpp"
#include "lnast.hpp"


class Graphviz {
private:
  const bool bits;
  const bool verbose;
  const std::string odir;


  static void populate_lg_handle_xedge(const Node &node, const XEdge &out, std::string &data);
  static std::string graphviz_legalize_name(std::string_view name);
  void populate_lg_data(LGraph *g);

public:
  void do_from_lnast(std::shared_ptr<Lnast> lnast); 
  void do_from_lgraph(LGraph *lg_parent);
  void do_hierarchy(LGraph *g);
  
  Graphviz(bool _bits, bool _verbose, std::string_view _odir);

};

