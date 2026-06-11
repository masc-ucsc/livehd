//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <format>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"
#include "const.hpp"
#include "hhds/graph.hpp"
#include "hlop/dlop.hpp"
#include "node_util.hpp"
#include "pass_opentimer.hpp"
#include "pin_tracker.hpp"
#include "str_tools.hpp"

// WARNING: opentimer has a nasty "define has_member" that overlaps with perfetto methods
#undef has_member
#include "perf_tracing.hpp"

using livehd::graph_util::bits_of;
using livehd::graph_util::color_of;
using livehd::graph_util::create_const;
using livehd::graph_util::debug_name;
using livehd::graph_util::default_instance_name;
using livehd::graph_util::get_driver_of_sink_name;
using livehd::graph_util::has_color;
using livehd::graph_util::hydrate_const;
using livehd::graph_util::is_const_pin;
using livehd::graph_util::is_graph_input_pin;
using livehd::graph_util::is_graph_output_pin;
using livehd::graph_util::pin_name_of;
using livehd::graph_util::set_color;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {

// Wire name for a pin as opentimer should see it: GraphIO pins use their
// declared name; internal pins fall back to graph_util::wire_name().
[[nodiscard]] std::string pin_net_name(const hhds::Pin_class& pin) {
  if (is_graph_input_pin(pin) || is_graph_output_pin(pin)) {
    auto n = pin.get_pin_name();
    if (!n.empty()) {
      return std::string{n};
    }
  }
  return wire_name(pin);
}

// Sink-pin name for a node + sink port. For Sub nodes the name is the
// sub-graph's IO declared name; for other nodes it's Ntype's sink_name.
[[nodiscard]] std::string sink_pin_name_of(const hhds::Node_class& node, const hhds::Pin_class& sink) {
  auto pid = sink.get_port_id();
  if (type_op_of(node) == Ntype_op::Sub) {
    auto n = sink.get_pin_name();
    if (!n.empty()) {
      return std::string{n};
    }
    return std::to_string(static_cast<uint32_t>(pid));
  }
  return Ntype::get_sink_name(type_op_of(node), pid);
}

// Driver-pin name for a node + driver port (used to name OT cell pins).
[[nodiscard]] std::string driver_pin_name_of(const hhds::Node_class& node, const hhds::Pin_class& dpin) {
  if (type_op_of(node) == Ntype_op::Sub) {
    auto n = dpin.get_pin_name();
    if (!n.empty()) {
      return std::string{n};
    }
    return std::to_string(static_cast<uint32_t>(dpin.get_port_id()));
  }
  return std::string{Ntype::get_driver_name(type_op_of(node))};
}

// Sub-graph cell type name (the module being instantiated).
[[nodiscard]] std::string sub_type_name(const hhds::Node_class& node) {
  auto io = node.get_subnode_io();
  if (!io) {
    return {};
  }
  return std::string{io->get_name()};
}

// Per-pin delay annotation helpers (replaces Lgraph's Node_pin::set_delay /
// has_delay / get_delay / del_delay).
inline void set_delay(const hhds::Pin_class& pin, float d) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_delay).set(d);
}
inline bool has_delay(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return false;
  }
  return pin.attr(livehd::attrs::pin_delay).has();
}
inline float get_delay(const hhds::Pin_class& pin) { return pin.attr(livehd::attrs::pin_delay).get(); }
inline void  del_delay(const hhds::Pin_class& pin) {
  if (pin.is_invalid()) {
    return;
  }
  pin.attr(livehd::attrs::pin_delay).del();
}

