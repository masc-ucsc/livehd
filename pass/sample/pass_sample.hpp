//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_sample : public Pass {
protected:

  void compute_histogram(LGraph *g);
  void compute_max_depth(LGraph *g);
  void annotate_placement(LGraph *g);
  void create_sample_graph(LGraph *g);

  void do_work(LGraph *g);
  void do_wirecount(LGraph *g, int indent);

  // eprp callbacks
  static void wirecount(Eprp_var &var);
public:
  static void work(Eprp_var &var);

  Pass_sample(const Eprp_var &var);

  static void setup();

};

