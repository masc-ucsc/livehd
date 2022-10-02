//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"
#include "str_tools.hpp"

// WARNING: opentimer has a nasty "define has_member" that overlaps with perfetto methods
#undef has_member
#include "perf_tracing.hpp"

void Pass_opentimer::time_work(Eprp_var &var) {
  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");
  Lbench b("pass.OPENTIMER_work");

  for (const auto &g : var.lgs) {
    pass.build_circuit(g);   // Task2: Traverse the lgraph and build the equivalent circuit (No dependencies) | Status: 50% done
    pass.read_sdc_spef();
    pass.compute_timing(g);  // Task4: Compute Timing | Status: 100% done
    pass.populate_table(g);   // Task5: Traverse the lgraph and populate the tables | Status: 0% done
  }
}

void Pass_opentimer::power_work(Eprp_var &var) {
  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");
  Lbench b("pass.OPENTIMER_work");

  // FIXME: build_circuit can overlap with read_vcd (thread_pool task)
  for (const auto &g : var.lgs) {
    pass.build_circuit(g);
    pass.read_sdc_spef();
    pass.read_vcd();
    pass.compute_power(g);
  }
}

// TODO: The IO pins have a separate name. Can we avoid this just for them?
std::string Pass_opentimer::get_driver_net_name(const Node_pin &dpin) const {
  auto it = overwrite_dpin2net.find(dpin.get_compact_driver());
  if (it != overwrite_dpin2net.end()) {
    return it->second;
  }

  return dpin.get_wire_name();
}

void Pass_opentimer::read_vcd() {
  vcd_list.resize(vcd_file_list.size());

  for(auto i=0u;i<vcd_file_list.size();++i) {
    const auto &f=vcd_file_list[i];

    bool ok = vcd_list[i].open(f); // FIXME: multithread?
    if (!ok) {
      Pass::error("could not read vcd {} file",f);
    }
  }
}

// SDC and SPEF must be read after the circuit is created
void Pass_opentimer::read_sdc_spef() {

  for(const auto &f:sdc_file_list) {
    read_sdc(f);
  }
  for(const auto &f:spef_file_list) {
    timer.read_spef(f);
  }
}

