//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <cmath>
#include <vector>

#include "firmap.hpp"
#include "lbench.hpp"
#include "lgraph.hpp"


Firmap::Firmap() {}

LGraph* Firmap::do_firrtl_mapping(LGraph *lg) {
  auto lg_name = lg->get_name();
  auto pos = lg_name.find("_firrtl");
  std::string  lg_source{lg->get_library().get_source(lg->get_lgid())}; // string, create can free it
  LGraph *new_lg = LGraph::create(lg->get_path(), lg_name.substr(0, pos), lg_source);
  
  // clone graph io 
  lg->each_graph_input([new_lg, this](Node_pin &dpin) {
      auto new_ginp = new_lg->add_graph_input(dpin.get_name(), dpin.get_pid(), dpin.get_bits());
      o2n_dpin.insert_or_assign(dpin, new_ginp);
  });

  lg->each_graph_output([new_lg, this](Node_pin &dpin) {
      auto new_gout = new_lg->add_graph_output(dpin.get_name(), dpin.get_pid(), dpin.get_bits());
      o2n_dpin.insert_or_assign(dpin, new_gout);
  });

  // clone graph main body
  for (auto node : lg->forward()) {
    auto op = node.get_type_op();
    fmt::print("{}\n", node.debug_name());
    if (op == Ntype_op::Sub) {
      auto subname = node.get_type_sub_node().get_name();
      if ( subname.substr(0,5) == "__fir") 
        map_fir_ops(node, subname, new_lg);
      else 
        clone_lg_ops(node, new_lg);
    } else {
      clone_lg_ops(node, new_lg); 
    }
  }
  return new_lg;
}

void Firmap::map_fir_ops(Node &node, std::string_view op, LGraph *new_lg) {
  if (op == "__fir_add") {
    map_fir_add(node, new_lg);
  } else if (op == "__fir_sub") {
    map_fir_sub(node, new_lg);
  }
}

void Firmap::map_fir_add(Node &old_node, LGraph *new_lg) {
  auto inp_edges = old_node.inp_edges();
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto e : old_node.inp_edges()) {
    auto it = o2n_dpin.find(e.driver);
    if (it == o2n_dpin.end())
      Pass::error("{} cannot found corresponding dpin in new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
  }

  I(old_node.out_connected_pins().size() == 1);
  for (auto old_dpin : old_node.out_connected_pins()) {
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
} 

void Firmap::map_fir_sub(Node &old_node, LGraph *new_lg) {
  auto inp_edges = old_node.inp_edges();
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto e : old_node.inp_edges()) {
    auto it = o2n_dpin.find(e.driver);
    if (it == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("B"));
    }
  }

  I(old_node.out_connected_pins().size() == 1);
  for (auto old_dpin : old_node.out_connected_pins()) {
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
  }
}

void Firmap::clone_lg_ops(Node &old_node, LGraph *new_lg) {
  auto inp_edges = old_node.inp_edges();
  auto new_node = new_lg->create_node(old_node);
  for (auto e : old_node.inp_edges()) {
    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin_raw(e.sink.get_pid()));
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin_raw(old_dpin.get_pid()));
  }
}

