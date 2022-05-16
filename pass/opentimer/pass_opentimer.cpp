//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "pass_opentimer.hpp"

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "perf_tracing.hpp"

static Pass_plugin sample("pass_opentimer", Pass_opentimer::setup);

void Pass_opentimer::setup() {
  Eprp_method m1("pass.opentimer", "timing analysis on lgraph", &Pass_opentimer::work);

  m1.add_label_required("lib", "Liberty file for timing");
  m1.add_label_optional("lib_max", "Liberty file for timing");
  m1.add_label_optional("lib_min", "Liberty file for timing");
  m1.add_label_required("sdc", "SDC file for timing");
  m1.add_label_required("spef", "SPEF file for timing");

  register_pass(m1);

  Eprp_method m2("inou.liberty", "Read liberty and populate blackboxes", &Pass_opentimer::liberty_open);

  m2.add_label_required("files", "Liberty files");

  register_pass(m2);
}

Pass_opentimer::Pass_opentimer(const Eprp_var &var) : Pass("pass.opentimer", var) {
  opt_lib = var.get("lib");
  if (var.has_label("lib_max")) {
    opt_lib_max = var.get("lib_max");
  } else {
    opt_lib_max = opt_lib;
  }
  if (var.has_label("lib_min")) {
    opt_lib_min = var.get("lib_min");
  } else {
    opt_lib_min = opt_lib;
  }

  opt_sdc  = var.get("opt_sdc");
  opt_spef = var.get("opt_spef");
}

void Pass_opentimer::liberty_open(Eprp_var &var) {
  Pass_opentimer p(var);

  TRACE_EVENT("pass", "OPENTIMER_liberty");
  Lbench b("pass.OPENTIMER_liberty");

  auto *library = Graph_library::instance(p.path);

  for (const auto f : absl::StrSplit(p.files, ',')) {
    fmt::print("reading liberty {}\n", f);

    ot::Timer timer;

    timer.read_celllib(f, ot::MIN);

    timer.update_timing();

    const auto &cl=timer.celllib(ot::MIN);
    for(const auto &it:cl->cells) {
      auto *g = library->try_find_lgraph(it.first);
      if (g == nullptr) {
        g = ::Lgraph::create(p.path, it.first, "-");
      }

      Sub_node *sub = g->ref_self_sub_node();
      sub->reset_pins();

      bool has_clock = false;
      for(const auto &pin:it.second.cellpins) {
        if (pin.second.is_clock && *pin.second.is_clock) {
          sub->set_loop_last();
          has_clock = true;
        }
        if (pin.second.direction) {
          Port_ID pid=0;
          if (*pin.second.direction == ot::CellpinDirection::INPUT)
            pid = sub->add_input_pin(pin.first);
          else if (*pin.second.direction == ot::CellpinDirection::OUTPUT)
            pid = sub->add_output_pin(pin.first);

          if (pid)
            sub->set_bits(pid, 1); // how does the liberty specify multiple bits?
        }

        if (pin.second.is_clock)
          has_clock |= *pin.second.is_clock;
      }

      if (!has_clock)
        sub->clear_loop_last();
    }
  }
}

void Pass_opentimer::work(Eprp_var &var) {
  Pass_opentimer pass(var);

  pass.read_files();      // Task1: Read input files (Read user from input) | Status: 75% done

  TRACE_EVENT("pass", "OPENTIMER_work");
  Lbench b("pass.OPENTIMER_work");

  for (const auto &g : var.lgs) {
    pass.build_circuit(g);  // Task2: Traverse the lgraph and build the equivalent circuit (No dependencies) | Status: 50% done
    pass.read_sdc();        // Task3: Traverse the lgraph and create fake SDC numbers | Status: 100% done
    pass.compute_timing();  // Task4: Compute Timing | Status: 100% done
    pass.populate_table();  // Task5: Traverse the lgraph and populate the tables | Status: 0% done
  }
}

void Pass_opentimer::read_files() {
  if (opt_lib_max != opt_lib)
    timer.read_celllib(opt_lib_max, ot::MAX);
  if (opt_lib_min != opt_lib)
    timer.read_celllib(opt_lib_min, ot::MIN);

  timer.read_celllib(opt_lib);
  timer.read_spef(opt_spef);
}

