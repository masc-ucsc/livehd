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

#include "boolector.h"
#include "cell.hpp"
#include "lezminisat.hpp"
#include "lezsat.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"
#include "sub_node.hpp"

double wallclock_time_started=0.0;

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

  fmt::print("\nDone with work.\n");
}

// void Pass_lec::match_inputs(Eprp_var &var){

//}

void Pass_lec::check_lec(Lgraph *g) {
  fmt::print("\n---Test CHECK LEC \nLGraph name: {}\n", g->get_name());

  int i_num = 0;  // counts inputs

  g->each_graph_input([&](const Node_pin &input_pin) {
    //(void)input_pin;

    i_num++;

    fmt::print("Lgraph input: {} {}\n", input_pin.get_name(), input_pin.get_pid());
  });

  // still need: Sum, Div, LT, GT,  SHL,  Mux
  Btor          *btor;
  BoolectorNode *x1, *x2, *inZero1, *eq1, *eq2, *formula;
  // BoolectorNode *inOne;

  BoolectorSort s;

  btor = boolector_new();
  s    = boolector_bitvec_sort(btor, 8);
  x1   = boolector_var(btor, s, "x1");
  x2   = boolector_var(btor, s, "x2");

  inZero1 = boolector_zero(btor, s);
  // inOne = boolector_one(btor, s);

  for (const auto &node : g->forward()) {
    fmt::print("Node type: {}, place: {}\n", node.get_type_name(), node.get_nid());

    if (node.get_type_op() == Ntype_op::And) {
      fmt::print(" {} found at {} \n", node.get_type_name(), node.get_nid());
      boolector_and(btor, x1, x2);
    }

    else if (node.get_type_op() == Ntype_op::Or) {
      fmt::print(" {} found at {} \n", node.get_type_name(), node.get_nid());
      boolector_or(btor, x1, x2);
    }

    else if (node.get_type_op() == Ntype_op::Xor) {
      fmt::print(" {} found at {} \n", node.get_type_name(), node.get_nid());
      boolector_xor(btor, x1, x2);
      // boolector_assert(btor, bool_xor_node);
    }

    else if (node.get_type_op() == Ntype_op::EQ) {
      boolector_eq(btor, x1, x2);
    }

    else if (node.get_type_op() == Ntype_op::Not) {
      fmt::print(" {} found at {} \n", node.get_type_name(), node.get_nid());
      boolector_not(btor, x1);
    }

    else if (node.get_type_op() == Ntype_op::Ror) {
      boolector_ror(btor, x1, x2);
    }

    else if (node.get_type_op() == Ntype_op::Mult) {
      boolector_mul(btor, x1, x2);
    }

    else if (node.get_type_op() == Ntype_op::SRA) {
      boolector_sra(btor, x1, x2);
    }

    else if (node.get_type_op() == Ntype_op::Sum) {
      boolector_add(btor, x1, x2);
    };

    // boolector_assert(btor, lg_temp_node);
  };

  eq1 = boolector_eq(btor, x1, inZero1);
  boolector_assert(btor, eq1);
  eq2 = boolector_eq(btor, x2, inZero1);
  boolector_assert(btor, eq2);

  formula = boolector_xor(btor, eq1, eq2);
  boolector_assert(btor, formula);

  int result = boolector_sat(btor);

  // fmt::print("Expect: unsat, Result: {}\n", result);
  fmt::print("Boolector: {}\n", result == BOOLECTOR_SAT ? "sat" : (result == BOOLECTOR_UNSAT ? "unsat" : "unknown"));

  // fmt::print("test");

  /*if(result != BOOLECTOR_UNSAT)
    {
      abort();
    };*/

  /*fmt::print("Boolector model:\n");
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

  boolector_release( btor, c);*/

  /*boolector_release(btor, x);
  boolector_release(btor, y);
  boolector_release(btor, bool_xor_node);
  fmt::print("test1");
  boolector_release_sort(btor, s);
  fmt::print("test2");

  assert (boolector_get_refs (btor) == 0);
  boolector_delete(btor);*/
}
