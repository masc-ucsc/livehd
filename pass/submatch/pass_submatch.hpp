//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <cstdint>

#include "hhds/graph.hpp"
#include "pass.hpp"

class pass_submatch : public Pass {
protected:
  static uint32_t submatch_depth;

  uint64_t hash_mffc_root(hhds::Node_class n);
  uint64_t hash_mffc_node(hhds::Node_class n_driver, uint64_t hash_sink, hhds::Port_id pid);
  uint64_t hash_node(hhds::Node_class node);

  void find_mffc_group(hhds::Graph* g);

  void find_subs(hhds::Graph* g);

  void do_work(hhds::Graph* g);

public:
  static void work(Eprp_var& var);

  pass_submatch(const Eprp_var& var);

  static void setup();
};