void Pass_opentimer::read_sdc() {
  std::vector<std::string> line_vec;
  std::ifstream            file(opt_sdc);

  if (!file.is_open()) {
    error("pass.opentimer could not open sdc:{}", opt_sdc);
    return;
  }

  std::string line;

  while (getline(file, line)) {
    std::stringstream datastream(line);
    std::copy(std::istream_iterator<std::string>(datastream), std::istream_iterator<std::string>(), std::back_inserter(line_vec));

    if (line_vec[0] == "create_clock") {
      int         period;
      std::string pname;
      for (std::size_t i = 1; i < line_vec.size(); i++) {
        if (line_vec[i] == "-period") {
          period = stoi(line_vec[++i]);
          continue;
        } else if (line_vec[i] == "-name") {
          pname = line_vec[++i];
          continue;
        }
      }
      timer.create_clock(pname, period);
    } else if (line_vec[0] == "set_input_delay") {
      std::string pname;
      int         delay = stoi(line_vec[1]);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
       if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
          continue;
        }
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_at(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_at(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_at(pname, ot::MIN, ot::FALL, delay);
      }
    } else if (line_vec[0] == "set_input_transition") {
      std::string pname;
      int         delay = stoi(line_vec[1]);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
          continue;
        }
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_slew(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_slew(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_slew(pname, ot::MIN, ot::FALL, delay);
      }
    } else if (line_vec[0] == "set_output_delay") {
      std::string pname;
      int         delay = stoi(line_vec[1]);
      for (std::size_t i = 2; i < line_vec.size(); i++) {
        if (line_vec[i] == "[get_ports") {
          pname = line_vec[++i];
          pname.pop_back();
          continue;
        }
      }
      if (line_vec[2] == "-min" && line_vec[3] == "-rise") {
        timer.set_rat(pname, ot::MIN, ot::RISE, delay);
      } else if (line_vec[2] == "-min" && line_vec[3] == "-fall") {
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-rise") {
        timer.set_rat(pname, ot::MAX, ot::RISE, delay);
      } else if (line_vec[2] == "-max" && line_vec[3] == "-fall") {
        timer.set_rat(pname, ot::MIN, ot::FALL, delay);
      }
    }
    line_vec.clear();
  }
  file.close();
}

void Pass_opentimer::build_circuit(Lgraph *g) {  // Enhance this for build_circuit
  TRACE_EVENT("pass", "OPENTIMER_build_circuit");
  //  Lbench b("pass.OPENTIMER_build_circuit");

  g->each_graph_input([this](const Node_pin &pin) {
    std::string driver_name(pin.get_name());  // OT needs std::string, not string_view support

    fmt::print("\nGraph Input Driver Name {}", driver_name);
    timer.insert_net(driver_name);
    timer.insert_primary_input(driver_name);
    timer.connect_pin(driver_name, driver_name);
  });

  g->each_graph_output([this](const Node_pin &pin) {
    std::string driver_name(pin.get_name());  // OT needs std::string, not string_view support

    fmt::print("\nGraph Output Driver Name {}", driver_name);
    timer.insert_net(driver_name);
    timer.insert_primary_output(driver_name);
    timer.connect_pin(driver_name, driver_name);
  });

  for (const auto node : g->forward()) {  // TODO: Do we really need a slow forward. Why not just fast??
    auto op = node.get_type_op();

    if (op != Ntype_op::Sub) {
      Pass::error("opentimer pass needs the lgraph to be tmap, found cell {} with type {}\n",
          node.debug_name(),
          Ntype::get_name(op));
      continue;
    }

    auto &sub_node = node.get_type_sub_node();
    if (!sub_node.is_black_box()) {
      fmt::print("Not BBox sub found {}, fixme to traverse hierarchy\n", sub_node.get_name());
      continue;
    }

    std::string instance_name(node.get_name());  // OT needs std::string
    std::string type_name(sub_node.get_name());  // OT needs std::string

    timer.insert_gate(instance_name, type_name);

    // CONNECT NETS TO PINS
    for (const auto &e : node.inp_edges()) {
      auto        nodepin_name = sub_node.get_name_from_instance_pid(e.sink.get_pid());
      auto        pin_name     = absl::StrCat(instance_name, ":", nodepin_name);
      std::string wname(e.driver.get_name());  // OT needs std::string

      fmt::print("\n{},{}", pin_name, wname);
      timer.insert_net(wname);
      timer.connect_pin(pin_name, wname);
    }
    timer.connect_pin("u3:Y", "out");  // This the last thing to fix
  }
}

void Pass_opentimer::compute_timing() {  // Expand this method to compute timing information
                                         //  Lbench b("pass.OPENTIMER_compute_timing");
  TRACE_EVENT("pass", "OPENTIMER_compute_timing");

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
  std::cout << "Critical Path" << opt_path[0] << '\n';

  // timer.dump_graph(std::cout);
}

void Pass_opentimer::populate_table() {  // Expand this method to populate the tables in lgraph
  TRACE_EVENT("pass", "OPENTIMER_populate_table");
  //  Lbench b("pass.OPENTIMER_populate_table");
}
