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

using mockturtle_network = mockturtle::mig_network; //chose mig or xag

//NOTE: In a vector of signals, the LSB signal is represented by index[0]
//while the MSB signal is represented by index[size()-1]
template<typename sig>
struct Ntk_Sigs {
  unsigned int gid;
  std::vector<sig> signals;
};

template<typename sig>
struct comparator_input_signal {
  bool is_signed;
  std::vector<sig> signals;
  comparator_input_signal &operator=(const comparator_input_signal &obj) {
    I(this != &obj); // Do not assign object to itself. works but wasteful
    is_signed  = obj.is_signed;
    signals = obj.signals;
    return *this;
  };
};

class Pass_mockturtle : public Pass {
protected:
  static void work(Eprp_var &var);

  absl::flat_hash_set<XEdge> bdinp_edges, bdout_edges;//boundary_input/output_edges
  absl::flat_hash_map<Node::Compact, unsigned int> node2gid; //gid == group id, nodes in node2gid should be lutified
  absl::flat_hash_map<unsigned int, mockturtle_network> gid2mock;
  absl::flat_hash_map<unsigned int, mockturtle::klut_network> gid2klut;
  absl::flat_hash_map<XEdge, Ntk_Sigs<mockturtle_network::signal>> edge2signals_mock; //lg<->mig, including all boundary i/o and internal wires
  absl::flat_hash_map<XEdge, Ntk_Sigs<mockturtle::klut_network::signal>> edge2signal_klut; //lg<->klut, search edge2signals_mock table, only record i/o mapping
  absl::flat_hash_map<Node::Compact, Node::Compact> old_node_to_new_node;
  absl::flat_hash_map<std::pair<unsigned int, mockturtle::klut_network::node>, Node::Compact> gidMTnode2LGnode;
  absl::flat_hash_map<std::pair<unsigned int, mockturtle::klut_network::signal>,
                      std::vector<std::pair<mockturtle::klut_network::node, Port_ID>> > gid_fanin2parent_pid;
  bool lg_partition(LGraph *);
  void dfs_populate_gid(Node, const unsigned int);
  void create_mockturtle_network(LGraph *);
  void convert_mockturtle_to_KLUT(LGraph *);
  void create_lutified_lgraph(LGraph *);

  void connect_complemented_signal(LGraph *, Node_pin &, Node_pin &, const mockturtle::klut_network &, const mockturtle::klut_network::signal &);

  template<typename sig_type, typename ntk_type>
  void setup_input_signals(const unsigned int &, const XEdge &, std::vector<sig_type> &, ntk_type &);

  template<typename sig_type, typename ntk_type>
  void setup_output_signals(const unsigned int &, const XEdge &, std::vector<sig_type> &, ntk_type &);

  template<typename signal>
  void split_input_signal(const std::vector<signal> &, std::vector<std::vector<signal>> &);

  template<typename sig_type, typename ntk_type>
  void convert_signed_to_unsigned(const comparator_input_signal<sig_type> &, comparator_input_signal<sig_type> &, ntk_type &);

  template<typename sig_type, typename ntk_type>
  void complement_to_SMR(std::vector<sig_type> const &, std::vector<sig_type> &, ntk_type &);

  template<typename sig_type, typename ntk_type>
  void shift_op(const std::vector<sig_type> &,
                const bool &, const bool &,
                const long unsigned int &,
                std::vector<sig_type> &, ntk_type &);

  template<typename sig_type, typename ntk_type>
  void create_n_bit_k_input_mux(std::vector<std::vector<sig_type>> const &,
                                std::vector<sig_type> const &,
                                std::vector<sig_type> &,
                                ntk_type &);

  template<typename sig_type, typename ntk_type>
  sig_type is_equal_op(const comparator_input_signal<sig_type> &,
                       const comparator_input_signal<sig_type> &,
                       ntk_type &);

