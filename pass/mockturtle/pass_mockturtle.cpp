//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_mockturtle.hpp"

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
  LGBench b("pass.mockturtle");

  fmt::print("Partitioning...\n");
  if (!lg_partition(g)) {
    fmt::print("There is no node to be lutified!\n");
    return;
  }
  for (const auto &group_id_it : node2gid) {
    fmt::print("node:{} -> gid:{}\n", group_id_it.first.get_node(g).debug_name(), group_id_it.second);
  }
  fmt::print("Partition finished.\n");

  fmt::print("Creating MIG network...\n");
  create_MIG_network(g);
  fmt::print("MIG network created.\n");

  fmt::print("Converting MIG networks to KLUT networks...\n");
  convert_MIG_to_KLUT(g);
  fmt::print("All MIG networks are converted to KLUT networks.\n");

  fmt::print("Creating lutified LGraph...\n");
  create_lutified_lgraph(g);
  fmt::print("Lutified LGraph created.\n");

  node2gid.clear();
  gid2mig.clear();
  gid2klut.clear();
  input_edges.clear();
  output_edges.clear();
  edge2signal_mig.clear();
  edge2signal_klut.clear();
  old_node_to_new_node.clear();
  new_node_to_old_node.clear();
  gidMTnode2LGnode.clear();
  gid_fanin2parent_pid.clear();
}

bool Pass_mockturtle::lg_partition(LGraph *g) {
  unsigned int new_group_id = 0;
  for(const auto node : g->forward()) {
    fmt::print("Node identifier:{}\n", node.debug_name());
    if (node2gid.find(node.get_compact()) == node2gid.end() && eligable_cell_op(node)) {
      new_group_id++;
      dfs_populate_gid(node, new_group_id);
    }
  }
  return !node2gid.empty();
}

void Pass_mockturtle::dfs_populate_gid(Node node, const unsigned int group_id) {
  if (node2gid.find(node.get_compact()) != node2gid.end()) {
    I(node2gid[node.get_compact()] == group_id);
    return;
  }
  I(node2gid.find(node.get_compact()) == node2gid.end());
  node2gid[node.get_compact()] = group_id;
  for(const auto &in_edge : node.inp_edges()) {
    auto peer_driver_node = in_edge.driver.get_node();
    if (eligable_cell_op(peer_driver_node)) {
      dfs_populate_gid(peer_driver_node, group_id);
    }
  }
  for(const auto &out_edge : node.out_edges()) {
    auto peer_sink_node = out_edge.sink.get_node();
    if (eligable_cell_op(peer_sink_node)) {
      dfs_populate_gid(peer_sink_node, group_id);
    }
  }
}

void Pass_mockturtle::setup_input_signal(const unsigned int &group_id,
                                         const XEdge &input_edge,
                                         std::vector<mockturtle::mig_network::signal> &input_signal,
                                         mockturtle::mig_network &mig)
{
  //check if this input edge is already in the output mapping table
  //then setup the input signal accordingly
  //fmt::print("input_edge:{}:{}->{}:{}\n",input_edge.driver.get_node().get_compact(), input_edge.driver.get_pid(), input_edge.sink.get_node().get_compact(), input_edge.sink.get_pid());
  if (edge2signal_mig.count(input_edge)!=0) {
    I(group_id == edge2signal_mig[input_edge].gid);
    //fetch the input signal from mapping table
    I(input_edge.get_bits() == edge2signal_mig[input_edge].signals.size());
    for (auto i = 0; i < input_edge.get_bits(); i++) {
      input_signal.emplace_back(edge2signal_mig[input_edge].signals[i]);
    }
  } else {
    input_edges.insert(input_edge);
    edge2signal_mig[input_edge].gid = group_id;
    //create new input signals and map them back
    for (auto i = 0; i < input_edge.get_bits(); i++) {
      input_signal.emplace_back(mig.create_pi());
    }
    edge2signal_mig[input_edge].signals=input_signal;
  }
}

void Pass_mockturtle::setup_output_signal(const unsigned int &group_id,
                                          const XEdge &output_edge,
                                          std::vector<mockturtle::mig_network::signal> &output_signal,
                                          mockturtle::mig_network &mig)
{
  //check if the output edge is already in the input mapping table
  //then setup/update the output/input table
  if (edge2signal_mig.count(output_edge)!=0) {
    I(group_id == edge2signal_mig[output_edge].gid);
    I(output_edge.get_bits() == edge2signal_mig[output_edge].signals.size());
    for (auto i = 0; i < output_edge.get_bits(); i++) {
      mockturtle::mig_network::node old_node = mig.get_node(edge2signal_mig[output_edge].signals[i]);
      mig.substitute_node(old_node,output_signal[i]);
      mig.take_out_node(old_node);
    }
  }
  //mapping output edge to output signal
  edge2signal_mig[output_edge].gid = group_id;
  edge2signal_mig[output_edge].signals=output_signal;
  //fmt::print("ouput_edge:{}:{}->{}:{}\n",output_edge.driver.get_node().get_compact(), output_edge.driver.get_pid(), output_edge.sink.get_node().get_compact(), output_edge.sink.get_pid());
}

