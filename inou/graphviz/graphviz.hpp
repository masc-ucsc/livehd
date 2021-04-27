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

  absl::flat_hash_map<int, std::string> color2rgb;

  void create_color_map(Lgraph *g);

  static void        populate_lg_handle_xedge(const Node &node, const XEdge &out, std::string &data, bool verbose);
  static std::string graphviz_legalize_name(std::string_view name);
  void               populate_lg_data(Lgraph *g, std::string_view dot_postfix = "");

	void save_graph(std::string_view name, std::string_view dot_postfix, const std::string &data);

public:
  void do_from_lnast(std::shared_ptr<Lnast> lnast, std::string_view dot_postfix = "");
  void do_from_lgraph(Lgraph *lg_parent, std::string_view dot_postfix = "");
  void do_hierarchy(Lgraph *g);

  Graphviz(bool _bits, bool _verbose, std::string_view _odir);
};
