//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lec.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include "annotate.hpp"
#include "boolector.h"
#include "cell.hpp"
#include "lbench.hpp"
#include "lezminisat.hpp"
#include "lezsat.hpp"
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

void Pass_lec::do_work(LGraph *g) {
  fmt::print("before check lec\n");
  check_lec(g);
  fmt::print("inside do work\n");
}
// do work: call function to get lgraph1, call function to get lgraph2, or get all lgraphs
// call function to check equivalence

void Pass_lec::work(Eprp_var &var) {
  Pass_lec p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
    fmt::print("Lgraph Size: {}\n", var.lgs.size());
    fmt::print("name: {}\n", g->get_name());
  }
}

void Pass_lec::check_lec(LGraph *g) {
  fmt::print("**********BEGIN check LEC\n");
  std::multimap<std::string, std::string> nodeMap;

  // fmt::print("TODO: implement LEC\n");
  //-------------------------------------------------

  fmt::print("LGraph name: {}\n", g->get_name());
  /*
  Btor *btor;
  BoolectorNode *input1, *input2, *formula1;
  BoolectorSort s;
  int result;

  btor = boolector_new();
  s = boolector_bitvec_sort( btor, 8);
  input1 = boolector_var( btor, s, NULL);
  input2 = boolector_var( btor, s, NULL);
  */
  // determine no of inputs
  int i_num  = 0;
  int i_bits = 0;

  g->each_graph_input([&i_num, &i_bits](const Node_pin &pin) {
    i_num++;
    i_bits += pin.get_bits();
  });

  fmt::print("num of inputs: {},  no of Bits: {}\n", i_num, i_bits);

  // determine no of outputs
  int o_num  = 0;
  int o_bits = 0;

  g->each_graph_output([&o_num, &o_bits](const Node_pin &pin) {
    o_num++;
    o_bits += pin.get_bits();
  });

  fmt::print("num of outputs: {},  no of Bits: {}\n", o_num, o_bits);

  for (const auto &node : g->forward()) {
    fmt::print("Node type: {}\n", node.get_type_name());
    nodeMap.insert(std::make_pair(node.get_type_name(), g->get_name()));

    // fmt::print("No of edges: {}, input edges: {}, output edges: {}\n", node.get_num_edges(), node.get_num_inp_edges(),
    // node.get_num_out_edges() );
    /*
      for (const auto &edge : node.inp_edges()){
        auto indpin     = edge.driver;
        auto indpin_pid = indpin.get_pid();
        //auto inspin_pid = edge.sink.get_pid();

        fmt::print("input edge driver pin id: {}\n", indpin_pid);
        //fmt::print("input edge sink pin id: {}\n", inspin_pid);

      };

      for (const auto &edge : node.out_edges()){
        //auto outdpin     = edge.driver;
        //auto outdpin_pid = outdpin.get_pid();
        auto outspin     = edge.sink;
        auto outspin_pid = outspin.get_pid();

        //fmt::print("output edge driver pid: {}\n", outdpin_pid);
        fmt::print("output edge sink pid: {}\n", outspin_pid);
      };

      fmt::print("\n");*/
  };

  for (auto &p : nodeMap) {
    fmt::print("{}->{}\n", p.first, p.second);
  }

  fmt::print("No of Graphs for XOR: {}\n", nodeMap.count("XOR"));
  fmt::print("*************END check LEC\n");

  /*
  //c = a ^ b

  formula1 =
  boolector_assert(btor, formula1);
  result = boolector_sat(btor);
  fmt::print("Expect: unsat\n");
  fmt::print("Boolector: {}\n",result == BOOLECTOR_SAT ? "sat" : (result == BOOLECTOR_UNSAT ? "unsat":"unknown"));

  if(result != BOOLECTOR_UNSAT)
  {
    abort();
  };


  //clean up
  boolector_release( btor, input1);
  boolector_release( btor, input2);
  boolector_release( btor, formula1);
  boolector_release_sort( btor, s);
  assert (boolector_get_refs (btor) == 0);
  boolector_delete(btor);*/
}
