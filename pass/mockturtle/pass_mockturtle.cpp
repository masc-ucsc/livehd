//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

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
  absl::flat_hash_map<Node::Compact, int> group_boundary;
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

  lg_partition(g);

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
  for (auto const group_id_it:group_boundary_set) {
    fmt::print("Group ID:{}\n", group_id_it.first);
    for (auto const node_it:group_id_it.second) {
      fmt::print("Node identifier:{}\n", node_it);
    }
  }
}
