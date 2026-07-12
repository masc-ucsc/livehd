//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"
#include "hhds/attrs/srcid.hpp"
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
using livehd::graph_util::set_color;
using livehd::graph_util::type_op_of;
using livehd::graph_util::wire_name;

namespace {

// A Sub-instance pin's declared name looked up in the sub-graph's GraphIO by
// port id. Instance pins loaded from an lg: library may carry no pin_name of
// their own (only the def declares names), so resolving through the decl is
// what keeps OT pin names equal to the Liberty pin names.
[[nodiscard]] std::string sub_pin_name_from_decl(const hhds::Node_class& node, hhds::Port_id pid, bool is_driver) {
  auto io = node.get_subnode_io();
  if (!io) {
    return {};
  }
  const auto& decls = is_driver ? io->get_output_pin_decls() : io->get_input_pin_decls();
  for (const auto& d : decls) {
    if (d.port_id == pid) {
      return d.name;
    }
  }
  return {};
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
    if (auto dn = sub_pin_name_from_decl(node, pid, /*is_driver=*/false); !dn.empty()) {
      return dn;
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
    if (auto dn = sub_pin_name_from_decl(node, dpin.get_port_id(), /*is_driver=*/true); !dn.empty()) {
      return dn;
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

// Minimal JSON string escape for the timing report (pin names / file paths).
std::string jesc(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += std::format("\\u{:04x}", static_cast<unsigned char>(c));
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

// "file:line" of a node's srcid (empty when absent/unresolvable). Mapped gates
// carry the srcid of the output cone they feed (pass.abc carry-through), so a
// critical pin points back at the pre-synthesis RTL.
std::string src_of_node(const std::shared_ptr<hhds::Graph>& g, const hhds::Node_class& n) {
  auto a = n.attr(hhds::attrs::srcid);
  if (!a.has() || a.get() == 0) {
    return {};
  }
  auto span = g->source_locator().resolve_span(a.get());
  if (span.file.empty() || !span.start_line.has_value()) {
    return {};
  }
  return span.file + ":" + std::to_string(*span.start_line);
}

}  // namespace

void Pass_opentimer::time_work(Eprp_var& var) {
  Pass_opentimer pass(var);

  TRACE_EVENT("pass", "OPENTIMER_work");

  // One ot::Timer holds ONE design: building several graphs into the same
  // timer silently merges them, so exactly one def is analyzed per run
  // (top_filter picks it out of a multi-def library).
  std::vector<std::shared_ptr<hhds::Graph>> selected;
  for (const auto& g : var.graphs) {
    if (!g) {
      continue;
    }
    if (!pass.top_filter.empty() && g->get_name() != pass.top_filter) {
      continue;
    }
    selected.push_back(g);
  }
  if (selected.empty()) {
    livehd::diag::err("pass.opentimer", "no-top", "unsupported")
        .msg("pass.opentimer: no module{} found in the input library",
             pass.top_filter.empty() ? std::string{} : std::format(" named '{}'", pass.top_filter))
        .fatal();
    return;
  }
  if (selected.size() > 1) {
    livehd::diag::err("pass.opentimer", "bad-option", "usage")
        .msg("pass.opentimer times one module per run ({} defs in the library): pass --top <module> to pick one",
             selected.size())
        .fatal();
    return;
  }

  const auto& g = selected.front();
  pass.build_circuit(g);
  pass.read_sdc_spef();
  pass.compute_timing(g);
  pass.populate_table(g);
  pass.write_qor();
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
  return wire_name(dpin);
}

void Pass_opentimer::read_vcd() {
  vcd_list.resize(vcd_file_list.size());

  for (auto i = 0u; i < vcd_file_list.size(); ++i) {
    const auto& f = vcd_file_list[i];

    bool ok = vcd_list[i].open(f);
    if (!ok) {
      livehd::diag::err("pass.opentimer", "missing-file", "io").msg("could not read vcd {} file", f).fatal();
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

  // Pin-level `bits` on a graph-IO pin is usually unset (widths live on the
  // GraphIO decl) — the pin tracker needs the real width, so fall back to the
  // decl when the driver is a module input.
  auto io_bits_of = [&](const hhds::Pin_class& dpin) -> int32_t {
    auto b = bits_of(dpin);
    if (b != 0 || !gio || !is_graph_input_pin(dpin)) {
      return b;
    }
    auto n = dpin.get_pin_name();
    if (n.empty()) {
      return b;
    }
    return bits_of(dpin, *gio, n);
  };

  // Tracker id for a driver pin. pass.partition names region boundary ports
  // after the SOURCE-side wire (a port can literally be called "get_mask_20"),
  // so an internal trackable node's synthetic wire_name can collide with a
  // port name and silently redefine the port's bus inside the string-keyed
  // tracker. Trackable outputs are pure rewiring — never real OT nets — so
  // decorate exactly those with a prefix no port/net name ever carries;
  // tracker LEAVES (ports, gate output nets) stay undecorated, which keeps
  // every pv root a name that exists as an OT net.
  auto trk_id = [&](const hhds::Pin_class& pin) -> std::string {
    if (is_graph_input_pin(pin) || is_graph_output_pin(pin)) {
      return wire_name(pin);
    }
    auto master = pin.get_master_node();
    if (!master.is_invalid() && Ntype::is_pin_trackable(type_op_of(master))) {
      return absl::StrCat("n$", wire_name(pin));
    }
    return wire_name(pin);
  };

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
      auto wname = trk_id(dpin0);
      if (op == Ntype_op::Set_mask) {
        auto a_dpin     = get_driver_of_sink_name(node, "a");
        auto mask_dpin  = get_driver_of_sink_name(node, "mask");
        auto value_dpin = get_driver_of_sink_name(node, "value");
        if (a_dpin.is_invalid() || mask_dpin.is_invalid() || value_dpin.is_invalid()) {
          livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
              .msg("Invalid corrupt set_mask node {} (cprop should have deleted it)", debug_name(node))
              .fatal();
          return;
        }
        if (!is_const_pin(mask_dpin)) {
          livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
              .msg("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node))
              .fatal();
          return;
        }
        auto mask_const = hydrate_const(mask_dpin);
        pin_tracker.add_set_mask(wname, trk_id(a_dpin), io_bits_of(a_dpin), mask_const, trk_id(value_dpin));
      } else if (op == Ntype_op::Get_mask) {
        auto a_dpin    = get_driver_of_sink_name(node, "a");
        auto mask_dpin = get_driver_of_sink_name(node, "mask");
        if (a_dpin.is_invalid() || mask_dpin.is_invalid()) {
          livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
              .msg("Invalid corrupt get_mask node {} (cprop should have deleted it)", debug_name(node))
              .fatal();
          return;
        }
        if (!is_const_pin(mask_dpin)) {
          livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
              .msg("opentimer can not handle non-constant masks on node {} (cprop/tmap first)", debug_name(node))
              .fatal();
          return;
        }
        auto mask_const = hydrate_const(mask_dpin);
        pin_tracker.add_get_mask(wname, trk_id(a_dpin), io_bits_of(a_dpin), mask_const);
      } else if (op == Ntype_op::SRA) {
        auto a_dpin = get_driver_of_sink_name(node, "a");
        auto b_dpin = get_driver_of_sink_name(node, "b");
        if (a_dpin.is_invalid() || b_dpin.is_invalid()) {
          livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
              .msg("Invalid corrupt SRA node {} (cprop should have deleted it)", debug_name(node))
              .fatal();
          return;
        }
        if (!is_const_pin(b_dpin)) {
          livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
              .msg("opentimer can not handle non-constant SRA on node {} (cprop/tmap first)", debug_name(node))
              .fatal();
          return;
        }
        auto b_const = hydrate_const(b_dpin);
        pin_tracker.add_sra(wname, trk_id(a_dpin), io_bits_of(a_dpin), b_const);
      } else if (op == Ntype_op::Sext) {
        auto a_dpin = get_driver_of_sink_name(node, "a");
        auto b_dpin = get_driver_of_sink_name(node, "b");
        if (a_dpin.is_invalid() || b_dpin.is_invalid()) {
          livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
              .msg("Invalid corrupt Sext node {} (cprop should have deleted it)", debug_name(node))
              .fatal();
          return;
        }
        if (!is_const_pin(b_dpin)) {
          livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
              .msg("opentimer can not handle non-constant Sext on node {} (cprop/tmap first)", debug_name(node))
              .fatal();
          return;
        }
        auto b_const = hydrate_const(b_dpin);
        pin_tracker.add_sext(wname, trk_id(a_dpin), io_bits_of(a_dpin), b_const);
      } else if (op == Ntype_op::SHL) {
        auto a_dpin = get_driver_of_sink_name(node, "a");
        if (a_dpin.is_invalid()) {
          livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
              .msg("Invalid corrupt SHL node {} (cprop should have deleted it)", debug_name(node))
              .fatal();
          return;
        }
        // SHL b is single-driver (the one-hot multi-shift form was removed).
        auto b_dpin = get_driver_of_sink_name(node, "b");
        if (!is_const_pin(b_dpin)) {
          livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
              .msg("opentimer can not handle non-constant SHL on node {} (cprop/tmap first)", debug_name(node))
              .fatal();
          return;
        }
        auto b_const = hydrate_const(b_dpin);
        pin_tracker.add_shl(wname, trk_id(a_dpin), io_bits_of(a_dpin), b_const);
      } else if (op == Ntype_op::Or) {
        for (auto e : node.inp_edges()) {
          pin_tracker.add_or(wname, trk_id(e.driver));
        }
      } else if (op == Ntype_op::And) {
        Dlop            a_mask = *Dlop::create_integer(-1);
        hhds::Pin_class a_dpin;
        for (auto e : node.inp_edges()) {
          if (is_const_pin(e.driver)) {
            a_mask = a_mask.and_op(hydrate_const(e.driver));
          } else {
            if (!a_dpin.is_invalid()) {
              livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
                  .msg("pin_tracker needed for netlist can not handle multiple unknowns on node {}", debug_name(node))
                  .fatal();
              return;
            }
            a_dpin = e.driver;
          }
        }
        if (!a_dpin.is_invalid()) {
          pin_tracker.add_and(wname, trk_id(a_dpin), a_mask);
        }
      } else {
        livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
            .msg("opentimer needs a tmap/synthesized netlist; got node {}", debug_name(node))
            .fatal();
        return;
      }
    }

    // setup driver pins and nets. Plain cells (trackable ops, flops) drive
    // through an implicit port-0 pin that materializes no PinEntry, so
    // out_pins() misses it — fall back to the port-0 driver handle explicitly
    // (Sub node ports are always materialized, no fallback there).
    std::vector<hhds::Pin_class> dpins;
    for (auto& dpin : node.out_pins()) {
      dpins.push_back(dpin);
    }
    if (dpins.empty() && op != Ntype_op::Sub) {
      auto dpin0 = node.create_driver_pin(0);
      if (!dpin0.is_invalid()) {
        dpins.push_back(dpin0);
      }
    }
    for (auto& dpin : dpins) {
      if (dpin.is_invalid() || dpin.out_edges().empty()) {
        continue;
      }
      if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
        I(!root_track);
        continue;
      }
      auto wname = root_track ? trk_id(dpin) : wire_name(dpin);

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
    if (op == Ntype_op::Flop || op == Ntype_op::Memory) {
      // Path boundary, not a cell (2opt-freq D): pass.abc keeps flops/memories
      // native — the Liberty stays combinational. Each consumed output (Q,
      // memory read data) becomes a virtual primary input arriving at 0, so
      // flop-to-flop segments are scored; the din/en/addr cones end at their
      // driving gate pins, which compute_timing already reads. Clock/reset
      // nets are not timed (no clock tree in this estimate). Flop Q is an
      // implicit port-0 pin (no PinEntry) — same fallback as the net walk.
      std::vector<hhds::Pin_class> bpins;
      for (auto& dpin : node.out_pins()) {
        bpins.push_back(dpin);
      }
      if (bpins.empty()) {
        auto dpin0 = node.create_driver_pin(0);
        if (!dpin0.is_invalid()) {
          bpins.push_back(dpin0);
        }
      }
      for (auto& dpin : bpins) {
        if (dpin.is_invalid() || dpin.out_edges().empty()) {
          continue;
        }
        if (overwrite_dpin2net.contains(dpin.get_class_index())) {
          continue;  // drives a primary output directly: already a PO net
        }
        auto        wname = wire_name(dpin);
        const auto  bits  = bits_of(dpin);
        timer.insert_primary_input(wname);  // idempotent net insert underneath
        set_input_delays(wname);
        for (auto i = 1; i < bits; ++i) {
          auto bus_bit_name = absl::StrCat(wname, ".", str_tools::to_s(i));
          timer.insert_primary_input(bus_bit_name);
          set_input_delays(bus_bit_name);
        }
      }
      continue;
    }
    if (op != Ntype_op::Sub) {
      livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
          .msg("opentimer pass needs the lgraph to be tmap, found cell {} with type {}", debug_name(node), Ntype::get_name(op))
          .fatal();
      return;
    }

    auto instance_name = default_instance_name(node);
    auto type_name     = sub_type_name(node);

    // ABC's builtin tie cells (emitted when the Liberty has no constant
    // cells): a constant never transitions, so it contributes no arrival —
    // leave its output net driverless and skip the gate.
    if (type_name == "_const0_" || type_name == "_const1_") {
      continue;
    }

    // A Sub that is not a Liberty cell (a child-module instance) cannot be
    // timed: ot::Timer::insert_gate would just log-and-skip, yielding silent
    // garbage. The celllib is loaded (ctor flushes the lineage).
    const auto& lib = timer.celllib(ot::MAX);
    if (!lib || lib->cell(type_name) == nullptr) {
      livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
          .msg("module instantiates '{}' (instance {}), which is not a cell in the Liberty library — pass.opentimer times one "
               "tech-mapped module at a time: run pass.abc first and pass --top of a mapped module (a region <mod>__c<N>, or "
               "an uncolored/flat map for whole-design timing)",
               type_name,
               instance_name)
          .fatal();
      return;
    }

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
      int32_t     bits = 0;
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

  // Every annotated gate output, kept for the timing report (2opt-freq D).
  struct Arrival {
    float            delay;
    std::string      pin;
    hhds::Node_class node;
  };
  std::vector<Arrival> arrivals;
  hhds::Node_class     max_node;

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
        arrivals.push_back({delay, pin_name, node});

        if (delay > max_delay) {
          max_delay = delay;
          max_pin   = pin_name;
          max_node  = node;
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

  if (qor_path.empty()) {
    return;
  }

  // One JSON block per analyzed design: max delay + the worst endpoints,
  // source-attributed through the gates' srcid (agent edit targets).
  std::sort(arrivals.begin(), arrivals.end(), [](const Arrival& a, const Arrival& b) { return a.delay > b.delay; });
  constexpr size_t kMaxEndpoints = 10;

  std::string j = std::format("{{\"module\":\"{}\"", jesc(g->get_name()));
  if (!max_pin.empty()) {
    j += std::format(",\"max_delay\":{:.6g},\"critical_pin\":\"{}\"", max_delay, jesc(max_pin));
    if (auto src = src_of_node(g, max_node); !src.empty()) {
      j += std::format(",\"critical_src\":\"{}\"", jesc(src));
    }
  }
  j += ",\"endpoints\":[";
  for (size_t i = 0; i < arrivals.size() && i < kMaxEndpoints; ++i) {
    if (i != 0) {
      j += ",";
    }
    j += std::format("{{\"pin\":\"{}\",\"delay\":{:.6g}", jesc(arrivals[i].pin), arrivals[i].delay);
    if (auto src = src_of_node(g, arrivals[i].node); !src.empty()) {
      j += std::format(",\"src\":\"{}\"", jesc(src));
    }
    j += "}";
  }
  j += "]}";
  qor_blocks_.push_back(std::move(j));
}

void Pass_opentimer::write_qor() const {
  if (qor_path.empty()) {
    return;
  }
  std::string j = "{\"schema_version\":1,\"kind\":\"sta\",\"designs\":[";
  for (size_t i = 0; i < qor_blocks_.size(); ++i) {
    if (i != 0) {
      j += ",";
    }
    j += qor_blocks_[i];
  }
  j += "]}";
  std::ofstream ofs(qor_path, std::ios::binary | std::ios::trunc);
  if (!ofs) {
    livehd::diag::err("pass.opentimer", "qor-write", "io").msg("pass.opentimer: cannot write timing file '{}'", qor_path).fatal();
    return;
  }
  ofs << j << "\n";
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

      cap  *= static_cast<float>(freq * power_unit * 0.5 * voltage * voltage * cap_unit / timeunit);
      ipwr *= static_cast<float>(freq * power_unit * cap_unit / timeunit);

      total_cap  += cap;
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
