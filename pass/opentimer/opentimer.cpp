//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

#include <algorithm>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "cell.hpp"
#include "flatten.hpp"
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

// Net name of a driver pin. In flat mode it is the plain wire_name. In the
// whole-design (flattened) walk an internal pin's net is its MASTER NODE's
// dotted hier path plus the numeric port id — mirroring wire_name's structure
// but with a per-instance prefix. Keying on the master node + port id (not the
// pin's own get_hier_name) is essential: node.out_pins() yields a driver pin
// with no pin_name while inp_edges().driver yields the SAME pin carrying its
// port name, so the pin's own get_hier_name differs between the two (one omits
// the port suffix, one appends it) and the driver/consumer nets would not meet.
// The master node's get_hier_name is the stable, representation-independent id
// (the same primitive LEC keys flops on). Module-IO pins keep their decl name.
[[nodiscard]] std::string net_of(const hhds::Pin_class& dpin, bool hier) {
  if (!hier) {
    return wire_name(dpin);
  }
  if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
    return wire_name(dpin);  // module IO: the declared port name (root level)
  }
  auto master = dpin.get_master_node();
  auto base   = master.get_hier_name();
  auto pid    = dpin.get_port_id();
  return pid == 0 ? base : absl::StrCat(base, "_", static_cast<uint32_t>(pid));
}

// Same net name, but for a DRIVER pin obtained from the traversal node's
// out_pins()/create_driver_pin(): those pins do NOT carry the hier instance
// chain (dpin.get_master_node().get_hier_name() drops the prefix), whereas the
// traversal `owner` node does. A pin resolved through a hier edge (e.driver)
// keeps its chain, so consumers use net_of(); this owner-based form is the
// matching driver-side spelling. Both yield the same string for the same gate.
[[nodiscard]] std::string net_of_node(const hhds::Node_class& owner, const hhds::Pin_class& dpin, bool hier) {
  if (!hier) {
    return wire_name(dpin);
  }
  if (is_graph_input_pin(dpin) || is_graph_output_pin(dpin)) {
    return wire_name(dpin);
  }
  auto base = owner.get_hier_name();
  auto pid  = dpin.get_port_id();
  return pid == 0 ? base : absl::StrCat(base, "_", static_cast<uint32_t>(pid));
}

// Gate/instance name of a node: hier-unique get_hier_name when flattening, else
// the flat instance name.
[[nodiscard]] std::string inst_of(const hhds::Node_class& node, bool hier) {
  return hier ? std::string{node.get_hier_name()} : default_instance_name(node);
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
  // A flattened leaf lives in a child graph, so its srcid indexes that child's
  // source map, not the top's — resolve against the node's own graph.
  auto* ng   = n.get_graph();
  auto  span = (ng != nullptr ? ng : g.get())->source_locator().resolve_span(a.get());
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

  auto g = selected.front();

  // Whole-design timing (`hier=true`): structurally inline the instance
  // hierarchy into a scratch def (pass/partition's flatten_hierarchy) and time
  // THAT through the classic single-module path below. The legacy forward_hier
  // walk kept module boundaries and stitched nets across them by name — the
  // multi-bit pass.abc get_mask/set_mask bus packing was never stitched, so
  // real netlists drowned in "can't connect pin" errors. Erasing the
  // boundaries instead makes the boundary glue plain class-context rewiring,
  // exactly what the pin tracker already handles in every mapped region. The
  // walk stays reachable as `hier=stitch` for debugging/comparison. The
  // scratch def exists only in memory (pass opentimer never saves the input
  // library) and is deleted after the run; node names carry the dotted
  // instance path, so reports still read hierarchically.
  std::shared_ptr<hhds::Graph> scratch;
  hhds::GraphLibrary*          scratch_lib = nullptr;
  std::string                  scratch_name;
  if (pass.hier_setting_ == "true") {
    bool has_hier = false;
    for (auto n : g->forward_class()) {
      if (livehd::graph_util::is_type_sub(n) && n.get_subnode_graph() != nullptr) {
        has_hier = true;
        break;
      }
    }
    if (has_hier) {
      pass.report_module_ = std::string{g->get_name()};  // reports keep the real top name
      scratch_lib         = g->get_io()->get_library();
      scratch_name        = pass.report_module_ + "__ot_flat_tmp";
      scratch             = livehd::partition::flatten_hierarchy(g.get(), scratch_lib, scratch_name);
      if (!scratch) {
        return;  // diag already emitted
      }
      g = scratch;
    }
  }

  pass.setup_hier(g);
  // The hier edge walk (inp_edges/out_edges -> resolve_hier_driver) consults the
  // AMBIENT opaque scope, not the set passed to forward_hier — so the Liberty
  // cells must be opaque here too, or edge resolution descends into their empty
  // blackbox bodies and every gate-to-gate net silently disconnects. Set it once
  // around the whole build+analyze (nullptr in flat mode == descend-all default).
  const auto*             opq = pass.opaque_gids_.empty() ? nullptr : &pass.opaque_gids_;
  hhds::Hier_opaque_scope opaque_scope(opq);

  pass.build_circuit(g);
  pass.read_sdc_spef();
  pass.compute_timing(g);
  pass.populate_table(g);
  pass.write_qor();

  if (scratch) {
    scratch_lib->delete_graph(scratch);
    scratch = nullptr;
    scratch_lib->delete_graphio(scratch_name);
  }
}

