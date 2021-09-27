//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_sat_opt.hpp"

#include "annotate.hpp"
#include "lbench.hpp"
#include "lezminisat.hpp"
#include "lezsat.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "node.hpp"
#include "node_pin.hpp"

//#define DEBUG

/*

1. Assign a SAT variable to each graph input
2. Tracerse the graph forward, foreach node, it's inputs are already assigned SAT variables, assign SAT variables to the node output
and add SAT formula to the solver based on node type

*/

static Pass_plugin sample("pass_sat_opt", Pass_sat_opt::setup);

void Pass_sat_opt::setup() {
  Eprp_method m1("pass.sat_opt", mmap_lib::str("Checks if MSBs of all the Lgraph node outputs are satisfiable"), &Pass_sat_opt::work);

  register_pass(m1);
}

Pass_sat_opt::Pass_sat_opt(const Eprp_var &var) : Pass("pass.sat_opt", var) {}

void Pass_sat_opt::do_work(Lgraph *g) { check_sat_opt(g); }

void Pass_sat_opt::work(Eprp_var &var) {
  Pass_sat_opt p(var);

  for (const auto &g : var.lgs) {
    p.do_work(g);
  }
}

void Pass_sat_opt::check_sat_opt(Lgraph *g) {
  fmt::print("Running SAT_OPT to check if MSB of the Lgraph node outputs are satisfiable\n");
  //-----------------------------------------------------------------------------------------------------
  lezMiniSAT sat;

  auto             va = sat.vec_var("a", 4);
  std::vector<int> modelExpressions;
  // std::vector<bool> modelValues;
  // bool              satisfiable;

  // std::map<Node_pin::Compact, std::vector<int>> dpin2sat_var;
  absl::flat_hash_map<Node_pin::Compact, std::vector<int>> dpin2sat_var;

  // determine no of inputs
  // int i_num  = 0;
  // int i_bits = 0;

  /*std::vector<std::vector<int>> lgraph_inputs;
g->each_graph_input([this, &i_num, &i_bits](const Node_pin &pin) {
  lgraph_inputs[i_num] = sat.vec_var("in" + std::str(i_num), pin.get_bits());
  sat.vec_append(modelExpressions, lgraph_inputs[i_num]);
  i_num++;
  //i_bits += pin.get_bits();
});*/

  // fmt::print("num of inputs: {},  no of Bits: {}\n", i_num, i_bits);

  // fmt::print("Declaring input variables.\n");

  // determine no of outputs
  // int o_num  = 0;
  // int o_bits = 0;

  /*std::vector<std::vector<int>> lgraph_outputs;
g->each_graph_output([this, &o_num, &o_bits](const Node_pin &pin) {
    lgraph_outputs[i_num] = sat.vec_var("out" + std::str(i_num), pin.get_bits());
    sat.vec_append(modelExpressions, lgraph_outputs[i_num]);
o_num++;
//o_bits += pin.get_bits();
});*/

  // fmt::print("num of outputs: {},  no of Bits: {}\n", o_num, o_bits);

  // fmt::print("Declaring output variables.\n");

  std::vector<int> model_expression_vec;

  g->each_graph_input([&sat, &dpin2sat_var, &model_expression_vec](const Node_pin &dpin) {
    auto v = sat.vec_var(dpin.debug_name(), dpin.get_bits());
    sat.vec_append(model_expression_vec, v);
    dpin2sat_var[dpin.get_compact()] = v;
  });

  // Traverse graph
  fmt::print("Begin forward traversal.\n");

  /*Number of pins and pin names for node types
  Sum - Input pin = "A", Output pin = "B"
  Mult - Input pin = "A", Output pin = "B"
  Div,

  And,
  Or,
  Xor,
  Ror,    // Reduce OR

  Not,    // bitwise not
  Tposs,  // To positive signed

  LT,     // Less Than   , also GE = !LT
  GT,     // Greater Than, also LE = !GT
  EQ,     // Equal       , also NE = !EQ

  SHL,    // Shift Left Logical
  SRA,    // Shift Right Arithmetic

  Mux,    // Multiplexor with many options
*/

  for (const auto &node : g->forward()) {
    // sat.generate_model();

    // auto node_type_name = node.get_type_name();
    fmt::print("node type: {}\n", node.get_type_name());

    if (node.get_type_op() == Ntype_op::And) {
      auto inp        = node.inp_edges();  // a vector of input edges - assume 2 for now although both lgraph and ezsat support more
      auto and_result = sat.vec_and_wrapper(dpin2sat_var[inp[0].driver.get_compact()], dpin2sat_var[inp[1].driver.get_compact()]);
      dpin2sat_var[node.setup_driver_pin().get_compact()] = and_result;

      fmt::print("--Debug AND: {}\n", node.setup_driver_pin().debug_name());

    } else if (node.get_type_op() == Ntype_op::Or) {
      auto inp       = node.inp_edges();  // a vector of input edges - assume 2 for now although both lgraph and ezsat support more
      auto or_result = sat.vec_or_wrapper(dpin2sat_var[inp[0].driver.get_compact()], dpin2sat_var[inp[1].driver.get_compact()]);
      dpin2sat_var[node.setup_driver_pin().get_compact()] = or_result;

      fmt::print("--Debug OR: {}\n", node.setup_driver_pin().debug_name());
    }

    else if (node.get_type_op() == Ntype_op::Xor) {
      auto inp        = node.inp_edges();  // a vector of input edges - assume 2 for now although both lgraph and ezsat support more
      auto xor_result = sat.vec_xor_wrapper(dpin2sat_var[inp[0].driver.get_compact()], dpin2sat_var[inp[1].driver.get_compact()]);
      dpin2sat_var[node.setup_driver_pin().get_compact()] = xor_result;

      fmt::print("--Debug XOR: {}\n", node.setup_driver_pin().debug_name());
    } else if (node.get_type_op() == Ntype_op::Ror) {
      auto inp        = node.inp_edges();  // a vector of input edges - assume 2 for now although both lgraph and ezsat support more
      auto ror_result = sat.vec_reduce_or(dpin2sat_var[inp[0].driver.get_compact()]);
      std::vector<int> ror_result_vec                     = (std::vector<int>)ror_result;
      dpin2sat_var[node.setup_driver_pin().get_compact()] = ror_result_vec;

      fmt::print("--Debug ROR: {}\n", node.setup_driver_pin().debug_name());
    }
    /*	else if (node.get_type_op() == Ntype_op::Mult) {
                    auto inp = node.inp_edges(); // a vector of input edges - assume 2 for now although both lgraph and ezsat
    support more auto or_result = sat.vec_or(dpin2sat_var[inp[0].driver.get_compact()], dpin2sat_var[inp[1].driver.get_compact()]);
                    dpin2sat_var[node.setup_driver_pin().get_compact()] = or_result;

                    fmt::print(" Presently not implemented, not sure if ezsat has this --Debug OR: {}\n",
    node.setup_driver_pin().debug_name());
    }*/
    else if (node.get_type_op() == Ntype_op::EQ) {
      auto inp       = node.inp_edges();  // a vector of input edges - assume 2 for now although both lgraph and ezsat support more
      auto eq_result = sat.vec_eq_wrapper(dpin2sat_var[inp[0].driver.get_compact()], dpin2sat_var[inp[1].driver.get_compact()]);
      std::vector<int> eq_result_vec{eq_result};
      dpin2sat_var[node.setup_driver_pin().get_compact()] = eq_result_vec;

      fmt::print("--Debug EQ: {}\n", node.setup_driver_pin().debug_name());
    }

    else if (node.get_type_op() == Ntype_op::Not) {
      auto             inp                                = node.inp_edges();  // will return one input edge
      auto             not_result                         = sat.vec_not(dpin2sat_var[inp[0].driver.get_compact()]);
      std::vector<int> not_result_vec                     = (std::vector<int>)not_result;
      dpin2sat_var[node.setup_driver_pin().get_compact()] = not_result_vec;

#ifdef DEBUG
      fmt::print(" EZSAT function is supposed to return vector<int>, build says it returns int, check, --Debug NOT: {}\n",
                 node.setup_driver_pin().debug_name());
#else
      fmt::print("--Debug NOT: {}\n", node.setup_driver_pin().debug_name());
#endif
    }

/*    else if (node.get_type_op() == Ntype_op::Get_mask || node.get_type_op() == Ntype_op::Set_mask) {

      I(false);                              // FIX to be a get_bits or set_bits (tposs is gone)
      auto inp          = node.inp_edges();  // will return one input edge
      auto tposs_result = sat.vec_cast(dpin2sat_var[inp[0].driver.get_compact()], inp[0].driver.get_bits() + 1, false);
      dpin2sat_var[node.setup_driver_pin().get_compact()] = tposs_result;

#ifdef DEBUG
      fmt::print(" EZSAT function is supposed to return vector<int>, build says it returns int, check, --Debug Tposs: {}\n",
                 node.setup_driver_pin().debug_name());
#else
      fmt::print("--Debug Tposs: {}\n", node.setup_driver_pin().debug_name());
#endif
    }*/


    else if (node.get_type_op() == Ntype_op::Sum) {
      auto a_input = node.get_sink_pin("A").inp_edges();
      std::cout << "a is -> " << &a_input << "\n";
      auto b_input = node.get_sink_pin("B").inp_edges();
      std::cout << "b is -> " << &b_input << "\n";
      auto a_result = sat.vec_add(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[a_input[1].driver.get_compact()]);
      std::cout << "a_result is -> " << &a_result << "\n";
      auto b_result = sat.vec_add(dpin2sat_var[b_input[0].driver.get_compact()], dpin2sat_var[b_input[1].driver.get_compact()]);
      std::cout << "b_result is -> " << &b_result << "\n";
      auto sum_result = sat.vec_sub(a_result, b_result);
      std::cout << "sum_result is -> " << &sum_result << "\n";
      dpin2sat_var[node.setup_driver_pin().get_compact()] = sum_result;

      fmt::print("--Debug Sum: {}\n", node.setup_driver_pin().debug_name());
    } else if (node.get_type_op() == Ntype_op::LT) {
      auto a_input = node.get_sink_pin("A").inp_edges();
      auto b_input = node.get_sink_pin("B").inp_edges();

      auto lt_result
          = sat.vec_lt_signed_wrapper(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[b_input[0].driver.get_compact()]);
      std::vector<int> lt_result_vec{lt_result};

      dpin2sat_var[node.setup_driver_pin().get_compact()] = lt_result_vec;

      fmt::print("--Debug LT: {}\n", node.setup_driver_pin().debug_name());
    } else if (node.get_type_op() == Ntype_op::GT) {
      auto a_input = node.get_sink_pin("A").inp_edges();
      auto b_input = node.get_sink_pin("B").inp_edges();

      auto gt_result
          = sat.vec_gt_signed_wrapper(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[b_input[0].driver.get_compact()]);
      std::vector<int> gt_result_vec{gt_result};

      dpin2sat_var[node.setup_driver_pin().get_compact()] = gt_result_vec;

      fmt::print("--Debug GT: {}\n", node.setup_driver_pin().debug_name());
    } else if (node.get_type_op() == Ntype_op::SHL) {
      auto a_input = node.get_sink_pin("a").inp_edges();
      auto b_input = node.get_sink_pin("b").inp_edges();
      std::cout << "a and b inputs gotten \n";
      auto shl_result
          = sat.vec_shl_LiveHD(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[b_input[0].driver.get_compact()]);
      std::cout << "shl_result gotten \n";
      dpin2sat_var[node.setup_driver_pin().get_compact()] = shl_result;

      fmt::print("--Debug SHL: {}\n", node.setup_driver_pin().debug_name());
    } else if (node.get_type_op() == Ntype_op::SRA) {
      auto a_input = node.get_sink_pin("a").inp_edges();
      auto b_input = node.get_sink_pin("b").inp_edges();

      std::cout << "a and b inputs  gotten \n";
      auto shrl_result
          = sat.vec_shrl_LiveHD(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[b_input[0].driver.get_compact()]);
      std::cout << "shrl_result gotten \n";
      dpin2sat_var[node.setup_driver_pin().get_compact()] = shrl_result;

      fmt::print("--Debug SHRL: {}\n", node.setup_driver_pin().debug_name());
    }

    else if (node.get_type_op() == Ntype_op::Get_mask) {
      auto a_input = node.get_sink_pin("a").inp_edges();
      auto mask_input = node.get_sink_pin("mask").inp_edges();

      std::cout << "a and mask inputs  gotten \n";
      //auto get_mask_result = sat.vec_get_mask(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[mask_input[0].driver.get_compact()]);
      std::cout << "get_mask_result gotten \n";
      //dpin2sat_var[node.setup_driver_pin().get_compact()] = get_mask_result;

      //fmt::print("--Debug Get_Mask: {}\n", node.setup_driver_pin().debug_name());
    }

	else if (node.get_type_op() == Ntype_op::Set_mask) {
      auto a_input = node.get_sink_pin("a").inp_edges();
      auto mask_input = node.get_sink_pin("mask").inp_edges();
	    auto value_input = node.get_sink_pin("value").inp_edges();

      std::cout << "a, mask and value inputs  gotten \n";
     // auto set_mask_result  = sat.vec_set_mask(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[mask_input[0].driver.get_compact()], dpin2sat_var[value_input[0].driver.get_compact()]);
      std::cout << "set_mask_result gotten \n";
      //dpin2sat_var[node.setup_driver_pin().get_compact()] = set_mask_result;

      //fmt::print("--Debug Set_mask: {}\n", node.setup_driver_pin().debug_name());
    }
    else if (node.get_type_op() == Ntype_op::Sext) {
      auto a_input = node.get_sink_pin("a").inp_edges();
      auto b_input = node.get_sink_pin("b").inp_edges();

      std::cout << "a and b inputs  gotten \n";
      // auto Sext_result = sat.vec_shrl_LiveHD(dpin2sat_var[a_input[0].driver.get_compact()], dpin2sat_var[b_input[0].driver.get_compact()]);
      std::cout << "Sext_result gotten \n";
      // dpin2sat_var[node.setup_driver_pin().get_compact()] = Sext_result;

      // fmt::print("--Debug Sext: {}\n", node.setup_driver_pin().debug_name());
    }
  /*else if (node.get_type_op() == Ntype_op::Mux) {
      auto a_input = node.get_sink_pin_raw("pid1").inp_edges();
      auto b_input = node.get_sink_pin_raw("pid2").inp_edges();
      auto sel_input = node.get_sink_pin_raw("pid0").inp_edges();

      std::cout << " Mux inputs  gotten \n";
      auto mux_result
          = sat.vec_mux(dpin2sat_var[a_input.driver.get_compact()], dpin2sat_var[b_input.driver.get_compact()], dpin2sat_var[sel_input.driver.get_compact()]);
      std::cout << "mux_result gotten \n";
      dpin2sat_var[node.setup_driver_pin().get_compact()] = mux_result;

      fmt::print("--Debug Mux: {}\n", node.setup_driver_pin().debug_name());
    }
   
*/
    else if (node.get_type_op() == Ntype_op::Const) {
      auto l_val                                          = node.get_type_const();
      auto s_val                                          = l_val.to_i();
      dpin2sat_var[node.setup_driver_pin().get_compact()] = sat.vec_const_signed(s_val, l_val.get_bits());
    } else {
      for (auto &dpin : node.out_connected_pins()) {
        auto v = sat.vec_var(dpin.debug_name(), dpin.get_bits());
        fmt::print("FIXME. once we have all the cells, this is gone\n");
        dpin2sat_var[dpin.get_compact()] = v;
      }
    }
  }

  g->each_graph_output([&sat, &dpin2sat_var, &model_expression_vec](const Node_pin &dpin) {
    if (dpin.get_bits() <= 1) {
      return true;  // Nothing possible to optimize
    }
    auto spin       = dpin.change_to_sink_from_graph_out_driver();
    auto out_driver = spin.get_driver_pin();

    auto sat_var_output = dpin2sat_var[out_driver.get_compact()];
    fmt::print("--Debug output driver: {}\n", out_driver.debug_name());

    // TODO: add gates over output to see if bit[msb]!=bit[msb-1])
    // If both bits = unequal, o/p = 1, else o/p = 0 => XOR gate
    auto msb0 = sat.vec_pick(sat_var_output, 1, dpin.get_bits() - 1);
    auto msb1 = sat.vec_pick(sat_var_output, 1, dpin.get_bits() - 2);

    auto eq = sat.vec_xor(msb0, msb1);

    std::vector<bool> modelValues;
    std::vector<int>  assumptions;

    if (sat.solve(model_expression_vec, modelValues, eq)) {
      fmt::print("satisfiable \n");
      for (int i = 0; i < int(eq.size()); i++) printf(" %s = %d", sat.to_string(eq[i]).c_str(), int(modelValues[i]));
      printf("\n\n");
    } else {
      fmt::print("unsatisfiable \n");
    }
    return true;
    // auto satisfiable = sat.vec_or(dpin2sat_var[inp[0].driver.get_compact()], dpin2sat_var[inp[1].driver.get_compact()]);

    //
    // TODO: Check if satisfiable (is there any input that makesit true
  });
}
