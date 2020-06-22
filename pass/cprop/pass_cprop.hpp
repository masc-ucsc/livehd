//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#pragma once

#include "lconst.hpp"
#include "node.hpp"
#include "pass.hpp"

class Pass_cprop : public Pass {
private:
protected:
  static void optimize(Eprp_var &var);

  void collapse_forward_same_op(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_always_pin0(Node &node, XEdge_iterator &inp_edges_ordered);
  void collapse_forward_for_pin(Node &node, Node_pin &new_dpin);
  void try_collapse_forward(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_part_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_all_inputs_const(Node &node, XEdge_iterator &inp_edges_ordered);
  void replace_node(Node &node, const Lconst &result);

  void trans(LGraph *orig);

public:
  Pass_cprop(const Eprp_var &var);

  static void setup();
};

