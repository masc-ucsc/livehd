//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <iostream>
#include <sstream>

#include "mockturtle/algorithms/cut_enumeration.hpp"

#include "mockturtle/algorithms/resubstitution.hpp"

#include "mockturtle/algorithms/refactoring.hpp"

#include "mockturtle/algorithms/collapse_mapped.hpp"

#include "mockturtle/algorithms/node_resynthesis.hpp"
#include "mockturtle/algorithms/node_resynthesis/akers.hpp"
#include "mockturtle/algorithms/node_resynthesis/direct.hpp"
#include "mockturtle/algorithms/node_resynthesis/mig_npn.hpp"
#include "mockturtle/algorithms/node_resynthesis/xmg_npn.hpp"

#include "mockturtle/algorithms/cleanup.hpp"
#include "mockturtle/generators/arithmetic.hpp"
#include "mockturtle/io/write_bench.hpp"
//#include "mockturtle/networks/aig.hpp"
#include "mockturtle/networks/klut.hpp"
#include "mockturtle/networks/mig.hpp"

#include "mockturtle/algorithms/lut_mapping.hpp"
#include "mockturtle/views/mapping_view.hpp"

#include "mockturtle/algorithms/miter.hpp"

#include "lgbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"

#include "pass.hpp"

#define LUTIFIED_NETWORK_NAME_SIGNATURE "_lutified"
#define BIT_WIDTH_THRESHOLD 2

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
  comparator_input_signal &operator=(const comparator_input_signal &obj) {
    I(this != &obj); // Do not assign object to itself. works but wastefull
    is_signed  = obj.is_signed;
    signals = obj.signals;
    return *this;
  };
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
  absl::flat_hash_map<std::pair<unsigned int, mockturtle::klut_network::signal>,
                      std::vector<std::pair<mockturtle::klut_network::node, Port_ID>>> gid_fanin2parent_pid;
  bool lg_partition(LGraph *);
  void dfs_populate_gid(Node, const unsigned int);
  void create_MIG_network(LGraph *);
  void convert_MIG_to_KLUT(LGraph *);
  void create_lutified_lgraph(LGraph *);
  void setup_input_signal(const unsigned int &, const XEdge &, std::vector<mockturtle::mig_network::signal> &, mockturtle::mig_network &);
  void setup_output_signal(const unsigned int &, const XEdge &, std::vector<mockturtle::mig_network::signal> &, mockturtle::mig_network &);
  void split_input_signal(const std::vector<mockturtle::mig_network::signal> &, std::vector<std::vector<mockturtle::mig_network::signal>> &);
  void convert_signed_to_unsigned(const comparator_input_signal &, comparator_input_signal &, mockturtle::mig_network &);
  void shr_op(std::vector<mockturtle::mig_network::signal> &,
              const std::vector<mockturtle::mig_network::signal> &,
              const bool &, const long unsigned int &, mockturtle::mig_network &);
  void create_n_bit_k_input_mux(std::vector<std::vector<mockturtle::mig_network::signal>> const &,
                                std::vector<mockturtle::mig_network::signal> const &,
                                std::vector<mockturtle::mig_network::signal> &,
                                mockturtle::mig_network &);
  mockturtle::mig_network::signal is_equal_op(const comparator_input_signal &,
                                              const comparator_input_signal &,
                                              mockturtle::mig_network &);
  mockturtle::mig_network::signal compare_op(const comparator_input_signal &,
                                             const comparator_input_signal &,
                                             const bool &, const bool &,
                                             mockturtle::mig_network &);
  void match_bit_width_by_sign_extension(const comparator_input_signal &,
                                         const comparator_input_signal &,
                                         comparator_input_signal &,
                                         comparator_input_signal &,
                                         mockturtle::mig_network &);
  void mapping_logic_cell_lg2mig(mockturtle::mig_network::signal (mockturtle::mig_network::*)(std::vector<mockturtle::mig_network::signal> const &),
                                 mockturtle::mig_network &, const Node &, const unsigned int &);
  void mapping_comparation_cell_lg2mig(const bool &, const bool &, mockturtle::mig_network &, const Node &, const unsigned int &);
  void mapping_shift_cell_lg2mig();
  void connect_complemented_signal(LGraph *, Node_pin &, Node_pin &, const mockturtle::klut_network &, const mockturtle::klut_network::signal &);

  template<typename signal, typename Ntk>
  void create_half_adder(const signal &x, const signal &y, signal &s, signal &c, Ntk &net) {
    s = net.create_xor(x, y);
    c = net.create_and(x, y);
  }

  template<typename signal, typename Ntk>
  signal create_lt(const signal &x, const signal &y, Ntk &net) {
    return net.create_lt(x, y);
  }

  template<typename signal, typename Ntk>
  signal create_gt(const signal &x, const signal &y, Ntk &net) {
    return net.create_lt(y, x);
  }

  bool eligable_cell_op(const Node &cell) {
    switch (cell.get_type().op) {
      case Not_Op:
        //fmt::print("Node: Not_Op\n");
        break;
      case And_Op:
        //fmt::print("Node: And_Op\n");
        break;
      case Or_Op:
        //fmt::print("Node: Or_Op\n");
        break;
      case Xor_Op:
        //fmt::print("Node: Xor_Op\n");
        break;
      case Equals_Op:
        //fmt::print("Node: Equals_Op\n");
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
      case LogicShiftRight_Op: {
        //fmt::print("Node: LogicShiftRight_Op\n");
        //if Node_Pin "B" is a constant or of small bit_width
        for (const auto &in_edge : cell.inp_edges()) {
          if (in_edge.sink.get_pid() == 1 && in_edge.get_bits() > BIT_WIDTH_THRESHOLD)
            return false;
        }
        break;
      }
      case ArithShiftRight_Op: {
        //fmt::print("Node: ArithShiftRight_Op\n");
        for (const auto &in_edge : cell.inp_edges()) {
          if (in_edge.sink.get_pid() == 1 && in_edge.get_bits() > BIT_WIDTH_THRESHOLD)
            return false;
        }
        break;
      }
/*
      case DynamicShiftRight_Op:
        //fmt::print("Node: DynamicShiftRight_Op\n");
        break;
      case ShiftRight_Op:
        //fmt::print("Node: ShiftRight_Op\n");
        break;
      case ShiftLeft_Op:
        //fmt::print("Node: ShiftLeft_Op\n");
        break;
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

