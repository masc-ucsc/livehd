//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "hhds/graph.hpp"
#include "pass.hpp"

class Pass_sample : public Pass {
protected:
  void compute_histogram(hhds::Graph* g);
  void compute_max_depth(hhds::Graph* g);
  void create_sample_graph(hhds::Graph* g);

  void do_work(hhds::Graph* g);
  void do_wirecount(hhds::Graph* g, int indent);

  // eprp callbacks
  static void wirecount(Eprp_var& var);

public:
  static void work(Eprp_var& var);

  Pass_sample(const Eprp_var& var);

  static void setup();
};
