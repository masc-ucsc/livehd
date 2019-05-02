//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgbench.hpp"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"

void setup_pass_opentimer() {
  Pass_opentimer p;
  p.setup();
}

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::work);
  register_pass(m1);
}

Pass_opentimer::Pass_opentimer()
    : Pass("opentimer") {
}

void Pass_opentimer::work(Eprp_var &var) {
  Pass_opentimer pass;

  fmt::print("\nOpenTimer-LGraph Action Going On...\n\n");

  pass.read_file();                                     // Task1: Read input files (implement read_sdc hack) | Status: 75% done
  for(const auto &g : var.lgs) {
      pass.build_circuit(g);                            // Task2: Traverse the lgraph and build the equivalent circuit (no dependencies) | Status: 15% done
  }
  pass.compute_timing();                                // Task3: Compute Timing (Will work once read_sdc() hack is implemented) | Status: 100% done
//  pass.populate_table();                                // Task4: Traverse the lgraph and populate the tables | Status: 0% done

}

void Pass_opentimer::read_file(){                        // Currently just reads hardcoded file
  LGBench b("pass.opentimer.read_file");
                                                         // Expand this method to reading from user input and later develop inou.add_liberty etc.

  timer.read_celllib ("pass/opentimer/ot_examples/osu018_stdcells.lib");

}

void Pass_opentimer::build_circuit(LGraph *g) {              //Enhance this for build_circuit
  LGBench b("pass.opentimer.build_circuit");

  std::string celltype;
  std::string instance_name;
  for(const auto &nid : g -> forward()) {
    auto node = Node(g,0,Node::Compact(nid));             //NOTE: To remove once new iterators are finished

    for(const auto &e:node.inp_edges()) {
      if(e.sink.get_pid() == LGRAPH_BBOP_TYPE) {
        if(e.driver.get_node().get_type().op != StrConst_Op)
            error("Internal Error: BB type is not a string.\n");
          celltype = e.driver.get_node().get_type_const_sview();
        } else if(e.sink.get_pid() == LGRAPH_BBOP_NAME) {
          if(e.driver.get_node().get_type().op != StrConst_Op)
            error("Internal Error: BB name is not a string.\n");
          instance_name = e.driver.get_node().get_type_const_sview();
        } else if(e.sink.get_pid() < LGRAPH_BBOP_OFFSET) {
          error("Unrecognized blackbox option, pid %hu\n", e.sink.get_pid());
      }
    }

    if(node.get_type().get_name() == "blackbox"){
      timer.insert_gate(instance_name,celltype);
      fmt::print("Cell Type: {} \t Instance Name {}\n", celltype, instance_name);
    }
  }
}

void Pass_opentimer::compute_timing(){                    // Expand this method to compute timing information
  LGBench b("pass.opentimer.compute_timing");

  timer.update_timing();

  auto num_gates = timer.num_gates();
  fmt::print("Number of gates {}\n", num_gates);

  auto num_primary_inputs = timer.num_primary_inputs();
  fmt::print("Number of primary inputs {}\n", num_primary_inputs);

  auto num_primary_outputs = timer.num_primary_outputs();
  fmt::print("Number of primary outputs {}\n", num_primary_outputs);

  auto num_pins = timer.num_pins();
  fmt::print("Number of pins {}\n", num_pins);

  auto num_nets = timer.num_nets();
  fmt::print("Number of nets {}\n\n", num_nets);

  timer.dump_graph(std::cout);

}

void Pass_opentimer::populate_table(){                    // Expand this method to populate the tables in lgraph
  LGBench b("pass.opentimer.populate_table");

}
