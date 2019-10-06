//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_mockturtle.hpp"

#include <mockturtle/algorithms/node_resynthesis.hpp>
#include <mockturtle/algorithms/node_resynthesis/akers.hpp>
#include <mockturtle/algorithms/node_resynthesis/direct.hpp>
#include <mockturtle/algorithms/node_resynthesis/mig_npn.hpp>
#include <mockturtle/algorithms/node_resynthesis/xmg_npn.hpp>
// FIXME: exact needs percy package in WORKSPACE
//#include <mockturtle/algorithms/node_resynthesis/exact.hpp>

void setup_pass_mockturtle() {
  Pass_mockturtle p;
  p.setup();
}

void Pass_mockturtle::setup() {
  Eprp_method m1("pass.mockturtle", "pass a lgraph using mockturtle", &Pass_mockturtle::work);

  register_pass(m1);
}

Pass_mockturtle::Pass_mockturtle()
    : Pass("mockturtle") {
}

void Pass_mockturtle::work(Eprp_var &var) {
  Pass_mockturtle pass;

  for(const auto &g : var.lgs) {
    pass.do_work(g);
  }
}

void Pass_mockturtle::do_work(LGraph *g) {
  //LGBench b("pass.mockturtle");

  fmt::print("Partitioning...\n");
  if (!lg_partition(g)) {
    fmt::print("There is no node to be lutified!\n");
    return;
  }
  for (const auto &group_id_it : node2gid) {
    fmt::print("node:{} -> gid:{}\n", group_id_it.first.get_node(g).debug_name(), group_id_it.second);
  }
  fmt::print("Partition finished.\n");

  fmt::print("Creating mockturtle network...\n");
  create_mockturtle_network(g);
  fmt::print("Mockturtle network created.\n\n");

  fmt::print("Converting mockturtle networks to KLUT networks...\n");
  convert_mockturtle_to_KLUT(g);
  fmt::print("All mockturtle networks are converted to KLUT networks.\n\n");

  fmt::print("Creating lutified LGraph...\n");
  create_lutified_lgraph(g);
  fmt::print("Lutified LGraph created.\n\n");

  node2gid.clear();
  gid2mock.clear();
  gid2klut.clear();
  bdinp_edges.clear();
  bdout_edges.clear();
  edge2signals_mock.clear();
  edge2signals_klut.clear();
  old_node_to_new_node.clear();
  gidMTnode2LGnode.clear();
  gid_fanin_sig2klut_node_lg_pid.clear();
}

bool Pass_mockturtle::lg_partition(LGraph *g) {
  unsigned int new_group_id = 0;

  for(const auto node : g->forward()) {
    if (node2gid.find(node.get_compact()) != node2gid.end())
      continue;
    if (!eligible_cell_op(node))
      continue;

    fmt::print("Node identifier:{}\n", node.debug_name());
    int propagate_id = -1;
    for(const auto &in_edge : node.inp_edges()) {
      auto peer_driver_node = in_edge.driver.get_node();
      if (!eligible_cell_op(peer_driver_node))
        continue;

      auto it = node2gid.find(peer_driver_node.get_compact());
      if (it != node2gid.end()) {
        GI(propagate_id>=0, it->second == propagate_id);
        propagate_id = it->second;
#ifndef NDEBUG
        break;  // First is enough for non-debug
#endif
      }
    }
    if (propagate_id<0)
      propagate_id = new_group_id++;

    node2gid[node.get_compact()] = propagate_id;
  }

  return !node2gid.empty();
}


template<typename sig_type, typename ntk_type>
void Pass_mockturtle::setup_input_signals(const unsigned int    &group_id,
                                          const XEdge           &input_edge,
                                          std::vector<sig_type> &inp_sigs_mt,
                                          ntk_type              &mig)
{
  //check if this input edge is already in the output mapping table
  //then setup the input signal accordingly
  if (edge2signals_mock.count(input_edge)!=0) {
    I(group_id == edge2signals_mock[input_edge].gid);
    I(input_edge.get_bits() == edge2signals_mock[input_edge].signals.size());
    for (auto i = 0UL; i < input_edge.get_bits(); i++)
      inp_sigs_mt.emplace_back(edge2signals_mock[input_edge].signals[i]);

  } else {
    bdinp_edges.insert(input_edge);
    edge2signals_mock[input_edge].gid = group_id;
    //create new input signals and map them back
#ifndef NDEBUG
    // To fix, change the edge2signal for a pin2signals (same pin, same signal)
    fmt::print("FIXME: create_pi {}->{}\n",input_edge.driver.debug_name(), input_edge.sink.debug_name());
    fmt::print("Hello");
#endif
    for (auto i = 0UL; i < input_edge.get_bits(); i++) {
      inp_sigs_mt.emplace_back(mig.create_pi());
    }
    edge2signals_mock[input_edge].signals = inp_sigs_mt;
  }
}
//collect single bit output from mt for future mapping back to lg
template<typename sig_type, typename ntk_type>
void Pass_mockturtle::setup_output_signals(const unsigned int    &group_id,
                                           const XEdge           &output_edge,
                                           std::vector<sig_type> &out_sigs_mt,
                                           ntk_type              &mig)
{
  //check if the output edge is already in the input mapping table
  //then setup/update the output/input table
  if (edge2signals_mock.count(output_edge)!=0) {
    I(group_id == edge2signals_mock[output_edge].gid);
    I(output_edge.get_bits() == edge2signals_mock[output_edge].signals.size());
    for (auto i = 0UL; i < output_edge.get_bits(); i++) {
      typename ntk_type::node old_node = mig.get_node(edge2signals_mock[output_edge].signals[i]);
      mig.substitute_node(old_node,out_sigs_mt[i]);
      mig.take_out_node(old_node);
    }
  }
  //mapping output edge to output signal
  edge2signals_mock[output_edge].gid = group_id;
  edge2signals_mock[output_edge].signals = out_sigs_mt;
}

//split the input signal by bits
template<typename signal>
void Pass_mockturtle::split_input_signal(const std::vector<signal> &input_signal,
                                         std::vector<std::vector<signal>> &splitted_input_signal)
{
  for (long unsigned int i = 0; i < input_signal.size(); i++) {
    if (splitted_input_signal.size()<=i) {
      splitted_input_signal.resize(i+1);
    }
    splitted_input_signal[i].emplace_back(input_signal[i]);
  }
}

