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

void Pass_mockturtle::lg_partition(LGraph *g) {
  std::map<int, int> group_id_mapping;
  int group_id = 0;

  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    fmt::print("Node identifier:{}\n", node.get_compact());
    if (eligable_cell_op(node.get_type().op)) {

      if (node2gid.find(node.get_compact())==node2gid.end()) {

        int current_node_group_id = 0;

        for(const auto &in_edge : node.inp_edges()) {
          auto peer_driver_node = in_edge.driver.get_node();
          if (node2gid.find(peer_driver_node.get_compact())!=node2gid.end()) {
            current_node_group_id = node2gid[peer_driver_node.get_compact()];
            break;
          }
        }

        if (current_node_group_id == 0) {
          group_id++;
          current_node_group_id = group_id;
        }
        node2gid[node.get_compact()] = current_node_group_id;
      }

      for(const auto &out_edge : node.out_edges()) {
        auto peer_sink_node = out_edge.sink.get_node();
        if (eligable_cell_op(peer_sink_node.get_type().op)) {
          if (node2gid.find(peer_sink_node.get_compact())==node2gid.end()) {
            node2gid[peer_sink_node.get_compact()] = node2gid[node.get_compact()];
          }
          else {
            group_id_mapping[node2gid[node.get_compact()]]=node2gid[peer_sink_node.get_compact()];
          }
        }
      }
    }
  }

  for (std::map<int,int>::reverse_iterator rit = group_id_mapping.rbegin(); rit!=group_id_mapping.rend(); rit++) {
    for (auto &it:node2gid) {
      if (it.second==rit->first)
        it.second=rit->second;
    }
  }
}

void Pass_mockturtle::setup_input_signal(const int &group_id, const XEdge &input_edge, std::vector<mockturtle::mig_network::signal> &input_signal, mockturtle::mig_network &mig) {
  //check if this input edge is already in the output mapping table
  //then setup the input signal accordingly
  if (edge2signal_mig.count(input_edge)!=0) {
    I(group_id == edge2signal_mig[input_edge].gid);
    //fetch the input signal from mapping table
    I(input_edge.get_bits() == edge2signal_mig[input_edge].signals.size());
    for (auto i=0;i<input_edge.get_bits();i++) {
      input_signal.emplace_back(edge2signal_mig[input_edge].signals[i]);
    }
  } else {
    input_edges.insert(input_edge);
    edge2signal_mig[input_edge].gid = group_id;
    //create new input signals and map them back
    for (auto i=0;i<input_edge.get_bits();i++) {
      input_signal.emplace_back(mig.create_pi());
    }
    edge2signal_mig[input_edge].signals=input_signal;
  }
}

void Pass_mockturtle::setup_output_signal(const int &group_id, const XEdge &output_edge, std::vector<mockturtle::mig_network::signal> &output_signal, mockturtle::mig_network &mig) {
  //check if the output edge is already in the input mapping table
  //then setup/update the output/input table
  if (edge2signal_mig.count(output_edge)!=0) {
    I(group_id == edge2signal_mig[output_edge].gid);
    I(output_edge.get_bits() == edge2signal_mig[output_edge].signals.size());
    for (auto i=0;i<output_edge.get_bits();i++) {
      mockturtle::mig_network::node old_node = mig.get_node(edge2signal_mig[output_edge].signals[i]);
      mig.substitute_node(old_node,output_signal[i]);
      mig.take_out_node(old_node);
      //FIX ME: remove dangling input node when converting to lgraph
      //because take_out_node() does not remove primary input nodes.
    }
  }
  //mapping output edge to output signal
  edge2signal_mig[output_edge].gid = group_id;
  edge2signal_mig[output_edge].signals=output_signal;
}

//split the input signal by bits
void Pass_mockturtle::split_input_signal(const std::vector<mockturtle::mig_network::signal> &input_signal, std::vector<std::vector<mockturtle::mig_network::signal>> &splitted_input_signal) {
  for (long unsigned int i=0;i<input_signal.size();i++) {
    if (splitted_input_signal.size()<=i) {
      splitted_input_signal.resize(i+1);
    }
    splitted_input_signal[i].emplace_back(input_signal[i]);
  }
}

