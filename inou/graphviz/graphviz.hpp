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

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "lgraphbase.hpp"
#include "lnast.hpp"

class Graphviz {
private:
  // const bool bits;
  const bool        verbose;
  const std::string odir;

  static void        populate_lg_handle_xedge(const Node &node, const XEdge &out, std::string &data, bool verbose);
  static std::string graphviz_legalize_name(std::string_view name);
  void               populate_lg_data(LGraph *g, std::string_view dot_postfix = "");

public:
  void do_from_lnast(std::shared_ptr<Lnast> lnast, std::string_view dot_postfix = "");
  void do_from_lgraph(LGraph *lg_parent, std::string_view dot_postfix = "");
  void do_hierarchy(LGraph *g);

  Graphviz(bool _bits, bool _verbose, std::string_view _odir);
};