//extend sign bit so that the bit width of two signals matches each other
template<typename sig_type, typename ntk_type>
void Pass_mockturtle::match_bit_width_by_sign_extension(const comparator_input_signal<sig_type> &sig1,
                                                        const comparator_input_signal<sig_type> &sig2,
                                                        comparator_input_signal<sig_type> &new_sig1,
                                                        comparator_input_signal<sig_type> &new_sig2,
                                                        ntk_type &net)
{
  const long unsigned int &sig1_bit_width = sig1.signals.size();
  const long unsigned int &sig2_bit_width = sig2.signals.size();

  I(sig1_bit_width > 0 && sig2_bit_width > 0);
  I(new_sig1.signals.empty() && new_sig2.signals.empty());

  if (sig1_bit_width < sig2_bit_width) {

    new_sig1 = sig1;
    if (sig1.is_signed) {
      while (new_sig1.signals.size() < sig2_bit_width) {
        new_sig1.signals.emplace_back(net.create_buf(sig1.signals[sig1_bit_width-1]));
      }
    } else {
      while (new_sig1.signals.size() < sig2_bit_width) {
        new_sig1.signals.emplace_back(net.get_constant(false));
      }
    }
    new_sig2 = sig2;

  }
  else if (sig1_bit_width > sig2_bit_width) {

    new_sig2 = sig2;
    if (sig2.is_signed) {
      while (new_sig2.signals.size() < sig1_bit_width) {
        new_sig2.signals.emplace_back(net.create_buf(sig2.signals[sig2_bit_width-1]));
      }
    } else {
      while (new_sig2.signals.size() < sig1_bit_width) {
        new_sig2.signals.emplace_back(net.get_constant(false));
      }
    }
    new_sig1 = sig1;

  } else {

    new_sig1 = sig1;
    new_sig2 = sig2;

  }
}

//creating and mapping a logic-op LGraph node to a mig node
//mapping it's both input and output LGraph edges to mig signals
template<typename sig_type, typename ntk_type>
void Pass_mockturtle::mapping_logic_cell_lg2mock(sig_type (ntk_type::*create_nary_op) (std::vector<sig_type> const &),
                                                 ntk_type &mock_ntk, const Node &node, const unsigned int &group_id)
{
  //mapping input edge to input signal
  //out_sig_0: regular OP
  //out_sig_1: reduced OP
  std::vector<sig_type> out_sigs_0, out_sigs_1;
  std::vector<std::vector<sig_type>> inp_sig_group_by_bit;
  //processing input signal
  for (const auto &in_edge : node.inp_edges()) {
    ////fmt::print("input_bit_width:{}\n",in_edge.get_bits());
    std::vector<sig_type> inp_sigs;
    setup_input_signals(group_id, in_edge, inp_sigs, mock_ntk);
    split_input_signal(inp_sigs, inp_sig_group_by_bit);
  }
  //creating output signal
  switch (node.inp_edges().size()) {
    case 1: {
      for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
        out_sigs_0.emplace_back(inp_sig_group_by_bit[i][0]);
      }
      break;
    }
    default: {
      for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
        out_sigs_0.emplace_back((mock_ntk.*create_nary_op)(inp_sig_group_by_bit[i]));
      }
      break;
    }
  }
  out_sigs_1.emplace_back((mock_ntk.*create_nary_op)(out_sigs_0));
  //processing output signal
  for (const auto &out_edge : node.out_edges()) {
    switch (out_edge.driver.get_pid()) {
      case 0: {
        I(out_edge.get_bits()==out_sigs_0.size());
        setup_output_signals(group_id, out_edge, out_sigs_0, mock_ntk);
        break;
      }
      case 1: {
        I(out_edge.get_bits()==out_sigs_1.size());
        setup_output_signals(group_id, out_edge, out_sigs_1, mock_ntk);
        break;
      }
      default:
        I(false);
        break;
    }
  }
  //It is unnecessary to delete unused output signal
  //because mockturtle can clean up dangling signals
}

//converting signed input to unsigned input using half adder
//1. old_sign_bit += 1
//2. new_sign_bit = input signal is signed? 0 : carry
//3. out = new_sign_bit concatenated by input_signal
template<typename sig_type, typename ntk_type>
void Pass_mockturtle::convert_signed_to_unsigned(const comparator_input_signal<sig_type> &signed_signal,
                                                 comparator_input_signal<sig_type> &unsigned_signal,
                                                 ntk_type &net)
{
  I(signed_signal.signals.size() >= 1);
  const auto old_sign_bit = signed_signal.signals[signed_signal.signals.size()-1];
  sig_type sum, carry, new_sign_bit;

  create_half_adder(old_sign_bit, net.get_constant(true), sum, carry, net);
  if (signed_signal.is_signed) {
    new_sign_bit = net.get_constant(false);
  } else {
    new_sign_bit = carry;
  }
  for (long unsigned int i = 0; i < signed_signal.signals.size()-1; i++) {
    unsigned_signal.signals.emplace_back(signed_signal.signals[i]);
  }
  unsigned_signal.signals.emplace_back(sum);
  unsigned_signal.signals.emplace_back(new_sign_bit);
  unsigned_signal.is_signed = false;
}

//(A[i]==B[i]) = X[i] = A[i]B[i] + A[i]'B[i]' = XNOR(A[i], B[i])
//(A==B) = X[n]X[n-1]X[n-2]...X[2]X[1]X[0]
template<typename sig_type, typename ntk_type>
sig_type Pass_mockturtle::is_equal_op(const comparator_input_signal<sig_type> &l_op,
                                      const comparator_input_signal<sig_type> &r_op,
                                      ntk_type &net)
{
  comparator_input_signal<sig_type> l_op_ext, r_op_ext;
  match_bit_width_by_sign_extension(l_op, r_op, l_op_ext, r_op_ext, net);
  I(l_op_ext.signals.size() == r_op_ext.signals.size());

  comparator_input_signal<sig_type> left_op, right_op;
  convert_signed_to_unsigned(l_op_ext, left_op, net);
  convert_signed_to_unsigned(r_op_ext, right_op, net);
  I(left_op.signals.size() == right_op.signals.size());
  I(left_op.is_signed == false && right_op.is_signed == false);
  //create is_equal signal for each bit
  const auto bit_width = left_op.signals.size();
  sig_type output;
  std::vector<sig_type> xor_bit_sigs;
  for (long unsigned int i = 0; i < bit_width; i++) {
    xor_bit_sigs.emplace_back(net.create_xor(left_op.signals[i], right_op.signals[i]));
  }
  //create the final result
  if (xor_bit_sigs.size() == 1){
    output = xor_bit_sigs[0];
  } else {
    output = net.create_nary_or(xor_bit_sigs);
  }

  return net.create_not(output);
}

