//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <iostream>
#include <sstream>

#include "mockturtle/algorithms/cleanup.hpp"
#include "mockturtle/algorithms/collapse_mapped.hpp"
#include "mockturtle/algorithms/cut_enumeration.hpp"
#include "mockturtle/algorithms/equivalence_checking.hpp"
#include "mockturtle/algorithms/miter.hpp"
#include "mockturtle/algorithms/node_resynthesis.hpp"
#include "mockturtle/algorithms/node_resynthesis/akers.hpp"
#include "mockturtle/algorithms/node_resynthesis/direct.hpp"
#include "mockturtle/algorithms/node_resynthesis/mig_npn.hpp"
#include "mockturtle/algorithms/node_resynthesis/xmg_npn.hpp"
#include "mockturtle/algorithms/refactoring.hpp"
#include "mockturtle/algorithms/resubstitution.hpp"
#include "mockturtle/generators/arithmetic.hpp"
#include "mockturtle/io/write_bench.hpp"
// #include "mockturtle/networks/aig.hpp"
#include "cell.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "mockturtle/algorithms/lut_mapping.hpp"
#include "mockturtle/networks/klut.hpp"
#include "mockturtle/networks/mig.hpp"
#include "mockturtle/views/mapping_view.hpp"
#include "pass.hpp"

#define LUTIFIED_NETWORK_NAME_SIGNATURE "_lutified"
#define BIT_WIDTH_THRESHOLD             2

using mockturtle_network = mockturtle::mig_network;  // chose mig or xag

// NOTE: In a vector of signals, the LSB signal is represented by index[0]
// while the MSB signal is represented by index[size()-1]
template <typename sig>
struct Ntk_sigs {
  unsigned int     gid;
  std::vector<sig> signals;
};

template <typename sig>
struct Comparator_input_signal {
  bool                     is_signed;
  std::vector<sig>         signals;
  Comparator_input_signal &operator=(const Comparator_input_signal &obj) {
    I(this != &obj);  // Do not assign object to itself. works but wasteful
    is_signed = obj.is_signed;
    signals   = obj.signals;
    return *this;
  };
};

template <>
struct Comparator_input_signal<mockturtle::mig_network::signal> {
  bool                                         is_signed;
  std::vector<mockturtle::mig_network::signal> signals;
};

class Pass_mockturtle : public Pass {
protected:
  static void work(Eprp_var &var);

  std::vector<XEdge> bdinp_edges, bdout_edges;  // boundary_input/output_edges
  // absl::flat_hash_set<XEdge> bdinp_edges, bdout_edges;//boundary_input/output_edges
  absl::flat_hash_map<Node::Compact, unsigned int>            node2gid;  // gid == group id, nodes in node2gid should be lutified
  absl::flat_hash_map<unsigned int, mockturtle_network>       gid2mt;
  absl::flat_hash_map<unsigned int, mockturtle::klut_network> gid2klut;
  absl::flat_hash_map<XEdge, Ntk_sigs<mockturtle_network::signal>>
      edge2mt_sigs;  // lg<->mig, including all boundary i/o and "internal" wires
  absl::flat_hash_map<XEdge, Ntk_sigs<mockturtle::klut_network::signal>>
      edge2klut_inp_sigs;  // lg<->klut, search edge2mt_sigs table, only input mapping
  absl::flat_hash_map<XEdge, Ntk_sigs<mockturtle::klut_network::signal>>
      edge2klut_out_sigs;  // lg<->klut, search edge2mt_sigs table, only output mapping
  absl::flat_hash_map<Node::Compact, Node::Compact>                                           old_node_to_new_node;
  absl::flat_hash_map<std::pair<unsigned int, mockturtle::klut_network::node>, Node::Compact> gid_klut_node2lg_node;
  absl::flat_hash_map<std::pair<unsigned int, mockturtle::klut_network::signal>,
                      std::vector<std::pair<mockturtle::klut_network::node, Port_ID>>>
       gid_pi2sink_node_lg_pid;
  bool lg_partition(Lgraph *);
  void create_mockturtle_network(Lgraph *);
  void convert_mockturtle_to_KLUT();
  void create_lutified_lgraph(Lgraph *);

  void connect_complemented_signal(Lgraph *, Node_pin &, Node_pin &, const mockturtle::klut_network &,
                                   const mockturtle::klut_network::signal &);

  template <typename sig_type, typename ntk_type>
  void setup_input_signals(const unsigned int &, const XEdge &, std::vector<sig_type> &, ntk_type &);

  template <typename sig_type, typename ntk_type>
  void setup_output_signals(const unsigned int &, const XEdge &, std::vector<sig_type> &, ntk_type &);

  template <typename signal>
  void split_input_signal(const std::vector<signal> &, std::vector<std::vector<signal>> &);

  template <typename sig_type, typename ntk_type>
  void convert_signed_to_unsigned(const Comparator_input_signal<sig_type> &, Comparator_input_signal<sig_type> &, ntk_type &);

  template <typename sig_type, typename ntk_type>
  void complement_to_SMR(std::vector<sig_type> const &, std::vector<sig_type> &, ntk_type &);

