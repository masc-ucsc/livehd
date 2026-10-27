// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "reset_tree.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::placement {
namespace {
namespace gu = graph_util;
using Node   = hhds::Node_class;
using Pin    = hhds::Pin_class;

// The smallest identity buffer the LEF can place (area, then name: deterministic).
std::optional<liberty::Comb_cell> smallest_buffer(const std::string& liberty, const physical::Technology& tech) {
  std::optional<liberty::Comb_cell> best;
  for (auto& cell : liberty::scan_comb_cells(liberty)) {
    if (cell.physical_barrier || cell.inputs.size() != 1 || liberty::cell_truth(cell) != uint64_t{0xaaaaaaaaaaaaaaaa}) {
      continue;
    }
    const auto macro = tech.macros.find(cell.name);
    if (macro == tech.macros.end() || !macro->second.klass.starts_with("CORE") || !macro->second.pins.contains(cell.output)
        || !macro->second.pins.contains(cell.inputs.front())) {
      continue;
    }
    if (!best || cell.area < best->area || (cell.area == best->area && cell.name < best->name)) {
      best = std::move(cell);
    }
  }
  return best;
}

std::shared_ptr<hhds::GraphIO> buffer_io(hhds::Graph& graph, const liberty::Comb_cell& b) {
  auto* lib = graph.get_io()->get_library();
  if (const auto io = lib->find_io(b.name)) {
    return io;
  }
  auto io = lib->create_io(b.name);
  io->add_input(b.inputs.front(), 0);
  io->set_bits(b.inputs.front(), 1);
  io->add_output(b.output, 0);
  io->set_bits(b.output, 1);
  return io;
}

// A placeable library cell: a Sub with a LEF master of the CORE class.
bool is_core_cell(Node n, const physical::Technology& tech) {
  const auto io = n.get_subnode_io();
  if (!io) {
    return false;
  }
  const auto macro = tech.macros.find(std::string(io->get_name()));
  return macro != tech.macros.end() && macro->second.klass.starts_with("CORE");
}

// Every driver of a trigger pin (and, with `roles`, every driver carrying the synthesis reset role) in
// graph order (the minted names follow it), then the tree under each one with more than `cap` loads.
Reset_tree_stats buffer_nets(hhds::Graph& graph, const physical::Technology& tech, const std::string& liberty, unsigned cap,
                             const Constant_test& is_constant, const Reset_pins& trigger_pins, bool roles, std::string_view tag) {
  Reset_tree_stats stats;
  if (cap < 2 || (trigger_pins.empty() && !roles)) {
    return stats;
  }
  const auto buffer = smallest_buffer(liberty, tech);
  if (!buffer) {
    return stats;
  }
  const auto& minted = tech.macros.at(buffer->name);
  stats.buffer_um2   = minted.width * minted.height;

  std::vector<Pin>                        drivers;
  std::set<std::pair<uint64_t, uint64_t>> seen;
  const auto                              add_driver = [&](const Pin& driver, bool count_skip) {
    if (driver.is_invalid() || driver.is_const()) {
      return;
    }
    if (gu::bits_of(driver) != 1) {
      stats.skipped += count_skip;  // a packed-bus alias: not a net this pass can move pin by pin
      return;
    }
    if (is_constant && is_constant(driver)) {
      return;  // the tied inactive pin of a clear+preset flop
    }
    // A second place() over a placed graph sees that tie as the cell it became: an input-less library cell is a
    // constant source, and a tree behind it would only add cells.
    if (const auto driver_io = driver.get_master_node().get_subnode_io(); driver_io && driver_io->get_input_pin_decls().empty()) {
      return;
    }
    if (seen.insert({driver.get_master_node().get_debug_nid(), driver.get_port_id()}).second) {
      drivers.push_back(driver);
    }
  };
  for (auto n : graph.body().nodes()) {
    const auto io = n.get_subnode_io();
    if (!io) {
      continue;
    }
    const auto it = trigger_pins.find(std::string(io->get_name()));
    if (it == trigger_pins.end()) {
      continue;
    }
    for (const auto& name : it->second) {
      const auto pin = n.get_sink_pin(name);
      if (pin.is_invalid()) {
        continue;
      }
      const auto drv = pin.get_driver_pins();
      if (drv.empty() || drv.front().is_invalid() || drv.front().is_const()) {
        continue;
      }
      if (drv.size() != 1) {
        ++stats.skipped;  // a multi-driver bus alias
        continue;
      }
      add_driver(drv.front(), true);
    }
  }
  if (roles) {
    // A reset net that reaches no reset pin: a synchronous reset folded into data logic keeps the
    // synthesis reset role on its driver (collect_control_roles reads the same metadata).
    const auto role_drivers = [&](const hhds::Node_class& node) {
      for (auto pin : node.out_sorted_pins()) {
        const auto r = gu::signal_roles_of(pin);
        if ((r & attrs::signal_reset) && !(r & attrs::signal_clock)) {
          add_driver(pin, false);  // a multi-bit bus alias is not the net; its 1-bit source is
        }
      }
    };
    role_drivers(graph.get_input_node());
    for (auto n : graph.body().nodes()) {
      role_drivers(n);
    }
  }

  const auto            prefix = std::format("__place_{}", tag);
  const auto            io     = buffer_io(graph, *buffer);
  std::set<std::string> names;  // a rerun over a graph that already holds trees must not reuse a minted name
  bool                  names_read = false;
  unsigned              serial     = 0;
  for (const auto& driver : drivers) {
    // The cell pins this net feeds. A graph output, a bus-wiring alias or a macro keeps its direct
    // connection to the driver; only a pin whose single driver is this net can move.
    std::vector<Pin> loads;
    size_t           total = 0;
    for (const auto& edge : driver.out_edges()) {
      ++total;
      const auto sink = edge.sink;
      if (is_core_cell(sink.get_master_node(), tech) && sink.get_driver_pins().size() == 1) {
        loads.push_back(sink);
      }
    }
    if (loads.size() <= cap || total <= cap) {
      continue;
    }
    {
      std::vector<std::pair<std::pair<std::string, uint64_t>, Pin>> keyed;
      keyed.reserve(loads.size());
      for (const auto& load : loads) {
        keyed.push_back({{std::string(load.get_master_node().get_name()), load.get_port_id()}, load});
      }
      std::stable_sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
      for (size_t i = 0; i < keyed.size(); ++i) {
        loads[i] = keyed[i].second;
      }
    }
    if (!names_read) {
      names_read = true;
      for (auto n : graph.body().nodes()) {
        if (const auto name = std::string(n.get_name()); name.find(prefix) != std::string::npos) {
          names.insert(name);
        }
      }
    }
    ++stats.nets;
    stats.sinks += static_cast<unsigned>(loads.size());
    unsigned level = 0;
    for (auto& load : loads) {
      load.del_sink(driver);
    }
    while (loads.size() > cap) {
      std::vector<Pin> next;
      for (size_t lo = 0; lo < loads.size(); lo += cap) {
        const size_t hi    = std::min(loads.size(), lo + cap);
        const auto   owner = loads[lo].get_master_node();
        auto         cell  = gu::create_typed_node(graph, Ntype_op::Sub);
        cell.set_subnode(io);
        // The buffer lives in its first load's hierarchy, so the placer's block of that load fits it.
        const auto  load_name = std::string(owner.get_name());
        const auto  dot       = load_name.rfind('.');
        std::string name;
        do {
          name = std::format("{}{}{}_{}",
                             dot == std::string::npos ? std::string{} : load_name.substr(0, dot + 1),
                             prefix,
                             level,
                             serial++);
        } while (!names.insert(name).second);
        ++stats.buffers;
        cell.set_name(name);
        cell.attr(attrs::color).set(gu::node_color_of(owner));
        if (const auto group = owner.attr(attrs::synth_color_group); group.has()) {
          cell.attr(attrs::synth_color_group).set(std::string(group.get()));
        }
        if (const auto block = owner.attr(attrs::placement_block); block.has()) {
          cell.attr(attrs::placement_block).set(std::string(block.get()));
        }
        if (const auto region = owner.attr(attrs::synth_region_id); region.has() && region.get()) {
          cell.attr(attrs::synth_region_id).set(region.get());
        }
        auto out = cell.create_driver_pin(buffer->output);
        gu::set_bits(out, 1);
        for (size_t i = lo; i < hi; ++i) {
          loads[i].connect_driver(out);
        }
        next.push_back(cell.create_sink_pin(buffer->inputs.front()));
      }
      loads = std::move(next);
      ++level;
    }
    for (auto& load : loads) {
      load.connect_driver(driver);
    }
    stats.levels = std::max(stats.levels, level);
  }
  return stats;
}
}  // namespace

Reset_tree_stats buffer_reset_nets(hhds::Graph& graph, const physical::Technology& tech, const std::string& liberty, unsigned cap,
                                   const Constant_test& is_constant, const Reset_pins& extra_reset_pins) {
  if (cap < 2) {
    return {};
  }
  Reset_pins reset_pins = extra_reset_pins;
  for (const auto& cell : liberty::scan_async_dff_cells(liberty)) {
    auto& pins = reset_pins[cell.name];
    for (const auto* pin : {&cell.reset0_pin, &cell.reset1_pin}) {
      if (!pin->empty() && std::find(pins.begin(), pins.end(), *pin) == pins.end()) {
        pins.push_back(*pin);
      }
    }
  }
  return buffer_nets(graph, tech, liberty, cap, is_constant, reset_pins, true, "rsttree");
}

Reset_tree_stats buffer_pin_nets(hhds::Graph& graph, const physical::Technology& tech, const std::string& liberty, unsigned cap,
                                 const Reset_pins& pins, std::string_view tag, const Constant_test& is_constant) {
  return buffer_nets(graph, tech, liberty, cap, is_constant, pins, false, tag);
}
}  // namespace livehd::placement