// Drivers feeding a named sink port — like inp_drivers_of but returning the
// (driver, edge) pairs in inp_edges order. opentimer iterates all "b" drivers
// of an SHL node.
[[nodiscard]] std::vector<hhds::Edge_class> inp_edges_of_sink(const hhds::Node_class& node, std::string_view name) {
  std::vector<hhds::Edge_class> out;
  auto                          op = type_op_of(node);
  hhds::Port_id                 target;
  if (op == Ntype_op::Sub) {
    auto pin = node.get_sink_pin(name);
    if (pin.is_invalid()) {
      return out;
    }
    target = pin.get_port_id();
  } else {
    target = Ntype::get_sink_pid(op, name);
    if (target == livehd::Port_invalid) {
      return out;
    }
  }
  for (const auto& e : node.inp_edges()) {
    if (e.sink.get_port_id() == target) {
      out.push_back(e);
    }
  }
  return out;
}

}  // namespace

void Pass_opentimer::time_work(Eprp_var& var) {
  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    pass.build_circuit(g);
    pass.read_sdc_spef();
    pass.compute_timing(g);
    pass.populate_table(g);
  }
}

void Pass_opentimer::power_work(Eprp_var& var) {
  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");

  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    pass.build_circuit(g);
    pass.read_sdc_spef();
    pass.read_vcd();
    pass.compute_power(g);
  }
}

std::string Pass_opentimer::get_driver_net_name(const hhds::Pin_class& dpin) const {
  auto it = overwrite_dpin2net.find(dpin.get_class_index());
  if (it != overwrite_dpin2net.end()) {
    return it->second;
  }
  return pin_net_name(dpin);
}

void Pass_opentimer::read_vcd() {
  vcd_list.resize(vcd_file_list.size());

  for (auto i = 0u; i < vcd_file_list.size(); ++i) {
    const auto& f = vcd_file_list[i];

    bool ok = vcd_list[i].open(f);
    if (!ok) {
      Pass::error("could not read vcd {} file", f);
    }
  }
}

// SDC and SPEF must be read after the circuit is created
void Pass_opentimer::read_sdc_spef() {
  for (const auto& f : sdc_file_list) {
    read_sdc(f);
  }
  for (const auto& f : spef_file_list) {
    timer.read_spef(f);
  }
}

void Pass_opentimer::set_input_delays(const std::string& pname) {
  timer.set_at(pname, ot::MIN, ot::FALL, 0.0);
  timer.set_at(pname, ot::MIN, ot::RISE, 0.0);
  timer.set_at(pname, ot::MAX, ot::FALL, 0.0);
  timer.set_at(pname, ot::MAX, ot::RISE, 0.0);

  timer.set_slew(pname, ot::MAX, ot::FALL, 0.0);
  timer.set_slew(pname, ot::MAX, ot::RISE, 0.0);
  timer.set_slew(pname, ot::MIN, ot::FALL, 0.0);
  timer.set_slew(pname, ot::MIN, ot::RISE, 0.0);
}

void Pass_opentimer::set_output_delays(const std::string& pname) {
  timer.set_rat(pname, ot::MIN, ot::FALL, 0.0);
  timer.set_rat(pname, ot::MIN, ot::RISE, 0.0);
  timer.set_rat(pname, ot::MAX, ot::FALL, 0.0);
  timer.set_rat(pname, ot::MAX, ot::RISE, 0.0);
}

