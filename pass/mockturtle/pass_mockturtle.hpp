//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "pass.hpp"

class Pass_mockturtle : public Pass {
protected:
  static void work(Eprp_var &var);

  absl::flat_hash_map<Node::Compact, int> group_boundary;
  void lg_partition(LGraph *g);

  bool eligable_cell_op(Node_Type_Op cell_op) {
    switch (cell_op) {
      case And_Op:
        //fmt::print("Node: And Gate\n");
        break;
      case Or_Op:
        //fmt::print("Node: Or Gate\n");
        break;
      case Xor_Op:
        //fmt::print("Node: Xor Gate\n");
        break;
      case LessThan_Op:
        break;
      case GreaterThan_Op:
        break;
      case LessEqualThan_Op:
        break;
      case GreaterEqualThan_Op:
        break;
      case Equals_Op:
        break;
      //case ShiftRight_Op:
      //  break;
      //case ShiftLeft_Op:
      //  break;
      case Not_Op:
        break;
      case Join_Op:
        break;
      case Pick_Op:
        break;
      default:
        //fmt::print("Node: Unknown\n");
        return false;
    }
    return true;
  }

public:
  Pass_mockturtle();

  void setup() final;

  void do_work(LGraph *g);
};

