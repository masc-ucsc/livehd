//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class pass_submatch : public Pass {
protected:
  static uint32_t submatch_depth;

  uint64_t hash_mffc_root(Node n);
  uint64_t hash_mffc_node(Node n_driver, uint64_t hash_sink, Port_ID pid);
  uint64_t hash_mffc_leaf(uint64_t hash_sink, Port_ID pid);
  uint64_t hash_node(Node node);
  
  inline uint32_t group_score(uint32_t group_size, uint32_t num_nodes);

  void find_mffc_group(Lgraph *g);

  void find_subs(Lgraph *g);

  void do_work(Lgraph *g);

public:
  static void work(Eprp_var &var);

  pass_submatch(const Eprp_var &var);

  static void setup();
};