void Pass_opentimer::build_circuit(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_build_circuit");

  overwrite_dpin2net.clear();

  Pin_tracker<std::string> pin_tracker("zero");

  auto gio = g->get_io();

  // 1st: primary inputs.
  if (gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      auto pin = g->get_input_pin(d.name);
      if (pin.is_invalid()) {
        continue;
      }
      std::string driver_name{d.name};
      auto        bits = bits_of(pin, *gio, d.name);
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }

      timer.insert_primary_input(driver_name);
      timer.insert_net(driver_name);
      for (auto i = 1; i < bits; ++i) {
        auto bus_bit_name = absl::StrCat(driver_name, ".", str_tools::to_s(i));
        timer.insert_primary_input(bus_bit_name);
        timer.insert_net(bus_bit_name);
      }
      pin_tracker.add_input(driver_name, bits);
    }
  }

  // 2nd: primary outputs.
  if (gio) {
    for (const auto& d : gio->get_output_pin_decls()) {
      auto out_sink = g->get_output_pin(d.name);
      if (out_sink.is_invalid()) {
        continue;
      }
      auto inps = out_sink.inp_edges();
      if (inps.empty()) {
        continue;
      }
      auto driver_dpin = inps.front().driver;

      std::string driver_name{d.name};
      auto        bits = bits_of(driver_dpin);
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }

      timer.insert_primary_output(driver_name);
      timer.insert_net(driver_name);
      for (auto i = 1; i < bits; ++i) {
        auto bus_bit_name = absl::StrCat(driver_name, ".", str_tools::to_s(i));
        timer.insert_primary_output(bus_bit_name);
        timer.insert_net(bus_bit_name);
      }
      // Multiple outputs sharing the same driver dpin is legal — last one wins
      // (mirrors the original code's behaviour).
      overwrite_dpin2net.insert_or_assign(driver_dpin.get_class_index(), driver_name);
    }
  }

  // 3rd: populate all the net names (forward walk, pin-tracker for trackable ops).
  for (auto node : g->forward_class()) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Nconst || op == Ntype_op::AttrSet) {
      continue;
    }

    bool root_track = Ntype::is_pin_trackable(op);
    if (root_track) {
      auto dpin0 = node.create_driver_pin(0);
      auto wname = pin_net_name(dpin0);
      if (op == Ntype_op::Set_mask) {
        auto a_dpin     = get_driver_of_sink_name(node, "a");
        auto mask_dpin  = get_driver_of_sink_name(node, "mask");
        auto value_dpin = get_driver_of_sink_name(node, "value");
        if (a_dpin.is_invalid() || mask_dpin.is_invalid() || value_dpin.is_invalid()) {
          Pass::error("Invalid corrupt set_mask node {} (cprop should have deleted it)", debug_name(node));
          return;
        }
        if (!is_const_pin(mask_dpin)) {
          Pass::error("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node));
          return;
        }
        auto mask_const = hydrate_const(mask_dpin);
        pin_tracker.add_set_mask(wname, pin_net_name(a_dpin), bits_of(a_dpin), mask_const, pin_net_name(value_dpin));
      } else if (op == Ntype_op::Get_mask) {
        auto a_dpin    = get_driver_of_sink_name(node, "a");
        auto mask_dpin = get_driver_of_sink_name(node, "mask");
        if (a_dpin.is_invalid() || mask_dpin.is_invalid()) {
          Pass::error("Invalid corrupt get_mask node {} (cprop should have deleted it)", debug_name(node));
          return;
        }
        if (!is_const_pin(mask_dpin)) {
          Pass::error("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node));
          return;
        }
        auto mask_const = hydrate_const(mask_dpin);
        pin_tracker.add_get_mask(wname, pin_net_name(a_dpin), bits_of(a_dpin), mask_const);
      } else if (op == Ntype_op::SRA) {
        auto a_dpin = get_driver_of_sink_name(node, "a");
        auto b_dpin = get_driver_of_sink_name(node, "b");
        if (a_dpin.is_invalid() || b_dpin.is_invalid()) {
          Pass::error("Invalid corrupt SRA node {} (cprop should have deleted it)", debug_name(node));
          return;
        }
        if (!is_const_pin(b_dpin)) {
          Pass::error("opentimer can not handle non-constant SRA on node {} (cprop/tmap first)", debug_name(node));
          return;
        }
        auto b_const = hydrate_const(b_dpin);
        pin_tracker.add_sra(wname, pin_net_name(a_dpin), bits_of(a_dpin), b_const);
      } else if (op == Ntype_op::Sext) {
        auto a_dpin = get_driver_of_sink_name(node, "a");
        auto b_dpin = get_driver_of_sink_name(node, "b");
        if (a_dpin.is_invalid() || b_dpin.is_invalid()) {
          Pass::error("Invalid corrupt Sext node {} (cprop should have deleted it)", debug_name(node));
          return;
        }
        if (!is_const_pin(b_dpin)) {
          Pass::error("opentimer can not handle non-constant Sext on node {} (cprop/tmap first)", debug_name(node));
          return;
        }
        auto b_const = hydrate_const(b_dpin);
        pin_tracker.add_sext(wname, pin_net_name(a_dpin), bits_of(a_dpin), b_const);
      } else if (op == Ntype_op::SHL) {
        auto a_dpin = get_driver_of_sink_name(node, "a");
        if (a_dpin.is_invalid()) {
          Pass::error("Invalid corrupt SHL node {} (cprop should have deleted it)", debug_name(node));
          return;
        }
        for (auto e : inp_edges_of_sink(node, "b")) {
          if (!is_const_pin(e.driver)) {
            Pass::error("opentimer can not handle non-constant SHL on node {} (cprop/tmap first)", debug_name(node));
            return;
          }
          auto b_const = hydrate_const(e.driver);
          pin_tracker.add_shl(wname, pin_net_name(a_dpin), bits_of(a_dpin), b_const);
        }
      } else if (op == Ntype_op::Or) {
        for (auto e : node.inp_edges()) {
          pin_tracker.add_or(wname, pin_net_name(e.driver));
        }
      } else if (op == Ntype_op::And) {
        Const           a_mask = *Dlop::create_integer(-1);
        hhds::Pin_class a_dpin;
        for (auto e : node.inp_edges()) {
          if (is_const_pin(e.driver)) {
            a_mask = a_mask.and_op(hydrate_const(e.driver));
          } else {
            if (!a_dpin.is_invalid()) {
              Pass::error("pin_tracker needed for netlist can not handle multiple unknowns on node {}", debug_name(node));
              return;
            }
            a_dpin = e.driver;
          }
        }
        if (!a_dpin.is_invalid()) {
          pin_tracker.add_and(wname, pin_net_name(a_dpin), a_mask);
        }
      } else {
        Pass::error("opentimer needs a tmap/synthesized netlist; got node {}", debug_name(node));
        return;
      }
    }

    // setup driver pins and nets
    for (auto& dpin : node.out_pins()) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
        I(!root_track);
        continue;
      }
      auto wname = pin_net_name(dpin);

      if (root_track) {
        const auto& pv = pin_tracker.get_pin_vector(wname);

        if (pv.size() == 1) {  // single bit tracking result
          auto dpin_cd = dpin.get_class_index();
          if (pv[0].pos < 0) {
            continue;  // no connection
          }
          if (pv[0].pos) {
            auto bus_bit_name = absl::StrCat(pv[0].id, ".", str_tools::to_s(pv[0].pos));
            overwrite_dpin2net.insert_or_assign(dpin_cd, bus_bit_name);
          } else {
            overwrite_dpin2net.insert_or_assign(dpin_cd, pv[0].id);
          }
        }
      } else {
        timer.insert_net(wname);
      }
    }
  }

  // 4th: populate the cells (Sub instances).
  for (auto node : g->fast_class()) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Nconst || op == Ntype_op::AttrSet) {
      continue;
    }
    if (Ntype::is_pin_trackable(op)) {
      continue;
    }
    if (op != Ntype_op::Sub) {
      Pass::error("opentimer pass needs the lgraph to be tmap, found cell {} with type {}", debug_name(node), Ntype::get_name(op));
      return;
    }

    auto instance_name = default_instance_name(node);
    auto type_name     = sub_type_name(node);

    timer.insert_gate(instance_name, type_name);

    // setup driver pins and nets
    for (auto& dpin : node.out_pins()) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      auto pin_name = absl::StrCat(instance_name, ":", driver_pin_name_of(node, dpin));
      auto wire     = get_driver_net_name(dpin);
      timer.connect_pin(pin_name, wire);
    }

    // connect input pins
    for (auto& e : node.inp_edges()) {
      I(!(is_graph_input_pin(e.driver) && bits_of(e.driver) > 2));

      auto wire     = get_driver_net_name(e.driver);
      auto pin_name = absl::StrCat(instance_name, ":", sink_pin_name_of(node, e.sink));

      timer.connect_pin(pin_name, wire);
    }
  }

  // 5th: zero-default the inputs/outputs at the OT side.
  if (gio) {
    for (const auto& d : gio->get_input_pin_decls()) {
      auto pin = g->get_input_pin(d.name);
      if (pin.is_invalid()) {
        continue;
      }
      std::string pname{d.name};
      auto        bits = bits_of(pin, *gio, d.name);
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }
      set_input_delays(pname);
      for (auto i = 1; i < bits; ++i) {
        set_input_delays(absl::StrCat(pname, ".", str_tools::to_s(i)));
      }
    }
    for (const auto& d : gio->get_output_pin_decls()) {
      auto pin = g->get_output_pin(d.name);
      if (pin.is_invalid()) {
        continue;
      }
      std::string pname{d.name};
      int32_t      bits = 0;
      auto        inps = pin.inp_edges();
      if (!inps.empty()) {
        bits = bits_of(inps.front().driver);
      }
      if (bits == 0) {
        bits = static_cast<int32_t>(d.bits);
      }
      set_output_delays(pname);
      for (auto i = 1; i < bits; ++i) {
        set_output_delays(absl::StrCat(pname, ".", str_tools::to_s(i)));
      }
    }
  }
}

