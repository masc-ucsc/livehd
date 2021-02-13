//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "lgraph.hpp"
#include "pass.hpp"
#include "node_tree.hpp"

class Pass_fplan_analyzefp : public Pass {
private:
  void print_area(const Node_tree& nt, const Tree_index& tidx) const;
  void print_children(const Node_tree& nt, const Tree_index& tidx) const;
public:
  Pass_fplan_analyzefp(const Eprp_var& var);
  static void setup();
  static void pass(Eprp_var& v);
};