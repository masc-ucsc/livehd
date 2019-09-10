//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef INOU_graphviz_H
#define INOU_graphviz_H

#include <atomic>

#include "lgraph.hpp"
#include "pass.hpp"
#include "lnast_parser.hpp"

class Inou_graphviz : public Pass {
private:
  Elab_scanner::Token_list token_list;
  std::string_view         memblock;
  Lnast_parser             lnast_parser;
protected:
  bool        bits;
  bool        verbose;
  std::string files;
  std::string odir;

  std::atomic<int> total;
  void             inc_total(Index_ID idx) {
    total++;
  };

  static void  fromlg(Eprp_var &var);
  static void  fromlnast(Eprp_var &var);
  static void  hierarchy(Eprp_var &var);

  void  do_hierarchy(LGraph* lg);

  void  do_fromlg(std::vector<LGraph *> &out);
  void  populate_lg_data(LGraph* lg);


  void  do_fromlnast(std::string_view files);
  void  populate_lnast_data(std::string_view files);

  std::string_view  setup_memblock(std::string_view files);

public:
  Inou_graphviz();

  void setup() final;
};

#endif