//(A[i]==B[i]) = X[i] = A[i]B[i] + A[i]'B[i]' = XNOR(A[i], B[i])
//(A[i]<B[i]) = L[i] = A[i]'B[i]
//(A[i]>B[i]) = G[i] = A[i]B[i]'
//(A < B) = Y[n] +
//          X[n]L[n-1] +
//          X[n]X[n-1]L[n-2] +
//          X[n]X[n-1]X[n-2]L[n-3] +
//          X[n]X[n-1]X[n-2]X[n-3]L[n-4]
//          + ... +
//          X[n]X[n-1]X[n-2]...X[3]X[2]X[1]L[0]
//(A > B) = Y[n] +
//          X[n]G[n-1] +
//          X[n]X[n-1]G[n-2] +
//          X[n]X[n-1]X[n-2]G[n-3] +
//          X[n]X[n-1]X[n-2]X[n-3]G[n-4]
//          + ... +
//          X[n]X[n-1]X[n-2]...X[3]X[2]X[1]G[0]
template<typename sig_type, typename ntk_type>
sig_type Pass_mockturtle::compare_op(const comparator_input_signal<sig_type> &l_op,
                                     const comparator_input_signal<sig_type> &r_op,
                                     const bool &lt_op, const bool &eq_op,
                                     ntk_type &net)
{
  comparator_input_signal<sig_type> l_op_ext, r_op_ext;
  match_bit_width_by_sign_extension(l_op, r_op, l_op_ext, r_op_ext, net);
  I(l_op_ext.signals.size() == r_op_ext.signals.size());

  comparator_input_signal<sig_type> left_op, right_op;
  convert_signed_to_unsigned(l_op_ext, left_op, net);
  convert_signed_to_unsigned(r_op_ext, right_op, net);
  I(left_op.signals.size() == right_op.signals.size());
  I(left_op.is_signed == false && right_op.is_signed == false);

  const auto bit_width = left_op.signals.size();
  sig_type output;
  std::vector<sig_type> is_equal_bit_sigs, comp_op_bit_sigs, res, kth_term;

  //create X[i] and L[i] = A[i]'B[i] for LessThan and GreaterEqualThan
  //create X[i] and G[i] = A[i]B[i]' for GreaterThan and LessEqualThan
  sig_type (Pass_mockturtle::*comp_op) (const sig_type &, const sig_type &, ntk_type &)
    = ((lt_op && !eq_op) || (!lt_op && eq_op))
      ? &Pass_mockturtle::create_lt<sig_type, ntk_type>
      : &Pass_mockturtle::create_gt<sig_type, ntk_type>;

  for (long unsigned int i = 0; i < bit_width; i++) {
    is_equal_bit_sigs.emplace_back(net.create_not(net.create_xor(left_op.signals[i], right_op.signals[i])));
    comp_op_bit_sigs.emplace_back((this->*comp_op)(left_op.signals[i], right_op.signals[i], net));
  }

  //create each term and push to res
  for (long unsigned int i = 0; i < bit_width; i++) {
    kth_term.clear();
    kth_term.emplace_back(comp_op_bit_sigs[i]);
    for (long unsigned int j = i + 1;  j < bit_width; j++) {
      kth_term.emplace_back(is_equal_bit_sigs[j]);
    }
    if (kth_term.size() == 1) {
      res.emplace_back(kth_term[0]);
    } else {
      res.emplace_back(net.create_nary_and(kth_term));
    }
  }

  //create the final comparison result
  if (res.size() == 1) {
    output = eq_op? net.create_not(res[0]) : res[0];
  } else {
    output = eq_op? net.create_not(net.create_nary_or(res)) : net.create_nary_or(res);
  }

  return output;
}

//creating and mapping a compare-op LGraph node to a mig node
//mapping it's both input and output LGraph edges to mig signals
template<typename ntk_type>
void Pass_mockturtle::mapping_comparison_cell_lg2mock(const bool &lt_op, const bool &eq_op,
                                                      ntk_type &mock_ntk, const Node &node,
                                                      const unsigned int &group_id)
{
  //mapping input edge to input signal
  //must differentiate between signed and unsigned input
  std::vector<typename ntk_type::signal> out_sigs, med_sig;
  std::vector<comparator_input_signal<typename ntk_type::signal>> left_operant_sigs, right_operant_sigs;
  for (const auto &in_edge : node.inp_edges()) {
    //fmt::print("input_bit_width:{}\n",in_edge.get_bits());
    comparator_input_signal<typename ntk_type::signal> inp_sigs;
    setup_input_signals(group_id, in_edge, inp_sigs.signals, mock_ntk);
    if (node.get_type().is_input_signed(in_edge.sink.get_pid())) {
      inp_sigs.is_signed = true;
    } else {
      inp_sigs.is_signed = false;
    }
    if (in_edge.sink.get_pid()>=0 && in_edge.sink.get_pid()<=1) {
      left_operant_sigs.emplace_back(inp_sigs);
    } else {
      right_operant_sigs.emplace_back(inp_sigs);
    }
  }
  //input validity check
  I(left_operant_sigs.size() >= 1);
  I(right_operant_sigs.size() >= 1);
  //creating output signal
  if (left_operant_sigs.size() == 1 && right_operant_sigs.size() == 1) {
    out_sigs.emplace_back(compare_op(left_operant_sigs[0], right_operant_sigs[0], lt_op, eq_op, mock_ntk));
  }
  else {
    for (const auto &l_op : left_operant_sigs) {
      for (const auto &r_op : right_operant_sigs) {
        med_sig.emplace_back(compare_op(l_op, r_op, lt_op, eq_op, mock_ntk));
      }
    }
    out_sigs.emplace_back(mock_ntk.create_nary_and(med_sig));
  }
  //processing output signal
  for (const auto &out_edge : node.out_edges()) {
    I(out_edge.get_bits()==1);
    setup_output_signals(group_id, out_edge, out_sigs, mock_ntk);
  }
}

template<typename sig_type, typename ntk_type>
void Pass_mockturtle::shift_op(const std::vector<sig_type> &opr,
                               const bool &is_shift_right, const bool &is_signed,
                               const long unsigned int &shift_bits,
                               std::vector<sig_type> &res, ntk_type &net) {
  I(opr.size() != 0);
  I(res.size() == 0);
  sig_type signed_bit = is_signed
                      ? opr[opr.size() - 1]
                      : net.get_constant(false);
  long unsigned int start_point = shift_bits;
  if (is_shift_right) {
    while (start_point < opr.size()) {
      res.emplace_back(opr[start_point]);
      start_point++;
    }
    while (res.size() < opr.size()) {
      res.emplace_back(signed_bit);
    }
  } else {
    for (long unsigned int i = 0; i < start_point; i++) {
      res.emplace_back(net.get_constant(false));
    }
    while (res.size() < opr.size()) {
      res.emplace_back(opr[start_point]);
      start_point++;
    }
  }
}

template<typename sig_type, typename ntk_type>
void Pass_mockturtle::create_n_bit_k_input_mux(std::vector<std::vector<sig_type>> const &input_sig_array,
                                               std::vector<sig_type> const &sel_sig,
                                               std::vector<sig_type> &res,
                                               ntk_type &net)
{
  I(sel_sig.size() != 0);
  const long unsigned int k = sel_sig.size();
  const long unsigned int inp_num = 1 << k;
  I(input_sig_array.size() == inp_num);
  const long unsigned int n = input_sig_array[0].size();
  for (long unsigned int i = 0; i < inp_num; i++) {
    I(input_sig_array[i].size() == n);
  }
  //FIX ME: optimize the calculation of coefficient of each term
  std::vector<sig_type> coeffi, temp, sel_not_sig;
  //creating complemented signals for selection signals
  for (long unsigned int i = 0; i < k; i++) {
    sel_not_sig.emplace_back(net.create_not(sel_sig[i]));
  }
  //creating coefficient signals for each term
  for (long unsigned int i = 0; i < inp_num; i++) {
    temp.clear();
    for (long unsigned int j = 0; j < k; j++) {
      if (((i >> j) & 1) == 1) {
        temp.emplace_back(sel_sig[j]);
      } else {
        temp.emplace_back(sel_not_sig[j]);
      }
    }
    coeffi.emplace_back(net.create_nary_and(temp));
  }
  //creating output arranged by bits
  for (long unsigned int i = 0; i < n; i++) {
    temp.clear();
    for (long unsigned int j = 0; j < inp_num; j++) {
      temp.emplace_back(net.create_and(input_sig_array[j][i], coeffi[j]));
    }
    res.emplace_back(net.create_nary_or(temp));
  }
}