void Pass_opentimer::build_circuit(Lgraph *g) {  // Enhance this for build_circuit
  TRACE_EVENT("pass", "OPENTIMER_build_circuit");
  //  Lbench b("pass.OPENTIMER_build_circuit");

  overwrite_dpin2net.clear();
  // FIXME: Add one net for each bit (expand multiple bits)

  g->each_graph_input([this](const Node_pin &pin) {
    std::string driver_name(pin.get_wire_name());  // OT needs std::string, not string_view support

    //fmt::print("Graph Input Driver Name {}\n", driver_name);
    timer.insert_primary_input(driver_name);
    timer.insert_net(driver_name);
  });

  g->each_graph_output([this](const Node_pin &pin) {
    std::string driver_name(pin.get_wire_name());  // OT needs std::string, not string_view support

    //fmt::print("Graph Output Driver Name {}\n", driver_name);
    timer.insert_primary_output(driver_name);
    timer.insert_net(driver_name);

    auto driver_dpin = pin.change_to_sink_from_graph_out_driver().get_driver_pin();

    if (!driver_dpin.is_invalid()) {  // It could be disconnected
      // WARNING: not good if the same dpin is connected to multiple outputs (legal) but inclear how to do in opentimer
      overwrite_dpin2net.insert_or_assign(driver_dpin.get_compact_driver(), driver_name);
    }
  });

  // 2nd: populate all the net names
  for (const auto node : g->fast()) {
    auto op = node.get_type_op();
    if (op != Ntype_op::Sub)
      continue;

    // setup driver pins and nets
    for (const auto &dpin : node.out_connected_pins()) {
      if (dpin.is_graph_io())
        continue;
      timer.insert_net(dpin.get_wire_name());
    }
  }

  // 3rd: populate the cells (since all the names are populated, any order is fine)
  for (const auto node : g->fast(false)) {
    auto op = node.get_type_op();

    if (op == Ntype_op::Const)
      continue;  // may be used later when connecting sub/getmask

    if (op == Ntype_op::Get_mask) {
      auto a_dpin = node.get_sink_pin("a").get_driver_pin();
      if (a_dpin.is_graph_io()) {
        overwrite_dpin2net.insert_or_assign(node.get_driver_pin().get_compact_driver(), a_dpin.get_name());
        continue;
      }
      node.dump();
      I(false);
      // FIXME: in get_mask goes to input, pick the expanded bit name directly (set the name in get_mask.dpin??)
      // FIXME: create a cell (buffer ?) to pick wire
      continue;
    }
    if (op == Ntype_op::Set_mask) {
      auto dpin = node.get_driver_pin();
      if (dpin.is_graph_io()) {
        continue;
      }
      node.dump();
      continue;
      I(false);
      // FIXME: in get_mask goes to input, pick the expanded bit name directly (set the name in get_mask.dpin??)
      // FIXME: create a cell (buffer ?) to pick wire
      continue;
    }

    if (op != Ntype_op::Sub) {
      Pass::error("opentimer pass needs the lgraph to be tmap, found cell {} with type {}\n",
                  node.debug_name(),
                  Ntype::get_name(op));
      continue;
    }

    auto       &sub_node      = node.get_type_sub_node();
    auto        instance_name = node.get_or_create_name();
    std::string type_name{sub_node.get_name()};  // OT needs std::string

    //fmt::print("CELL: instance_name:{} type_name:{}\n", instance_name, type_name);
    timer.insert_gate(instance_name, type_name);

    // setup driver pins and nets
    for (const auto &dpin : node.out_connected_pins()) {
      auto pin_name  = absl::StrCat(instance_name, ":", dpin.get_pin_name());
      auto wire_name = get_driver_net_name(dpin);

      timer.connect_pin(pin_name, wire_name);
      //fmt::print("   pin_name:{} wire_name:{}\n", pin_name, wire_name);
    }

    // connect input pins
    for (const auto &e : node.inp_edges()) {
      //I(get_driver_net_name(e.driver) == e.driver.get_wire_name());
      auto wire_name = get_driver_net_name(e.driver);
      auto pin_name  = absl::StrCat(instance_name, ":", e.sink.get_pin_name());

      timer.connect_pin(pin_name, wire_name);
      //fmt::print("   pin_name:{} wire_name:{}\n", pin_name, wire_name);
    }
  }

  // Set all the inputs/outputs to zero by default
  g->each_graph_input([this](Node_pin &dpin) {
    auto pname = dpin.get_name();

    timer.set_at(pname, ot::MIN, ot::FALL, 0.0);
    timer.set_at(pname, ot::MIN, ot::RISE, 0.0);
    timer.set_at(pname, ot::MAX, ot::FALL, 0.0);
    timer.set_at(pname, ot::MAX, ot::RISE, 0.0);

    timer.set_slew(pname, ot::MAX, ot::FALL, 0.0);
    timer.set_slew(pname, ot::MAX, ot::RISE, 0.0);
    timer.set_slew(pname, ot::MIN, ot::FALL, 0.0);
    timer.set_slew(pname, ot::MIN, ot::RISE, 0.0);
  });
  g->each_graph_output([this](Node_pin &dpin) {
    auto pname = dpin.get_name();

    timer.set_rat(pname, ot::MIN, ot::FALL, 0.0);
    timer.set_rat(pname, ot::MIN, ot::RISE, 0.0);
    timer.set_rat(pname, ot::MAX, ot::FALL, 0.0);
    timer.set_rat(pname, ot::MAX, ot::RISE, 0.0);
  });
}

void Pass_opentimer::compute_timing(Lgraph *g) {  // Expand this method to compute timing information
  //  Lbench b("pass.OPENTIMER_compute_timing");
  TRACE_EVENT("pass", "OPENTIMER_compute_timing");

  timer.update_timing();

#if 0
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

  auto opt_path = timer.update_timing();

  if (opt_path.size())
    std::cout << "Critical Path" << opt_path[0] << '\n';

  // timer.dump_graph(std::cout);
#endif

  max_delay=0;
  std::string max_pin;

  const auto &pins = timer.pins();

  for (const auto node : g->fast()) {
    auto op = node.get_type_op();
    if (op != Ntype_op::Sub)
      continue;

    auto       &sub_node      = node.get_type_sub_node();
    auto        instance_name = node.get_or_create_name();
    std::string type_name{sub_node.get_name()};  // OT needs std::string

    //fmt::print("CELL: instance_name:{} type_name:{}\n", instance_name, type_name);
    timer.insert_gate(instance_name, type_name);

    // setup driver pins and nets
    for (auto &dpin : node.out_connected_pins()) {
      auto pin_name = absl::StrCat(instance_name, ":", dpin.get_pin_name());
      auto it = pins.find(pin_name);
      I(it != pins.end()); // just inserted before

      auto at_f = it->second.at(ot::MAX, ot::FALL);
      auto at_r = it->second.at(ot::MAX, ot::RISE);

      float delay = 0.0;
      if (at_f) {
        delay = *at_f;
      }
      if (at_r && *at_r > delay) {
        delay = *at_r;
      }
      if (delay>0) {
        dpin.set_delay(delay);

        //auto wire_name = get_driver_net_name(dpin);
        //fmt::print(" pin {} {} wname:{}\n", pin_name, delay, wire_name);

        if (delay > max_delay) {
          max_delay = delay;
          max_pin   = pin_name;
        }
      }else{
        dpin.del_delay();
      }
    }

  }

  if (!max_pin.empty()) {
    if (margin) {
      margin_delay = (max_delay/100.0) * (100-margin);
      fmt::print("slowest delay:{} pin:{} margin:{}% (margin_delay:{})\n", max_delay, max_pin, margin, margin_delay);
    }else{
      fmt::print("slowest delay:{} pin:{} NO MARGIN selected\n", max_delay, max_pin);
    }
  }
}