//split the input signal by bits
void Pass_mockturtle::split_input_signal(const std::vector<mockturtle::mig_network::signal> &input_signal,
                                         std::vector<std::vector<mockturtle::mig_network::signal>> &splitted_input_signal)
{
  for (long unsigned int i = 0; i < input_signal.size(); i++) {
    if (splitted_input_signal.size()<=i) {
      splitted_input_signal.resize(i+1);
    }
    splitted_input_signal[i].emplace_back(input_signal[i]);
  }
}

//extend sign bit so that the bit width of two signals matches each other
void Pass_mockturtle::match_bit_width_by_sign_extension(const comparator_input_signal &sig1,
                                                        const comparator_input_signal &sig2,
                                                        comparator_input_signal &new_sig1,
                                                        comparator_input_signal &new_sig2,
                                                        mockturtle::mig_network &net)
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
void Pass_mockturtle::mapping_logic_cell_lg2mig(mockturtle::mig_network::signal (mockturtle::mig_network::*create_nary_op)
                                                                                (std::vector<mockturtle::mig_network::signal> const &),
                                                mockturtle::mig_network &mig_ntk, const Node &node, const unsigned int &group_id)
{
  //mapping input edge to input signal
  //out_sig_0: regular OP
  //out_sig_1: reduced OP
  std::vector<mockturtle::mig_network::signal> out_sig_0, out_sig_1;
  std::vector<std::vector<mockturtle::mig_network::signal>> inp_sig_group_by_bit;
  //processing input signal
  for (const auto &in_edge : node.inp_edges()) {
    //fmt::print("input_bit_width:{}\n",in_edge.get_bits());
    std::vector<mockturtle::mig_network::signal> inp_sig;
    setup_input_signal(group_id, in_edge, inp_sig, mig_ntk);
    split_input_signal(inp_sig, inp_sig_group_by_bit);
  }
  //creating output signal
  switch (node.inp_edges().size()) {
    case 1: {
      for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
        out_sig_0.emplace_back(inp_sig_group_by_bit[i][0]);
      }
      break;
    }
    default: {
      for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
        out_sig_0.emplace_back((mig_ntk.*create_nary_op)(inp_sig_group_by_bit[i]));
      }
      break;
    }
  }
  out_sig_1.emplace_back((mig_ntk.*create_nary_op)(out_sig_0));
  //processing output signal
  for (const auto &out_edge : node.out_edges()) {
    switch (out_edge.driver.get_pid()) {
      case 0: {
        I(out_edge.get_bits()==out_sig_0.size());
        setup_output_signal(group_id, out_edge, out_sig_0, mig_ntk);
        break;
      }
      case 1: {
        I(out_edge.get_bits()==out_sig_1.size());
        setup_output_signal(group_id, out_edge, out_sig_1, mig_ntk);
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
void Pass_mockturtle::convert_signed_to_unsigned(const comparator_input_signal &signed_signal,
                                                 comparator_input_signal &unsigned_signal,
                                                 mockturtle::mig_network &mig)
{
  I(signed_signal.signals.size() >= 1);
  const auto old_sign_bit = signed_signal.signals[signed_signal.signals.size()-1];
  mockturtle::mig_network::signal sum, carry, new_sign_bit;

  create_half_adder(old_sign_bit, mig.get_constant(true), sum, carry, mig);
  if (signed_signal.is_signed) {
    new_sign_bit = mig.get_constant(false);
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
mockturtle::mig_network::signal Pass_mockturtle::is_equal_op(const comparator_input_signal &l_op,
                                                             const comparator_input_signal &r_op,
                                                             mockturtle::mig_network &mig)
{
  comparator_input_signal l_op_ext, r_op_ext;
  match_bit_width_by_sign_extension(l_op, r_op, l_op_ext, r_op_ext, mig);
  I(l_op_ext.signals.size() == r_op_ext.signals.size());

  comparator_input_signal left_op, right_op;
  convert_signed_to_unsigned(l_op_ext, left_op, mig);
  convert_signed_to_unsigned(r_op_ext, right_op, mig);
  I(left_op.signals.size() == right_op.signals.size());
  I(left_op.is_signed == false && right_op.is_signed == false);
  //create is_equal signal for each bit
  const auto bit_width = left_op.signals.size();
  mockturtle::mig_network::signal output;
  std::vector<mockturtle::mig_network::signal> xor_bit_sigs;
  for (long unsigned int i = 0; i < bit_width; i++) {
    xor_bit_sigs.emplace_back(mig.create_xor(left_op.signals[i], right_op.signals[i]));
  }
  //create the final result
  if (xor_bit_sigs.size() == 1){
    output = xor_bit_sigs[0];
  } else {
    output = mig.create_nary_or(xor_bit_sigs);
  }

  return mig.create_not(output);
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
mockturtle::mig_network::signal Pass_mockturtle::compare_op(const comparator_input_signal &l_op,
                                                            const comparator_input_signal &r_op,
                                                            const bool &lt_op,
                                                            const bool &eq_op,
                                                            mockturtle::mig_network &mig)
{
  comparator_input_signal l_op_ext, r_op_ext;
  match_bit_width_by_sign_extension(l_op, r_op, l_op_ext, r_op_ext, mig);
  I(l_op_ext.signals.size() == r_op_ext.signals.size());

  comparator_input_signal left_op, right_op;
  convert_signed_to_unsigned(l_op_ext, left_op, mig);
  convert_signed_to_unsigned(r_op_ext, right_op, mig);
  I(left_op.signals.size() == right_op.signals.size());
  I(left_op.is_signed == false && right_op.is_signed == false);

  const auto bit_width = left_op.signals.size();
  mockturtle::mig_network::signal output;
  std::vector<mockturtle::mig_network::signal> is_equal_bit_sigs, comp_op_bit_sigs, res, kth_term;

  //create X[i] and L[i] = A[i]'B[i] for LessThan and GreaterEqualThan
  //create X[i] and G[i] = A[i]B[i]' for GreaterThan and LessEqualThan
  mockturtle::mig_network::signal
    (Pass_mockturtle::*comp_op) (const mockturtle::mig_network::signal&,
                                 const mockturtle::mig_network::signal&,
                                 mockturtle::mig_network&) =
    ((lt_op && !eq_op) || (!lt_op && eq_op))
      ? &Pass_mockturtle::create_lt<mockturtle::mig_network::signal, mockturtle::mig_network>
      : &Pass_mockturtle::create_gt<mockturtle::mig_network::signal, mockturtle::mig_network>;

  for (long unsigned int i = 0; i < bit_width; i++) {
    is_equal_bit_sigs.emplace_back(mig.create_not(mig.create_xor(left_op.signals[i], right_op.signals[i])));
    comp_op_bit_sigs.emplace_back((this->*comp_op)(left_op.signals[i], right_op.signals[i], mig));
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
      res.emplace_back(mig.create_nary_and(kth_term));
    }
  }

  //create the final comparison result
  if (res.size() == 1) {
    output = eq_op? mig.create_not(res[0]) : res[0];
  } else {
    output = eq_op? mig.create_not(mig.create_nary_or(res)) : mig.create_nary_or(res);
  }

  return output;
}

//creating and mapping a compare-op LGraph node to a mig node
//mapping it's both input and output LGraph edges to mig signals
void Pass_mockturtle::mapping_comparison_cell_lg2mig(const bool &lt_op, const bool &eq_op,
                                                      mockturtle::mig_network &mig_ntk, const Node &node,
                                                      const unsigned int &group_id)
{
  //mapping input edge to input signal
  //must differentiate between signed and unsigned input
  std::vector<mockturtle::mig_network::signal> out_sig, med_sig;
  std::vector<comparator_input_signal> left_operant_sigs, right_operant_sigs;
  for (const auto &in_edge : node.inp_edges()) {
    fmt::print("input_bit_width:{}\n",in_edge.get_bits());
    comparator_input_signal inp_sig;
    setup_input_signal(group_id, in_edge, inp_sig.signals, mig_ntk);
    if (node.get_type().is_input_signed(in_edge.sink.get_pid())) {
      inp_sig.is_signed = true;
    } else {
      inp_sig.is_signed = false;
    }
    if (in_edge.sink.get_pid()>=0 && in_edge.sink.get_pid()<=1) {
      left_operant_sigs.emplace_back(inp_sig);
    } else {
      right_operant_sigs.emplace_back(inp_sig);
    }
  }
  //input validity check
  I(left_operant_sigs.size() >= 1);
  I(right_operant_sigs.size() >= 1);
  //creating output signal
  if (left_operant_sigs.size() == 1 && right_operant_sigs.size() == 1) {
    out_sig.emplace_back(compare_op(left_operant_sigs[0], right_operant_sigs[0], lt_op, eq_op, mig_ntk));
  }
  else {
    for (const auto &l_op : left_operant_sigs) {
      for (const auto &r_op : right_operant_sigs) {
        med_sig.emplace_back(compare_op(l_op, r_op, lt_op, eq_op, mig_ntk));
      }
    }
    out_sig.emplace_back(mig_ntk.create_nary_and(med_sig));
  }
  //processing output signal
  for (const auto &out_edge : node.out_edges()) {
    I(out_edge.get_bits()==1);
    setup_output_signal(group_id, out_edge, out_sig, mig_ntk);
  }
}

void Pass_mockturtle::shr_op(std::vector<mockturtle::mig_network::signal> &res,
                             const std::vector<mockturtle::mig_network::signal> &opr,
                             const bool &is_signed, const long unsigned int &shift_bits,
                             mockturtle::mig_network &mig) {
  I(opr.size() != 0);
  I(res.size() == 0);
  mockturtle::mig_network::signal signed_bit = is_signed
                                               ? opr[opr.size() - 1]
                                               : mig.get_constant(false);
  long unsigned int start_point = shift_bits;
  while (start_point < opr.size()) {
    res.emplace_back(opr[start_point]);
    start_point++;
  }
  while (res.size() < opr.size()) {
    res.emplace_back(signed_bit);
  }
}

void Pass_mockturtle::create_n_bit_k_input_mux(std::vector<std::vector<mockturtle::mig_network::signal>> const &input_sig_array,
                                               std::vector<mockturtle::mig_network::signal> const &sel_sig,
                                               std::vector<mockturtle::mig_network::signal> &res,
                                               mockturtle::mig_network &mig)
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
  std::vector<mockturtle::mig_network::signal> coeffi, temp, sel_not_sig;
  //creating complemented signals for selection signals
  for (long unsigned int i = 0; i < k; i++) {
    sel_not_sig.emplace_back(mig.create_not(sel_sig[i]));
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
    coeffi.emplace_back(mig.create_nary_and(temp));
  }
  //creating output arranged by bits
  for (long unsigned int i = 0; i < n; i++) {
    temp.clear();
    for (long unsigned int j = 0; j < inp_num; j++) {
      temp.emplace_back(mig.create_and(input_sig_array[j][i], coeffi[j]));
    }
    res.emplace_back(mig.create_nary_or(temp));
  }
}

void Pass_mockturtle::mapping_shift_cell_lg2mig(const bool &sign_ext, mockturtle::mig_network &mig_ntk,
                                                const Node &node, const unsigned int &group_id)
{

}

void Pass_mockturtle::create_MIG_network(LGraph *g) {
  for(const auto node : g->forward()) {
    if (node2gid.find(node.get_compact())==node2gid.end())
      continue;
    unsigned int group_id = node2gid[node.get_compact()];
    if (gid2mig.find(group_id)==gid2mig.end())
      gid2mig[group_id] = mockturtle::mig_network();
    auto &mig_ntk = gid2mig[group_id];

    switch (node.get_type().op) {
      case Not_Op: {
        //Note: Don't need to check the node_pin pid since Not_Op has only one sink pin and one driver pin
        fmt::print("Not_Op in gid:{}\n",group_id);
        //Not_Op should only have single input edge
        I(node.inp_edges().size()==1 && node.out_edges().size()>0);

        std::vector<mockturtle::mig_network::signal> inp_sig, out_sig;
        //processing input signal
        fmt::print("input_bit_width:{}\n",node.inp_edges()[0].get_bits());
        setup_input_signal(group_id, node.inp_edges()[0], inp_sig, mig_ntk);
        //creating output signal
        for (long unsigned int i = 0; i < inp_sig.size(); i++) {
          out_sig.emplace_back(mig_ntk.create_not(inp_sig[i]));
        }
        //processing output signal
        for (const auto &out_edge : node.out_edges()) {
          fmt::print("output_bit_width:{}\n",out_edge.get_bits());
          //make sure the bit-width matches each other
          I(out_edge.get_bits()==out_sig.size());
          setup_output_signal(group_id, out_edge, out_sig, mig_ntk);
        }
        break;
      }

      case And_Op: {
        fmt::print("And_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>0 && node.out_edges().size()>0);
        mapping_logic_cell_lg2mig(&mockturtle::mig_network::create_nary_and, mig_ntk, node, group_id);
        break;
      }

      case Or_Op: {
        fmt::print("Or_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>0 && node.out_edges().size()>0);
        mapping_logic_cell_lg2mig(&mockturtle::mig_network::create_nary_or, mig_ntk, node, group_id);
        break;
      }

      case Xor_Op:
        fmt::print("Xor_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>0 && node.out_edges().size()>0);
        mapping_logic_cell_lg2mig(&mockturtle::mig_network::create_nary_xor, mig_ntk, node, group_id);
        break;

      case Equals_Op: {
        fmt::print("Equals_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        //mapping input edge to input signal
        //must differentiate between signed and unsigned input
        std::vector<mockturtle::mig_network::signal> out_sig, med_sig;
        std::vector<comparator_input_signal> operant_sigs;
        for (const auto &in_edge : node.inp_edges()) {
          fmt::print("input_bit_width:{}\n",in_edge.get_bits());
          comparator_input_signal inp_sig;
          setup_input_signal(group_id, in_edge, inp_sig.signals, mig_ntk);
          if (node.get_type().is_input_signed(in_edge.sink.get_pid())) {
            inp_sig.is_signed = true;
          } else {
            inp_sig.is_signed = false;
          }
          operant_sigs.emplace_back(inp_sig);
        }
        //creating output signal
        for (long unsigned int i = 0; i < operant_sigs.size()-1; i++) {
          med_sig.emplace_back(is_equal_op(operant_sigs[i], operant_sigs[i+1], mig_ntk));
        }
        out_sig.emplace_back(mig_ntk.create_nary_and(med_sig));
        //processing output signal
        for (const auto &out_edge : node.out_edges()) {
          I(out_edge.get_bits()==1);
          setup_output_signal(group_id, out_edge, out_sig, mig_ntk);
        }
        break;
      }

      case LessThan_Op: {
        fmt::print("LessThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mig(true, false, mig_ntk, node, group_id);
        break;
      }

      case GreaterThan_Op: {
        fmt::print("GreaterThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mig(false, false, mig_ntk, node, group_id);
        break;
      }

      case LessEqualThan_Op: {
        fmt::print("LessEqualThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mig(true, true, mig_ntk, node, group_id);
        break;
      }

      case GreaterEqualThan_Op: {
        fmt::print("GreaterEqualThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>=2 && node.out_edges().size()>0);
        mapping_comparison_cell_lg2mig(false, true, mig_ntk, node, group_id);
        break;
      }

      //A >> B, A is treated unsigned
      case LogicShiftRight_Op: {
        fmt::print("LogicShiftRight_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()==2 && node.out_edges().size()>0);
        I(node.inp_edges()[0].sink.get_pid() != node.inp_edges()[1].sink.get_pid());

        XEdge opr_A_edge = node.inp_edges()[0].sink.get_pid() == 0
                           ? opr_A_edge = node.inp_edges()[0]
                           : opr_A_edge = node.inp_edges()[1];
        XEdge opr_B_edge = node.inp_edges()[0].sink.get_pid() == 1
                           ? opr_B_edge = node.inp_edges()[0]
                           : opr_B_edge = node.inp_edges()[1];

        std::vector<mockturtle::mig_network::signal> opr_A_sig, out_sig;
        //processing input signal
        fmt::print("opr_A_bit_width:{}\n",opr_A_edge.get_bits());
        fmt::print("opr_B_bit_width:{}\n",opr_B_edge.get_bits());
        setup_input_signal(group_id, opr_A_edge, opr_A_sig, mig_ntk);
        if (opr_B_edge.driver.get_node().get_type().op == U32Const_Op) {
          //creating output signal for const shift
          uint32_t offset = opr_B_edge.driver.get_node().get_type_const_value();
          shr_op(out_sig, opr_A_sig, false, offset, mig_ntk);
        } else {
          std::vector<mockturtle::mig_network::signal> opr_B_sig, temp_out;
          std::vector<std::vector<mockturtle::mig_network::signal>> out_enum;
          I(opr_B_edge.get_bits() != 0);
          setup_input_signal(group_id, opr_B_edge, opr_B_sig, mig_ntk);
          for (long unsigned int ofs = 0; ofs < (long unsigned int)(1<<opr_B_sig.size()); ofs++) {
            temp_out.clear();
            shr_op(temp_out, opr_A_sig, false, ofs, mig_ntk);
            out_enum.emplace_back(temp_out);
          }
          //using B to select output (mux)
          //create a mux
          create_n_bit_k_input_mux(out_enum, opr_B_sig, out_sig, mig_ntk);
        }
        //processing output signal
        for (const auto &out_edge : node.out_edges()) {
          I(out_edge.get_bits() == out_sig.size());
          setup_output_signal(group_id, out_edge, out_sig, mig_ntk);
        }
        break;
      }

      case ArithShiftRight_Op: {
        fmt::print("ArithShiftRight_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()==2 && node.out_edges().size()>0);
        I(node.inp_edges()[0].sink.get_pid() != node.inp_edges()[1].sink.get_pid());
        break;
      }
/*
      case ShiftLeft_Op:
        //fmt::print("Node: ShiftLeft_Op\n");
        break;
*/
      default:
        fmt::print("Unknown_Op in gid:{}\n",group_id);
        break;
    }

  }

  //create mig network output signal for each group
  for(const auto node : g->forward()) {
    if (node2gid.find(node.get_compact()) == node2gid.end())
      continue;
    for (const auto &out_edge : node.out_edges()) {
      if (node2gid.find(out_edge.sink.get_node().get_compact())==node2gid.end()) {
        output_edges.insert(out_edge);
        I(node2gid[node.get_compact()] == edge2signal_mig[out_edge].gid);
        for (const auto sig : edge2signal_mig[out_edge].signals) {
          gid2mig[node2gid[node.get_compact()]].create_po(sig);
        }
      }
    }
  }
}

void Pass_mockturtle::convert_MIG_to_KLUT(LGraph *g) {
  absl::flat_hash_map<unsigned int,
                      absl::flat_hash_map<mockturtle::mig_network::node,
                                          mockturtle::klut_network::signal>> gid2mig2klut_io_signal;
  for (const auto &gid2mig_iter : gid2mig) {
    const unsigned int group_id = gid2mig_iter.first;
    const mockturtle::mig_network &mig_ntk = gid2mig_iter.second;
    //mockturtle::mig_network &mig_ntk = gid2mig[group_id];
    mockturtle::mig_network cleaned_mig_ntk = cleanup_dangling(mig_ntk);
    fmt::print("MIG network (gid:{}):\n", group_id);
    mockturtle::write_bench(mig_ntk,std::cout);
    //mockturtle::write_bench(cleaned_mig_ntk,std::cout);
    //create klut network for each group
    //converting mig to klut
    fmt::print("Converting MIG network (gid:{}) to KLUT network...\n", group_id);
    mockturtle::mapping_view<mockturtle::mig_network, true> mapped_mig{cleaned_mig_ntk};
    mockturtle::lut_mapping_params ps;
    ps.cut_enumeration_ps.cut_size = LUT_input_bits;
    mockturtle::lut_mapping<mockturtle::mapping_view<mockturtle::mig_network, true>, true>(mapped_mig, ps);
    mockturtle::klut_network klut_ntk =*mockturtle::collapse_mapped_network<mockturtle::klut_network>(mapped_mig);
    fmt::print("finished.\n");
    fmt::print("KLUT network (gid:{}):\n", group_id);
    mockturtle::write_bench(klut_ntk,std::cout);
    //equivalence checking using miter
    //const auto miter = *mockturtle::miter<mockturtle::mig_network>(cleaned_mig_ntk, klut_ntk);
    gid2klut[group_id]=klut_ntk;

    //mapping mig IO signal to klut IO signal
    //FIX ME: foreach_pi returns node while foreach_po returns signal
    //but in klut network, you can regard them as the same
    fmt::print("(Group ID:{}) Mapping MIG network IO signal to KLUT network IO signal...\n", group_id);
    I(mig_ntk.num_pis()==klut_ntk.num_pis() && mig_ntk.num_pos()==klut_ntk.num_pos());
    std::vector<mockturtle::mig_network::node> mig_signal;
    std::vector<mockturtle::klut_network::signal> klut_signal;
    mig_ntk.foreach_pi( [&](const auto& n) { mig_signal.emplace_back(n); } );
    mig_ntk.foreach_po( [&](const auto& n) { mig_signal.emplace_back(mig_ntk.get_node(n)); } );
    klut_ntk.foreach_pi( [&](const auto& n) { klut_signal.emplace_back(n); } );
    klut_ntk.foreach_po( [&](const auto& n) { klut_signal.emplace_back(n); } );
    absl::flat_hash_map<mockturtle::mig_network::node, mockturtle::klut_network::signal> mig2klut_io_signal;
    auto mig_signal_iter = mig_signal.begin();
    auto klut_signal_iter = klut_signal.begin();
    while (mig_signal_iter!=mig_signal.end()) {
      mig2klut_io_signal[*mig_signal_iter] = *klut_signal_iter;
      //fmt::print("MIG IO({}) -> KLUT IO({})\n", mig_ntk.node_to_index(*mig_signal_iter), klut_ntk.node_to_index(*klut_signal_iter));
      mig_signal_iter++;
      klut_signal_iter++;
    }
    gid2mig2klut_io_signal[group_id] = mig2klut_io_signal;
    fmt::print("finished.\n");
  }

  //mapping lgraph edges to klut io signals
  absl::flat_hash_set<XEdge> boundary_edges;
  fmt::print("IO edges info:\n");
  /*fmt::print("input edges:\n");
  for (const auto &in_edge : input_edges) {
    fmt::print("{}:{}->{}:{}\n",in_edge.driver.get_node().get_compact(), in_edge.driver.get_pid(), in_edge.sink.get_node().get_compact(), in_edge.sink.get_pid());
  }
  fmt::print("output edges:\n");
  for (const auto &out_edge : output_edges) {
    fmt::print("{}:{}->{}:{}\n",out_edge.driver.get_node().get_compact(), out_edge.driver.get_pid(), out_edge.sink.get_node().get_compact(), out_edge.sink.get_pid());
  }*/
  boundary_edges.insert(input_edges.begin(),input_edges.end());
  boundary_edges.insert(output_edges.begin(),output_edges.end());
  for (const auto &io_edge_it : boundary_edges) {
    const unsigned int gid = edge2signal_mig[io_edge_it].gid;
    edge2signal_klut[io_edge_it].gid = gid;
    for (const auto &mig_sig_it : edge2signal_mig[io_edge_it].signals) {
      edge2signal_klut[io_edge_it].signals.emplace_back(gid2mig2klut_io_signal[gid][gid2mig[gid].get_node(mig_sig_it)]);
    }
  }
  //print out the signal mapping information
  for (const auto &edge : boundary_edges) {
    I(edge2signal_mig[edge].gid == edge2signal_klut[edge].gid);
    const auto gid = edge2signal_klut[edge].gid;
    fmt::print("Group_ID:{} IO_XEdge:{}_to_{}\n", gid, edge.driver.get_node().debug_name(), edge.sink.get_node().debug_name());
    I(edge2signal_klut[edge].signals.size() == edge2signal_mig[edge].signals.size());
    const long unsigned int signal_width = edge2signal_mig[edge].signals.size();
    for (long unsigned int i = 0; i < signal_width; i++) {
      fmt::print("MIG IO({}) -> KLUT IO({})\n",
          gid2mig[gid].node_to_index(gid2mig[gid].get_node(edge2signal_mig[edge].signals[i])),
          gid2klut[gid].node_to_index(gid2klut[gid].get_node(edge2signal_klut[edge].signals[i])));
    }
  }
}

void Pass_mockturtle::create_lutified_lgraph(LGraph *g) {
  auto lg_path = g->get_path();
  auto lg_source = g->get_library().get_source(g->get_lgid());
  std::string g_name(g->get_name());
  std::string lg_name = absl::StrCat(g_name, LUTIFIED_NETWORK_NAME_SIGNATURE);
  LGraph *lg = LGraph::create(lg_path, lg_name, lg_source);
  //create unchanged portion
  //FIX ME: 1. copy name of the driver_pin
  //        2. add graph_io into internal_node_mapping
  fmt::print("Start mapping unchanged part...\n");
  for (const auto old_node : g->forward()) {
    if (node2gid.find(old_node.get_compact())==node2gid.end()) {
      Node new_node = lg->create_node(old_node);
      old_node_to_new_node[old_node.get_compact()] = new_node.get_compact();
      new_node_to_old_node[new_node.get_compact()] = old_node.get_compact();
      //create unchanged edges
      for (const auto &in_edge : old_node.inp_edges()) {
        if (edge2signal_mig.find(in_edge)==edge2signal_mig.end()) {
          auto peer_driver_node = in_edge.driver.get_node();
          if (old_node_to_new_node.find(peer_driver_node.get_compact())!=old_node_to_new_node.end()) {
            auto driver_node = old_node_to_new_node[peer_driver_node.get_compact()].get_node(lg); // TODO: Use this API instead of Node(lg,...)
            auto driver_pin = driver_node.setup_driver_pin(in_edge.driver.get_pid());
            auto sink_pin = new_node.setup_sink_pin(in_edge.sink.get_pid());
            lg->add_edge(driver_pin, sink_pin);
          }
        }
      }
      for (const auto &out_edge : old_node.out_edges()) {
        if (edge2signal_mig.find(out_edge)==edge2signal_mig.end()) {
          auto peer_sink_node = out_edge.sink.get_node();
          if (old_node_to_new_node.find(peer_sink_node.get_compact())!=old_node_to_new_node.end()) {
            auto sink_node = Node(lg,old_node_to_new_node[peer_sink_node.get_compact()]);
            auto sink_pin = sink_node.setup_sink_pin(out_edge.sink.get_pid());
            auto driver_pin = new_node.setup_driver_pin(out_edge.driver.get_pid());
            lg->add_edge(driver_pin, sink_pin);
          }
        }
      }
    }
  }
  fmt::print("Unchanged part mapped.\n");
  fmt::print("old_node -> new_node:\n");
  for (const auto &it : old_node_to_new_node) {
    fmt::print("{}->{}\n",it.first.get_node(lg).debug_name(),it.second.get_node(lg).debug_name());
  }

  //create lutified portion
  fmt::print("Start mapping lutified part...\n");
  for (const auto &gid2klut_iter : gid2klut) {
    const auto group_id = gid2klut_iter.first;
    const auto &klut_ntk = gid2klut_iter.second;
    //create new lut nodes
    fmt::print("Creating KLUT network (gid:{}) cells in LGraph...\n", group_id);
    klut_ntk.foreach_node( [&](const auto &klut_ntk_node) {
      if (klut_ntk.is_pi(klut_ntk_node)) {
        //this is an primary input of the klut network
        //does not have any fanin
        return; //continue
      }

      //check whether there is a complemented fanin,
      //a signal that is the opposite of the original signal
      //then change the truth table accordingly
      auto func = klut_ntk.node_function(klut_ntk_node);
      klut_ntk.foreach_fanin(klut_ntk_node, [&](const auto &sig, auto i) {
        if (klut_ntk.is_complemented(sig)) {
          kitty::flip_inplace(func, i);
        }
        if (klut_ntk.is_pi(klut_ntk.get_node(sig))) {
          Port_ID pid = (uint32_t) i;
          gid_fanin2parent_pid[std::make_pair(group_id, sig)].emplace_back(std::make_pair(klut_ntk_node, pid));
        }
      } );

      auto encoding = std::stoul(kitty::to_hex(func), nullptr, 16);
      auto new_node = lg->create_node();
      //if (!klut_ntk.is_constant(klut_ntk_node))
      //  fmt::print("LUT created!\n");
      new_node.set_type_lut(encoding);
      gidMTnode2LGnode[std::make_pair(group_id, klut_ntk_node)] = new_node.get_compact();
    } );
    fmt::print("finished.\n");

    //create new inner edges connecting nodes already created
    fmt::print("Creating KLUT network (gid:{}) inner edges in LGraph...\n", group_id);
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
        auto driver_node = Node(lg,gidMTnode2LGnode[std::make_pair(group_id, child)]);
        auto sink_node = Node(lg,gidMTnode2LGnode[std::make_pair(group_id, parent)]);
        auto driver_pin = driver_node.setup_driver_pin(0);
        auto sink_pin = sink_node.setup_sink_pin(i);
        lg->add_edge(driver_pin, sink_pin);
      } );
    } );
    fmt::print("finished.\n");
  }
  fmt::print("Lutified part mapped.\n");

  //create edges for input signals
  fmt::print("Creating KLUT network input edges in LGraph...\n");
  for (const auto &in_edge : input_edges) {
    const auto group_id = edge2signal_klut[in_edge].gid;
    const std::vector<mockturtle::klut_network::signal> &sigs = edge2signal_klut[in_edge].signals;
    I(old_node_to_new_node.find(in_edge.driver.get_node().get_compact()) != old_node_to_new_node.end());
    auto driver_node = Node(lg,old_node_to_new_node[in_edge.driver.get_node().get_compact()]);
    auto driver_pin = driver_node.setup_driver_pin(in_edge.driver.get_pid());
    const auto bit_width = in_edge.get_bits();
    I(bit_width == sigs.size());
    if (bit_width == 1) {
      for (const auto &parent_and_pid : gid_fanin2parent_pid[std::make_pair(group_id, sigs[0])]) {
        I(gidMTnode2LGnode.find(std::make_pair(group_id, parent_and_pid.first)) != gidMTnode2LGnode.end());
        auto sink_node = Node(lg,gidMTnode2LGnode[std::make_pair(group_id, parent_and_pid.first)]);
        const auto pid = parent_and_pid.second;
        auto sink_pin = sink_node.setup_sink_pin(pid);
        lg->add_edge(driver_pin, sink_pin);
      }
    } else {
      for (auto i = 0; i < bit_width; i++) {
        for (const auto &parent_and_pid : gid_fanin2parent_pid[std::make_pair(group_id, sigs[i])]) {
          I(gidMTnode2LGnode.find(std::make_pair(group_id, parent_and_pid.first)) != gidMTnode2LGnode.end());
          auto sink_node = Node(lg,gidMTnode2LGnode[std::make_pair(group_id, parent_and_pid.first)]);
          const auto pid = parent_and_pid.second;
          auto sink_pin = sink_node.setup_sink_pin(pid);
          //NOTE: update this simple Pick_Op node when the more powerful Pick_Op is readly
          auto pick_node = lg->create_node(Pick_Op);
          auto pick_node_sink_pin = pick_node.setup_sink_pin(0);
          auto pick_node_offset_pin = pick_node.setup_sink_pin(1);
          auto pick_node_driver_pin = pick_node.setup_driver_pin(0);
          auto const_node_for_bit_select = lg->create_node_const(i, 0);
          auto bit_select_signal = const_node_for_bit_select.setup_driver_pin(0);
          pick_node_driver_pin.set_bits(1);
          lg->add_edge(bit_select_signal, pick_node_offset_pin);
          lg->add_edge(driver_pin, pick_node_sink_pin);
          lg->add_edge(pick_node_driver_pin, sink_pin);
        }
      }
    }
  }
  fmt::print("finished.\n");

  //create edges for output signal
  fmt::print("Creating KLUT network output edges in LGraph...\n");
  for (const auto &out_edge : output_edges) {
    const auto group_id = edge2signal_klut[out_edge].gid;
    const auto klut = gid2klut[group_id];
    const std::vector<mockturtle::klut_network::signal> &sigs = edge2signal_klut[out_edge].signals;
    I(old_node_to_new_node.find(out_edge.sink.get_node().get_compact()) != old_node_to_new_node.end());
    auto sink_node = Node(lg,old_node_to_new_node[out_edge.sink.get_node().get_compact()]);
    auto sink_pin = sink_node.setup_sink_pin(out_edge.sink.get_pid());
    const auto bit_width = out_edge.get_bits();
    I(bit_width == sigs.size());
    if (bit_width == 1) {
      I(gidMTnode2LGnode.find(std::make_pair(group_id, gid2klut[group_id].get_node(sigs[0]))) != gidMTnode2LGnode.end());
      auto driver_node = Node(lg,gidMTnode2LGnode[std::make_pair(group_id, klut.get_node(sigs[0]))]);
      auto driver_pin = driver_node.setup_driver_pin(0);
      connect_complemented_signal(lg, driver_pin, sink_pin, klut, sigs[0]);
    } else {
      auto join_node = lg->create_node(Join_Op);
      for (auto i = 0; i < bit_width; i++) {
        I(gidMTnode2LGnode.find(std::make_pair(group_id, gid2klut[group_id].get_node(sigs[i]))) != gidMTnode2LGnode.end());
        auto driver_node = Node(lg,gidMTnode2LGnode[std::make_pair(group_id, klut.get_node(sigs[i]))]);
        auto kth_driver_pin = driver_node.setup_driver_pin(0);
        auto kth_sink_pin = join_node.setup_sink_pin(i);
        connect_complemented_signal(lg, kth_driver_pin, kth_sink_pin, klut, sigs[i]);
      }
      auto driver_pin = join_node.setup_driver_pin(0);
      lg->add_edge(driver_pin, sink_pin);
    }
  }
  fmt::print("finished.\n");

}

//slove complemented signal
void Pass_mockturtle::connect_complemented_signal(LGraph *g, Node_pin &driver_pin, Node_pin &sink_pin, const mockturtle::klut_network &klut, const mockturtle::klut_network::signal &signal) {
  if (klut.is_complemented(signal)) {
    auto not_gate = g->create_node(Not_Op);
    auto not_gate_sink_pin = not_gate.setup_sink_pin(0);
    auto not_gate_driver_pin = not_gate.setup_driver_pin(0);
    g->add_edge(driver_pin, not_gate_sink_pin);
    g->add_edge(not_gate_driver_pin, sink_pin);
  } else {
    g->add_edge(driver_pin, sink_pin);
  }
}