template<typename ntk_type>
void Pass_mockturtle::mapping_shift_cell_lg2mock(const bool &is_shift_right, const bool &sign_ext,
                                                 ntk_type &mock_ntk, const Node &node,
                                                 const unsigned int &group_id)
{
  XEdge opr_A_edge = node.inp_edges()[0].sink.get_pid() == 0
                     ? opr_A_edge = node.inp_edges()[0]
                     : opr_A_edge = node.inp_edges()[1];
  XEdge opr_B_edge = node.inp_edges()[0].sink.get_pid() == 1
                     ? opr_B_edge = node.inp_edges()[0]
                     : opr_B_edge = node.inp_edges()[1];

  std::vector<typename ntk_type::signal> opr_A_sigs, out_sigs;
  //processing input signal
  ////fmt::print("opr_A_bit_width:{}\n",opr_A_edge.get_bits());
  ////fmt::print("opr_B_bit_width:{}\n",opr_B_edge.get_bits());
  setup_input_signals(group_id, opr_A_edge, opr_A_sigs, mock_ntk);
  if (opr_B_edge.driver.get_node().get_type().op == U32Const_Op) {
    //creating output signal for const shift
    uint32_t offset = opr_B_edge.driver.get_node().get_type_const_value();
    shift_op(opr_A_sigs, is_shift_right, sign_ext, offset, out_sigs, mock_ntk);
  } else {
    std::vector<typename ntk_type::signal> opr_B_sigs, temp_out;
    std::vector<std::vector<typename ntk_type::signal>> out_enum;
    I(opr_B_edge.get_bits() != 0);
    setup_input_signals(group_id, opr_B_edge, opr_B_sigs, mock_ntk);
    for (long unsigned int ofs = 0; ofs < (long unsigned int)(1<<opr_B_sigs.size()); ofs++) {
      temp_out.clear();
      shift_op(opr_A_sigs, is_shift_right, sign_ext, ofs, temp_out, mock_ntk);
      out_enum.emplace_back(temp_out);
    }
    //using B to select output (mux)
    //create a mux
    create_n_bit_k_input_mux(out_enum, opr_B_sigs, out_sigs, mock_ntk);
  }
  //processing output signal
  for (const auto &out_edge : node.out_edges()) {
    I(out_edge.get_bits() == out_sigs.size());
    setup_output_signals(group_id, out_edge, out_sigs, mock_ntk);
  }
}

template<typename sig_type, typename ntk_type>
void Pass_mockturtle::complement_to_SMR(std::vector<sig_type> const &complement_sig,
                                        std::vector<sig_type> &SMR_sig,
                                        ntk_type &mock_ntk)
{
  auto bit_width = complement_sig.size();
  I(bit_width > 0);
  std::vector<sig_type> unsigned_sig, signed_sig;
  sig_type c_in = mock_ntk.get_constant(true);
  sig_type sign_bit = complement_sig[bit_width-1];
  for (long unsigned int i = 0; i < bit_width-1; i++) {
    unsigned_sig.emplace_back(complement_sig[i]);
    sig_type c_out, sum;
    create_half_adder(mock_ntk.create_not(complement_sig[i]), c_in, sum, c_out, mock_ntk);
    c_in = c_out;
    signed_sig.emplace_back(sum);
  }
  unsigned_sig.emplace_back(mock_ntk.get_constant(false));
  signed_sig.emplace_back(c_in);
  std::vector<std::vector<sig_type>> in_arr;
  in_arr.emplace_back(unsigned_sig);
  in_arr.emplace_back(signed_sig);
  std::vector<sig_type> selector;
  selector.emplace_back(sign_bit);
  create_n_bit_k_input_mux(in_arr, selector, SMR_sig, mock_ntk);
  SMR_sig.emplace_back(sign_bit);
}

template<typename ntk_type>
void Pass_mockturtle::mapping_dynamic_shift_cell_lg2mock(const bool &is_shift_right,
                                                         ntk_type &mock_ntk,
                                                         const Node &node, const unsigned int &group_id)
{
  XEdge opr_A_edge = node.inp_edges()[0].sink.get_pid() == 0
                     ? opr_A_edge = node.inp_edges()[0]
                     : opr_A_edge = node.inp_edges()[1];
  XEdge opr_B_edge = node.inp_edges()[0].sink.get_pid() == 1
                     ? opr_B_edge = node.inp_edges()[0]
                     : opr_B_edge = node.inp_edges()[1];
  std::vector<typename ntk_type::signal> opr_A_sigs, out_sigs;
  //processing input signal
  ////fmt::print("opr_A_bit_width:{}\n",opr_A_edge.get_bits());
  ////fmt::print("opr_B_bit_width:{}\n",opr_B_edge.get_bits());
  setup_input_signals(group_id, opr_A_edge, opr_A_sigs, mock_ntk);
  if (opr_B_edge.driver.get_node().get_type().op == U32Const_Op) {
    //creating output signal for const shift
    uint32_t ofs;
    bool is_negative;
    converting_uint32_to_signed_SMR(opr_B_edge.driver.get_node().get_type_const_value(), ofs, is_negative);
    if (is_negative) {
      shift_op(opr_A_sigs, !is_shift_right, false, ofs, out_sigs, mock_ntk);
    } else {
      shift_op(opr_A_sigs, is_shift_right, false, ofs, out_sigs, mock_ntk);
    }
  } else {
    std::vector<typename ntk_type::signal> opr_B_sigs, temp_outs, opr_B_SMR_sigs;
    std::vector<typename ntk_type::signal> ofs_sel, sign_sel, out_shr_sig, out_shl_sig;
    std::vector<std::vector<typename ntk_type::signal>> out_shr_enum, out_shl_enum, out_enum;
    I(opr_B_edge.get_bits() != 0);
    setup_input_signals(group_id, opr_B_edge, opr_B_sigs, mock_ntk);
    for (long unsigned int ofs = 0; ofs < (long unsigned int)(1<<opr_B_sigs.size()); ofs++) {
      temp_outs.clear();
      shift_op(opr_A_sigs, is_shift_right, false, ofs, temp_outs, mock_ntk);
      out_shr_enum.emplace_back(temp_outs);
      temp_outs.clear();
      shift_op(opr_A_sigs, !is_shift_right, false, ofs, temp_outs, mock_ntk);
      out_shl_enum.emplace_back(temp_outs);
    }
    complement_to_SMR(opr_B_sigs, opr_B_SMR_sigs, mock_ntk);
    sign_sel.emplace_back(opr_B_SMR_sigs[opr_B_SMR_sigs.size()-1]);
    ofs_sel = opr_B_SMR_sigs;
    ofs_sel.pop_back();
    create_n_bit_k_input_mux(out_shr_enum, ofs_sel, out_shr_sig, mock_ntk);
    create_n_bit_k_input_mux(out_shl_enum, ofs_sel, out_shl_sig, mock_ntk);
    out_enum.emplace_back(out_shr_sig);
    out_enum.emplace_back(out_shl_sig);
    create_n_bit_k_input_mux(out_enum, sign_sel, out_sigs, mock_ntk);
  }
  //processing output signal
  for (const auto &out_edge : node.out_edges()) {
    I(out_edge.get_bits() == out_sigs.size());
    setup_output_signals(group_id, out_edge, out_sigs, mock_ntk);
  }
}

