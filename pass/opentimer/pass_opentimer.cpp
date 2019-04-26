//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgbench.hpp"

#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"
#include "ot/timer/timer.hpp"

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

  ot::Timer timer;
  pass.file_reader();                                     // Make OpenTimer read the input files

  for(const auto &g : var.lgs) {
      pass.circuit_builder();                             // Traverse the lgraph and build the circuit (not there yet)
      pass.list_cells(g);                                 // Traverse the lgraph and list the cells for the moment (for debug only)
  }

}

void Pass_opentimer::file_reader(){                       // Currently just reads hardcoded file
                                                          // Exapnd this to reading from user input and later develop inou.add_liberty etc.
  ot::Timer timer;

  timer.read_celllib("ot_examples/osu018_stdcells.lib")
       .read_verilog("ot_examples/unit.v")
       .read_spef("ot_examples/unit.spef");

}

void Pass_opentimer::circuit_builder() {                  // Currently just using a sample to check if the API works
                                                          // Expand this to traversing the graph and building the circuit
    ot::Timer timer;

    timer.read_celllib("ot_examples/osu018_stdcells.lib");

    timer.insert_gate("FA1", "FAX1")
         .insert_gate("FA2", "FAX1")
         .insert_net("n1")
         .connect_pin("FA1:YC", "n1")
         .connect_pin("FA2:C", "n1");

    timer.num_gates();
}

void Pass_opentimer::list_cells(LGraph *g) {

  int cell = 0;
  uint32_t gates = 0;
  std::string instance_name;

  ot::Timer timer;

  timer.read_celllib("ot_examples/osu018_stdcells.lib");

  LGBench b("pass.opentimer.list_cells");

  for(const auto &nid : g -> forward()) {
    auto node = Node(g,0,Node::Compact(nid));  //NOTE: To remove once new iterators are finished
    cell++;
    std::string name (node.get_type().get_name());
    fmt::print("Cell\t{}\n", name);
    instance_name = name+std::to_string(cell);            // This makes sure 2 cells of the same strength have distinct instance names
    timer.insert_gate(instance_name,name);
  }

  timer.insert_gate("FA1", "FAX1");

  fmt::print("Cells {}\n", cell);
  gates = timer.num_gates();
  fmt::print("Gates {}\n", gates);
}