void Pass_mockturtle::create_LUT_network(LGraph *g) {
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished
    if (node2gid.find(node.get_compact())==node2gid.end())
      continue;
    int group_id = node2gid[node.get_compact()];
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
        for (long unsigned int i=0;i<inp_sig.size();i++) {
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

        //mapping all input edge to input signal
        //out_sig_0: regular AND
        //out_sig_1: reduced AND
        std::vector<mockturtle::mig_network::signal> out_sig_0, out_sig_1;
        std::vector<std::vector<mockturtle::mig_network::signal>> inp_sig_group_by_bit;
        //processing input signal
        for (const auto &in_edge : node.inp_edges()) {
          fmt::print("input_bit_width:{}\n",in_edge.get_bits());
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
              out_sig_0.emplace_back(mig_ntk.create_nary_and(inp_sig_group_by_bit[i]));
            }
            break;
          }
        }
        out_sig_1.emplace_back(mig_ntk.create_nary_and(out_sig_0));
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
        //FIX ME: delete unused output signal
        //mockturtle can clean up dangling signals
        break;
      }
/*
      case Or_Op:
        fmt::print("Or_Op in gid:{}\n",group_id);
        break;
      case Xor_Op:
        fmt::print("Xor_Op in gid:{}\n",group_id);
        break;
*/
      case Equals_Op: {
        fmt::print("Equals_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>1 && node.out_edges().size()>0);
        //mapping all input edge to input signal
        std::vector<mockturtle::mig_network::signal> out_sig, med_sig;
        std::vector<std::vector<mockturtle::mig_network::signal>> inp_sig_group_by_bit;
        for (const auto &in_edge : node.inp_edges()) {
          fmt::print("input_bit_width:{}\n",in_edge.get_bits());
          std::vector<mockturtle::mig_network::signal> inp_sig;
          setup_input_signal(group_id, in_edge, inp_sig, mig_ntk);
          split_input_signal(inp_sig, inp_sig_group_by_bit);
        }
        //creating output signal
        //A[0..n]==B[0..n]==C[0..n] iff
        //A0^B0==0 && B0^C0==0 && A1^B1==0 && B1^C1==0
        //&& ... && AN^BN==0 && BN^CN==0
        //that is: ~(|{(A0,B0),(B0,C0) ... (AN,BN), (BN,CN)})==1
        for (long unsigned int i = 0; i < inp_sig_group_by_bit.size(); i++) {
          for (long unsigned int j = 1; j < inp_sig_group_by_bit[i].size(); j++) {
            med_sig.emplace_back(mig_ntk.create_xor(inp_sig_group_by_bit[i][j-1],inp_sig_group_by_bit[i][j]));
          }
        }
        out_sig.emplace_back(mig_ntk.create_not(mig_ntk.create_nary_or(med_sig)));
        //processing output signal
        for (const auto &out_edge : node.out_edges()) {
          I(out_edge.get_bits()==1);
          setup_output_signal(group_id, out_edge, out_sig, mig_ntk);
        }
        break;
      }
/*
      case LessThan_Op: {
        //CAUTION: mockturtle has neither binary nor n-ary LessThan
        //therefore the implementation would be tricky and TBD
        fmt::print("LessThan_Op in gid:{}\n",group_id);
        I(node.inp_edges().size()>1 && node.out_edges().size()>0);
        //
        break;
      }
*/
      default:
        fmt::print("Unknown_Op in gid:{}\n",group_id);
        break;
    }

  }

  //create mig network output signal for each group
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished
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

  absl::flat_hash_map<int, absl::flat_hash_map<mockturtle::mig_network::node, mockturtle::klut_network::signal>> gid2mig2klut_io_signal;
  for (const auto &gid2mig_iter : gid2mig) {
    const int group_id = gid2mig_iter.first;
    const mockturtle::mig_network &mig_ntk = gid2mig_iter.second;
    //mockturtle::mig_network &mig_ntk = gid2mig[group_id];
    mockturtle::mig_network cleaned_mig_ntk = cleanup_dangling(mig_ntk);
    fmt::print("MIG network under Group ID:{}\n", group_id);
    mockturtle::write_bench(mig_ntk,std::cout);
    //mockturtle::write_bench(cleaned_mig_ntk,std::cout);
    //create klut network for each group
    //converting mig to klut
    mockturtle::mapping_view<mockturtle::mig_network, true> mapped_mig{cleaned_mig_ntk};
    mockturtle::lut_mapping_params ps;
    ps.cut_enumeration_ps.cut_size = LUT_input_bits;
    mockturtle::lut_mapping<mockturtle::mapping_view<mockturtle::mig_network, true>, true>(mapped_mig, ps);
    mockturtle::klut_network klut_ntk =*mockturtle::collapse_mapped_network<mockturtle::klut_network>(mapped_mig);
    fmt::print("KLUT network under Group ID:{}\n", group_id);
    mockturtle::write_bench(klut_ntk,std::cout);
    //equivalence checking using miter
    //const auto miter = *mockturtle::miter<mockturtle::mig_network>(cleaned_mig_ntk, klut_ntk);
    gid2klut[group_id]=klut_ntk;
    //mapping mig io signal to klut io signal
    //FIX ME: foreach_pi returns node while foreach_po returns signal
    //but in klut network, you can regard them as the same
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
  }
  absl::flat_hash_set<XEdge> boundary_edges;
  fmt::print("input edges:\n");
  for (const auto &in_edge : input_edges) {
    fmt::print("{}:{}->{}:{}\n",in_edge.driver.get_node().get_compact(), in_edge.driver.get_pid(), in_edge.sink.get_node().get_compact(), in_edge.sink.get_pid());
  }
  fmt::print("output edges:\n");
  for (const auto &out_edge : output_edges) {
    fmt::print("{}:{}->{}:{}\n",out_edge.driver.get_node().get_compact(), out_edge.driver.get_pid(), out_edge.sink.get_node().get_compact(), out_edge.sink.get_pid());
  }
  boundary_edges.insert(input_edges.begin(),input_edges.end());
  boundary_edges.insert(output_edges.begin(),output_edges.end());
  //mapping lgraph edges to klut io signals
  for (const auto &io_edge_it : boundary_edges) {
    const int gid = edge2signal_mig[io_edge_it].gid;
    edge2signal_klut[io_edge_it].gid = gid;
    for (const auto &mig_sig_it : edge2signal_mig[io_edge_it].signals) {
      edge2signal_klut[io_edge_it].signals.emplace_back(gid2mig2klut_io_signal[gid][gid2mig[gid].get_node(mig_sig_it)]);
    }
  }
  //print out the signal mapping information
  for (const auto &edge : boundary_edges) {
    I(edge2signal_mig[edge].gid == edge2signal_klut[edge].gid);
    const auto gid = edge2signal_klut[edge].gid;
    fmt::print("Group_ID:{} IO_XEdge:{}_to_{}\n", gid, edge.driver.get_node().get_compact(), edge.sink.get_node().get_compact());
    I(edge2signal_klut[edge].signals.size() == edge2signal_mig[edge].signals.size());
    const unsigned int signal_width = edge2signal_mig[edge].signals.size();
    for (unsigned int i = 0; i < signal_width; i++) {
      fmt::print("MIG IO({}) -> KLUT IO({})\n",
          gid2mig[gid].node_to_index(gid2mig[gid].get_node(edge2signal_mig[edge].signals[i])),
          gid2klut[gid].node_to_index(gid2klut[gid].get_node(edge2signal_klut[edge].signals[i])));
    }
  }
}