  template <typename sig_type, typename ntk_type>
  void shift_op(const std::vector<sig_type> &, const bool &, const bool &, const long unsigned int &, std::vector<sig_type> &,
                ntk_type &);

  template <typename sig_type, typename ntk_type>
  void create_n_bit_k_input_mux(std::vector<std::vector<sig_type>> const &, std::vector<sig_type> const &, std::vector<sig_type> &,
                                ntk_type &);

  template <typename sig_type, typename ntk_type>
  sig_type is_equal_op(const Comparator_input_signal<sig_type> &, const Comparator_input_signal<sig_type> &, ntk_type &);

  template <typename sig_type, typename ntk_type>
  sig_type compare_op(const Comparator_input_signal<sig_type> &, const Comparator_input_signal<sig_type> &, const bool &,
                      const bool &, ntk_type &);

  template <typename sig_type, typename ntk_type>
  void match_bit_width_by_sign_extension(const Comparator_input_signal<sig_type> &, const Comparator_input_signal<sig_type> &,
                                         Comparator_input_signal<sig_type> &, Comparator_input_signal<sig_type> &, ntk_type &);

  template <typename sig_type, typename ntk_type>
  void mapping_logic_cell_lg2mt(sig_type (ntk_type::*)(std::vector<sig_type> const &), ntk_type &, const Node &,
                                const unsigned int &);

  template <typename ntk_type>
  void mapping_comparison_cell_lg2mt(const bool &, const bool &, ntk_type &, const Node &, const unsigned int &);

  template <typename ntk_type>
  void mapping_shift_cell_lg2mt(const bool &, const bool &, ntk_type &, const Node &, const unsigned int &);

  template <typename ntk_type>
  void mapping_dynamic_shift_cell_lg2mt(const bool &, ntk_type &, const Node &, const unsigned int &);

  template <typename signal, typename ntk>
  void create_half_adder(const signal &x, const signal &y, signal &s, signal &c, ntk &net) {
    s = net.create_xor(x, y);
    c = net.create_and(x, y);
  }

  template <typename ntk>
  void create_full_adder(const typename ntk::signal &a, const typename ntk::signal &b, const typename ntk::signal &c_in,
                         typename ntk::signal &s, typename ntk::signal &c_out, ntk &net) {
    typename ntk::signal a_xor_b       = net.create_xor(a, b);
    typename ntk::signal a_and_b       = net.create_and(a, b);
    typename ntk::signal axorb_and_cin = net.create_and(a_xor_b, c_in);
    c_out                              = net.create_or(a_and_b, axorb_and_cin);
    s                                  = net.create_xor(a_xor_b, c_in);
  }

  template <typename signal, typename ntk>
  signal create_lt(const signal &x, const signal &y, ntk &net) {
    return net.create_lt(x, y);
  }

  template <typename signal, typename ntk>
  signal create_gt(const signal &x, const signal &y, ntk &net) {
    // return net.create_gt(y, x);
    return net.create_lt(y, x);
  }

  void converting_uint32_to_signed_SMR(const uint32_t &in, uint32_t &out, bool &is_neg) {
    if (((1ULL << 31) & in) != 0) {
      out    = (~in) + 1;
      is_neg = true;
    } else {
      out    = in;
      is_neg = false;
    }
  }

  bool eligible_cell_op(const Node &cell) {
    switch (cell.get_type_op()) {
      // case GraphIO_Op:
      //  //std::cout << "Node: GraphIO_Op";
      //  break;
      case Ntype_op::Not:
        // std::cout << "Node: Not_Op\n";
        break;
#if 0
      case Pick_Op:
        //std::cout << "Node: And_Op\n";
        break;
#endif
      case Ntype_op::And:
        // std::cout << "Node: And_Op\n";
        break;
      case Ntype_op::Or:
        // std::cout << "Node: Or_Op\n";
        break;
      case Ntype_op::Xor:
        // std::cout << "Node: Xor_Op\n";
        break;
      case Ntype_op::EQ:
        // std::cout << "Node: Equals_Op\n";
        break;
      case Ntype_op::LT:
        // std::cout << "Node: LessThan_Op\n";
        break;
      case Ntype_op::GT:
        // std::cout << "Node: GreaterThan_Op\n";
        break;
      case Ntype_op::SHL:
        // std::cout << "Node: ShiftLeft_Op\n";
        // check if Node_Pin "B" is a constant or of small bit_width
        for (const auto &in_edge : cell.inp_edges()) {
          if (in_edge.sink.get_pid() == 1 && in_edge.get_bits() > BIT_WIDTH_THRESHOLD) {
            return false;
          }
        }
        break;
      case Ntype_op::SRA: {
        // std::cout << "Node: ArithShiftRight_Op\n";
        for (const auto &in_edge : cell.inp_edges()) {
          if (in_edge.sink.get_pid() == 1 && in_edge.get_bits() > BIT_WIDTH_THRESHOLD) {
            return false;
          }
        }
        break;
      }
      default:
        // std::cout << "Node: Unknown\n";
        return false;
    }
    return true;
  }

  void do_work(Lgraph *g);

public:
  Pass_mockturtle(const Eprp_var &var) : Pass("pass.mockturtle", var) {};

  static void setup();
};