void Pass_opentimer::compute_timing(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_compute_timing");

  timer.update_timing();

  max_delay = 0;
  std::string max_pin;

  const auto& pins = timer.pins();

  for (auto node : g->fast_class()) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Sub) {
      continue;
    }

    auto instance_name = default_instance_name(node);

    for (auto& dpin : node.out_pins()) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      auto pin_name = absl::StrCat(instance_name, ":", driver_pin_name_of(node, dpin));

      auto it = pins.find(pin_name);
      if (it == pins.end()) {
        continue;
      }

      auto at_f = it->second.at(ot::MAX, ot::FALL);
      auto at_r = it->second.at(ot::MAX, ot::RISE);

      float delay = 0.0;
      if (at_f) {
        delay = *at_f;
      }
      if (at_r && *at_r > delay) {
        delay = *at_r;
      }
      if (delay > 0) {
        set_delay(dpin, delay);

        if (delay > max_delay) {
          max_delay = delay;
          max_pin   = pin_name;
        }
      } else {
        del_delay(dpin);
      }
    }
  }

  if (!max_pin.empty()) {
    if (margin) {
      margin_delay = (max_delay / 100.0) * (100 - margin);
      std::print("slowest delay:{} pin:{} margin:{}% (margin_delay:{})\n", max_delay, max_pin, margin, margin_delay);
    } else {
      std::print("slowest delay:{} pin:{} NO MARGIN selected\n", max_delay, max_pin);
    }
  }
}

