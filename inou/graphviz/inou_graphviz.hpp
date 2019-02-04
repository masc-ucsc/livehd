//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#ifndef INOU_graphviz_H
#define INOU_graphviz_H

#include <atomic>

#include "lgraph.hpp"
#include "pass.hpp"

class Inou_graphviz : public Pass {
private:
protected:
  std::string odir;
  bool        bits;
  bool        verbose;

  std::atomic<int> total;
  void             inc_total(Index_ID idx) {
    total++;
  };

  static void fromlg(Eprp_var &var);

  void do_fromlg(std::vector<LGraph *> &out);
  void populate_data(LGraph* lg);

public:
  Inou_graphviz();

  void setup() final;
};

#endif
