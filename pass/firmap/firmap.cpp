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
  
  // clone graph input 
  lg->each_graph_input([new_lg, this](Node_pin &dpin) {
      auto new_ginp = new_lg->add_graph_input(dpin.get_name(), dpin.get_pid(), dpin.get_bits());
      o2n_dpin.insert_or_assign(dpin, new_ginp);
  });

  // clone graph output 
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

  // connect graph output to its driver
  lg->each_graph_output([this](Node_pin &dpin) {
    auto spin = dpin.get_sink_from_output();
    auto out_driver = spin.get_driver_pin();

    if (o2n_dpin.find(out_driver) == o2n_dpin.end())
      Pass::error("graph-out {} cannot find corresponding driver in new lgraph\n", out_driver.debug_name());

    if (o2n_dpin.find(dpin) == o2n_dpin.end()) 
      Pass::error("graph-out {} cannot find corresponding graph-out in new lgraph\n", dpin.debug_name());

    o2n_dpin[out_driver].connect_sink(o2n_dpin[dpin]);
  });

  return new_lg;
}

void Firmap::map_fir_ops(Node &node, std::string_view op, LGraph *new_lg) {
  if (op == "__fir_add") {
    map_fir_add(node, new_lg);
  } else if (op == "__fir_sub") {
    map_fir_sub(node, new_lg);
  } else if (op == "__fir_mul") {
    map_fir_mul(node, new_lg);
  } else if (op == "__fir_div") {
    map_fir_div(node, new_lg);
  } else if (op == "__fir_rem") {
    ; //todo
  } else if (op == "__fir_lt"  || op == "__fir_gt") {
    map_fir_lt_gt(node, new_lg, op);
  } else if (op == "__fir_leq" || op == "__fir_geq") {
    map_fir_leq_geq(node, new_lg, op);
  } else if (op == "__fir_eq") {
    map_fir_eq(node, new_lg);
  } else if (op == "__fir_neq") {
    map_fir_neq(node, new_lg);
  }
}


// A neq B == ~(A eq B) 
void Firmap::map_fir_neq(Node &old_node, LGraph *new_lg) {
  auto new_node_eq = new_lg->create_node(Ntype_op::EQ);
  auto new_node_not = new_lg->create_node(Ntype_op::Not);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node_eq.setup_sink_pin("A"));
  }
  
  new_node_eq.setup_driver_pin().connect_sink(new_node_not.setup_sink_pin("a"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_not.setup_driver_pin());
}

void Firmap::map_fir_eq(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::EQ);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


// A geq B == (A gt B) or (A eq B)
void Firmap::map_fir_leq_geq(Node &old_node, LGraph *new_lg, std::string_view op) {

  Node new_node_eq = new_lg->create_node(Ntype_op::EQ);
  Node new_node_or = new_lg->create_node(Ntype_op::Or);
  Node new_node_cmp;
  if (op == "__fir_leq")
    new_node_cmp = new_lg->create_node(Ntype_op::LT);
  else 
    new_node_cmp = new_lg->create_node(Ntype_op::GT);

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node_cmp.setup_sink_pin("A"));
      o2n_dpin[e.driver].connect_sink(new_node_eq.setup_sink_pin("A"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node_cmp.setup_sink_pin("B"));
      o2n_dpin[e.driver].connect_sink(new_node_eq.setup_sink_pin("B"));
    }
  }

  new_node_cmp.setup_driver_pin().connect_sink(new_node_or.setup_sink_pin("A"));
  new_node_eq.setup_driver_pin().connect_sink(new_node_or.setup_sink_pin("A"));

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node_or.setup_driver_pin());
}



void Firmap::map_fir_lt_gt(Node &old_node, LGraph *new_lg, std::string_view op) {
  Node new_node;
  if (op == "__fir_lt")
    new_node = new_lg->create_node(Ntype_op::LT);
  else 
    new_node = new_lg->create_node(Ntype_op::GT);

  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


void Firmap::map_fir_div(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Div);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("{} cannot find corresponding dpin in new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("a"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("b"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 


void Firmap::map_fir_mul(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Mult);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("{} cannot find corresponding dpin in new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 


void Firmap::map_fir_add(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("{} cannot find corresponding dpin in new lgraph", e.driver.debug_name());

    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
} 


void Firmap::map_fir_sub(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(Ntype_op::Sum);
  for (auto e : old_node.inp_edges()) {
    if (o2n_dpin.find(e.driver) == o2n_dpin.end())
      Pass::error("dpin:{} cannot found corresponding dpin in new lgraph", e.driver.debug_name());

    if (e.sink == old_node.setup_sink_pin("e1")) {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("A"));
    } else {
      o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin("B"));
    }
  }

  for (auto old_dpin : old_node.out_connected_pins()) 
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin());
}


void Firmap::clone_lg_ops(Node &old_node, LGraph *new_lg) {
  auto new_node = new_lg->create_node(old_node);
  for (auto e : old_node.inp_edges()) {
    o2n_dpin[e.driver].connect_sink(new_node.setup_sink_pin_raw(e.sink.get_pid()));
  }

  for (auto old_dpin : old_node.out_connected_pins()) {
    o2n_dpin.insert_or_assign(old_dpin, new_node.setup_driver_pin_raw(old_dpin.get_pid()));
    if (old_dpin.has_name())
      new_node.setup_driver_pin_raw(old_dpin.get_pid()).set_name(old_dpin.get_name());
  }
}

void Firmap::map_fir_rem(Node &old_node, LGraph *new_lg) {
  ; //todo
} 