void Pass_opentimer::compute_power(Lgraph *g) {  // Expand this method to compute timing information
  TRACE_EVENT("pass", "OPENTIMER_compute_power");

  timer.update_timing();

  const auto &pins = timer.pins();

  double total_cap  = 0;
  double total_ipwr = 0;

  float voltage = 1;
  auto x = timer.cell_voltage();
  if (x) {
    voltage = *x;
  }

  for (const auto node : g->fast()) {
    auto op = node.get_type_op();
    if (op != Ntype_op::Sub)
      continue;

    auto       &sub_node      = node.get_type_sub_node();
    auto        instance_name = node.get_or_create_name();
    std::string type_name{sub_node.get_name()};  // OT needs std::string

    timer.insert_gate(instance_name, type_name);

    for (auto &e:node.inp_edges()) {

      auto pin_name = absl::StrCat(instance_name, ":", e.sink.get_pin_name());
      auto it = pins.find(pin_name);
      if (it == pins.end())
        continue;

      auto [cap, ipwr] = it->second.power();

      total_cap += cap;
      total_ipwr += ipwr;

      //auto wire_name = get_driver_net_name(e.driver);
      auto hier_name = e.driver.get_hier_name();
      auto node_name = node.get_name();

      auto vcd_name = absl::StrCat(hier_name,",", node_name, ",", e.sink.get_pin_name());

      double power = ipwr + cap*voltage*voltage;
      //double power = ipwr; // FIXME
      for(auto &pvcd:vcd_list) {
        pvcd.add(vcd_name, power);
      }

      // fmt::print("power hier:{} inst:{} driver:{} {} cap:{} ipwr:{}\n", hier_name, node_name, wire_name, pin_name, cap, ipwr);
      //fmt::print("power {} cap:{} ipwr:{}\n", vcd_name, cap, ipwr);
    }
  }

  for(auto &pvcd:vcd_list) {
    pvcd.compute(odir);
  }

  fmt::print("power TOTAL switch:{} internal:{} voltage:{}\n", total_cap, total_ipwr, voltage);
}

void Pass_opentimer::populate_table(Lgraph *lg) {
  TRACE_EVENT("pass", "OPENTIMER_populate_table");
  //  Lbench b("pass.OPENTIMER_populate_table");

  if (margin_delay<=0 || max_delay<=0)
    return; // Nothing to do

  lg->ref_node_color_map()->clear(); // Delete all the colors

  for(auto node:lg->fast(true)) {
    for (auto &dpin:node.out_connected_pins()) {
      if (!dpin.has_delay())
        continue;

      auto delay = dpin.get_delay();
      if (delay < margin_delay)
        continue;

      fmt::print("---------FAST delay:{}\n", delay);
      node.dump();

      int color = 100 * ((delay - margin_delay)/(max_delay - margin_delay));

      if (node.has_color()) {
        auto co = node.get_color();
        if (co >= color)
          continue;
      }
      node.set_color(color);

      backpath_set_color(node, color);
    }
  }
}

void Pass_opentimer::backpath_set_color(Node &node, int color) {

  I(node.get_color() == color);

  // Find inp_edge with highest delay
  Node_pin dpin;
  float    dpin_delay=0;
  for(auto &e:node.inp_edges()) {
    if (!e.driver.has_delay())
      continue;

    auto delay = e.driver.get_delay();
    if (delay < dpin_delay)
      continue;

    dpin = e.driver;
    dpin_delay = delay;
  }

  if (dpin_delay <= 0)
    return;

  if (dpin.is_graph_io())
    return;

  auto back_node = dpin.get_node();
  if (back_node.is_type_loop_first() || back_node.is_type_loop_last())
    return; // Do no cross constants/flops/memories

  fmt::print("---------DEP\n");
  back_node.dump();

  if (!back_node.has_color()) {
    back_node.set_color(color);
    return;
  }
  auto back_color = back_node.get_color();
  if (back_color >= color)
    return;

  back_node.set_color(color);

  fmt::print("---------REC\n");
  back_node.dump();
  backpath_set_color(back_node, color); // recursive (should not be too deep stop in flops)
}