// Legacy hier-walk mode selection. `hier=true` no longer lands here as a walk:
// time_work structurally flattens the design first (pass/partition
// flatten_hierarchy) and the flat single-module path times the scratch def —
// no cross-boundary stitching at all. This walk (forward_hier + name-keyed
// overwrite_hnet_ stitching) remains reachable as `hier=stitch` for
// debugging/comparison only: it chains combinational gate-to-gate paths across
// module boundaries but multi-bit buses crossing them (the pass.abc
// get_mask/set_mask packing glue) are not stitched — resolve_hier_driver
// returns those child module input ports as leaves, so a hierarchical top
// reports unconnected boundary nets.
void Pass_opentimer::setup_hier(const std::shared_ptr<hhds::Graph>& g) {
  const auto& lib     = timer.celllib(ot::MAX);
  auto        is_cell = [&](std::string_view name) { return lib && lib->cell(std::string{name}) != nullptr; };

  hier_mode_ = (hier_setting_ == "stitch");
  if (!hier_mode_) {
    return;
  }

  // Every instantiated Liberty cell in the hierarchy is a non-descend leaf; the
  // hier walk then yields those as gates and recurses only through design
  // modules. hier_range visits one instance per subnode at every depth.
  for (auto inst : g->hier_range()) {
    auto tg = inst.get_target_graph();
    if (tg && is_cell(tg->get_name())) {
      opaque_gids_.insert(inst.get_target_gid());
    }
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
  if (hier_mode_) {
    auto hn = net_of(dpin, true);
    auto it = overwrite_hnet_.find(hn);
    return it != overwrite_hnet_.end() ? it->second : hn;
  }
  auto it = overwrite_dpin2net.find(dpin.get_class_index());
  if (it != overwrite_dpin2net.end()) {
    return it->second;
  }
  return wire_name(dpin);
}

std::string Pass_opentimer::driver_net_of(const hhds::Node_class& owner, const hhds::Pin_class& dpin) const {
  if (hier_mode_) {
    auto key = net_of_node(owner, dpin, true);
    auto it  = overwrite_hnet_.find(key);
    return it != overwrite_hnet_.end() ? it->second : key;
  }
  auto it = overwrite_dpin2net.find(dpin.get_class_index());
  if (it != overwrite_dpin2net.end()) {
    return it->second;
  }
  return wire_name(dpin);
}

std::vector<hhds::Node_class> Pass_opentimer::leaf_nodes(const std::shared_ptr<hhds::Graph>& g) const {
  std::vector<hhds::Node_class> v;
  if (hier_mode_) {
    const auto* opq = opaque_gids_.empty() ? nullptr : &opaque_gids_;
    for (auto n : g->forward_hier(true, false, opq)) {
      v.push_back(n);
    }
  } else {
    for (auto n : g->fast_class()) {
      v.push_back(n);
    }
  }
  return v;
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
    if (b != 0 || !is_graph_input_pin(dpin)) {
      return b;
    }
    auto n = dpin.get_pin_name();
    if (n.empty()) {
      return b;
    }
    // Resolve the declared width against the pin's OWN graph IO: a flattened
    // leaf input can be a child-module port, not a top port, so the top gio
    // would not declare it (get_bits asserts on an unknown name).
    auto pio = gio;
    if (hier_mode_) {
      auto* pg = dpin.get_master_node().get_graph();
      pio      = (pg != nullptr) ? pg->get_io() : nullptr;
    }
    if (!pio) {
      return b;
    }
    return bits_of(dpin, *pio, n);
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
      return net_of(pin, hier_mode_);
    }
    auto master = pin.get_master_node();
    if (!master.is_invalid() && Ntype::is_pin_trackable(type_op_of(master))) {
      return absl::StrCat("n$", net_of(pin, hier_mode_));
    }
    return net_of(pin, hier_mode_);
  };

  // Record a driver pin -> net-name override. Keyed by the hier-unique net name
  // when flattening (Class_index collides across instances), by Class_index in
  // flat mode (byte-for-byte the original behaviour).
  // key_net is the driver's net name computed with the correct hier context by
  // the caller (net_of_node for a traversal-node driver, net_of for a resolved
  // edge driver); the flat map keys on the pin's Class_index as before.
  auto set_overwrite = [&](const std::string& key_net, const hhds::Pin_class& dpin, const std::string& netname) {
    if (hier_mode_) {
      overwrite_hnet_.insert_or_assign(key_net, netname);
    } else {
      overwrite_dpin2net.insert_or_assign(dpin.get_class_index(), netname);
    }
  };
  auto is_overwritten = [&](const std::string& key_net, const hhds::Pin_class& dpin) -> bool {
    return hier_mode_ ? overwrite_hnet_.contains(key_net) : overwrite_dpin2net.contains(dpin.get_class_index());
  };

  // Driver feeding a named sink, HIER-resolved. get_driver_of_sink_name reads the
  // pin-level inp_edges (Graph::inp_edges(Pin_class)), which is LOCAL-only — it
  // never crosses a module boundary — so the tracker would build on a child
  // module's own input port (a bare "io_x") instead of the parent's driver.
  // node.inp_edges() (the Node overload) is the hier-resolving one, so route the
  // tracker's operand lookups through it when flattening.
  auto hier_driver_of = [&](const hhds::Node_class& n, std::string_view sname) -> hhds::Pin_class {
    if (!hier_mode_) {
      return get_driver_of_sink_name(n, sname);
    }
    for (auto& e : n.inp_edges()) {
      if (sink_pin_name_of(n, e.sink) == sname) {
        return e.driver;
      }
    }
    return {};
  };

  // The node set for the forward (net-population) walk. Flat mode iterates the
  // def's own forward_class; the whole-design walk iterates forward_hier
  // (descending design modules, yielding Liberty-cell leaves via opaque_gids_)
  // and is snapshot into a vector first: processing materializes port-0 driver
  // pins on shared child bodies, which must not mutate a live hier iterator.
  auto forward_nodes = [&]() {
    std::vector<hhds::Node_class> v;
    if (hier_mode_) {
      const auto* opq = opaque_gids_.empty() ? nullptr : &opaque_gids_;
      for (auto n : g->forward_hier(true, false, opq)) {
        v.push_back(n);
      }
    } else {
      for (auto n : g->forward_class()) {
        v.push_back(n);
      }
    }
    return v;
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
      // (mirrors the original code's behaviour). driver_dpin is a resolved edge
      // driver (its master carries the hier chain), so net_of is the right key.
      // A feed-through (output driven straight by a graph INPUT — the flatten
      // as-top shape) must NOT overwrite: it would rename the input's net onto
      // the PO net, silently un-timing every gate cone fed by that input (the
      // PI arrival lands on the original net). The feed-through PO itself has
      // no combinational content to time. Consts likewise never carry a net.
      if (!is_graph_input_pin(driver_dpin) && !is_const_pin(driver_dpin)) {
        set_overwrite(net_of(driver_dpin, hier_mode_), driver_dpin, driver_name);
      }
    }
  }

  // 3rd: populate all the net names (forward walk, pin-tracker for trackable ops).
  for (auto& node : forward_nodes()) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Nconst || op == Ntype_op::AttrSet) {
      continue;
    }

    bool root_track = Ntype::is_pin_trackable(op);
    if (root_track) {
      auto dpin0 = node.create_driver_pin(0);
      // This trackable node's OWN output: name it from the traversal node (its
      // hier chain is intact; a create_driver_pin/out_pins handle drops it). The
      // "n$" prefix keeps the pure-rewiring output out of the real-net space.
      auto wname = hier_mode_ ? absl::StrCat("n$", net_of_node(node, dpin0, true)) : trk_id(dpin0);
      if (op == Ntype_op::Set_mask) {
        auto a_dpin     = hier_driver_of(node, "a");
        auto mask_dpin  = hier_driver_of(node, "mask");
        auto value_dpin = hier_driver_of(node, "value");
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
        auto a_dpin    = hier_driver_of(node, "a");
        auto mask_dpin = hier_driver_of(node, "mask");
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
        auto a_dpin = hier_driver_of(node, "a");
        auto b_dpin = hier_driver_of(node, "b");
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
        auto a_dpin = hier_driver_of(node, "a");
        auto b_dpin = hier_driver_of(node, "b");
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
        auto a_dpin = hier_driver_of(node, "a");
        if (a_dpin.is_invalid()) {
          livehd::diag::err("pass.opentimer", "netlist-malformed", "internal")
              .msg("Invalid corrupt SHL node {} (cprop should have deleted it)", debug_name(node))
              .fatal();
          return;
        }
        // SHL b is single-driver (the one-hot multi-shift form was removed).
        auto b_dpin = hier_driver_of(node, "b");
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
      // Driver-side net name comes from the traversal node (hier chain intact).
      auto dnet  = net_of_node(node, dpin, hier_mode_);
      auto wname = root_track ? (hier_mode_ ? absl::StrCat("n$", dnet) : trk_id(dpin)) : dnet;

      if (root_track) {
        const auto& pv = pin_tracker.get_pin_vector(wname);

        if (pv.size() == 1) {  // single bit tracking result
          if (pv[0].pos < 0) {
            continue;  // no connection
          }
          if (pv[0].pos) {
            auto bus_bit_name = absl::StrCat(pv[0].id, ".", str_tools::to_s(pv[0].pos));
            set_overwrite(dnet, dpin, bus_bit_name);
          } else {
            set_overwrite(dnet, dpin, pv[0].id);
          }
        } else if (pv.size() > 1 && pv[0].pos >= 0 && !is_overwritten(dnet, dpin)) {
          // MULTI-bit tracker result: a module-boundary packed bus (pass.abc
          // glue). bit 0 is the real signal, higher bits are const padding for a
          // wide port, so a 1-bit cell reading this driver reads bit 0 — map it
          // to pv[0]. A consumer that reads a specific higher bit goes through a
          // Get_mask, which resolves inside the tracker (not via this overwrite);
          // the is_overwritten guard keeps a phase-2 primary-output net intact.
          if (pv[0].pos) {
            set_overwrite(dnet, dpin, absl::StrCat(pv[0].id, ".", str_tools::to_s(pv[0].pos)));
          } else {
            set_overwrite(dnet, dpin, pv[0].id);
          }
        }
      } else {
        timer.insert_net(wname);
      }
    }
  }

  // 4th: populate the cells (Sub instances). Whole-design mode iterates the
  // flattened leaf set (forward_hier descends design modules; Liberty-cell
  // leaves are yielded via opaque_gids_), so gates from every instance land in
  // the single ot::Timer under their hier-unique names.
  for (auto& node : leaf_nodes(g)) {
    auto op = type_op_of(node);
    if (op == Ntype_op::Nconst || op == Ntype_op::AttrSet) {
      continue;
    }
    if (Ntype::is_pin_trackable(op)) {
      continue;
    }
    if (op == Ntype_op::Flop || op == Ntype_op::Memory) {
      // Path boundary, not a cell (2opt-freq D): pass.abc keeps flops/memories
      // native — the Liberty stays combinational. Each consumed output (a flop Q,
      // a memory read-data port) becomes a virtual primary input arriving at 0, so
      // flop-to-flop segments are scored; the din/en/addr cones end at their
      // driving gate pins, which compute_timing already reads. Clock/reset nets
      // are not timed (no clock tree in this estimate).
      //
      // out_pins() does NOT materialize these outputs — a flop Q is an implicit
      // port-0 pin, and a MEMORY exposes each read-data port only on its
      // consuming edges (out_pins() is empty). Collect the actually-driven output
      // pins from out_edges (deduped by pin) so every read port becomes a net.
      std::vector<hhds::Pin_class>           bpins;
      absl::flat_hash_set<hhds::Class_index> seen;
      for (const auto e : node.out_edges()) {
        if (!e.driver.is_invalid() && seen.insert(e.driver.get_class_index()).second) {
          bpins.push_back(e.driver);
        }
      }
      if (bpins.empty()) {  // no consuming edge (a dead flop): fall back to port 0
        auto dpin0 = node.create_driver_pin(0);
        if (!dpin0.is_invalid()) {
          bpins.push_back(dpin0);
        }
      }
      for (auto& dpin : bpins) {
        if (dpin.is_invalid() || dpin.out_edges().empty()) {
          continue;
        }
        auto dnet = net_of_node(node, dpin, hier_mode_);  // flop/mem output (driver side)
        if (is_overwritten(dnet, dpin)) {
          continue;  // drives a primary output directly: already a PO net
        }
        auto        wname = dnet;
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

    auto instance_name = inst_of(node, hier_mode_);
    auto type_name     = sub_type_name(node);

    // ABC's builtin tie cells (emitted when the Liberty has no constant
    // cells): a constant never transitions, so it contributes no arrival —
    // leave its output net driverless and skip the gate.
    if (type_name == "_const0_" || type_name == "_const1_") {
      continue;
    }

    // A Sub that is not a Liberty cell. In whole-design mode a design module is
    // descended into by the hier walk (never yielded here); reaching this with
    // a body means a black-box the flatten cannot enter — skip it defensively
    // (its boundary nets simply stay unscored). ot::Timer::insert_gate would
    // otherwise log-and-skip, yielding silent garbage. The celllib is loaded.
    const auto& lib = timer.celllib(ot::MAX);
    if (!lib || lib->cell(type_name) == nullptr) {
      if (hier_setting_ != "false") {
        if (node.get_subnode_graph() != nullptr) {
          continue;  // design-module body already flattened in via the hier walk
        }
        livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
            .msg("whole-design timing hit black-box '{}' (instance {}): no Liberty cell and no descendable body to flatten",
                 type_name,
                 instance_name)
            .fatal();
        return;
      }
      livehd::diag::err("pass.opentimer", "netlist-unsupported", "unsupported")
          .msg("module instantiates '{}' (instance {}), which is not a cell in the Liberty library — hier=false times one "
               "tech-mapped module per run. Pass --top of a mapped region (<mod>__c<N>), or drop hier=false: the default "
               "(pass.opentimer.hier=true) flattens the whole design across the instance hierarchy",
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
      auto wire     = driver_net_of(node, dpin);
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

  // OT gate/instance name -> node, to source-attribute the path points below.
  absl::flat_hash_map<std::string, hhds::Node_class> inst2node;

  for (auto& node : leaf_nodes(g)) {
    auto op = type_op_of(node);
    if (op != Ntype_op::Sub) {
      continue;
    }

    auto instance_name = inst_of(node, hier_mode_);
    inst2node.emplace(instance_name, node);

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

  // Critical-path trace for the report: one point per pin, source -> critical
  // pin/endpoint. report_timing ranks slack over ENDPOINTS, which exist only
  // at RAT'd primary outputs (flops stay native, so no Liberty timing tests)
  // — its path is used only when it actually reaches the critical arrival;
  // otherwise (a flop-din cone) reconstruct by walking max-arrival fanin
  // backward from the critical pin. Best-effort: an empty path still leaves a
  // renderable summary + endpoints table.
  struct Path_point {
    std::string     pin;
    float           at;
    bool            rise;
    const ot::Gate* gate;  // nullptr for ports / flop-boundary virtual PIs
  };
  std::vector<Path_point> path_pts;
  if (!max_pin.empty()) {
    auto paths = timer.report_timing(1, ot::MAX);
    if (!paths.empty() && !paths.front().empty() && paths.front().back().at >= max_delay - 1e-4F * std::max(1.0F, max_delay)) {
      for (const auto& p : paths.front()) {
        path_pts.push_back({p.pin.name(), p.at, p.transition == ot::RISE, p.pin.gate()});
      }
    }
  }
  if (path_pts.empty() && !max_pin.empty()) {
    constexpr float kNinf      = -std::numeric_limits<float>::infinity();
    // Worst MAX-corner arrival of a pin over both edges: {at, is_rise}.
    auto            arrival_of = [](const ot::Pin& p) -> std::pair<float, bool> {
      auto  af = p.at(ot::MAX, ot::FALL);
      auto  ar = p.at(ot::MAX, ot::RISE);
      float f  = af ? *af : kNinf;
      float r  = ar ? *ar : kNinf;
      return r >= f ? std::pair{r, true} : std::pair{f, false};
    };
    // net -> its driver (rct-root) pin, for the backward net hop of the walk.
    absl::flat_hash_map<const ot::Net*, const ot::Pin*> net_driver;
    for (const auto& [pname, p] : pins) {
      if (p.net() != nullptr && p.is_rct_root()) {
        net_driver.emplace(p.net(), &p);
      }
    }

    std::vector<Path_point>             rev;
    absl::flat_hash_set<const ot::Pin*> seen;  // a comb loop must not cycle the walk
    auto                                pit = pins.find(max_pin);
    const ot::Pin*                      cur = pit != pins.end() ? &pit->second : nullptr;
    while (cur != nullptr && seen.insert(cur).second) {
      auto [at, rise] = arrival_of(*cur);
      rev.push_back({cur->name(), at == kNinf ? 0.0F : at, rise, cur->gate()});
      if (cur->primary_input() != nullptr) {
        break;  // module input or flop/memory virtual PI: the path source
      }
      const ot::Pin* next = nullptr;
      if (cur->is_input()) {  // gate input: hop the net to its driver
        auto nd = net_driver.find(cur->net());
        if (nd != net_driver.end() && nd->second != cur) {
          next = nd->second;
        }
      } else if (const auto* gate = cur->gate(); gate != nullptr) {
        // gate output: greedy max-arrival input pin of the same gate. A
        // backward walk must be time-monotonic: an input arriving LATER than
        // this output has no timing arc to it (a sequential cell's D vs Q —
        // greedily picking D would splice the previous cycle's path in front
        // of this one, with the Time column running backward). Bound the pick
        // by the current arrival (+ float noise); at a DFF this follows the
        // clock pin (the launch path) or ends the walk.
        const float bound = at + 1e-4F * std::max(1.0F, std::abs(at));
        float       best  = kNinf;
        for (const ot::Pin* ip : gate->pins()) {
          if (ip == nullptr || !ip->is_input()) {
            continue;
          }
          if (auto [ia, ir] = arrival_of(*ip); ia > best && ia <= bound) {
            best = ia;
            next = ip;
          }
        }
      }
      cur = next;  // nullptr (untimed fanin: a tie cell / clock) ends the walk
    }
    path_pts.assign(rev.rbegin(), rev.rend());
  }

  // One JSON block per analyzed design: max delay + the critical-path points
  // + the worst endpoints, source-attributed through the gates' srcid (agent
  // edit targets).
  std::sort(arrivals.begin(), arrivals.end(), [](const Arrival& a, const Arrival& b) { return a.delay > b.delay; });
  constexpr size_t kMaxEndpoints = 10;

  std::string j = std::format("{{\"module\":\"{}\"", jesc(report_module_.empty() ? std::string{g->get_name()} : report_module_));
  if (!max_pin.empty()) {
    j += std::format(",\"max_delay\":{:.6g},\"critical_pin\":\"{}\"", max_delay, jesc(max_pin));
    if (auto src = src_of_node(g, max_node); !src.empty()) {
      j += std::format(",\"critical_src\":\"{}\"", jesc(src));
    }
  }
  j             += ",\"path\":[";
  float prev_at  = 0.0F;
  for (size_t i = 0; i < path_pts.size(); ++i) {
    const auto& p = path_pts[i];
    if (i != 0) {
      j += ",";
    }
    j += std::format("{{\"pin\":\"{}\",\"at\":{:.6g},\"delay\":{:.6g},\"dir\":\"{}\"",
                     jesc(p.pin),
                     p.at,
                     p.at - prev_at,
                     p.rise ? "rise" : "fall");
    if (p.gate != nullptr) {
      j += std::format(",\"cell\":\"{}\"", jesc(p.gate->cell_name()));
      if (auto it = inst2node.find(p.gate->name()); it != inst2node.end()) {
        if (auto src = src_of_node(g, it->second); !src.empty()) {
          j += std::format(",\"src\":\"{}\"", jesc(src));
        }
      }
    }
    j       += "}";
    prev_at  = p.at;
  }
  j += "],\"endpoints\":[";
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
  // The Liberty time unit as a label (arrivals are plain numbers in this
  // unit), so the pretty renderer can print real units. Omitted when the
  // library declares none / an uncommon one.
  std::string tu;
  if (auto u = timer.time_unit(); u) {
    auto         near = [](double a, double b) { return a > b * 0.999 && a < b * 1.001; };
    const double v    = u->value();
    if (near(v, 1e-6)) {
      tu = "us";
    } else if (near(v, 1e-9)) {
      tu = "ns";
    } else if (near(v, 1e-12)) {
      tu = "ps";
    }
  }
  std::string j = "{\"schema_version\":1,\"kind\":\"sta\",";
  if (!tu.empty()) {
    j += std::format("\"time_unit\":\"{}\",", tu);
  }
  j += "\"designs\":[";
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
