//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <sstream>

#include <mockturtle/algorithms/cut_enumeration.hpp>

#include <mockturtle/algorithms/resubstitution.hpp>

#include <mockturtle/algorithms/refactoring.hpp>

#include <mockturtle/algorithms/collapse_mapped.hpp>

#include <mockturtle/algorithms/node_resynthesis.hpp>
#include <mockturtle/algorithms/node_resynthesis/akers.hpp>
#include <mockturtle/algorithms/node_resynthesis/direct.hpp>
#include <mockturtle/algorithms/node_resynthesis/mig_npn.hpp>
#include <mockturtle/algorithms/node_resynthesis/xmg_npn.hpp>

#include <mockturtle/algorithms/cleanup.hpp>
#include <mockturtle/generators/arithmetic.hpp>
#include <mockturtle/io/write_bench.hpp>
//#include <mockturtle/networks/aig.hpp>
#include <mockturtle/networks/klut.hpp>
#include <mockturtle/networks/mig.hpp>

#include <mockturtle/algorithms/lut_mapping.hpp>
#include <mockturtle/views/mapping_view.hpp>

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "pass.hpp"

class Pass_mockturtle : public Pass {
protected:
  static void work(Eprp_var &var);

  absl::flat_hash_map<Node::Compact, int> group_boundary;
  absl::flat_hash_map<int, mockturtle::klut_network> gid2klut;
  absl::flat_hash_map<int, std::list<Node::Compact>> group_boundary_set;
  absl::flat_hash_map<XEdge, mockturtle::klut_network::signal> edge_signal_mapping;
  void lg_partition(LGraph *g);
  void create_LUT_network(LGraph *g);

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