void Pass_mockturtle::create_mockturtle_network(LGraph *g) {
  for(const auto node : g->forward()) {
    if (node2gid.find(node.get_compact())==node2gid.end())
      continue;

    unsigned int group_id = node2gid[node.get_compact()];
    if (gid2mock.find(group_id)==gid2mock.end())
      gid2mock[group_id] = mockturtle_network();

    auto &mt_ntk = gid2mock[group_id];

    switch (node.get_type().op) {
      case Not_Op: {
        //Note: Don't need to check the node_pin pid since Not_Op has only one sink pin and one driver pin
        //fmt::print("Not_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()==1 && !node.out_edges().empty()); //Not_Op should only have single input edge

        std::vector<mockturtle_network::signal> inp_sigs_mt, out_sigs_mt;
        //processing input signal
        //fmt::print("input_bit_width:{}\n",node.inp_edges()[0].get_bits());
        setup_input_signals(group_id, node.inp_edges()[0], inp_sigs_mt, mt_ntk);
        //creating output signal
        for (long unsigned int i = 0; i < inp_sigs_mt.size(); i++)
          out_sigs_mt.emplace_back(mt_ntk.create_not(inp_sigs_mt[i]));

        //processing output signal
        for (const auto &out_edge : node.out_edges()) {
          //fmt::print("output_bit_width:{}\n",out_edge.get_bits());
          //make sure the bit-width matches each other
          I(out_edge.get_bits()==out_sigs_mt.size());
          setup_output_signals(group_id, out_edge, out_sigs_mt, mt_ntk);
        }
        break;
      }

      case And_Op: {
        //fmt::print("And_Op in gid:{}\n",group_id);
        I(!node.inp_edges().empty() && !node.out_edges().empty());
        mapping_logic_cell_lg2mock(&mockturtle_network::create_nary_and, mt_ntk, node, group_id);
        break;
      }

      case Or_Op: {
        //fmt::print("Or_Op in gid:{}\n",group_id);
        I(!node.inp_edges().empty() && !node.out_edges().empty());
        mapping_logic_cell_lg2mock(&mockturtle_network::create_nary_or, mt_ntk, node, group_id);
        break;
      }

      case Xor_Op:
        //fmt::print("Xor_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>0 && node.out_edges().size()>0);
        mapping_logic_cell_lg2mock(&mockturtle_network::create_nary_xor, mt_ntk, node, group_id);
        break;

      case Equals_Op: {
        //fmt::print("Equals_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        //mapping input edge to input signal
        //must differentiate between signed and unsigned input
        std::vector<mockturtle_network::signal> out_sigs, med_sig;
        std::vector<comparator_input_signal<mockturtle_network::signal>> operant_sigs;
        for (const auto &in_edge : node.inp_edges()) {
          //fmt::print("input_bit_width:{}\n",in_edge.get_bits());
          comparator_input_signal<mockturtle_network::signal> inp_sig;
          setup_input_signals(group_id, in_edge, inp_sig.signals, mt_ntk);
          if (node.get_type().is_input_signed(in_edge.sink.get_pid())) {
            inp_sig.is_signed = true;
          } else {
            inp_sig.is_signed = false;
          }
          operant_sigs.emplace_back(inp_sig);
        }
        //creating output signal
        for (long unsigned int i = 0; i < operant_sigs.size()-1; i++) {
          med_sig.emplace_back(is_equal_op(operant_sigs[i], operant_sigs[i+1], mt_ntk));
        }
        out_sigs.emplace_back(mt_ntk.create_nary_and(med_sig));
        //processing output signal
        for (const auto &out_edge : node.out_edges()) {
          I(out_edge.get_bits()==1);
          setup_output_signals(group_id, out_edge, out_sigs, mt_ntk);
        }
        break;
      }

      case LessThan_Op: {
        //fmt::print("LessThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mock(true, false, mt_ntk, node, group_id);
        break;
      }

      case GreaterThan_Op: {
        //fmt::print("GreaterThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mock(false, false, mt_ntk, node, group_id);
        break;
      }

      case LessEqualThan_Op: {
        //fmt::print("LessEqualThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mock(true, true, mt_ntk, node, group_id);
        break;
      }

      case GreaterEqualThan_Op: {
        //fmt::print("GreaterEqualThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mock(false, true, mt_ntk, node, group_id);
        break;
      }

      //A << B
      case ShiftLeft_Op:
        //fmt::print("Node: ShiftLeft_Op\n");
        I(node.inp_edges().size()==2 && node.out_edges().size()>0);
        I(node.inp_edges()[0].sink.get_pid() != node.inp_edges()[1].sink.get_pid());
        mapping_shift_cell_lg2mock(false, false, mt_ntk, node, group_id);
        break;

      //A >> B, A is treated unsigned
      case LogicShiftRight_Op: {
        //fmt::print("LogicShiftRight_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()==2 && node.out_edges().size()>0);
        I(node.inp_edges()[0].sink.get_pid() != node.inp_edges()[1].sink.get_pid());
        mapping_shift_cell_lg2mock(true, false, mt_ntk, node, group_id);
        break;
      }

      case ArithShiftRight_Op: {
        //fmt::print("ArithShiftRight_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()==2 && node.out_edges().size()>0);
        I(node.inp_edges()[0].sink.get_pid() != node.inp_edges()[1].sink.get_pid());
        mapping_shift_cell_lg2mock(true, true, mt_ntk, node, group_id);
        break;
      }

      case DynamicShiftRight_Op: {
        //fmt::print("DynamicShiftRight_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()==2 && node.out_edges().size()>0);
        I(node.inp_edges()[0].sink.get_pid() != node.inp_edges()[1].sink.get_pid());
        mapping_dynamic_shift_cell_lg2mock(true, mt_ntk, node, group_id);
        break;
      }

      case DynamicShiftLeft_Op: {
        //fmt::print("DynamicShiftLeft_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()==2 && node.out_edges().size()>0);
        I(node.inp_edges()[0].sink.get_pid() != node.inp_edges()[1].sink.get_pid());
        mapping_dynamic_shift_cell_lg2mock(false, mt_ntk, node, group_id);
        break;
      }

      default:
        //fmt::print("Unknown_Op in gid:{}\n",group_id);
        break;
    }

  }

  //create mig network output signal for each group
  for(const auto node : g->forward()) {
    if (node2gid.find(node.get_compact()) == node2gid.end())
      continue;
    for (const auto &out_edge : node.out_edges()) {
      if (node2gid.find(out_edge.sink.get_node().get_compact())==node2gid.end()) {
        bdout_edges.insert(out_edge);
        I(node2gid[node.get_compact()] == edge2signals_mock[out_edge].gid);
        for (const auto sig : edge2signals_mock[out_edge].signals) {
          gid2mock[node2gid[node.get_compact()]].create_po(sig);
        }
      }
    }
  }
}

