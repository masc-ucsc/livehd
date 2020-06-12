//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_cprop.hpp"

#include <time.h>

#include <string>

#include "lgedgeiter.hpp"
#include "lgraph.hpp"

void setup_pass_cprop() { Pass_cprop::setup(); }

void Pass_cprop::setup() {
  Eprp_method m1("pass.cprop", "in-place copy propagation", &Pass_cprop::optimize);

  register_pass(m1);
}

Pass_cprop::Pass_cprop(const Eprp_var &var) : Pass("pass.cprop", var) {}

void Pass_cprop::optimize(Eprp_var &var) {
  Pass_cprop pass(var);

  for (auto &l : var.lgs) {
    pass.trans(l);
  }
}

void Pass_cprop::try_collapse_forward(Node &node) {

  // No need to collapse things like const -> join because the Lconst will be forward eval

  auto op = node.get_type().op;
  if (op != Sum_Op)  // FIXME: most logic, join and other ops can collapse forward
    return;

  for (auto &out : node.out_edges()) {
    if (out.sink.get_node().get_type().op!=op)
      continue;

    for (auto &inp : node.inp_edges()) {
      inp.driver.connect_sink(out.sink);
    }
    out.del_edge();
  }
}

void Pass_cprop::replace_node(Node &node, const Lconst &result) {
  auto new_node = node.get_class_lgraph()->create_node_const(result);
  auto dpin     = new_node.get_driver_pin();

  for(auto &out:node.out_edges()) {
    if (dpin.get_bits() == out.driver.get_bits() || out.driver.get_bits()==0) {
      dpin.connect_sink(out.sink);
    }else{
      // create new const node to preserve bits
      auto result2 = result.adjust_bits(out.driver.get_bits());

      auto new_node2 = node.get_class_lgraph()->create_node_const(result2);
      auto dpin2     = new_node2.get_driver_pin();

      fmt::print("creating const:{} {}bits {}  from const:{} {}bits\n"
          , result2.to_pyrope(), out.driver.get_bits(), dpin2.get_bits()
          , result.to_pyrope() , dpin.get_bits());

      dpin2.connect_sink(out.sink);
    }

    //out.del_edge();
  }

  node.del_node();
}

void Pass_cprop::trans(LGraph *g) {

  for (auto node : g->forward()) {

    if (!node.has_outputs()) {
      if (!node.is_type_sub()) // No subs (inside side-effets
        node.del_node();
      continue;
    }

    try_collapse_forward(node);

    int  n_inputs_constant   = 0;
    int  n_inputs            = 0;

    for (auto e : node.inp_edges()) {
      n_inputs++;
      if (e.driver.get_node().is_type_const())
        n_inputs_constant++;
    }

    if (n_inputs_constant==0)
      continue;

    bool all_inputs_constant = n_inputs == n_inputs_constant;

    fmt::print("node:{} has {} constant inputs out of {} {}\n", node.debug_name(),
        n_inputs_constant,n_inputs, all_inputs_constant?"(all the inputs)":"");

    auto op = node.get_type().op;
    if (op == Join_Op && all_inputs_constant) {
      Lconst result;
      for(auto &i:node.inp_edges_ordered()) {
        result = result << i.driver.get_bits();
        auto c = i.driver.get_node().get_type_const();
        I(c<=i.driver.get_bits());
        result  = result | c;
      }
      result.adjust_bits(node.get_driver_pin().get_bits());

      fmt::print("cprop: join to {}\n", result.to_pyrope());

      replace_node(node, result);

    }else if (op == ShiftLeft_Op && all_inputs_constant) {

      Lconst val = node.get_sink_pin("A").get_driver_node().get_type_const();
      Lconst amt = node.get_sink_pin("B").get_driver_node().get_type_const();

      Lconst result = val<<amt;

      fmt::print("cprop: shl to {} ({}<<{})\n", result.to_pyrope(), val.to_pyrope(), amt.to_pyrope());

      replace_node(node, result);
    }else if (op == Sum_Op && all_inputs_constant) {

      Lconst result;
      for(auto &i:node.inp_edges()) {
        auto c = i.driver.get_node().get_type_const();
        if (i.driver.get_pid()==0 || i.driver.get_pid()==1) {
          result = result + c;
        }else{
          result = result - c;
        }
      }

      fmt::print("cprop: add to {}\n", result.to_pyrope());

      replace_node(node, result);
    }
  }

  for(auto node:g->fast()) {
    if (!node.has_outputs()) {
      if (!node.is_type_sub()) // No subs (inside side-effets
        node.del_node();
      continue;
    }
  }

  g->sync();
}
