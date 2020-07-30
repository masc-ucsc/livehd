//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lec.hpp"

#include "annotate.hpp"
#include "ezminisat.hpp"
#include "ezsat.hpp"
#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

static Pass_plugin sample("pass_lec", Pass_lec::setup);

void Pass_lec::setup() {
  Eprp_method m1("pass.lec", "Checks if all the LGraph outputs are satisfiable", &Pass_lec::work);

  register_pass(m1);
}

Pass_lec::Pass_lec(const Eprp_var &var) : Pass("pass.lec", var) {}

void Pass_lec::do_work(LGraph *g) { check_lec(g); }

void Pass_lec::work(Eprp_var &var) {
  Pass_lec p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
  }
}

void Pass_lec::check_lec(LGraph *g) {
  fmt::print("TODO: implement LEC\n");
  //-----------------------------------------------------------------------------------------------------
  ezMiniSAT sat;

  std::vector<int>  modelExpressions;
  std::vector<bool> modelValues;

  // determine no of inputs
  int i_num  = 0;
  int i_bits = 0;

  g->each_graph_input([this, &i_num, &i_bits](const Node_pin &pin) {
    i_num++;
    i_bits += pin.get_bits();
  });

  fmt::print("num of inputs: {},  no of Bits: {}\n", i_num, i_bits);

  // fmt::print("Declaring input variables.\n");

  // determine no of outputs
  int o_num  = 0;
  int o_bits = 0;

  g->each_graph_output([this, &o_num, &o_bits](const Node_pin &pin) {
    o_num++;
    o_bits += pin.get_bits();
  });

  fmt::print("num of outputs: {},  no of Bits: {}\n", o_num, o_bits);

  // fmt::print("Declaring output variables.\n");

  // Traverse graph
  fmt::print("Begin forward traversal.\n");

  for (const auto node : g->forward()) {
    // sat.generate_model();
    fmt::print("node type: {}\n", node.get_type().get_name());
  };
}
