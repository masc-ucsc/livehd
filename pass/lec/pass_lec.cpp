//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_lec.hpp"

#include <stdio.h>
#include <stdlib.h>

#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
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
#include "sub_node.hpp"

static Pass_plugin sample("pass_lec", Pass_lec::setup);

void Pass_lec::setup() {
  Eprp_method m1("pass.lec", "Checks if all the Lgraph outputs are satisfiable", &Pass_lec::work);

  register_pass(m1);
}

Pass_lec::Pass_lec(const Eprp_var &var) : Pass("pass.lec", var) {}

void Pass_lec::do_work(Lgraph *g) {
  fmt::print("\n--- DO WORK\n");
  // input_compare(g);
  check_lec(g);
  // call_sat(g);
  // find_matches(g);
}

void Pass_lec::work(Eprp_var &var) {
  Pass_lec p(var);

  fmt::print("\nStarting pass lec.....{}\n", var.lgs.size());

  // for (const auto &g : var.lgs) {
  // / p.input_compare(g);
  // };

  for (const auto &g : var.lgs) {
    p.do_work(g);
  };

  fmt::print("Done with work.\n");
}

// void Pass_lec::match_inputs(Eprp_var &var){

//}

void Pass_lec::check_lec(Lgraph *g) {
  fmt::print("\n---Test CHECK LEC \nLGraph name: {}\n", g->get_name());

  /*Btor *btorInputs;     //creates boolector instance
  BoolectorNode *x1, *bool_in_var; // *x2, *y1, *y2;
  BoolectorSort s;

  boolector_set_opt(btorInputs, BTOR_OPT_MODEL_GEN, 2);     //allows model generation

  btorInputs = boolector_new();

  s = boolector_bitvec_sort(btorInputs, 1);

  //x1 = boolector_var(btorInputs, s, NULL );
 // x2 = boolector_var(btorInputs, s, NULL );
  //x3 = boolector_var(btorInputs, s, input_pin.get_name() );
 // y1 = boolector_var(btorInputs, s, NULL);
 // y2 = boolector_var(btorInputs, s, NULL );
 // y3 = boolector_var(btorInputs, s, input_pin.get_name() );*/

  // int i_num  = 0;   //counts inputs

  // Store and match inputs
  // finds dupliucates while looking at all inputs for given graph
  g->each_graph_input([&](const Node_pin &input_pin) {
    (void)input_pin;

    // i_num++;

    // fmt::print("Lgraph input: {} {}\n", input_pin.get_name(), input_pin.get_pid() );

    /*auto name = input_pin.get_name();

    bool_in_var = boolector_var(btorInputs, s,std::string(name).c_str() );

    graphInNames.insert(bool_in_var);*/
  });

  // Alternative way to find matches
  std::map<std::string, int> countInpups;

  std::set s(graphIOs.begin(), graphIOs.end());

  int matchingInputs = graphIOs.size() - s.size();

  if (matchingInputs > 0) {
    fmt::print(" {} matches found!\n", matchingInputs);
  } else {
    fmt::print("no matching inputs found\n");
  };

  // prints graphInputs (for now)
  for (auto const &in : graphIOs) {
    fmt::print("Input: {}, place: {}\n", in.first, in.second);
  };

  // Btor *btor;
  // BoolectorNode *bool_xor_node; // *c, *bool_and, *bool_nand;//  *not_a, *not_ab_or, *or_notab, *and_ab, *c;

  /*btor = boolector_new();

  not_a = boolector_not(btor, in_a);
  or_notab = boolector_or(btor, not_a, in_b);
  not_ab_or = boolector_not(btor, or_notab);

  and_ab = boolector_and(btor, in_a, in_b);

  bool_and = boolector_and(btor, in_a, in_b);
  boolector_assert(btor, bool_and);

  bool_nand = boolector_nand(btor, in_a, in_b);
  boolector_assert(btor, bool_nand);


  c = boolector_xor(btor, bool_and, bool_nand);
  boolector_assert(btor, c);*/

  // int result = boolector_sat(btor);

  // still need: Sum, Mult, Div, Ror, LT, GT, EQ, SHL, SRA, Mux

  for (const auto &node : g->forward()) {
    // fmt::print("Node type: {}, place: {}\n", node.get_type_name(), node.get_nid());

    if (node.get_type_op() == Ntype_op::And) {
      // fmt::print(" {} found at {} \n", node.get_type_name(),node.get_nid() );
    } else if (node.get_type_op() == Ntype_op::Or) {
      // fmt::print(" {} found at {} \n", node.get_type_name(),node.get_nid() );
    }

    else if (node.get_type_op() == Ntype_op::Xor) {
      // fmt::print(" {} found at {} \n", node.get_type_name(),node.get_nid() );
      // bool_xor_node = boolector_xor(btor,
      // boolector_assert(btor, bool_xor_node);

    }

    else if (node.get_type_op() == Ntype_op::Not) {
      // fmt::print(" {} found at {} \n", node.get_type_name(),node.get_nid() );
    };
  };

  // int result = boolector_sat(btor);

  // print out map
  /*for ( auto const& elements : graphMap1 ){
    fmt::print("Node type: {}, place: {}\n", elements.first, elements.second);
  };*/
}

/*
  BoolectorNode *in_a = boolector_var(btor, s, "x");
  BoolectorNode *in_b = boolector_var(btor, s, "b");


  c = boolector_xor(btor, bool_and, bool_nand);
  boolector_assert(btor, c);

  int result = boolector_sat(btor);

  //fmt::print("Expect: unsat\n");
  fmt::print("Boolector: {}\n",result == BOOLECTOR_SAT ? "sat" : (result == BOOLECTOR_UNSAT ? "unsat":"unknown"));

  if(result != BOOLECTOR_UNSAT)
  {
    abort();
  };

  fmt::print("Boolector model:\n");
  std::string type = "btor";
  char* btype = const_cast<char*>(type.c_str());

  boolector_print_model(btor, btype, stdout);

  //clean up
  boolector_release( btor, in_a);
  boolector_release( btor, in_b);
  boolector_release( btor, not_a);
  boolector_release( btor, or_notab);
  boolector_release( btor, not_ab_or);
  boolector_release( btor, and_ab);
  boolector_release( btor, bool_and);
  boolector_release( btor, bool_nand);

  boolector_release( btor, c);
  boolector_release_sort( btor, s);


  assert (boolector_get_refs (btor) == 0);
  boolector_delete(btor);

  }*/