void Pass_mockturtle::convert_mockturtle_to_KLUT(LGraph *g) {

  for (const auto &gid2mock_iter : gid2mock) {
    const unsigned int           group_id = gid2mock_iter.first;
    const mockturtle::mig_network &mt_ntk = gid2mock_iter.second;

#if 1
    auto net0 = mt_ntk;

    // net0 = mockturtle::cleanup_dangling(net0);

    mockturtle::refactoring_params rf_ps;
    rf_ps.max_pis = 4;
    mockturtle::mig_npn_resynthesis resyn1;
    mockturtle::refactoring(net0, resyn1, rf_ps);
    net0 = mockturtle::cleanup_dangling(net0);

    mockturtle::akers_resynthesis<mockturtle::mig_network> resyn2;
    const auto mig = mockturtle::node_resynthesis<mockturtle::mig_network>( net0, resyn2 );
    net0 = mockturtle::cleanup_dangling(net0);

    mockturtle::mapping_view<mockturtle::mig_network, true> mapped_mig{net0};
#else
    mockturtle::mig_network cleaned_mt_ntk = cleanup_dangling(mt_ntk);

    mockturtle::mapping_view<mockturtle::mig_network, true> mapped_mig{cleaned_mt_ntk};//todo:might not suit for xag
#endif
    mockturtle::lut_mapping_params ps;
    ps.cut_enumeration_ps.cut_size = LUT_input_bits;
    mockturtle::lut_mapping<mockturtle::mapping_view<mockturtle::mig_network, true>, true>(mapped_mig, ps);
    mockturtle::klut_network klut_ntk =*mockturtle::collapse_mapped_network<mockturtle::klut_network>(mapped_mig);

    //fmt::print("finished.\n");
    //fmt::print("KLUT network (gid:{}):\n", group_id);
    //mockturtle::write_bench(klut_ntk,std::cout);
    //equivalence checking using miter
    //const auto miter = *mockturtle::miter<mockturtle::mig_network>(cleaned_mt_ntk, klut_ntk);
    gid2klut[group_id]=klut_ntk;

    //mapping mig IO signal to klut IO signal
    //FIXME: foreach_pi returns node while foreach_po returns signal
    //but in klut network, you can regard them as the same
    //SH:FIXME: all the input output of network should use different table to maintain because for simple "NOT"
    //network, there is only one node to represent pi and Not operation, but the klut network have separate io

    //fmt::print("\n(Group ID:{}) Mapping mockturtle network IO signal to KLUT network IO signal...\n", group_id);
    I(mt_ntk.num_pis()==klut_ntk.num_pis() && mt_ntk.num_pos()==klut_ntk.num_pos());
    std::vector<mockturtle::mig_network::node>      mig_inp_signal;
    std::vector<mockturtle::mig_network::node>      mig_out_signal;
    std::vector<mockturtle::klut_network::signal>   klut_inp_signal;
    std::vector<mockturtle::klut_network::signal>   klut_out_signal;
    mt_ntk.foreach_pi( [&](const auto& n)   { mig_inp_signal.emplace_back(n); } );
    mt_ntk.foreach_po( [&](const auto& n)   { mig_out_signal.emplace_back(mt_ntk.get_node(n));} );

    klut_ntk.foreach_pi( [&](const auto& n) { klut_inp_signal.emplace_back(n); } );
    klut_ntk.foreach_po( [&](const auto& n) { klut_out_signal.emplace_back(klut_ntk.get_node(n)); } );


    absl::flat_hash_map<mockturtle::mig_network::node, mockturtle::klut_network::signal> mig2klut_inp_signal;
    absl::flat_hash_map<mockturtle::mig_network::node, mockturtle::klut_network::signal> mig2klut_out_signal;
    auto mig_inp_signal_iter = mig_inp_signal.begin();
    auto klut_inp_signal_iter = klut_inp_signal.begin();
    while (mig_inp_signal_iter!=mig_inp_signal.end()) {
      mig2klut_inp_signal[*mig_inp_signal_iter] = *klut_inp_signal_iter;
      //fmt::print("Mockturtle IO({}) -> KLUT IO({})\n", mt_ntk.node_to_index(*mig_inp_signal_iter), klut_ntk.node_to_index(*klut_inp_signal_iter));
      mig_inp_signal_iter++;
      klut_inp_signal_iter++;
    }

    auto mig_out_signal_iter = mig_out_signal.begin();
    auto klut_out_signal_iter = klut_out_signal.begin();
    while (mig_out_signal_iter!=mig_out_signal.end()) {
      mig2klut_out_signal[*mig_out_signal_iter] = *klut_out_signal_iter;
      //fmt::print("Mockturtle IO({}) -> KLUT IO({})\n", mt_ntk.node_to_index(*mig_out_signal_iter), klut_ntk.node_to_index(*klut_out_signal_iter));
      mig_out_signal_iter++;
      klut_out_signal_iter++;
    }

    int pos = 0;
    for (const auto &inp_edge : bdinp_edges) {
      I(klut_ntk.size() > pos);
      edge2signals_klut[inp_edge].gid = group_id;
      auto stored_mig_signals = edge2signals_mock[inp_edge].signals;
      for(const auto &itr_mig_sig : stored_mig_signals)
        edge2signals_klut[inp_edge].signals.emplace_back(mig2klut_inp_signal[mt_ntk.get_node(itr_mig_sig)]);
    }

    for (const auto &out_edge : bdout_edges) {
      I(klut_ntk.size() > pos);
      edge2signals_klut[out_edge].gid = group_id;
      auto stored_mig_signals = edge2signals_mock[out_edge].signals;
      for(const auto &itr_mig_sig : stored_mig_signals)
        edge2signals_klut[out_edge].signals.emplace_back(mig2klut_out_signal[mt_ntk.get_node(itr_mig_sig)]);
    }
    //fmt::print("finished.\n\n");
  }

  //mapping lgraph edges to klut io signals
  absl::flat_hash_set<XEdge> boundary_edges;
  //fmt::print("IO edges info:\n");
  //fmt::print("input edges:\n");
  //for (const auto &in_edge : bdinp_edges)
  //  fmt::print("{}:{}->{}:{}\n",in_edge.driver.get_node().debug_name(), in_edge.driver.get_pid(), in_edge.sink.get_node().debug_name(), in_edge.sink.get_pid());

  //fmt::print("output edges:\n");
  //for (const auto &out_edge : bdout_edges)
  //  fmt::print("{}:{}->{}:{}\n",out_edge.driver.get_node().debug_name(), out_edge.driver.get_pid(), out_edge.sink.get_node().debug_name(), out_edge.sink.get_pid());


  boundary_edges.insert(bdinp_edges.begin(),bdinp_edges.end());
  boundary_edges.insert(bdout_edges.begin(),bdout_edges.end());

#ifndef NDEBUG
  //print out the signal mapping information
  for (const auto &edge : boundary_edges) {
    //fmt::print("IO_XEdge:{}_to_{}\n", edge.driver.get_node().debug_name(), edge.sink.get_node().debug_name());
    I(edge2signals_mock[edge].gid == edge2signals_klut[edge].gid);
    I(edge2signals_klut[edge].signals.size() == edge2signals_mock[edge].signals.size());

    //const auto gid = edge2signals_klut[edge].gid;
    //fmt::print("Group_ID:{}\n", gid);
    //fmt::print("klut[edge] size:{}, mock[edge] size:{}\n", edge2signals_klut[edge].signals.size(), edge2signals_mock[edge].signals.size());
#if 0
    const long unsigned int signal_width = edge2signals_mock[edge].signals.size();
    for (long unsigned int i = 0; i < signal_width; i++) {
      //fmt::print("Mockturtle IO({}) -> KLUT IO({})\n",
      //  gid2mock[gid].node_to_index(gid2mock[gid].get_node(edge2signals_mock[edge].signals[i])),
      //  gid2klut[gid].node_to_index(gid2klut[gid].get_node(edge2signals_klut[edge].signals[i])));
    }
#endif
  }
#endif
}

