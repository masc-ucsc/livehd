//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lgbench.hpp"

#include "lgedgeiter.hpp"
#include "annotate.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"

void setup_pass_opentimer() {
  Pass_opentimer p;
  p.setup();
}

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::work);

  m1.add_label_optional("lib:", "Liberty file for timing");
  m1.add_label_optional("lib_max:", "Liberty file for timing");
  m1.add_label_optional("lib_min:", "Liberty file for timing");
  m1.add_label_optional("sdc:", "SDC file for timing");
  m1.add_label_optional("spef:", "SPEF file for timing");

  register_pass(m1);
}

Pass_opentimer::Pass_opentimer()
    : Pass("opentimer") {
}


void Pass_opentimer::work(Eprp_var &var) {
  Pass_opentimer pass;

  auto lib = var.get("lib");
  auto lib_max = var.get("lib_max");
  auto lib_min = var.get("lib_min");
  auto sdc = var.get("sdc");
  auto spef = var.get("spef");

  for(const auto &g : var.lgs) {
      pass.read_file(g, lib, lib_max, lib_min, spef, sdc);          // Task1: Read input files (Read user from input) | Status: 75% done
  //    pass.build_circuit(g);                                   // Task2: Traverse the lgraph and build the equivalent circuit (No dependencies) | Status: 50% done
      pass.read_sdc(sdc);                                        // Task3: Traverse the lgraph and create fake SDC numbers | Status: 100% done
      pass.compute_timing();                                   // Task4: Compute Timing | Status: 100% done
      pass.populate_table();                                   // Task5: Traverse the lgraph and populate the tables | Status: 0% done
  }
}

void Pass_opentimer::read_file(LGraph *g, std::string_view lib, std::string_view lib_max, std::string_view lib_min, std::string_view spef, std::string_view sdc) {
//  LGBench b("pass.opentimer.read_file");      // Expand this method to reading from user input and later develop inou.add_liberty etc.


  if(lib_max.length()==0 || lib_min.length()==0){
    timer.read_celllib (lib);
  }else{
    timer.read_celllib (lib_max,ot::MAX);
    timer.read_celllib (lib_min,ot::MIN);
  }
  //pass.read_sdc(sdc);
  timer.read_verilog ("pass/opentimer/tests/simple.v"); //USING THIS FOR THE MOMENT AS THE CIRCUIT IS NOT CONSTRUCTED FROM LGRAPH YET
  timer.read_spef (spef);

}

void Pass_opentimer::read_sdc(std::string_view sdc){
  std::vector<std::string> line_vec;
  std::ifstream file(static_cast<std::string>(sdc));
  if(file.is_open()) {
    std::string line;

    while (getline(file, line)) {
      std::stringstream datastream(line);
      std::copy(std::istream_iterator<std::string>(datastream), std::istream_iterator<std::string>(),std::back_inserter(line_vec));

      if(line_vec[0] == "create_clock"){
        int period;
        std::string name;
        for(std::size_t i = 1; i < line_vec.size(); i++){
          if(line_vec[i] == "-period"){
            period = stoi(line_vec[++i]);
            continue;
          } else if(line_vec[i] == "-name"){
              name = line_vec[++i];
              continue;
            }
        }
        timer.create_clock(name, period);
      } else if(line_vec[0] == "set_input_delay"){
          std::string name;
          int delay = stoi(line_vec[1]);
          for(std::size_t i = 2; i < line_vec.size(); i++){
            if(line_vec[i] == "[get_ports"){
              name = line_vec[++i];
              name.pop_back();
              continue;
            }
          }
          if(line_vec[2] == "-min" && line_vec[3] == "-rise"){
            timer.set_at(name, ot::MIN, ot::RISE, delay);
          } else if(line_vec[2] == "-min" && line_vec[3] == "-fall"){
            timer.set_at(name, ot::MIN, ot::FALL, delay);
          } else if(line_vec[2] == "-max" && line_vec[3] == "-rise"){
            timer.set_at(name, ot::MAX, ot::RISE, delay);
          } else if(line_vec[2] == "-max" && line_vec[3] == "-fall"){
            timer.set_at(name, ot::MIN, ot::FALL, delay);
          }
      } else if(line_vec[0] == "set_input_transition"){
          std::string name;
          int delay = stoi(line_vec[1]);
          for(std::size_t i = 2; i < line_vec.size(); i++){
            if(line_vec[i] == "[get_ports"){
              name = line_vec[++i];
              name.pop_back();
              continue;
            }
          }
          if(line_vec[2] == "-min" && line_vec[3] == "-rise"){
            timer.set_slew(name, ot::MIN, ot::RISE, delay);
          } else if(line_vec[2] == "-min" && line_vec[3] == "-fall"){
            timer.set_slew(name, ot::MIN, ot::FALL, delay);
          } else if(line_vec[2] == "-max" && line_vec[3] == "-rise"){
            timer.set_slew(name, ot::MAX, ot::RISE, delay);
          } else if(line_vec[2] == "-max" && line_vec[3] == "-fall"){
            timer.set_slew(name, ot::MIN, ot::FALL, delay);
          }
      } else if(line_vec[0] == "set_output_delay"){
          std::string name;
          int delay = stoi(line_vec[1]);
          for(std::size_t i = 2; i < line_vec.size(); i++){
            if(line_vec[i] == "[get_ports"){
              name = line_vec[++i];
              name.pop_back();
              continue;
            }
          }
          if(line_vec[2] == "-min" && line_vec[3] == "-rise"){
            timer.set_rat(name, ot::MIN, ot::RISE, delay);
          } else if(line_vec[2] == "-min" && line_vec[3] == "-fall"){
            timer.set_rat(name, ot::MIN, ot::FALL, delay);
          } else if(line_vec[2] == "-max" && line_vec[3] == "-rise"){
            timer.set_rat(name, ot::MAX, ot::RISE, delay);
          } else if(line_vec[2] == "-max" && line_vec[3] == "-fall"){
            timer.set_rat(name, ot::MIN, ot::FALL, delay);
          }
      }
      line_vec.clear();
    }
    file.close();
  }
}

