//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_sample : public Pass {
protected:
  void compute_histogram(Lgraph *g);
  void compute_max_depth(Lgraph *g);
  void annotate_placement(Lgraph *g);
  void create_sample_graph(Lgraph *g);

  void do_work(Lgraph *g);
  void do_wirecount(Lgraph *g, int indent);

  // eprp callbacks
  static void wirecount(Eprp_var &var);

public:
  static void work(Eprp_var &var);

  Pass_sample(const Eprp_var &var);

  static void setup();
};