void Pass_opentimer::compute_power(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_compute_power");

  timer.update_timing();

  const auto& gates = timer.gates();

  double total_cap  = 0;
  double total_ipwr = 0;

  float voltage = 1;
  auto  x       = timer.cell_voltage();
  if (x) {
    voltage = *x;
  }

  double cap_unit   = timer.capacitance_unit()->value();
  double timeunit   = timer.time_unit()->value();
  double power_unit = timer.power_unit()->value();

  for (auto& pvcd : vcd_list) {
    pvcd.set_timescale(timeunit);
  }
  std::cout << "================================\n";
  for (auto node : g->fast_class()) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Sub) {
      continue;
    }

    auto instance_name = default_instance_name(node);

    auto it2 = gates.find(instance_name);
    if (it2 == gates.end()) {
      std::print("WEIRD. Where is the gate named {}? (node {})\n", instance_name, debug_name(node));
      continue;
    }

    for (const auto* pin : it2->second.pins()) {
      auto [cap, ipwr] = pin->power();

      cap *= static_cast<float>(freq * power_unit * 0.5 * voltage * voltage * cap_unit / timeunit);
      ipwr *= static_cast<float>(freq * power_unit * cap_unit / timeunit);

      total_cap += cap;
      total_ipwr += ipwr;

      // WARNING: Replace last ':' for ','
      // -OpenTimer uses as pin name: "whatever":"pin"
      // -Power_vcd uses "whatever","pin"
      std::string pin_name{pin->name()};
      auto        last_colon_pos = pin_name.rfind(':');
      I(last_colon_pos != std::string::npos);
      pin_name[last_colon_pos] = ',';

      for (auto& pvcd : vcd_list) {
        pvcd.add(pin_name, ipwr + cap);
      }

      std::print("iname:{} pin:{} ipwr:{} cap:{}\n", instance_name, pin_name, ipwr, cap);
    }
  }

  std::cout << "================================\n";
  for (auto& pvcd : vcd_list) {
    pvcd.compute(odir);
    std::print("AVG power:{} for {}\n", pvcd.get_power_average(), pvcd.get_filename());
  }

  std::print("TOTAL power:{} DYNAMIC power:{} INTERNAL power:{} W voltage:{} V freq={}MHz\n",
             total_cap + total_ipwr,
             total_cap,
             total_ipwr,
             voltage,
             freq / 1e6);
}

