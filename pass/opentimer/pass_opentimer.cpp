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

  m1.add_label_optional("verilog:", "Verilog file for timing");
  m1.add_label_optional("liberty:", "Liberty file for timing");
  m1.add_label_optional("sdc:", "SDC file for timing");
  m1.add_label_optional("spef:", "SPEF file for timing");

  register_pass(m1);
}

Pass_opentimer::Pass_opentimer()
    : Pass("opentimer") {
}

void Pass_opentimer::work(Eprp_var &var) {
  Pass_opentimer pass;

  auto liberty = var.get("liberty");
  auto verilog = var.get("verilog");
  auto sdc = var.get("sdc");
  auto spef = var.get("spef");

  for(const auto &g : var.lgs) {
      pass.read_file(g);          // Task1: Read input files (Read user from input) | Status: 75% done
      pass.build_circuit(g);      // Task2: Traverse the lgraph and build the equivalent circuit (no dependencies) | Status: 15% done
      pass.compute_timing();      // Task3: Compute Timing (Will work once read_sdc() hack is implemented) | Status: 100% done
      pass.read_sdc(g);           // Task4: SDC 
      pass.populate_table();      // Task5: Traverse the lgraph and populate the tables | Status: 0% done
  }

}

void Pass_opentimer::read_file(LGraph *g){      // Currently just reads hardcoded file
//  LGBench b("pass.opentimer.read_file");      // Expand this method to reading from user input and later develop inou.add_liberty etc.

  timer.read_celllib ("pass/opentimer/ot_examples/osu018_stdcells.lib");

}

void Pass_opentimer::build_circuit(LGraph *g) {       // Enhance this for build_circuit
//  LGBench b("pass.opentimer.build_circuit");

  std::string celltype;
  std::string instance_name;

  // BUILD GATE
  for(const auto &nid : g -> forward()) {
    auto node = Node(g,0,Node::Compact(nid));

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
      //fmt::print("Cell Type: {} \t Instance Name {}\n", celltype, instance_name);
    }
  }

  // BUILD NETS AND IO PINS
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    std::string name(node.get_type().get_name());

    if(node.get_type().get_name() == "blackbox"){
     // fmt::print("Name {}\n",name);

      for(const auto &edge : node.out_edges()) {
          std::string driver_name (edge.driver.get_name());
       //   fmt::print("Driver Name {}\n\n", driver_name);
          timer.insert_net(driver_name);
      }
    }

    if(node.get_type().get_name() == "graphio"){
     // fmt::print("Name {}\n",name);

      for(const auto &edge : node.out_edges()) {
          std::string driver_name (edge.driver.get_name());
          if(edge.driver.is_graph_input()){
       //   fmt::print("Driver Name {}\n\n", driver_name);
            timer.insert_net(driver_name);
            timer.insert_primary_input(driver_name);
          }

          if(edge.driver.is_graph_output()){                                 // Check how to add the driver of the output graphio and add it
          fmt::print("\n\nHello!\n\n");
            timer.insert_net("out");
            timer.insert_primary_output("out");
          }
      }
    }
  }
}

void Pass_opentimer::read_sdc(LGraph *g){
  timer.create_clock("tau2015_clk", 5);

  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    std::string name(node.get_type().get_name());

    if(node.get_type().get_name() == "graphio"){
  //    fmt::print("Name {}\n",name);
      for(const auto &edge : node.out_edges()) {
        if(edge.driver.is_graph_input()){
          std::string driver_name (edge.driver.get_name());
  //        fmt::print("Driver Name {}\n\n", driver_name);

          timer.set_at(driver_name,ot::MIN,ot::RISE,0)
               .set_at(driver_name,ot::MIN,ot::FALL,0)
               .set_at(driver_name,ot::MAX,ot::RISE,0)
               .set_at(driver_name,ot::MAX,ot::FALL,0);

          timer.set_slew(driver_name,ot::MIN,ot::RISE,0)
               .set_slew(driver_name,ot::MIN,ot::FALL,0)
               .set_slew(driver_name,ot::MAX,ot::RISE,0)
               .set_slew(driver_name,ot::MAX,ot::FALL,0);
        }
      }

      for(const auto &edge : node.out_edges()) {                             // Check how to add the driver of the output graphio and add it
        if(edge.driver.is_graph_output()){
          std::string driver_name (edge.driver.get_name());
  //        fmt::print("Driver Name {}\n\n", driver_name);
          timer.set_rat(driver_name,ot::MIN,ot::RISE,0)
               .set_rat(driver_name,ot::MIN,ot::FALL,0)
               .set_rat(driver_name,ot::MAX,ot::RISE,0)
               .set_rat(driver_name,ot::MAX,ot::FALL,0);
        }
      }
    }
  }
}

void Pass_opentimer::compute_timing(){                    // Expand this method to compute timing information
//  LGBench b("pass.opentimer.compute_timing");

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

//  auto opt_path = timer.report_timing(1);
//  std::cout << "Critical Path" << opt_path[0] <<'\n';

  timer.dump_graph(std::cout);

}

void Pass_opentimer::populate_table(){                    // Expand this method to populate the tables in lgraph
//  LGBench b("pass.opentimer.populate_table");

}
