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

#include <mockturtle/algorithms/miter.hpp>

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "pass.hpp"

#define LUTIFIED_NETWORK_NAME_SIGNATURE "_lutified"

//NOTE: In a vector of signals, the LSB signal is represented by index[0]
//while the MSB signal is represented by index[size()-1]
template<typename sig>
struct Ntk_Sigs {
  unsigned int gid;
  std::vector<sig> signals;
};

struct comparator_input_signal {
  bool is_signed;
  std::vector<mockturtle::mig_network::signal> signals;
};

class Pass_mockturtle : public Pass {
protected:
  static void work(Eprp_var &var);

  absl::flat_hash_set<XEdge> input_edges, output_edges;
  absl::flat_hash_map<Node::Compact, unsigned int> node2gid;
  absl::flat_hash_map<unsigned int, mockturtle::mig_network> gid2mig;
  absl::flat_hash_map<unsigned int, mockturtle::klut_network> gid2klut;
  absl::flat_hash_map<XEdge, Ntk_Sigs<mockturtle::mig_network::signal>> edge2signal_mig;
  absl::flat_hash_map<XEdge, Ntk_Sigs<mockturtle::klut_network::signal>> edge2signal_klut;
  absl::flat_hash_map<Node::Compact, Node::Compact> old_node_to_new_node, new_node_to_old_node;
  absl::flat_hash_map<std::pair<unsigned int, mockturtle::klut_network::node>, Node::Compact> gidMTnode2LGnode;
  absl::flat_hash_map<std::pair<unsigned int, mockturtle::klut_network::signal>, std::vector<std::pair<mockturtle::klut_network::node, Port_ID>>> gid_fanin2parent_pid;
  void lg_partition(LGraph *);
  void dfs_populate_gid(Node, const unsigned int);
  void create_MIG_network(LGraph *);
  void convert_MIG_to_KLUT(LGraph *);
  void create_lutified_lgraph(LGraph *);
  void setup_input_signal(const unsigned int &, const XEdge &, std::vector<mockturtle::mig_network::signal> &, mockturtle::mig_network &);
  void setup_output_signal(const unsigned int &, const XEdge &, std::vector<mockturtle::mig_network::signal> &, mockturtle::mig_network &);
  void split_input_signal(const std::vector<mockturtle::mig_network::signal> &, std::vector<std::vector<mockturtle::mig_network::signal>> &);
  void convert_signed_to_unsigned(const comparator_input_signal &, comparator_input_signal &);
  mockturtle::mig_network::signal calc_lt(const comparator_input_signal &, const comparator_input_signal &, mockturtle::mig_network &);

  template<typename signal, typename Ntk>
  signal create_eq(const signal &x, const signal &y, Ntk &net) {
    signal not_x = net.create_not(x);
    signal not_y = net.create_not(y);
    signal x_and_y = net.create_and(x, y);
    signal not_x_and_not_y = net.create_and(not_x, not_y);
    signal is_x_y_equal = net.create_or(x_and_y, not_x_and_not_y);
    return is_x_y_equal;
  }

  bool eligable_cell_op(const Node_Type_Op &cell_op) {
    switch (cell_op) {
      case Not_Op:
        //fmt::print("Node: Not_Op\n");
        break;
      case And_Op:
        //fmt::print("Node: And_Op\n");
        break;
/*
      case Or_Op:
        //fmt::print("Node: Or_Op\n");
        break;
      case Xor_Op:
        //fmt::print("Node: Xor_Op\n");
        break;
      case Join_Op:
        //fmt::print("Node: Join_Op\n");
        break;
      case Pick_Op:
        //fmt::print("Node: Pick_Op\n");
        break;
*/
      case Equals_Op:
        //fmt::print("Node: Equals_Op\n");
        break;
      case LessThan_Op:
        //fmt::print("Node: LessThan_Op\n");
        break;
/*
      case GreaterThan_Op:
        //fmt::print("Node: GreaterThan_Op\n");
        break;
      case LessEqualThan_Op:
        //fmt::print("Node: LessEqualThan_Op\n");
        break;
      case GreaterEqualThan_Op:
        //fmt::print("Node: GreaterEqualThan_Op\n");
        break;
      //case ShiftRight_Op:
        //fmt::print("Node: ShiftRight_Op\n");
      //  break;
      //case ShiftLeft_Op:
        //fmt::print("Node: ShiftLeft_Op\n");
      //  break;
*/
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