void Pass_mockturtle::create_lutified_lgraph(LGraph *old_lg) {
  //I(false);
  LGraph *new_lg = old_lg->clone_skeleton(LUTIFIED_NETWORK_NAME_SIGNATURE);

  auto old_inp_node = old_lg->get_graph_input_node();
  auto old_out_node = old_lg->get_graph_output_node();
  auto new_inp_node = new_lg->get_graph_input_node();
  auto new_out_node = new_lg->get_graph_output_node();
  old_node_to_new_node[old_inp_node.get_compact()] = new_inp_node.get_compact();
  old_node_to_new_node[old_out_node.get_compact()] = new_out_node.get_compact();

  //create unchanged portion
  //FIX ME: 1. copy name of the driver_pin
  //        2. add graph_io into internal_node_mapping
  //fmt::print("Step-I: Start mapping unchanged part...\n");
  for (const auto old_node : old_lg->forward()) { // TODO?: It may be faster to do two passes with  fast (first create nodes, then connect edges.
    if (node2gid.find(old_node.get_compact())!=node2gid.end())
      continue;
    //if (old_node.is_type(GraphIO_Op))
    //  continue;

    Node new_node;
    //note: new_lg ios have been created before the main loop
    if (!old_node.is_type(GraphIO_Op)) {
      new_node = new_lg->create_node(old_node);
      old_node_to_new_node[old_node.get_compact()] = new_node.get_compact();
    } else {
      new_node = old_node_to_new_node[old_node.get_compact()].get_node(new_lg);
    }

    //create edges which connect unchanged parts in lgraph
    //fmt::print("old_node type:{}, is_graph_input? {}\n", old_node.get_type().get_name(), old_node == old_lg->get_graph_input_node());
    for (const auto &in_edge : old_node.inp_edges()) {
      if (edge2signals_mock.find(in_edge)==edge2signals_mock.end()) {
        auto peer_driver_node = in_edge.driver.get_node();
        if (old_node_to_new_node.find(peer_driver_node.get_compact())!=old_node_to_new_node.end()) {
          auto driver_node = old_node_to_new_node[peer_driver_node.get_compact()].get_node(new_lg);
          auto driver_pin = driver_node.setup_driver_pin(in_edge.driver.get_pid());
          auto sink_pin = new_node.setup_sink_pin(in_edge.sink.get_pid());
          if(!new_lg->has_edge(driver_pin, sink_pin)) // TODO: THis is slow. Can we do better?
            new_lg->add_edge(driver_pin, sink_pin);
        }
      }
    }
    for (const auto &out_edge : old_node.out_edges()) {
      if (edge2signals_mock.find(out_edge) == edge2signals_mock.end()) {
        auto peer_sink_node = out_edge.sink.get_node();
        if (old_node_to_new_node.find(peer_sink_node.get_compact()) != old_node_to_new_node.end()) {
          auto sink_node = old_node_to_new_node[peer_sink_node.get_compact()].get_node(new_lg);
          auto sink_pin = sink_node.setup_sink_pin(out_edge.sink.get_pid());
          auto driver_pin = new_node.setup_driver_pin(out_edge.driver.get_pid());
          if(!new_lg->has_edge(driver_pin, sink_pin)) // TODO: THis is slow. Can we do better?
            new_lg->add_edge(driver_pin, sink_pin);
        }
      }
    }
  }
  //fmt::print("Unchanged part mapped.\n\n");


  //create lutified portion
  //fmt::print("Step-II: Start mapping lutified part...\n");
  for (const auto &gid2klut_iter : gid2klut) {
    const auto group_id = gid2klut_iter.first;
    const auto &klut_ntk = gid2klut_iter.second;
    //create new lut nodes
    //fmt::print("Step-II-a: Creating KLUT network (gid:{}) cells in LGraph...\n", group_id);
    klut_ntk.foreach_node( [&](const auto &klut_ntk_node) {
      //this is an primary input of the klut network, does not have any fanin, continue
      if (klut_ntk.is_pi(klut_ntk_node))
        return;

      //check whether there is a complemented fanin,
      //a signal that is the opposite of the original signal
      //then change the truth table accordingly
      auto func = klut_ntk.node_function(klut_ntk_node);

      klut_ntk.foreach_fanin(klut_ntk_node, [&](const auto &sig, auto i) {
        if (klut_ntk.is_complemented(sig))
          kitty::flip_inplace(func, i);

        if (klut_ntk.is_pi(klut_ntk.get_node(sig))) {
          auto pid = (uint32_t) i;
          auto key = std::make_pair(group_id, sig);
          gid_fanin_sig2klut_node_lg_pid[key] = std::make_pair(klut_ntk_node, pid);
          //fmt::print("gid_fanin_sig2klut_node_lg_pid group:{}, signal:{}, pid:{}\n", key.first, key.second, pid);
        }
      } );

      auto encoding = std::stoul(kitty::to_hex(func), nullptr, 16);
      auto new_node = new_lg->create_node();

      new_node.set_type_lut(encoding);
      gidMTnode2LGnode[std::make_pair(group_id, klut_ntk_node)] = new_node.get_compact();
    } );
    //fmt::print("finished.\n");

    //create inner edges to connect lg_nodes already created
    //fmt::print("Step-II-b: Creating KLUT network (gid:{}) inner edges in LGraph...\n", group_id);
    klut_ntk.foreach_node( [&](const auto &klut_ntk_node) {
      if (klut_ntk.is_pi(klut_ntk_node) || klut_ntk.is_constant(klut_ntk_node)) {
        //constant and primary input do not have fanin
        return;
      }
      klut_ntk.foreach_fanin(klut_ntk_node, [&](auto const& sig, auto i) {
        //variable "i" is an index indicating the order of the fanin signals
        //therefore it can be used as pid.
        if (klut_ntk.is_pi(klut_ntk.get_node(sig)))
          return; //continue
        const auto child = klut_ntk.get_node(sig);
        const auto parent = klut_ntk_node;
        I(gidMTnode2LGnode.find(std::make_pair(group_id, child)) != gidMTnode2LGnode.end());
        I(gidMTnode2LGnode.find(std::make_pair(group_id, parent)) != gidMTnode2LGnode.end());
        auto driver_node = Node(new_lg,gidMTnode2LGnode[std::make_pair(group_id, child)]);
        auto sink_node = Node(new_lg,gidMTnode2LGnode[std::make_pair(group_id, parent)]);
        auto driver_pin = driver_node.setup_driver_pin(0);
        auto sink_pin = sink_node.setup_sink_pin(i);
        new_lg->add_edge(driver_pin, sink_pin, 1);//lut io should always be 1 bit
      } );
    } );
    //fmt::print("finished.\n");
  }
  //fmt::print("Lutified part mapped.\n\n");

  //create edges for input signals
  //fmt::print("Creating KLUT network input edges in LGraph...\n");
  for (const auto &in_edge : bdinp_edges) {
    const auto group_id = edge2signals_klut[in_edge].gid;
    const std::vector<mockturtle::klut_network::signal> &sigs = edge2signals_klut[in_edge].signals;

    I(old_node_to_new_node.find(in_edge.driver.get_node().get_compact()) != old_node_to_new_node.end());
    auto driver_node = old_node_to_new_node[in_edge.driver.get_node().get_compact()].get_node(new_lg);
    auto driver_pin = driver_node.setup_driver_pin(in_edge.driver.get_pid());
    const auto bit_width = in_edge.get_bits();
    I(bit_width == sigs.size());
    I(gid_fanin_sig2klut_node_lg_pid.find(std::make_pair(group_id, sigs[0])) != gid_fanin_sig2klut_node_lg_pid.end());
    if (bit_width == 1) {
      //fmt::print("group_id:{}, sigs[0]:{}\n", group_id, sigs[0]);
      const auto & klut_node_and_lg_pid = gid_fanin_sig2klut_node_lg_pid[std::make_pair(group_id, sigs[0])];

      I(gidMTnode2LGnode.find(std::make_pair(group_id, klut_node_and_lg_pid.first)) != gidMTnode2LGnode.end());
      auto sink_node = gidMTnode2LGnode[std::make_pair(group_id, klut_node_and_lg_pid.first)].get_node(new_lg);
      const auto pid = klut_node_and_lg_pid.second;
      auto sink_pin = sink_node.setup_sink_pin(pid);
      new_lg->add_edge(driver_pin, sink_pin, 1);
    } else {
      for (auto i = 0UL; i < bit_width; i++) {
        //fmt::print("group_id:{}, sigs[i]:{}\n", group_id, sigs[i]);
        const auto & klut_node_and_lg_pid = gid_fanin_sig2klut_node_lg_pid[std::make_pair(group_id, sigs[i])];
        I(gidMTnode2LGnode.find(std::make_pair(group_id, klut_node_and_lg_pid.first)) != gidMTnode2LGnode.end());
        auto sink_node = gidMTnode2LGnode[std::make_pair(group_id, klut_node_and_lg_pid.first)].get_node(new_lg);
        const auto pid = klut_node_and_lg_pid.second;
        auto sink_pin = sink_node.setup_sink_pin(pid);
        //NOTE: update this simple Pick_Op node when the more powerful Pick_Op is readly
        auto pick_node = new_lg->create_node(Pick_Op);
        auto pick_node_sink_pin = pick_node.setup_sink_pin(0);
        auto pick_node_offset_pin = pick_node.setup_sink_pin(1);
        auto pick_node_driver_pin = pick_node.setup_driver_pin();
        auto const_node_for_bit_select = new_lg->create_node_const(i);
        auto bit_select_signal = const_node_for_bit_select.get_driver_pin();
        pick_node_driver_pin.set_bits(1);
        new_lg->add_edge(bit_select_signal, pick_node_offset_pin);
        new_lg->add_edge(driver_pin, pick_node_sink_pin);
        new_lg->add_edge(pick_node_driver_pin, sink_pin);
      }
    }
  }
  //fmt::print("finished.\n");

  //create edges for output signal
  //fmt::print("Creating KLUT network output edges in LGraph...\n");
  for (const auto &out_edge : bdout_edges) {
    const auto group_id = edge2signals_klut[out_edge].gid;
    const auto klut = gid2klut[group_id];
    const std::vector<mockturtle::klut_network::signal> &sigs = edge2signals_klut[out_edge].signals;
    I(old_node_to_new_node.find(out_edge.sink.get_node().get_compact()) != old_node_to_new_node.end());
    //auto sink_node = Node(new_lg,old_node_to_new_node[out_edge.sink.get_node().get_compact()]);
    auto sink_node = old_node_to_new_node[out_edge.sink.get_node().get_compact()].get_node(new_lg);
    auto sink_pin = sink_node.setup_sink_pin(out_edge.sink.get_pid());
    const auto bit_width = out_edge.get_bits();
    I(bit_width == sigs.size());
    if (bit_width == 1) {
      I(gidMTnode2LGnode.find(std::make_pair(group_id, gid2klut[group_id].get_node(sigs[0]))) != gidMTnode2LGnode.end());
      //auto driver_node = Node(new_lg,gidMTnode2LGnode[std::make_pair(group_id, klut.get_node(sigs[0]))]);
      auto driver_node = gidMTnode2LGnode[std::make_pair(group_id, klut.get_node(sigs[0]))].get_node(new_lg);
      auto driver_pin = driver_node.setup_driver_pin();
      connect_complemented_signal(new_lg, driver_pin, sink_pin, klut, sigs[0]);
    } else {
      auto join_node = new_lg->create_node(Join_Op);
      for (auto i = 0UL; i < bit_width; i++) {
        I(gidMTnode2LGnode.find(std::make_pair(group_id, gid2klut[group_id].get_node(sigs[i]))) != gidMTnode2LGnode.end());
        //auto driver_node = Node(new_lg,gidMTnode2LGnode[std::make_pair(group_id, klut.get_node(sigs[i]))]);
        auto driver_node = gidMTnode2LGnode[std::make_pair(group_id, klut.get_node(sigs[i]))].get_node(new_lg);
        auto kth_driver_pin = driver_node.setup_driver_pin();
        auto kth_sink_pin = join_node.setup_sink_pin(i);
        connect_complemented_signal(new_lg, kth_driver_pin, kth_sink_pin, klut, sigs[i]);
      }
      auto driver_pin = join_node.setup_driver_pin();
      new_lg->add_edge(driver_pin, sink_pin, bit_width);
    }
  }
  //fmt::print("finished.\n");
}

//solve complemented signal
void Pass_mockturtle::connect_complemented_signal(LGraph *g, Node_pin &driver_pin, Node_pin &sink_pin, const mockturtle::klut_network &klut, const mockturtle::klut_network::signal &signal) {
  if (klut.is_complemented(signal)) {
    auto not_gate = g->create_node(Not_Op);
    auto not_gate_sink_pin = not_gate.setup_sink_pin(0);
    auto not_gate_driver_pin = not_gate.setup_driver_pin();
    g->add_edge(driver_pin, not_gate_sink_pin, 1);
    g->add_edge(not_gate_driver_pin, sink_pin, 1);
  } else {
    g->add_edge(driver_pin, sink_pin, 1);
  }
}
