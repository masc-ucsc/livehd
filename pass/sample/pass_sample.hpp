//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_sample : public Pass {
protected:
  static void work(Eprp_var &var);
  static void wirecount(Eprp_var &var);

  void compute_histogram(LGraph *g);
  void compute_max_depth(LGraph *g);
  void annotate_placement(LGraph *g);
  void create_sample_graph(LGraph *g);

public:
  Pass_sample();

  void setup() final;

  void do_work(LGraph *g);
  void do_wirecount(LGraph *g);
};

