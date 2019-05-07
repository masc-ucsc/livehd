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

  m1.add_label_optional("liberty:", "Liberty file for timing");
  m1.add_label_optional("spef:", "SPEF file for timing");

  register_pass(m1);
}

Pass_opentimer::Pass_opentimer()
    : Pass("opentimer") {
}

void Pass_opentimer::work(Eprp_var &var) {
  Pass_opentimer pass;

  auto liberty = var.get("liberty");
  auto spef = var.get("spef");

  for(const auto &g : var.lgs) {
      pass.read_file(g, liberty, spef);          // Task1: Read input files (Read user from input) | Status: 75% done
      pass.build_circuit(g);                                   // Task2: Traverse the lgraph and build the equivalent circuit (No dependencies) | Status: 50% done
      pass.read_sdc(g);                                        // Task3: Traverse the lgraph and create fake SDC numbers | Status: 100% done
      pass.compute_timing();                                   // Task4: Compute Timing | Status: 100% done
      pass.populate_table();                                   // Task5: Traverse the lgraph and populate the tables | Status: 0% done
  }
}

void Pass_opentimer::read_file(LGraph *g,std::string_view liberty, std::string_view spef){
//  LGBench b("pass.opentimer.read_file");      // Expand this method to reading from user input and later develop inou.add_liberty etc.

//  fmt::print("\n\nLiberty path: {}\n\n", liberty);
//  timer.read_celllib ("pass/opentimer/ot_examples/osu018_stdcells.lib");
  timer.read_celllib (liberty)
       .read_spef (spef);

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

      // BUILD NETS AND IO PINS
      for(const auto &edge : node.out_edges()) {
          std::string driver_name (edge.driver.get_name());
       //   fmt::print("Driver Name {}\n\n", driver_name);
          timer.insert_net(driver_name);
      }
    }

    if(node.get_type().get_name() == "graphio"){
     // fmt::print("Name {}\n",name);

      for(const auto &edge : node.out_edges()) {
          if(edge.driver.is_graph_input()){
            std::string driver_name (edge.driver.get_name());
       //   fmt::print("Driver Name {}\n\n", driver_name);
            timer.insert_net(driver_name);
            timer.insert_primary_input(driver_name);
          }
      }

      for(const auto &edge : node.inp_edges()) {
          if(edge.sink.is_graph_output()){
            std::string driver_name (edge.driver.get_name());
   //         fmt::print("Sink Name {}\n\n", driver_name);
            timer.insert_net(driver_name);
            timer.insert_primary_output(driver_name);
          }
      }
    }
  }

  // CONNECT NETS TO PINS
  for(const auto &nid : g->forward()) {
    auto node = Node(g,0,Node::Compact(nid)); // NOTE: To remove once new iterators are finished

    if(node.get_type().get_name() == "blackbox"){
     // fmt::print("Name {}\n",name);

     // for(const auto &edge : node.out_edges()) {
        //  std::string driver_name (edge.driver.get_name());
        //  std::string node_pin_name (node.get_driver_pin(0).get_name());
        //  fmt::print("Node Name: {} | Driver Name: {}\n", node_pin_name, driver_name);

        // Get the name of the node (eg: NAND2X1), get the name of the corresponding node pin (eg: Y)
        // Connect the driver (eg: n1) from the corresponding node pin
        // Get the name of the node to which the driver is connected to (eg: NOR4X1), get the name of the corresponding node pin (eg: A)
        // Concatenate strings with a : in between. Use absl::append ? 
        // Use timer.connect_pin() to do the connections. eg: timer.connect_pin(NAND2X1:A, n1);
     // }
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

      for(const auto &edge : node.inp_edges()) {
          if(edge.sink.is_graph_output()){
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

  auto opt_path = timer.report_timing(1);
  std::cout << "Critical Path" << opt_path[0] <<'\n';

//  timer.dump_graph(std::cout);

}

void Pass_opentimer::populate_table(){                    // Expand this method to populate the tables in lgraph
//  LGBench b("pass.opentimer.populate_table");

}