  template<typename sig_type, typename ntk_type>
  sig_type compare_op(const comparator_input_signal<sig_type> &,
                      const comparator_input_signal<sig_type> &,
                      const bool &, const bool &,
                      ntk_type &);

  template<typename sig_type, typename ntk_type>
  void match_bit_width_by_sign_extension(const comparator_input_signal<sig_type> &,
                                         const comparator_input_signal<sig_type> &,
                                         comparator_input_signal<sig_type> &,
                                         comparator_input_signal<sig_type> &,
                                         ntk_type &);

  template<typename sig_type, typename ntk_type>
  void mapping_logic_cell_lg2mock(sig_type (ntk_type::*)(std::vector<sig_type> const &),
                                  ntk_type &, const Node &, const unsigned int &);

  template<typename ntk_type>
  void mapping_comparison_cell_lg2mock(const bool &, const bool &, ntk_type &, const Node &, const unsigned int &);

  template<typename ntk_type>
  void mapping_shift_cell_lg2mock(const bool &, const bool &, ntk_type &, const Node &, const unsigned int &);

  template<typename ntk_type>
  void mapping_dynamic_shift_cell_lg2mock(const bool &, ntk_type &, const Node &, const unsigned int &);

  template<typename signal, typename ntk>
  void create_half_adder(const signal &x, const signal &y, signal &s, signal &c, ntk &net) {
    s = net.create_xor(x, y);
    c = net.create_and(x, y);
  }

  template<typename ntk>
  void create_full_adder(const typename ntk::signal &a, const typename ntk::signal &b, const typename ntk::signal &c_in, typename ntk::signal &s, typename ntk::signal &c_out, ntk &net) {
    typename ntk::signal a_xor_b = net.create_xor(a, b);
    typename ntk::signal a_and_b = net.create_and(a, b);
    typename ntk::signal axorb_and_cin = net.create_and(a_xor_b, c_in);
    c_out = net.create_or(a_and_b, axorb_and_cin);
    s = net.create_xor(a_xor_b, c_in);
  }

  template<typename signal, typename ntk>
  signal create_lt(const signal &x, const signal &y, ntk &net) {
    return net.create_lt(x, y);
  }

  template<typename signal, typename ntk>
  signal create_gt(const signal &x, const signal &y, ntk &net) {
    return net.create_lt(y, x);
  }

  void converting_uint32_to_signed_SMR(const uint32_t &in, uint32_t &out, bool &is_neg) {
      if (((1ULL<<31)&in) != 0) {
        out = (~in) + 1;
        is_neg = true;
      } else {
        out = in;
        is_neg = false;
      }
  }

  bool eligible_cell_op(const Node &cell) {
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
      case ShiftLeft_Op:
        //fmt::print("Node: ShiftLeft_Op\n");
        //check if Node_Pin "B" is a constant or of small bit_width
        for (const auto &in_edge : cell.inp_edges()) {
          if (in_edge.sink.get_pid() == 1 && in_edge.get_bits() > BIT_WIDTH_THRESHOLD)
            return false;
        }
        break;
      case LogicShiftRight_Op: {
        //fmt::print("Node: LogicShiftRight_Op\n");
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
      case DynamicShiftRight_Op: {
        //fmt::print("Node: DynamicShiftRight_Op\n");
        for (const auto &in_edge : cell.inp_edges()) {
          if (in_edge.sink.get_pid() == 1 && in_edge.get_bits() > BIT_WIDTH_THRESHOLD - 1)
            return false;
        }
        break;
      }
      case DynamicShiftLeft_Op: {
        //fmt::print("Node: DynamicShiftLeft_Op\n");
        for (const auto &in_edge : cell.inp_edges()) {
          if (in_edge.sink.get_pid() == 1 && in_edge.get_bits() > BIT_WIDTH_THRESHOLD - 1)
            return false;
        }
        break;
      }
/*
      case ShiftRight_Op:
        //fmt::print("Node: ShiftRight_Op\n");
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