void Pass_mockturtle::create_lutified_lgraph(LGraph *g) {
  auto lg_path = g->get_path();
  auto lg_source = g->get_library().get_source(g->get_lgid());
  std::string lg_name(g->get_name());
  lg_name += LUTIFIED_NETWORK_NAME_SIGNATURE;
  LGraph *lg = LGraph::create(lg_path, lg_name, lg_source);
  //create unchanged portion
  for (const auto &nid : g->forward()) {
    auto old_node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished
    if (node2gid.find(old_node.get_compact())==node2gid.end()) {
      Node_Type_Op op = old_node.get_type().op;
      //FIX ME: op doesn't not reflect the true operation id of subgraph, tmap, uconst, conststr and lut.
      auto new_node = lg->create_node(op);
      //new_node.set_type();
      old_node_to_new_node[old_node.get_compact()] = new_node.get_compact();
      new_node_to_old_node[new_node.get_compact()] = old_node.get_compact();
      //create unchanged edges
      for (const auto &in_edge : old_node.inp_edges()) {
        if (edge2signal_mig.find(in_edge)==edge2signal_mig.end()) {
          auto peer_driver_node = in_edge.driver.get_node();
          if (old_node_to_new_node.find(peer_driver_node.get_compact())!=old_node_to_new_node.end()) {
            auto driver_node = Node(lg,0,Node::Compact(old_node_to_new_node[peer_driver_node.get_compact()]));
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
            auto sink_node = Node(lg,0,Node::Compact(old_node_to_new_node[peer_sink_node.get_compact()]));
            auto sink_pin = sink_node.setup_sink_pin(out_edge.sink.get_pid());
            auto driver_pin = new_node.setup_driver_pin(out_edge.driver.get_pid());
            lg->add_edge(driver_pin, sink_pin);
          }
        }
      }
    }
  }
  fmt::print("old_node->new_node:\n");
  for (const auto &it : old_node_to_new_node) {
    fmt::print("{}->{}\n",it.first,it.second);
  }
  //create lutified portion
  for (const auto &gid2klut_iter : gid2klut) {
    const auto group_id = gid2klut_iter.first;
    const auto &klut_ntk = gid2klut_iter.second;
    //create new lut nodes
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
          Port_ID pid = (uint16_t) i;
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

    //create new edges connecting nodes already created
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
        auto driver_node = Node(lg,0,Node::Compact(gidMTnode2LGnode[std::make_pair(group_id, child)]));
        auto sink_node = Node(lg,0,Node::Compact(gidMTnode2LGnode[std::make_pair(group_id, parent)]));
        auto driver_pin = driver_node.setup_driver_pin(0);
        auto sink_pin = sink_node.setup_sink_pin(i);
        /*auto result = */lg->add_edge(driver_pin, sink_pin);
        //fmt::print("Edge added!\n");
      } );

    } );
  }
  //create edges for input signals
  for (const auto &in_edge : input_edges) {
    const auto group_id = edge2signal_klut[in_edge].gid;
    const std::vector<mockturtle::klut_network::signal> &sigs = edge2signal_klut[in_edge].signals;
    auto driver_node = Node(lg,0,Node::Compact(old_node_to_new_node[in_edge.driver.get_node().get_compact()]));
    auto driver_pin = driver_node.setup_driver_pin(in_edge.driver.get_pid());
    const auto bit_width = in_edge.get_bits();
    I(bit_width == sigs.size());
    for (auto i=0; i<bit_width; i++) {
      for (const auto &parent_and_pid : gid_fanin2parent_pid[std::make_pair(group_id, sigs[i])]) {
        I(gidMTnode2LGnode.find(std::make_pair(group_id, parent_and_pid.first)) != gidMTnode2LGnode.end());
        auto sink_node = Node(lg,0,Node::Compact(gidMTnode2LGnode[std::make_pair(group_id, parent_and_pid.first)]));
        const auto pid = parent_and_pid.second;
        auto sink_pin = sink_node.setup_sink_pin(pid);
        //FIX ME: use join_op to connect the nodes
        lg->add_edge(driver_pin, sink_pin);
      }
    }
  }
  //create edges for output signal
  for (const auto &out_edge : output_edges) {
    const auto group_id = edge2signal_klut[out_edge].gid;
    const std::vector<mockturtle::klut_network::signal> &sigs = edge2signal_klut[out_edge].signals;
    auto sink_node = Node(lg,0,Node::Compact(old_node_to_new_node[out_edge.sink.get_node().get_compact()]));
    auto sink_pin = sink_node.setup_sink_pin(out_edge.sink.get_pid());
    const auto bit_width = out_edge.get_bits();
    I(bit_width == sigs.size());
    for (auto i=0; i<bit_width; i++) {
      I(gidMTnode2LGnode.find(std::make_pair(group_id, gid2klut[group_id].get_node(sigs[i]))) != gidMTnode2LGnode.end());
      const auto klut = gid2klut[group_id];
      auto driver_node = Node(lg,0,Node::Compact(gidMTnode2LGnode[std::make_pair(group_id, klut.get_node(sigs[i]))]));
      auto driver_pin = driver_node.setup_driver_pin(0);
      //slove complemented signal
      if (klut.is_complemented(sigs[i])) {
        auto not_gate = lg->create_node(Not_Op);
        auto not_gate_sink_pin = not_gate.setup_sink_pin(0);
        auto not_gate_driver_pin = not_gate.setup_driver_pin(0);
        lg->add_edge(driver_pin, not_gate_sink_pin);
        lg->add_edge(not_gate_driver_pin, sink_pin);
      } else {
        lg->add_edge(driver_pin, sink_pin);
      }
    }
  }
  lg->close();
}

void Pass_mockturtle::do_work(LGraph *g) {
  LGBench b("pass.mockturtle");

  fmt::print("Start partition...\n");

  lg_partition(g);

  for (const auto &group_id_it:node2gid) {
    fmt::print("nid{} -> gid:{}\n", group_id_it.first, group_id_it.second);
  }

  fmt::print("Creating LUT network...\n");

  create_LUT_network(g);

  create_lutified_lgraph(g);
}
