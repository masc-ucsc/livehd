//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include "lbench.hpp"
#include "lgedgeiter.hpp"
#include "lgraph.hpp"
#include "pass_opentimer.hpp"
#include "perf_tracing.hpp"
#include "str_tools.hpp"

void Pass_opentimer::work(Eprp_var &var) {
  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");
  Lbench b("pass.OPENTIMER_work");

  for (const auto &g : var.lgs) {
    pass.build_circuit(g);   // Task2: Traverse the lgraph and build the equivalent circuit (No dependencies) | Status: 50% done
    pass.compute_timing(g);  // Task4: Compute Timing | Status: 100% done
    pass.populate_table();   // Task5: Traverse the lgraph and populate the tables | Status: 0% done
  }
}

std::string Pass_opentimer::get_driver_net_name(const Node_pin &dpin) const {
  auto it = overwrite_dpin2net.find(dpin.get_compact_driver());
  if (it != overwrite_dpin2net.end()) {
    return it->second;
  }

  return dpin.get_wire_name();
}

void Pass_opentimer::build_circuit(Lgraph *g) {  // Enhance this for build_circuit
  TRACE_EVENT("pass", "OPENTIMER_build_circuit");
  //  Lbench b("pass.OPENTIMER_build_circuit");

  overwrite_dpin2net.clear();
  // FIXME: Add one net for each bit (expand multiple bits)

  g->each_graph_input([this](const Node_pin &pin) {
    std::string driver_name(pin.get_wire_name());  // OT needs std::string, not string_view support

    fmt::print("Graph Input Driver Name {}\n", driver_name);
    timer.insert_primary_input(driver_name);
    timer.insert_net(driver_name);
  });

  g->each_graph_output([this](const Node_pin &pin) {
    std::string driver_name(pin.get_wire_name());  // OT needs std::string, not string_view support

    fmt::print("Graph Output Driver Name {}\n", driver_name);
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

  for (const auto node : g->forward()) {  // TODO: Do we really need a slow forward. Why not just fast??
    auto op = node.get_type_op();

    if (op == Ntype_op::Const)
      continue;  // may be used later when connecting sub/getmask

    if (op == Ntype_op::Get_mask) {
      auto a_dpin = node.get_sink_pin("a").get_driver_pin();
      if (a_dpin.is_graph_io()) {
        overwrite_dpin2net.insert_or_assign(node.get_driver_pin().get_compact_driver(), a_dpin.get_name());
        continue;
      }
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
    auto        instance_name = node.create_name();
    std::string type_name{sub_node.get_name()};  // OT needs std::string

    fmt::print("CELL: instance_name:{} type_name:{}\n", instance_name, type_name);
    timer.insert_gate(instance_name, type_name);

    // setup driver pins and nets
    for (const auto &dpin : node.out_connected_pins()) {
      auto wire_name = get_driver_net_name(dpin);
      auto pin_name  = absl::StrCat(instance_name, ":", dpin.get_pin_name());

      timer.connect_pin(pin_name, wire_name);
      fmt::print("   pin_name:{} wire_name:{}\n", pin_name, wire_name);
    }

    // connect input pins
    for (const auto &e : node.inp_edges()) {
      auto wire_name = get_driver_net_name(e.driver);
      auto pin_name  = absl::StrCat(instance_name, ":", e.sink.get_pin_name());

      timer.connect_pin(pin_name, wire_name);
      fmt::print("   pin_name:{} wire_name:{}\n", pin_name, wire_name);
    }
  }
}

void Pass_opentimer::compute_timing(Lgraph *g) {  // Expand this method to compute timing information
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

  // timer.dump_graph(std::cout);

  auto opt_path = timer.report_timing(1);

  if (opt_path.size())
    std::cout << "Critical Path" << opt_path[0] << '\n';

  for (const auto node : g->fast()) {
    auto op = node.get_type_op();
    if (op != Ntype_op::Sub)
      continue;

    auto       &sub_node      = node.get_type_sub_node();
    auto        instance_name = node.create_name();
    std::string type_name{sub_node.get_name()};  // OT needs std::string

    fmt::print("CELL: instance_name:{} type_name:{}\n", instance_name, type_name);
    timer.insert_gate(instance_name, type_name);

    // setup driver pins and nets
    for (const auto &dpin : node.out_connected_pins()) {
      auto pin_name = absl::StrCat(instance_name, ":", dpin.get_pin_name());
      auto delay_at = timer.report_at(pin_name, ot::MAX, ot::FALL);
      if (delay_at)
        fmt::print(" delay_at pin:{} at:{}\n", pin_name, *delay_at);
    }
  }

  timer.dump_slack(std::cout);
}

void Pass_opentimer::populate_table() {  // Expand this method to populate the tables in lgraph
  TRACE_EVENT("pass", "OPENTIMER_populate_table");
  //  Lbench b("pass.OPENTIMER_populate_table");
}