void Pass_opentimer::build_circuit(LGraph *g) {       // Enhance this for build_circuit
//  LGBench b("pass.opentimer.build_circuit");

  std::string celltype;
  std::string instance_name;

  for(const auto &nid : g -> forward()) {
    auto node = Node(g,0,Node::Compact(nid));

    // CREATE GATES
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
      //fmt::print("Cell Type: {} \t Instance Name {}\n", celltype, instance_name);

      // CREATE NETS  ??
      for(const auto &edge : node.out_edges()) {
        if(edge.driver.has_name()){
          std::string driver_name (edge.driver.get_name());
        //  fmt::print("Driver Name {}\n\n", driver_name);
          timer.insert_net(driver_name);
        }
      }
    }

    // CREATE PRIMARY INPUTS AND PRIMARY OUTPUTS
    if(node.get_type().get_name() == "graphio"){
      for(const auto &edge : node.out_edges()) {
          if(edge.driver.is_graph_input()){
            if(edge.driver.has_name()){
              std::string driver_name (edge.driver.get_name());
            //  fmt::print("Graph Input Driver Name {}\n\n", driver_name);
              timer.insert_net(driver_name);
              timer.insert_primary_input(driver_name);
            }
          }
      }
      for(const auto &edge : node.inp_edges()) {
          if(edge.sink.is_graph_output()){
            if(edge.driver.has_name()){
              std::string driver_name (edge.driver.get_name());
            //  fmt::print("Graph Output Sink Name {}\n\n", driver_name);
              timer.insert_net(driver_name);
              timer.insert_primary_output(driver_name);
            }
          }
      }
    }

      fmt::print("Cell Name {}\n\n", celltype);
      timer.insert_gate(instance_name,celltype);

  // CONNECT NETS TO PINS

    //  bool is_param = false;
      std::string current_name = "bbox";
      for(const auto &e:node.inp_edges()) {
        if(e.sink.get_pid() < LGRAPH_BBOP_OFFSET)
          continue;

        auto dpin_node = e.driver.get_node();

        if(LGRAPH_BBOP_ISIPARAM(e.sink.get_pid())) {
        //  is_param = dpin_node.get_type_const_value() == 1;
        //  current_name = static_cast<std::string>(dpin_node.get_type_const_sview());
          fmt::print("Current_Name {}\n", current_name);
        } else if(LGRAPH_BBOP_ISICONNECT(e.sink.get_pid())) {
            if(dpin_node.get_type().op == U32Const_Op) {
              int IDK = dpin_node.get_type_const_value();
              fmt::print("? Value {}\n\n", IDK);
            } else if(dpin_node.get_type().op == StrConst_Op) {
                std::string IDK (dpin_node.get_type_const_sview());
                fmt::print("? Name {}\n\n", IDK);
            } else {
                if (e.driver.has_name()) {
                  std::string wname (e.driver.get_name());
                  fmt::print("Wire Name 1 {}\n\n", wname);
                }
            }
        }
      }

      for(const auto &e:node.inp_edges()) {
        if (e.driver.is_graph_input())
          continue;
        if(LGRAPH_BBOP_ISICONNECT(e.sink.get_pid())) {
          if (e.driver.has_name()) {
            std::string wname (e.driver.get_name());
            fmt::print("Wire Name 2 {}\n\n", wname);
          }
        //  else {
        //    wname = "\\wire_";
        //    wname = unique_name(g, wname);
        //  }
        //  cell->setPort(wname, cell_output_map[e.driver.get_compact()]);
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

  //timer.dump_graph(std::cout);

}

void Pass_opentimer::populate_table(){                    // Expand this method to populate the tables in lgraph
//  LGBench b("pass.opentimer.populate_table");

}