void Pass_opentimer::populate_table(const std::shared_ptr<hhds::Graph>& g) {
  TRACE_EVENT("pass", "OPENTIMER_populate_table");

  if (margin_delay <= 0 || max_delay <= 0) {
    return;
  }

  // Clear any pre-existing colors before annotating critical paths.
  for (auto node : g->fast_class()) {
    node.attr(livehd::attrs::color).del();
  }

  for (auto node : g->fast_class()) {
    for (auto& dpin : node.out_pins()) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      if (!has_delay(dpin)) {
        continue;
      }

      auto delay = get_delay(dpin);
      if (delay < margin_delay) {
        continue;
      }

      int color = 100 * ((delay - margin_delay) / (max_delay - margin_delay));

      if (has_color(node)) {
        auto co = color_of(node);
        if (co >= color) {
          continue;
        }
      }
      set_color(node, color);

      backpath_set_color(node, color);
    }
  }
}

void Pass_opentimer::backpath_set_color(hhds::Node_class node, int color) {
  I(color_of(node) == color);

  // Find inp_edge with highest delay
  hhds::Pin_class dpin;
  float           dpin_delay = 0;
  for (auto& e : node.inp_edges()) {
    if (!has_delay(e.driver)) {
      continue;
    }
    auto delay = get_delay(e.driver);
    if (delay < dpin_delay) {
      continue;
    }
    dpin       = e.driver;
    dpin_delay = delay;
  }

  if (dpin_delay <= 0) {
    return;
  }

  if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
    return;
  }

  auto back_node = dpin.get_master_node();
  auto back_op   = type_op_of(back_node);
  if (Ntype::is_loop_first(back_op) || Ntype::is_loop_last(back_op)) {
    return;  // Do not cross constants/flops/memories
  }

  std::cout << "---------DEP\n";
  std::print("{}\n", debug_name(back_node));

  if (!has_color(back_node)) {
    set_color(back_node, color);
    return;
  }
  auto back_color = color_of(back_node);
  if (back_color >= color) {
    return;
  }

  set_color(back_node, color);

  std::cout << "---------REC\n";
  std::print("{}\n", debug_name(back_node));
  backpath_set_color(back_node, color);  // recursive (should not be too deep stop in flops)
}
