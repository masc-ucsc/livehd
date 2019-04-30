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
  pass.example_ot();                                    // Traverse the lgraph and build the circuit (not there yet)

//  pass.read_file();                                     // Make OpenTimer read the input files
//  pass.build_circuit();                                 // Traverse the lgraph and build the circuit (not there yet)
  pass.compute_timing();                                // Traverse the lgraph and build the circuit (not there yet)
  pass.populate_table();                                // Traverse the lgraph and build the circuit (not there yet)


  for(const auto &g : var.lgs) {
//      pass.list_cells(g);                                 // Traverse the lgraph and list the cells for the moment (for debug only)
  }

}

// Temporary Methods for debugging -----------------------------------------------------------------------------------------------------------------
void Pass_opentimer::example_ot(){                       // Read SDC does not work and thereby dependent commands don't work..

  ot::Timer timer;

  // Read design
  timer.read_celllib("pass/opentimer/ot_examples/optimizer_Early.lib", ot::MIN)
       .read_celllib("pass/opentimer/ot_examples/optimizer_Late.lib", ot::MAX)
       .read_verilog("pass/opentimer/ot_examples/optimizer.v")
       .read_sdc   ("pass/opentimer/ot_examples/optimizer.sdc")
       .read_spef   ("pass/opentimer/ot_examples/optimizer.spef");

  // Report the TNS and WNS
  if(std::optional<float> tns = timer.report_tns(); tns) {
    std::cout << "TNS: " << *tns << '\n';
  }
  else {
    std::cout << "TNS is not available\n";
  }

  if(std::optional<float> wns = timer.report_wns(); wns) {
    std::cout << "WNS: " << *wns << '\n';
  }
  else {
    std::cout << "WNS is not available\n";
  }

  // repower a gate and insert a buffer
  timer.repower_gate("inst_10", "INV_X16")
       .insert_gate("TAUGATE_1", "BUF_X2")
       .insert_net("TAUNET_1")
       .disconnect_pin("inst_3:ZN")
       .connect_pin("inst_3:ZN", "TAUNET_1")
       .connect_pin("TAUGATE_1:A", "TAUNET_1")
       .connect_pin("TAUGATE_1:Z", "net_14")
       .read_spef("pass/opentimer/ot_examples/change_1.spef");

  // report the slack at a G17
  std::cout << "Late/Fall slack at pin G17: "
            << *timer.report_slack("G17", ot::MAX, ot::FALL)
            << '\n';

  // report the arrival time at G17
  std::cout << "Late/Fall arrival time at pin G17: "
            << *timer.report_at("G17", ot::MAX, ot::FALL)
            << '\n';

  // report the required arrival time at G17
  std::cout << "Late/Fall required arrival time at pin G17: "
            << *timer.report_rat("G17", ot::MAX, ot::FALL)
            << '\n';

}

void Pass_opentimer::list_cells(LGraph *g) {              //Enhance this for build_circuit

  int cell = 0;
  std::string instance_name;

  timer.read_celllib("pass/opentimer/ot_examples/osu018_stdcells.lib");

  LGBench b("pass.opentimer.list_cells");

  for(const auto &nid : g -> forward()) {
    auto node = Node(g,0,Node::Compact(nid));             //NOTE: To remove once new iterators are finished
    cell++;
    std::string name (node.get_name());
    fmt::print("Cell\t{}\n", name);
    instance_name = name+std::to_string(cell);            // This makes sure 2 cells of the same strength have distinct instance names
    timer.insert_gate(instance_name,name);
  }

  timer.insert_gate("FA1", "FAX1");

  fmt::print("Cells {}\n", cell);
  auto gates = timer.num_gates();
  fmt::print("Gates {}\n", gates);
}

// Methods to use finally---------------------------------------------------------------------------------------------------------------------------
void Pass_opentimer::read_file(){                        // Currently just reads hardcoded file
                                                         // Exapnd this method to reading from user input and later develop inou.add_liberty etc.

  timer.read_celllib ("pass/opentimer/ot_examples/osu018_stdcells.lib");

}

void Pass_opentimer::build_circuit() {                    // Expand this method to traversing the graph and building the circuit

  timer.insert_gate("FA1", "FAX1")
       .insert_gate("FA2", "FAX1")
       .insert_net("n1")
       .connect_pin("FA1:YC", "n1")
       .connect_pin("FA2:C", "n1");

}

void Pass_opentimer::compute_timing(){                    // Expand this method to compute timing information

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
  fmt::print("Number of nets {}\n", num_nets);

  timer.dump_graph(std::cout);

}

void Pass_opentimer::populate_table(){                    // Expand this method to populate the tables in lgraph


}

// --------------------------------------------------------------------------------------------------------------------------------------------------------
