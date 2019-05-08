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
  absl::flat_hash_map<XEdge, std::vector<mockturtle::klut_network::signal>> edge_signal_mapping;
  void lg_partition(LGraph *g);
  void create_LUT_network(LGraph *g);

  bool eligable_cell_op(Node_Type_Op cell_op) {
    switch (cell_op) {
      case And_Op:
        //fmt::print("Node: And_Op\n");
        break;
      case Or_Op:
        //fmt::print("Node: Or_Op\n");
        break;
      case Xor_Op:
        //fmt::print("Node: Xor_Op\n");
        break;
      case LessThan_Op:
        //fmt::print("Node: LessThan_Op\n");
        break;
      case GreaterThan_Op:
        //fmt::print("Node: GreaterThan_Op\n");
        break;
      case LessEqualThan_Op:
        //fmt::print("Node: LessEqualThan_Op\n");
        break;
      case GreaterEqualThan_Op:
        //fmt::print("Node: GreaterEqualThan_Op\n");
        break;
      case Equals_Op:
        //fmt::print("Node: Equals_Op\n");
        break;
      //case ShiftRight_Op:
        //fmt::print("Node: ShiftRight_Op\n");
      //  break;
      //case ShiftLeft_Op:
        //fmt::print("Node: ShiftLeft_Op\n");
      //  break;
      case Not_Op:
        //fmt::print("Node: Not_Op\n");
        break;
      case Join_Op:
        //fmt::print("Node: Join_Op\n");
        break;
      case Pick_Op:
        //fmt::print("Node: Pick_Op\n");
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

