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

      if (group_boundary.find(node.get_compact())==group_boundary.end()) {

        int current_node_group_id = 0;

        for(const auto &in_edge : node.inp_edges()) {
          auto peer_driver_node = in_edge.driver.get_node();
          if (group_boundary.find(peer_driver_node.get_compact())!=group_boundary.end()) {
            current_node_group_id = group_boundary[peer_driver_node.get_compact()];
            break;
          }
        }

        if (current_node_group_id == 0) {
          group_id++;
          current_node_group_id = group_id;
        }
        group_boundary[node.get_compact()] = current_node_group_id;
      }

      for(const auto &out_edge : node.out_edges()) {
        auto peer_sink_node = out_edge.sink.get_node();
        if (eligable_cell_op(peer_sink_node.get_type().op)) {
          if (group_boundary.find(peer_sink_node.get_compact())==group_boundary.end()) {
            group_boundary[peer_sink_node.get_compact()] = group_boundary[node.get_compact()];
          }
          else {
            group_id_mapping[group_boundary[node.get_compact()]]=group_boundary[peer_sink_node.get_compact()];
          }
        }
      }
    }
  }

  for (std::map<int,int>::reverse_iterator rit = group_id_mapping.rbegin(); rit!=group_id_mapping.rend(); rit++) {
    for (auto &it:group_boundary) {
      if (it.second==rit->first)
        it.second=rit->second;
    }
  }

  for (const auto &it:group_boundary) {
    group_boundary_set[it.second].push_back(it.first);
  }

}

void Pass_mockturtle::create_LUT_network(LGraph *g) {
  for (const auto gid:group_boundary_set) {
    auto klut = mockturtle::klut_network();
    gid2klut[gid.first]=klut;
    for (const auto gid_node:gid.second) {
      auto cur_node = Node(g, 0, gid_node);

      switch (cur_node.get_type().op) {
        case And_Op:
          /*
          std::vector<mockturtle::klut_network::signal> in_sig;
          for (const auto &in_edge : cur_node.inp_edges() ) {
            const auto x = klut.create_pi();
            edge_signal_mapping[in_edge] = x;
            in_sig.push_back(x);
          }
          //check: how many inputs we have?
          auto out_sig = in_sig[0];
          for (long unsigned int i = 1; i < in_sig.size(); i++) {
            out_sig = klut.create_and(in_sig[i], out_sig);
          }
          //FIX ME:
          edge_signal_mapping[cur_node.out_edges()[0]]=out_sig;
          */
          break;

        case Or_Op:
          break;
        case Xor_Op:
          break;
        default:
          break;
      }


    }
  }
}

void Pass_mockturtle::do_work(LGraph *g) {
  LGBench b("pass.mockturtle");

  //absl::flat_hash_map<Node::Compact, std::pair<std::string,std::pair<int,int>>> cell_type;

  int cell_amount = 0;
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished
    node.get_type(); //avoid warning
    cell_amount++;
/*
    auto name = node.get_type().get_name();
    int in_edges_num = 0;
    for(const auto &in_edge : node.inp_edges()) {
      in_edges_num++;
    }
    int out_edges_num = 0;
    for(const auto &out_edge : node.out_edges()) {
      out_edges_num++;
    }
    cell_type[node.get_compact()]=std::make_pair(name,std::make_pair(in_edges_num,out_edges_num));
*/
  }
//  fmt::print("Pass: number of cells {}\n", cell_type.size());
  fmt::print("{} nodes passed.\n", cell_amount);
/*
  for (auto const it:group_id_mapping) {
    fmt::print("GID:{}->{}\n", it.first, it.second);
  }
  for(auto const it:cell_type) {
    fmt::print("node_type:{} in_edges:{} out_edges:{}\n", it.second.first, it.second.second.first, it.second.second.second);
  }
*/
  fmt::print("Start partition...");

  lg_partition(g);

  for (const auto &group_id_it:group_boundary_set) {
    fmt::print("Group ID:{}\n", group_id_it.first);
    for (const auto &node_it:group_id_it.second) {
      fmt::print("Node identifier:{}\n", node_it);
    }
  }

  fmt::print("Creating k-LUT network...");

  create_LUT_network(g);


}
