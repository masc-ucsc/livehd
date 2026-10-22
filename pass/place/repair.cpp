// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "repair.hpp"

#include <fnmatch.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/partition/flatten.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "repair_internal.hpp"
#include "steiner_repair.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
namespace detail {
namespace gu = graph_util;
namespace fs = std::filesystem;
struct Edit {
  std::string kind, node, master;
  unsigned    fanout  = 4;
  double      gain_ps = 0;
  std::string driver{};
};
const Buffer* combinational(Node n, const Inventory& cells) {
  const auto io = n.get_subnode_io();
  if (!io) {
    return nullptr;
  }
  const auto it = cells.find(std::string(io->get_name()));
  return it == cells.end() ? nullptr : &it->second;
}
bool identity(const Buffer& cell) { return cell.inputs.size() == 1 && liberty::cell_truth(cell) == uint64_t{0xaaaaaaaaaaaaaaaa}; }
bool matches_dont_use(const std::string& name, const Options& options) {
  return std::any_of(options.resize_dont_use.begin(), options.resize_dont_use.end(), [&](const auto& pattern) {
    return fnmatch(pattern.c_str(), name.c_str(), 0) == 0;
  });
}
const Buffer* buffer(Node n, const Inventory& cells) {
  const auto* cell = combinational(n, cells);
  return cell && identity(*cell) ? cell : nullptr;
}
bool fixed(Node n) {
  const auto p = n.attr(attrs::place);
  return p.has() && (p.get().status == Ann_place::Status::FIXED || p.get().status == Ann_place::Status::LOCKED);
}
Pin              input(Node n, const Buffer& cell) { return n.get_sink_pin(cell.inputs.front()).get_driver_pin(); }
Pin              output(Node n, const Buffer& cell) { return n.get_driver_pin(cell.output); }
std::vector<Pin> sinks(Pin driver) {
  std::vector<Pin> result;
  for (const auto& e : driver.out_edges()) {
    result.push_back(e.sink);
  }
  return result;
}
Inventory inventory(const Options& options, const physical::Technology& tech) {
  if (options.repair_reset || options.reset_fanout) {
    throw std::invalid_argument("pre-CTS clock/reset sizing and buffering are disabled");
  }
  Inventory result;
  for (auto cell : liberty::scan_comb_cells(options.liberty)) {
    if (cell.physical_barrier || !tech.macros.contains(cell.name) || !liberty::cell_truth(cell)) {
      continue;
    }
    const auto& macro = tech.macros.at(cell.name);
    if (!macro.klass.starts_with("CORE") || !macro.pins.contains(cell.output)
        || std::any_of(cell.inputs.begin(), cell.inputs.end(), [&](const auto& pin) { return !macro.pins.contains(pin); })) {
      continue;
    }
    result.emplace(cell.name, std::move(cell));
  }
  return result;
}
// Protect all combinational logic upstream of clock/reset sinks, including a whole tree
// when it branches into both a protected net and a data net. Conservative name
// matching covers uncharacterized macros and primary clocks too. Pre-CTS
// callers always protect reset cones; the old repair-reset override is rejected.
// Liberty parses are expensive and independent of the graph: floorplan repair scans once.
Special_pins scan_special_pins(const std::string& libs, bool protect_reset) {
  Special_pins special;
  auto         state = liberty::scan_dff_cells(libs);
  const auto   async = liberty::scan_async_dff_cells(libs);
  state.insert(state.end(), async.begin(), async.end());
  const auto latch = liberty::scan_latch_cells(libs);
  state.insert(state.end(), latch.begin(), latch.end());
  for (const auto& c : state) {
    special[c.name] = {c.clk_pin};
    if (protect_reset) {
      special[c.name].insert({c.reset0_pin, c.reset1_pin});
    }
  }
  for (const auto& c : liberty::scan_icg_cells(libs)) {
    special[c.name] = {c.clk_pin, c.en_pin, c.test_pin};
  }
  for (const auto& cell : liberty::scan_memory_cells(libs)) {
    for (const auto& pin : cell.pins) {
      if (pin.clock_input) {
        special[cell.name].insert(pin.name);
      }
    }
  }
  return special;
}
std::set<std::string> protected_buffers(hhds::Graph& graph, const Inventory& cells, const Special_pins& special, bool protect_reset,
                                        const std::string& control_ports) {
  std::set<std::string> protected_names;
  const auto            roots = prects_library("", control_ports);
  for (const auto& port : roots.ports) {
    protected_names.insert("@port:" + port);
  }
  std::set<uint64_t>    visited;
  const auto            protect = [&](auto&& self, Pin driver) -> void {
    if (driver.is_invalid() || driver.is_const()) {
      return;
    }
    auto n = driver.get_master_node();
    if (n.is_input_node()) {
      protected_names.emplace("@port:" + std::string(driver.get_pin_name()));
      return;
    }
    const auto* b = combinational(n, cells);
    if (!visited.insert(n.get_debug_nid()).second) {
      return;
    }
    protected_names.emplace(n.get_name());
    const auto op     = gu::type_op_of(n);
    const bool wiring = !n.get_subnode_io() && op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::Memory;
    if (b || wiring) {
      for (auto sink : n.inp_sorted_pins()) {
        for (auto upstream : sink.get_driver_pins()) {
          self(self, upstream);
        }
      }
    }
  };
  if (!roots.override_controls) {
    for (auto driver : graph.get_input_node().out_sorted_pins()) {
      const auto mask = attrs::signal_clock | (protect_reset ? attrs::signal_reset : 0);
      if (gu::signal_roles_of(driver) & mask) {
        protect(protect, driver);
      }
    }
  }
  for (auto n : graph.body().nodes()) {
    const auto io = n.get_subnode_io();
    if (!roots.override_controls) {
      for (auto driver : n.out_sorted_pins()) {
        const auto mask = attrs::signal_clock | (protect_reset ? attrs::signal_reset : 0);
        if (gu::signal_roles_of(driver) & mask) {
          protect(protect, driver);
        }
      }
    }
    for (auto sink : n.inp_sorted_pins()) {
      std::string name(sink.get_pin_name());
      if (io) {
        for (const auto& decl : io->get_input_pin_decls()) {
          if (decl.port_id == sink.get_port_id()) {
            name = decl.name;
          }
        }
      }
      const auto op  = gu::type_op_of(n);
      const auto off = sink.get_port_id() % Ntype::Memory_port_stride;
      if (!roots.override_controls
          && ((op == Ntype_op::Memory && (off == 2 || off == 14))
              || ((op == Ntype_op::Flop || op == Ntype_op::Latch)
                  && (Ntype::get_sink_name(op, sink.get_port_id()) == "clock_pin"
                      || (protect_reset && Ntype::get_sink_name(op, sink.get_port_id()) == "reset_pin")))
              || (io && special.contains(std::string(io->get_name())) && special.at(std::string(io->get_name())).contains(name)))) {
        for (auto driver : sink.get_driver_pins()) {
          protect(protect, driver);
        }
      }
    }
  }
  // A control tree can branch into a buffered data-looking sink. Protect all
  // unary distribution branches, not just the ancestry of a recognized leaf.
  std::unordered_map<std::string, std::vector<std::string>> branches;
  for (auto n : graph.body().nodes()) {
    const auto* cell = combinational(n, cells);
    if (!cell || cell->inputs.size() != 1) {
      continue;
    }
    const auto truth = liberty::cell_truth(*cell);
    if (!truth || (*truth != 0xaaaaaaaaaaaaaaaaULL && *truth != 0x5555555555555555ULL)) {
      continue;
    }
    for (auto driver : n.get_sink_pin(cell->inputs.front()).get_driver_pins()) {
      const auto from = driver.get_master_node();
      const auto key  = from.is_input_node() ? "@port:" + std::string(driver.get_pin_name()) : std::string(from.get_name());
      branches[key].emplace_back(n.get_name());
    }
  }
  std::vector<std::string> pending(protected_names.begin(), protected_names.end());
  for (size_t i = 0; i < pending.size(); ++i) {
    if (auto found = branches.find(pending[i]); found != branches.end()) {
      for (const auto& name : found->second) {
        if (protected_names.insert(name).second) {
          pending.push_back(name);
        }
      }
    }
  }
  return protected_names;
}
std::set<std::string> protected_buffers(hhds::Graph& graph, const Inventory& cells, const std::string& libs, bool protect_reset,
                                        const std::string& control_ports) {
  auto special = scan_special_pins(libs, protect_reset);
  return protected_buffers(graph, cells, special, protect_reset, control_ports);
}
std::pair<double, double> wire_rc(const Options& options, const physical::Technology& tech) {
  const auto rc
      = technology_wire_rc(tech, options.liberty, options.tech_rc_file, options.resistance_ohm_um, options.capacitance_ff_um);
  return {std::max(rc.horizontal_r, rc.vertical_r), std::max(rc.horizontal_c, rc.vertical_c)};
}
double wire_limit(const Buffer& b, double r, double c) {
  if (!(r > 0 && c > 0 && b.intrinsic_ps > 0)) {
    return std::numeric_limits<double>::infinity();
  }
  const double cap       = b.input_capacitance_ff.empty() ? 0 : b.input_capacitance_ff.front();
  const double intrinsic = b.intrinsic_ps + b.resistance_ohm * cap * 1e-3;
  double       length    = std::sqrt(2 * intrinsic / (r * c * 1e-3));
  if (b.max_capacitance_ff && *b.max_capacitance_ff > cap) {
    length = std::min(length, (*b.max_capacitance_ff * 0.9 - cap) / c);
  }
  return std::max(length, 1e-6);
}
std::string physical_label(Pin p) {
  auto n = p.get_master_node();
  if (n.is_output_node() || n.is_input_node()) {
    return std::string(p.get_pin_name());
  }
  if (const auto io = n.get_subnode_io()) {
    const auto& decls = p.is_driver() ? io->get_output_pin_decls() : io->get_input_pin_decls();
    for (const auto& d : decls) {
      if (d.port_id == p.get_port_id()) {
        return std::string(n.get_name()) + ":" + d.name;
      }
    }
  }
  return {};
}
using Rc_index = std::unordered_map<std::string_view, const physical::Rc_net*>;
bool needs_repair(Node n, const Inventory& cells, const Timing_feedback& timing, const Result& state, double limit_dbu,
                  const Rc_index& nets) {
  const auto io = n.get_subnode_io();
  if (!io || io->get_output_pin_decls().size() != 1) {
    return false;
  }
  auto out = n.try_get_driver_pin(io->get_output_pin_decls().front().port_id);
  if (out.is_invalid() || gu::bits_of(out) != 1) {
    return false;
  }
  const auto label = physical_label(out);
  const auto found = nets.find(label);
  if (found == nets.end()) {
    return false;
  }
  const auto* physical = found->second;
  if (physical->nodes.size()
      && std::count_if(physical->nodes.begin(), physical->nodes.end(), [](const auto& p) { return !p.pin.empty(); }) > 4) {
    return true;
  }
  const auto point = state.pin_locations.find(label);
  if (point != state.pin_locations.end()) {
    for (const auto& p : physical->nodes) {
      const auto xy = state.pin_locations.find(p.pin);
      if (xy != state.pin_locations.end()
          && std::abs(xy->second.first - point->second.first) + std::abs(xy->second.second - point->second.second) > limit_dbu) {
        return true;
      }
    }
  }
  const auto* c   = combinational(n, cells);
  const auto  pin = timing.pins.find(label);
  if (c && pin != timing.pins.end()) {
    const auto& p = pin->second;
    if (c->max_capacitance_ff && p.load_ff && *p.load_ff > *c->max_capacitance_ff) {
      return true;
    }
    if (c->max_transition_ps && p.slew && *p.slew * timing.time_unit_ps > *c->max_transition_ps) {
      return true;
    }
    if (c->max_fanout && sinks(out).size() > *c->max_fanout) {
      return true;
    }
  }
  return false;
}
// First-order Liberty delay estimate is a proposal heuristic only. Fresh STA
// (including the changed input capacitance) remains the acceptance authority.
double drive_cost(const Buffer& cell, double load_ff) { return cell.intrinsic_ps + cell.resistance_ohm * load_ff * 1e-3; }
double drive_stress(const auto& cell, const Timing_feedback::Pin& pin, double time_unit_ps) {
  double stress = 0;
  if (cell.max_capacitance_ff && *cell.max_capacitance_ff > 0 && pin.load_ff) {
    stress = std::max(stress, *pin.load_ff / *cell.max_capacitance_ff);
  }
  if (cell.max_transition_ps && *cell.max_transition_ps > 0 && pin.slew) {
    stress = std::max(stress, *pin.slew * time_unit_ps / *cell.max_transition_ps);
  }
  return stress;
}
// Sensitivity of each sink arrival to its input capacitance: source drive R
// plus the resistance along its physical RC-tree path. This prevents an upsize
// from fixing its own output while overloading a weak, distant upstream gate.
std::unordered_map<std::string, double> input_resistance(hhds::Graph& graph, const Inventory& cells, const Result& placed,
                                                         const std::vector<liberty::Output_drive>& output_drives) {
  std::map<std::pair<std::string, std::string>, double> sequential_drives;
  for (const auto& drive : output_drives) {
    if (drive.sequential && (drive.resistance_ohm > 0 || drive.intrinsic_ps > 0)) {
      sequential_drives.emplace(std::pair{drive.name, drive.output}, drive.resistance_ohm);
    }
  }
  std::unordered_map<std::string, double> drivers;
  for (auto n : graph.body().nodes()) {
    if (const auto* c = combinational(n, cells)) {
      drivers.emplace(physical_label(output(n, *c)), c->resistance_ohm);
    } else if (const auto io = n.get_subnode_io()) {
      for (const auto& decl : io->get_output_pin_decls()) {
        const auto drive = sequential_drives.find({std::string(io->get_name()), decl.name});
        if (drive == sequential_drives.end()) {
          continue;
        }
        const auto pin = n.try_get_driver_pin(decl.port_id);
        if (!pin.is_invalid()) {
          drivers.emplace(physical_label(pin), drive->second);
        }
      }
    }
  }
  std::unordered_map<std::string, double> result;
  for (const auto& net : placed.parasitics->nets) {
    if (net.nodes.empty()) {
      continue;
    }
    std::vector<std::vector<std::pair<unsigned, double>>> adjacent(net.nodes.size());
    for (const auto& edge : net.edges) {
      if (edge.from >= adjacent.size() || edge.to >= adjacent.size() || edge.resistance_ohm < 0
          || !std::isfinite(edge.resistance_ohm)) {
        throw std::runtime_error("invalid repair RC-tree edge");
      }
      adjacent[edge.from].emplace_back(edge.to, edge.resistance_ohm);
      adjacent[edge.to].emplace_back(edge.from, edge.resistance_ohm);
    }
    const auto root = std::find_if(net.nodes.begin(), net.nodes.end(), [&](const auto& p) { return p.pin == net.driver; });
    if (root == net.nodes.end()) {
      throw std::runtime_error("repair RC tree has no driver");
    }
    const auto            source  = drivers.find(net.driver);
    const double          drive_r = source == drivers.end() ? 0 : source->second;
    std::vector<double>   resistance(net.nodes.size(), -1);
    std::vector<unsigned> pending{static_cast<unsigned>(root - net.nodes.begin())};
    resistance[pending.front()] = drive_r;
    for (size_t i = 0; i < pending.size(); ++i) {
      const auto n = pending[i];
      if (!net.nodes[n].pin.empty()) {
        result.emplace(net.nodes[n].pin, resistance[n]);
      }
      for (const auto& [next, r] : adjacent[n]) {
        if (resistance[next] < 0) {
          resistance[next] = resistance[n] + r;
          pending.push_back(next);
        }
      }
    }
  }
  return result;
}
std::vector<Edit> edits(hhds::Graph& graph, const Inventory& cells, const Timing_feedback& timing,
                        const std::set<std::string>& protected_names, const Result& placed, const Options& options,
                        const physical::Technology& tech, const std::vector<liberty::Output_drive>* scanned_drives = nullptr) {
  const auto output_drives = timing.pins.empty() ? std::vector<liberty::Output_drive>{}
                             : scanned_drives    ? *scanned_drives
                                                 : liberty::scan_output_drives(options.liberty);
  const auto input_r
      = timing.pins.empty() ? std::unordered_map<std::string, double>{} : input_resistance(graph, cells, placed, output_drives);
  std::vector<Node> buffers;
  for (auto n : graph.body().nodes()) {
    if (combinational(n, cells) && !fixed(n) && !protected_names.contains(std::string(n.get_name()))) {
      buffers.push_back(n);
    }
  }
  const auto critical = [&](const Node& n) {
    const auto stem = std::format("{}__n{}:", gu::default_instance_name(n), n.get_debug_nid());
    return std::any_of(timing.path.begin(), timing.path.end(), [&](const auto& p) {
      return p.starts_with(stem) || p.starts_with(std::string(n.get_name()) + ":");
    });
  };
  std::set<uint64_t> critical_nodes;
  for (auto n : buffers) {
    if (critical(n)) {
      critical_nodes.insert(n.get_debug_nid());
    }
  }
  struct Node_priority {
    std::pair<double, double> score;
    bool                      critical;
    std::string               name;
  };
  std::unordered_map<uint64_t, Node_priority> priorities;
  priorities.reserve(buffers.size());
  // These graph/name/feedback lookups do not mutate the graph. Compute them
  // once instead of rebuilding long physical labels in every sort comparison.
  // Keep sorting the original Node vector with the same comparison sequence,
  // including its name tie-break, so equivalent keys retain the old permutation.
  for (const auto& n : buffers) {
    const auto*  c      = combinational(n, cells);
    const auto   p      = timing.pins.find(physical_label(output(n, *c)));
    const double stress = p == timing.pins.end() ? 0 : drive_stress(*c, p->second, timing.time_unit_ps);
    const double slack  = p == timing.pins.end() ? 0 : -p->second.slack.value_or(0);
    priorities.emplace(n.get_debug_nid(),
                       Node_priority{
                           {stress > 1 ? stress : 0, slack},
                           critical_nodes.contains(n.get_debug_nid()),
                           std::string(n.get_name())
    });
  }
  std::sort(buffers.begin(), buffers.end(), [&](const auto& a, const auto& b) {
    const auto& pa = priorities.at(a.get_debug_nid());
    const auto& pb = priorities.at(b.get_debug_nid());
    if (pa.score != pb.score) {
      return pa.score > pb.score;
    }
    return pa.critical != pb.critical ? pa.critical > pb.critical : pa.name < pb.name;
  });
  Rc_index nets;
  nets.reserve(placed.parasitics->nets.size());
  for (const auto& net : placed.parasitics->nets) {
    nets.emplace(net.driver, &net);
  }
  std::map<std::string, std::vector<const Buffer*>> ladders;
  std::map<std::string, std::optional<uint64_t>>    truths;  // one truth table per cell, not per pair
  for (const auto& [name, c] : cells) {
    truths.emplace(name, liberty::cell_truth(c));
  }
  for (const auto& [master, current] : cells) {
    auto&      ladder = ladders[master];
    const auto truth  = truths.at(master);
    for (const auto& [name, c] : cells) {
      if (name != master && c.inputs == current.inputs && c.output == current.output && c.footprint == current.footprint
          && truths.at(name) == truth && !matches_dont_use(name, options)) {
        ladder.push_back(&c);
      }
    }
    std::sort(ladder.begin(), ladder.end(), [](const auto* a, const auto* b) {
      return std::tie(a->area, a->name) < std::tie(b->area, b->name);
    });
  }
  const Buffer* preferred = nullptr;
  for (const auto& [name, c] : cells) {
    if (identity(c) && !matches_dont_use(name, options)
        && (!preferred || std::tie(c.area, name) < std::tie(preferred->area, preferred->name))) {
      preferred = &c;
    }
  }
  const auto [wire_r, wire_c] = wire_rc(options, tech);
  const double limit_dbu = preferred ? wire_limit(*preferred, wire_r, wire_c) * tech.dbu : std::numeric_limits<double>::infinity();
  std::vector<Edit> result;
  // Interleave action classes so a bounded search does not spend its entire
  // budget sizing a long chain before it ever considers removing/rebuilding it.
  for (auto n : buffers) {
    const auto* current   = combinational(n, cells);
    const bool  is_buffer = identity(*current);
    if (is_buffer) {
      result.push_back({"remove", std::string(n.get_name()), {}, 0});
    }
    auto                ladder = ladders.at(current->name);
    std::vector<double> upstream;
    for (const auto& pin : current->inputs) {
      const auto r = input_r.find(physical_label(n.get_sink_pin(pin)));
      upstream.push_back(r == input_r.end() ? 0 : r->second);
    }
    const auto pin            = timing.pins.find(physical_label(output(n, *current)));
    const auto load           = pin != timing.pins.end() ? pin->second.load_ff.value_or(0) : 0;
    const auto candidate_cost = [&](const Buffer* c) {
      double input_delay = -std::numeric_limits<double>::infinity();
      for (size_t i = 0; i < upstream.size(); ++i) {
        const auto from = i < current->input_capacitance_ff.size() ? current->input_capacitance_ff[i] : 0;
        const auto to   = i < c->input_capacitance_ff.size() ? c->input_capacitance_ff[i] : from;
        input_delay     = std::max(input_delay, (to - from) * upstream[i] * 1e-3);
      }
      return drive_cost(*c, load) + (upstream.empty() ? 0 : input_delay);
    };
    if (pin != timing.pins.end() && pin->second.load_ff) {
      std::stable_sort(ladder.begin(), ladder.end(), [&](const auto* a, const auto* b) {
        const bool recover_area
            = pin->second.slack.value_or(0) > 0 && drive_stress(*current, pin->second, timing.time_unit_ps) <= 1;
        const double budget = .5 * pin->second.slack.value_or(0) * timing.time_unit_ps;
        const auto   cost   = [&](const Buffer* c) {
          const auto delay = candidate_cost(c);
          return std::tuple{bool(c->max_capacitance_ff && load > *c->max_capacitance_ff)
                                || (recover_area && delay > drive_cost(*current, load) + budget),
                            recover_area ? c->area : delay,
                            recover_area ? delay : c->area,
                            c->name};
        };
        return cost(a) < cost(b);
      });
    }
    for (const auto* c : ladder) {
      result.push_back({is_buffer ? "resize" : "gate_resize",
                        std::string(n.get_name()),
                        c->name,
                        0,
                        drive_cost(*current, load) - candidate_cost(c)});
    }
    if (is_buffer) {
      // Existing excluded masters still need characterization and may be
      // removed. Rebuilding would create new instances, so use allowed cells.
      if (!matches_dont_use(current->name, options)) {
        result.push_back({"rebuild", std::string(n.get_name()), current->name, 2});
      }
      if (!ladder.empty()) {
        result.push_back({"rebuild", std::string(n.get_name()), ladder.back()->name, 4});
      }
    }
  }
  if (!timing.pins.empty() && preferred) {
    std::map<std::pair<std::string, std::string>, const liberty::Output_drive*> state_drives;
    for (const auto& drive : output_drives) {
      if (drive.sequential && (drive.resistance_ohm > 0 || drive.intrinsic_ps > 0)) {
        state_drives.emplace(std::pair{drive.name, drive.output}, &drive);
      }
    }
    std::unordered_map<std::string, Node> nodes;
    for (auto n : graph.body().nodes()) {
      if (!n.get_name().empty()) {
        nodes.emplace(std::string(n.get_name()), n);
      }
    }
    for (const auto& net : placed.parasitics->nets) {
      const auto    colon = net.driver.rfind(':');
      std::string   owner;
      const Buffer* cell = nullptr;
      if (colon == std::string::npos) {
        owner = "__port_" + net.driver;
        if (protected_names.contains("@port:" + net.driver)) {
          continue;
        }
      } else {
        owner           = net.driver.substr(0, colon);
        const auto node = nodes.find(owner);
        if (node == nodes.end() || protected_names.contains(owner)) {
          continue;
        }
        // Movable buffer chains belong to their upstream forest. Fixed buffers
        // remain drivers and may have their downstream loads repaired.
        if (const auto* b = buffer(node->second, cells); b && !fixed(node->second) && sinks(output(node->second, *b)).size() <= 1) {
          continue;
        }
        cell = combinational(node->second, cells);
      }
      const auto pin = timing.pins.find(net.driver);
      if (pin != timing.pins.end() && pin->second.clock) {
        continue;
      }
      const bool                   critical_net = pin != timing.pins.end() && pin->second.slack.value_or(0) < 0;
      const liberty::Output_drive* state_drive  = nullptr;
      if (!cell && colon != std::string::npos) {
        const auto node  = nodes.find(owner);
        const auto io    = node == nodes.end() ? nullptr : node->second.get_subnode_io();
        const auto model = io ? state_drives.find({std::string(io->get_name()), net.driver.substr(colon + 1)}) : state_drives.end();
        if (model != state_drives.end()) {
          state_drive = model->second;
        }
      }
      const bool characterize_source = cell || state_drive;
      const auto sink_count  = std::count_if(net.nodes.begin(), net.nodes.end(), [](const auto& p) { return !p.pin.empty(); }) - 1;
      // A characterized gate or state output does not need a fanout-four tree merely because
      // it has several loads. Repair electrical violations, critical long
      // wires, or removable existing chains; leave healthy positive-slack nets
      // alone. Unknown sources retain the conservative fanout fallback.
      bool       needed      = (!characterize_source || critical_net) && sink_count > 3;
      const auto max_fanout  = cell ? cell->max_fanout : state_drive ? state_drive->max_fanout : std::nullopt;
      needed                |= max_fanout && sink_count > *max_fanout;
      if (state_drive && pin != timing.pins.end()) {
        needed |= drive_stress(*state_drive, pin->second, timing.time_unit_ps) > 1;
      }
      if (cell && pin != timing.pins.end()) {
        needed |= drive_stress(*cell, pin->second, timing.time_unit_ps) > 1;
      }
      const auto root = placed.pin_locations.find(net.driver);
      for (const auto& p : net.nodes) {
        if (p.pin.empty() || p.pin == net.driver) {
          continue;
        }
        const auto split = p.pin.rfind(':');
        if (split != std::string::npos) {
          const auto child  = nodes.find(p.pin.substr(0, split));
          needed           |= child != nodes.end() && buffer(child->second, cells) && !fixed(child->second);
        }
        const auto xy = placed.pin_locations.find(p.pin);
        if (root != placed.pin_locations.end() && xy != placed.pin_locations.end()) {
          needed
              |= (!characterize_source || critical_net)
                 && std::abs(xy->second.first - root->second.first) + std::abs(xy->second.second - root->second.second) > limit_dbu;
        }
      }
      if (needed) {
        result.push_back({"tree", owner, preferred->name, 4, 0, net.driver});
      }
    }
  } else {
    for (auto n : graph.body().nodes()) {
      if (!n.get_subnode_io() || buffer(n, cells) || fixed(n) || protected_names.contains(std::string(n.get_name()))) {
        continue;
      }
      const auto io = n.get_subnode_io();
      if (io->get_output_pin_decls().size() != 1) {
        continue;
      }
      auto       p             = n.try_get_driver_pin(io->get_output_pin_decls().front().port_id);
      const auto targets       = p.is_invalid() ? std::vector<Pin>{} : sinks(p);
      const bool drives_buffer = std::any_of(targets.begin(), targets.end(), [&](const auto& sink) {
        return buffer(sink.get_master_node(), cells) != nullptr;
      });
      if (p.is_invalid() || gu::bits_of(p) != 1 || (!drives_buffer && !needs_repair(n, cells, timing, placed, limit_dbu, nets))) {
        continue;
      }
      for (const auto& [name, c] : cells) {
        if (!preferred || !identity(c) || c.footprint != preferred->footprint || matches_dont_use(name, options)) {
          continue;
        }
        result.push_back({"tree", std::string(n.get_name()), name, 4});
      }
    }
  }
  std::map<std::string, unsigned>        rank;
  std::vector<std::pair<unsigned, Edit>> fair;
  for (auto& edit : result) {
    fair.emplace_back(rank[edit.node]++, std::move(edit));
  }
  std::stable_sort(fair.begin(), fair.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  result.clear();
  for (auto& [round, edit] : fair) {
    (void)round;
    result.push_back(std::move(edit));
  }
  return result;
}
std::shared_ptr<hhds::GraphIO> buffer_io(hhds::Graph& graph, const Buffer& b) {
  auto* lib = graph.get_io()->get_library();
  if (const auto io = lib->find_io(b.name)) {
    return io;
  }
  auto io = lib->create_io(b.name);
  for (size_t i = 0; i < b.inputs.size(); ++i) {
    io->add_input(b.inputs[i], i);
    io->set_bits(b.inputs[i], 1);
  }
  io->add_output(b.output, 0);
  io->set_bits(b.output, 1);
  return io;
}
void change_master(Node n, const Buffer& from, const Buffer& to) {
  std::map<std::string, Pin> drivers;
  for (const auto& name : from.inputs) {
    const auto p = n.get_sink_pin(name);
    const auto d = p.get_driver_pins();
    if (d.size() != 1 || gu::bits_of(d.front()) != 1) {
      throw std::runtime_error("gate sizing requires scalar, singly driven inputs");
    }
    drivers.emplace(name, d.front());
  }
  const auto targets = sinks(output(n, from));
  const auto io      = buffer_io(*n.get_graph(), to);
  // Port IDs can differ between independently imported black-box declarations.
  for (auto p : n.inp_pins_snapshot()) {
    gu::drop_drivers(p);
  }
  const auto old_output = output(n, from);
  const auto old_roles  = gu::signal_roles_of(old_output);
  for (auto p : targets) {
    p.del_sink(old_output);
  }
  n.set_subnode(io);
  for (const auto& name : to.inputs) {
    n.create_sink_pin(name).connect_driver(drivers.at(name));
  }
  auto out = n.create_driver_pin(to.output);
  gu::set_bits(out, 1);
  gu::add_signal_roles(out, old_roles);
  for (auto p : targets) {
    p.connect_driver(out);
  }
}
Physical_sink physical_pin(hhds::Graph& graph, const std::string& label, const Node_index& nodes, bool driver) {
  const auto bit = [](const std::string& text, const std::string& prefix) -> std::optional<unsigned> {
    if (!text.starts_with(prefix + "[") || text.back() != ']') {
      return {};
    }
    const auto number       = text.substr(prefix.size() + 1, text.size() - prefix.size() - 2);
    unsigned   value        = 0;
    const auto [end, error] = std::from_chars(number.data(), number.data() + number.size(), value);
    return error == std::errc{} && end == number.data() + number.size() ? std::optional<unsigned>{value} : std::nullopt;
  };
  const auto colon = label.rfind(':');
  const auto name  = colon == std::string::npos ? label : label.substr(colon + 1);
  if (colon == std::string::npos) {
    for (const auto& p : (driver ? graph.get_io()->get_input_pin_decls() : graph.get_io()->get_output_pin_decls())) {
      if (p.name == name) {
        return {(driver ? graph.get_input_pin(p.name) : graph.get_output_pin(p.name)),
                0,
                static_cast<unsigned>(std::max<int64_t>(1, graph.get_io()->get_bits(p.name)))};
      }
    }
    for (const auto& p : (driver ? graph.get_io()->get_input_pin_decls() : graph.get_io()->get_output_pin_decls())) {
      if (const auto b = bit(name, p.name); b) {
        const auto width = static_cast<unsigned>(graph.get_io()->get_bits(p.name));
        if (*b < width) {
          return {(driver ? graph.get_input_pin(p.name) : graph.get_output_pin(p.name)), *b, width};
        }
      }
    }
  } else {
    const auto found = nodes.find(label.substr(0, colon));
    if (found == nodes.end()) {
      throw std::runtime_error("physical edit lost instance: " + label);
    }
    auto n = found->second;
    if (const auto io = n.get_subnode_io()) {
      for (const auto& p : (driver ? io->get_output_pin_decls() : io->get_input_pin_decls())) {
        if (p.name == name) {
          return {(driver ? n.get_driver_pin(p.port_id) : n.get_sink_pin(p.port_id)),
                  0,
                  static_cast<unsigned>(std::max<int64_t>(1, io->get_bits(p.name)))};
        }
      }
      for (const auto& p : (driver ? io->get_output_pin_decls() : io->get_input_pin_decls())) {
        if (const auto b = bit(name, p.name); b) {
          const auto width = static_cast<unsigned>(io->get_bits(p.name));
          if (*b < width) {
            return {(driver ? n.get_driver_pin(p.port_id) : n.get_sink_pin(p.port_id)), *b, width};
          }
        }
      }
    } else if (gu::type_op_of(n) == Ntype_op::Memory && name.starts_with(driver ? "out_" : "in_")) {
      const auto   open       = name.find('[');
      const size_t prefix     = driver ? 4 : 3;
      const auto   pid_text   = name.substr(prefix, open - prefix);
      unsigned     pid        = 0;
      const auto [end, error] = std::from_chars(pid_text.data(), pid_text.data() + pid_text.size(), pid);
      const auto b            = bit(name, std::string(driver ? "out_" : "in_") + pid_text);
      if (error == std::errc{} && end == pid_text.data() + pid_text.size() && b) {
        auto       p     = driver ? n.get_driver_pin(pid) : n.get_sink_pin(pid);
        const auto width = static_cast<unsigned>(std::max(1, gu::bits_of(driver ? p : p.get_driver_pin())));
        if (*b < width) {
          return {p, *b, width};
        }
      }
    }
  }
  throw std::runtime_error("cannot resolve physical repair sink: " + label);
}
Physical_sink physical_sink(hhds::Graph& graph, const std::string& label, const Node_index& nodes) {
  return physical_pin(graph, label, nodes, false);
}
void connect_bit(hhds::Graph& graph, const Physical_sink& sink, Pin driver) {
  const auto old = sink.pin.get_driver_pin();
  if (old.is_invalid()) {
    throw std::runtime_error("undriven repair sink");
  }
  if (sink.width > 1) {
    auto replacement = gu::create_set_mask(graph, old, driver, sink.bit, sink.bit + 1).get_driver_pin(0);
    gu::set_bits(replacement, sink.width);
    if (gu::is_unsign(old)) {
      gu::set_unsign(replacement);
    } else {
      gu::set_sign(replacement);
    }
    driver = replacement;
  }
  gu::drop_drivers(sink.pin);
  sink.pin.connect_driver(driver);
}
struct Apply_index {
  Node_index                                                                          nodes;
  std::set<std::string>                                                               names;
  std::map<std::string, const physical::Rc_net*>                                      nets;
  std::optional<std::pair<double, double>>                                            rc;
  std::optional<std::map<std::pair<std::string, std::string>, liberty::Output_drive>> output_drives;
  std::unordered_map<std::string, std::string>                                        pin_driver, origins;
  std::map<std::string, double>                                                       weights;
  size_t                                                                              sequential_drivers = 0;
  Apply_index(hhds::Graph& graph, const Result& placed) : weights(placed.net_weights) {
    for (auto n : graph.body().nodes()) {
      if (!n.get_name().empty()) {
        nodes.emplace(std::string(n.get_name()), n);
        names.emplace(n.get_name());
      }
    }
    for (const auto& net : placed.parasitics->nets) {
      nets.emplace(net.driver, &net);
      for (const auto& p : net.nodes) {
        if (!p.pin.empty()) {
          pin_driver.emplace(p.pin, net.driver);
        }
      }
    }
  }
};
std::optional<Buffer_tree> apply(hhds::Graph& graph, const Inventory& cells, const Edit& edit,
                                 const std::set<std::string>& protected_names, const Result& placed, const Timing_feedback& timing,
                                 const Options& options, const physical::Technology& tech, Apply_index* shared = nullptr) {
  std::optional<Apply_index> owned;
  if (!shared) {
    owned.emplace(graph, placed);
    shared = &*owned;
  }
  std::optional<Physical_sink> source;
  if (!edit.driver.empty()) {
    source = physical_pin(graph, edit.driver, shared->nodes, true);
  }
  auto        n       = source ? source->pin.get_master_node() : shared->nodes.at(edit.node);
  const auto* current = source ? nullptr : buffer(n, cells);
  if (edit.kind == "resize" || edit.kind == "gate_resize") {
    change_master(n, *combinational(n, cells), cells.at(edit.master));
    return {};
  }
  if (edit.kind == "remove") {
    const auto driver = input(n, *current);
    if (driver.is_const() || gu::bits_of(driver) != 1) {
      throw std::runtime_error("buffer input is not scalar");
    }
    for (auto target : sinks(output(n, *current))) {
      gu::drop_drivers(target);
      target.connect_driver(driver);
    }
    shared->weights.erase(physical_label(output(n, *current)));
    shared->nodes.erase(std::string(n.get_name()));
    n.del_node();
    return {};
  }
  const auto& b    = cells.at(edit.master);
  Pin         root = source    ? source->pin
                     : current ? input(n, *current)
                               : n.try_get_driver_pin(n.get_subnode_io()->get_output_pin_decls().front().port_id);
  if (root.is_invalid() || root.is_const() || (!source && gu::bits_of(root) != 1)) {
    throw std::runtime_error("buffer tree root is not scalar");
  }
  const auto&                nets       = shared->nets;
  const auto&                node_index = shared->nodes;
  std::vector<Node>          removed;
  std::vector<Physical_sink> leaves;
  std::vector<std::string>   leaf_names;
  std::set<std::string>      visited, leaf_set;
  const auto                 collect = [&](const std::string& start) {
    std::vector<std::string> pending{start};
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
      const auto driver = pending[cursor];
      if (!visited.insert(driver).second) {
        throw std::runtime_error("cycle in physical buffer tree");
      }
      const auto net = nets.find(driver);
      if (net == nets.end()) {
        throw std::runtime_error("missing physical buffer net: " + driver);
      }
      for (const auto& point : net->second->nodes) {
        if (point.pin.empty() || point.pin == driver) {
          continue;
        }
        const auto leaf = physical_sink(graph, point.pin, node_index);
        auto       node = leaf.pin.get_master_node();
        if (protected_names.contains(std::string(node.get_name()))) {
          throw std::runtime_error("protected tree leaf");
        }
        // Retain distribution forks as timing cut points. Flattening every fork
        // can replace an existing shallow backbone with a long wire-minimal
        // path. Single-load chains remain removable/rebuildable as a unit.
        if (const auto* next = buffer(node, cells);
            next && !fixed(node) && leaf.width == 1 && sinks(output(node, *next)).size() <= 1) {
          removed.push_back(node);
          pending.push_back(physical_label(output(node, *next)));
        } else if (leaf_set.insert(point.pin).second) {
          leaves.push_back(leaf);
          leaf_names.push_back(point.pin);
        }
      }
    }
  };
  std::string root_name = source ? edit.driver : physical_label(root);
  if (current) {
    removed.push_back(n);
    const auto entry = physical_label(n.get_sink_pin(current->inputs.front()));
    if (const auto net = shared->pin_driver.find(entry); net != shared->pin_driver.end()) {
      root_name = net->second;
    }
    collect(physical_label(output(n, *current)));
  } else {
    collect(root_name);
  }
  if (leaves.empty()) {
    throw std::runtime_error("tree leaf count outside repair budget");
  }
  Node owner = n;
  if (n.is_input_node()) {
    std::vector<Node> members = removed;
    for (const auto& leaf : leaves) {
      const auto node = leaf.pin.get_master_node();
      if (!node.is_output_node()) {
        members.push_back(node);
      }
    }
    if (!members.empty()) {
      const auto group = members.front().attr(attrs::synth_color_group);
      if (group.has() && !group.get().empty() && std::all_of(members.begin(), members.end(), [&](const auto& member) {
            const auto candidate = member.attr(attrs::synth_color_group);
            return candidate.has() && candidate.get() == group.get();
          })) {
        owner = members.front();
      }
    }
  }
  const std::string owner_name       = std::string(owner.get_name());
  const auto        owner_color      = gu::node_color_of(owner);
  const auto        color_group_attr = owner.attr(attrs::synth_color_group);
  const std::string color_group      = color_group_attr.has() ? color_group_attr.get() : std::string{};
  const auto        block_attr       = owner.attr(attrs::placement_block);
  const std::string owner_block      = block_attr.has() ? block_attr.get() : std::string{};
  const auto        region_attr      = owner.attr(attrs::synth_region_id);
  const auto        owner_region     = region_attr.has() ? region_attr.get() : 0;
  const auto        root_xy          = placed.pin_locations.find(root_name);
  if (root_xy == placed.pin_locations.end()) {
    throw std::runtime_error("missing Steiner root pin geometry: " + root_name);
  }
  std::vector<Point>  locations;
  std::vector<double> capacitances;
  for (const auto& name : leaf_names) {
    const auto xy = placed.pin_locations.find(name);
    if (xy == placed.pin_locations.end()) {
      throw std::runtime_error("missing Steiner sink geometry: " + name);
    }
    locations.push_back(xy->second);
    const auto pin = timing.pins.find(name);
    if (pin != timing.pins.end() && pin->second.clock) {
      throw std::runtime_error("Steiner branch reaches a clock sink");
    }
    capacitances.push_back(pin == timing.pins.end() ? 0 : pin->second.capacitance_ff.value_or(pin->second.load_ff.value_or(0)));
  }
  if (!shared->rc) {
    shared->rc = wire_rc(options, tech);
  }
  const auto [r, c]      = *shared->rc;
  const double input_cap = b.input_capacitance_ff.empty() ? 0 : b.input_capacitance_ff.front();
  const double cap_limit = b.max_capacitance_ff.value_or(std::numeric_limits<double>::infinity()) * 0.9;
  const auto   fanout    = b.max_fanout ? std::min(edit.fanout, static_cast<unsigned>(std::max(0.0, *b.max_fanout))) : edit.fanout;
  Buffer_tree  plan;
  std::vector<const Buffer*> drive_cells{&b};
  if (!timing.pins.empty()) {
    drive_cells.clear();
    std::vector<Buffer_drive> drives;
    const auto                model = [&](const Buffer& cell) {
      return Buffer_drive{
          cell.input_capacitance_ff.empty() ? 0 : cell.input_capacitance_ff.front(),
          cell.max_capacitance_ff.value_or(std::numeric_limits<double>::infinity()) * .9,
          wire_limit(cell, r, c) * tech.dbu,
          cell.max_fanout ? static_cast<unsigned>(std::clamp(*cell.max_fanout, 1.0, double(std::max<size_t>(2, locations.size()))))
                          : static_cast<unsigned>(std::max<size_t>(2, locations.size())),
          cell.intrinsic_ps,
          cell.resistance_ohm,
          cell.area};
    };
    for (const auto& [name, cell] : cells) {
      if (identity(cell) && (!cell.max_fanout || *cell.max_fanout >= 2) && cell.inputs == b.inputs && cell.output == b.output
          && cell.footprint == b.footprint && !matches_dont_use(name, options)) {
        drive_cells.push_back(&cell);
        drives.push_back(model(cell));
      }
    }
    // An unconstrained sink (no required time) must not look like the most critical one:
    // it sits well after every constrained sink. With no constraint at all every sink is 0.
    std::vector<std::optional<double>> constrained;
    double                             latest = 0;
    bool                               any    = false;
    for (const auto& name : leaf_names) {
      const auto pin = timing.pins.find(name);
      if (pin == timing.pins.end() || !pin->second.required) {
        constrained.emplace_back();
        continue;
      }
      constrained.emplace_back(*pin->second.required * timing.time_unit_ps);
      latest = any ? std::max(latest, *constrained.back()) : *constrained.back();
      any    = true;
    }
    std::vector<double> required;
    for (const auto& value : constrained) {
      required.push_back(value ? *value : any ? latest + 1e6 : 0);
    }
    auto        driver_cell_it = shared->nodes.find(root_name.substr(0, root_name.rfind(':')));
    const auto* root_cell      = driver_cell_it == shared->nodes.end() ? nullptr : combinational(driver_cell_it->second, cells);
    auto        root_model     = root_cell ? model(*root_cell) : model(b);
    // A register output is not a combinational resize candidate, but its
    // clock-to-Q tables and electrical limits still characterize its drive.
    // Only genuinely uncharacterized ports/macros use the conservative fallback.
    if (!root_cell) {
      if (!shared->output_drives) {
        shared->output_drives.emplace();
        for (auto drive : liberty::scan_output_drives(options.liberty)) {
          shared->output_drives->emplace(std::pair{drive.name, drive.output}, std::move(drive));
        }
      }
      const auto io       = driver_cell_it == shared->nodes.end() ? nullptr : driver_cell_it->second.get_subnode_io();
      const auto pin_name = root_name.substr(root_name.rfind(':') + 1);
      const auto characterized
          = io ? shared->output_drives->find({std::string(io->get_name()), pin_name}) : shared->output_drives->end();
      if (characterized != shared->output_drives->end() && characterized->second.sequential
          && (characterized->second.resistance_ohm > 0 || characterized->second.intrinsic_ps > 0)) {
        ++shared->sequential_drivers;
        const auto& drive = characterized->second;
        Buffer      estimate;
        estimate.intrinsic_ps       = drive.intrinsic_ps;
        estimate.resistance_ohm     = drive.resistance_ohm;
        estimate.max_capacitance_ff = drive.max_capacitance_ff;
        root_model.intrinsic_ps     = drive.intrinsic_ps;
        root_model.resistance_ohm   = drive.resistance_ohm;
        root_model.max_cap_ff       = drive.max_capacitance_ff.value_or(std::numeric_limits<double>::infinity()) * .9;
        root_model.max_fanout
            = drive.max_fanout
                  ? static_cast<unsigned>(std::clamp(*drive.max_fanout, 1.0, double(std::max<size_t>(2, locations.size()))))
                  : static_cast<unsigned>(std::max<size_t>(2, locations.size()));
        root_model.max_distance_dbu = wire_limit(estimate, r, c) * tech.dbu;
      } else {
        root_model.max_cap_ff     = std::numeric_limits<double>::infinity();
        root_model.resistance_ohm = 0;
        root_model.max_fanout     = 4;  // uncharacterized output: validate the conservative tree with fresh STA
      }
    }
    // Optimal repeater spacing is a topology-generation heuristic, not an
    // electrical maximum wire length for the original source. Capacitance and
    // real fanout limits still apply; RC delay decides whether a direct source
    // connection or an extra root buffer is faster.
    root_model.max_distance_dbu = std::numeric_limits<double>::infinity();
    plan = plan_buffer_choices(root_xy->second, locations, capacitances, required, drives, root_model, r / tech.dbu, c / tech.dbu);
  } else {
    plan = plan_buffers(root_xy->second,
                        locations,
                        capacitances,
                        input_cap,
                        cap_limit,
                        fanout,
                        wire_limit(b, r, c) * tech.dbu,
                        c / tech.dbu);
  }
  if (plan.buffers.empty() && removed.empty()) {
    throw std::runtime_error("Steiner split is not needed for this drive");
  }
  for (const auto& leaf : leaves) {
    if (leaf.pin.get_driver_pin().is_invalid() || leaf.bit >= leaf.width) {
      throw std::runtime_error("invalid repair sink wiring");
    }
  }
  if (source && source->width > 1) {
    root = gu::create_get_mask(graph, root, source->bit).get_driver_pin(0);
    gu::set_bits(root, 1);
  }
  // Keep all original wiring aliases well driven while bypassing the identity
  // cells. Physical sinks are then rewired per bit; unrelated packed bits stay.
  for (auto removed_cell : removed) {
    const auto* original = buffer(removed_cell, cells);
    const auto  old      = output(removed_cell, *original);
    for (auto target : sinks(old)) {
      target.del_sink(old);
      target.connect_driver(root);
    }
  }
  for (auto removed_cell : removed) {
    shared->weights.erase(physical_label(output(removed_cell, *buffer(removed_cell, cells))));
    shared->nodes.erase(std::string(removed_cell.get_name()));
    removed_cell.del_node();
  }
  std::vector<Node> nodes;
  std::vector<Pin>  outputs;
  unsigned          serial = 0;
  auto&             names  = shared->names;
  for (const auto& branch : plan.buffers) {
    const auto& selected = *drive_cells.at(branch.drive);
    const auto& macro    = tech.macros.at(selected.name);
    auto        cell     = gu::create_typed_node(graph, Ntype_op::Sub);
    cell.set_subnode(buffer_io(graph, selected));
    std::string name;
    do {
      name = std::format("__place_rebuffer_{}_{}", edit.node, serial++);
    } while (!names.insert(name).second);
    cell.set_name(name);
    shared->nodes.emplace(name, cell);
    shared->origins.emplace(name, owner_name);
    cell.attr(attrs::color).set(owner_color);
    if (!color_group.empty()) {
      cell.attr(attrs::synth_color_group).set(color_group);
    }
    if (!owner_block.empty()) {
      cell.attr(attrs::placement_block).set(owner_block);
    }
    if (owner_region) {
      cell.attr(attrs::synth_region_id).set(owner_region);
    }
    Ann_place seed(branch.position.first - macro.width * tech.dbu / 2,
                   branch.position.second - macro.height * tech.dbu / 2,
                   macro.width * tech.dbu,
                   macro.height * tech.dbu);
    seed.status = Ann_place::Status::PLACED;
    cell.attr(attrs::place).set(seed);
    auto out = cell.create_driver_pin(b.output);
    gu::set_bits(out, 1);
    nodes.push_back(cell);
    outputs.push_back(out);
  }
  const auto connect = [&](Pin driver, const std::vector<Buffer_tree::Load>& loads) {
    for (const auto& load : loads) {
      if (load.buffer) {
        nodes.at(load.index).create_sink_pin(b.inputs.front()).connect_driver(driver);
      } else {
        connect_bit(graph, leaves.at(load.index), driver);
      }
    }
  };
  for (size_t i = 0; i < plan.buffers.size(); ++i) {
    connect(outputs[i], plan.buffers[i].loads);
  }
  connect(root, plan.root);
  std::vector<std::string> output_names;
  for (const auto& pin : outputs) {
    output_names.push_back(physical_label(pin));
  }
  inherit_buffer_weights(shared->weights, root_name, output_names);
  return plan;
}

// Use the final physical terminals, not graph edge count: wiring aliases and
// output branches can conceal extra loads. Both nets must be simple cell nets.
std::vector<Edit> final_buffer_candidates(const Result& state, const Inventory& cells, const std::set<std::string>& protected_names,
                                          const physical::Technology& tech) {
  std::unordered_map<std::string, const physical::Rc_net*> nets, incoming;
  for (const auto& net : state.parasitics->nets) {
    nets.emplace(net.driver, &net);
    for (const auto& point : net.nodes) {
      if (!point.pin.empty() && point.pin != net.driver) {
        incoming.emplace(point.pin, &net);
      }
    }
  }
  const auto length = [&](const physical::Rc_net& net) -> std::optional<double> {
    std::vector<std::string> terminals;
    for (const auto& point : net.nodes) {
      if (!point.pin.empty()) {
        if (point.pin.find(':') == std::string::npos) {
          return {};  // primary port load/driver
        }
        terminals.push_back(point.pin);
      }
    }
    if (terminals.size() != 2 || !state.pin_locations.contains(terminals[0]) || !state.pin_locations.contains(terminals[1])) {
      return {};
    }
    const auto a = state.pin_locations.at(terminals[0]), b = state.pin_locations.at(terminals[1]);
    return std::abs(a.first - b.first) + std::abs(a.second - b.second);
  };
  std::vector<std::pair<double, Edit>> ranked;
  for (auto node : state.graph->body().nodes()) {
    const auto* cell = buffer(node, cells);
    if (!cell || fixed(node) || protected_names.contains(std::string(node.get_name()))) {
      continue;
    }
    const auto in  = incoming.find(physical_label(node.get_sink_pin(cell->inputs.front())));
    const auto out = nets.find(physical_label(output(node, *cell)));
    if (in == incoming.end() || out == nets.end()) {
      continue;
    }
    const auto upstream = input(node, *cell);
    if (upstream.is_invalid() || upstream.is_const() || gu::bits_of(upstream) != 1 || upstream.get_master_node().is_input_node()
        || protected_names.contains(std::string(upstream.get_master_node().get_name()))) {
      continue;
    }
    const auto   a = length(*in->second), b = length(*out->second);
    const double nearby = 2 * tech.macros.at(cell->name).height * tech.dbu;
    if (a && b && std::min(*a, *b) <= nearby) {
      ranked.push_back({
          std::min(*a, *b),
          {"remove", std::string(node.get_name()), {}, 0}
      });
    }
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
    return std::tie(a.first, a.second.node) < std::tie(b.first, b.second.node);
  });
  std::vector<Edit> result;
  for (auto& item : ranked) {
    result.push_back(std::move(item.second));
  }
  return result;
}

std::vector<std::string_view> retention_failures(const Timing_feedback& before, const Timing_feedback& after) {
  constexpr double              epsilon   = 1e-6;
  const auto                    not_worse = [&](auto a, auto b) { return !a || (b && *b + epsilon >= *a); };
  std::vector<std::string_view> failures;
  if (!std::isfinite(after.delay) || after.delay > before.delay + epsilon) {
    failures.emplace_back("max_delay");
  }
  if (!not_worse(before.wns, after.wns)) {
    failures.emplace_back("setup_wns");
  }
  if (!not_worse(before.tns, after.tns)) {
    failures.emplace_back("setup_tns");
  }
  if (!(!before.hold_wns || (after.hold_wns && std::min(0.0, *after.hold_wns) + epsilon >= std::min(0.0, *before.hold_wns)))) {
    failures.emplace_back("hold_wns");
  }
  if (after.violations > before.violations) {
    failures.emplace_back("electrical_violations");
  }
  if (after.electrical_excess > before.electrical_excess + epsilon) {
    failures.emplace_back("electrical_excess");
  }
  if (failures.empty()
      && !(after.delay + epsilon < before.delay || after.area + epsilon < before.area || after.violations < before.violations
           || after.electrical_excess + epsilon < before.electrical_excess)) {
    failures.emplace_back("no_improvement");
  }
  return failures;
}
bool retain(const Timing_feedback& before, const Timing_feedback& after) { return retention_failures(before, after).empty(); }

// A failed batch is useful feedback: isolate the newly stressed pins and their
// immediate electrical neighborhood instead of blindly halving instance names.
// This only proposes a smaller batch; fresh STA still decides acceptance.
std::set<std::string> regressed_nodes(const Timing_feedback& before, const Timing_feedback& after,
                                      const physical::Parasitics& nets) {
  constexpr double eps = 1e-6;
  const bool       setup
      = (before.wns && after.wns && *after.wns < *before.wns - eps) || (before.tns && after.tns && *after.tns < *before.tns - eps);
  const bool electrical = before.electrical_pin_feedback && after.electrical_pin_feedback
                          && (after.violations > before.violations || after.electrical_excess > before.electrical_excess + eps);
  std::set<std::string> bad_pins, nodes;
  const auto            add_node = [&](const std::string& pin) {
    const auto colon = pin.rfind(':');
    if (colon != std::string::npos) {
      nodes.insert(pin.substr(0, colon));
    }
  };
  for (const auto& [name, pin] : after.pins) {
    const auto old = before.pins.find(name);
    if (old == before.pins.end() || pin.clock) {
      continue;
    }
    const auto& prior = old->second;
    if ((electrical && (pin.violations > prior.violations || pin.electrical_excess > prior.electrical_excess + eps))
        || (setup && pin.slack && prior.slack && *pin.slack < std::min(0.0, *prior.slack) - eps)) {
      bad_pins.insert(name);
      add_node(name);
    }
  }
  // Do not expand recursively: a shared high-fanout net must not flood the
  // entire design simply because a neighbor was marked by an earlier net.
  for (const auto& net : nets.nets) {
    if (std::any_of(net.nodes.begin(), net.nodes.end(), [&](const auto& pin) { return bad_pins.contains(pin.pin); })) {
      for (const auto& pin : net.nodes) {
        add_node(pin.pin);
      }
    }
  }
  return nodes;
}
}  // namespace detail
using namespace detail;
bool            timing_improves(const Timing_feedback& before, const Timing_feedback& after) { return retain(before, after); }
Timing_feedback timing_feedback(std::string_view text) {
  rapidjson::Document json;
  json.Parse(text.data(), text.size());
  if (json.HasParseError() || !json.IsObject() || !json.HasMember("designs") || !json["designs"].IsArray()
      || json["designs"].Size() != 1) {
    throw std::runtime_error("buffer repair requires one timing design");
  }
  const auto& d = json["designs"][0];
  if (!d.HasMember("max_delay") || !d.HasMember("area") || !d.HasMember("electrical") || !d.HasMember("timing_cells_complete")
      || !d["timing_cells_complete"].GetBool()) {
    throw std::runtime_error("buffer repair requires complete cell timing, area and electrical feedback");
  }
  Timing_feedback result;
  result.delay = d["max_delay"].GetDouble();
  result.area  = d["area"].GetDouble();
  if (d.HasMember("wns")) {
    result.wns = d["wns"].GetDouble();
  }
  if (d.HasMember("tns")) {
    result.tns = d["tns"].GetDouble();
  }
  if (d.HasMember("hold_wns")) {
    result.hold_wns = d["hold_wns"].GetDouble();
  }
  result.violations              = d["electrical"]["violations"].GetUint();
  result.electrical_excess       = d["electrical"]["excess"].GetDouble();
  result.electrical_pin_feedback = d.HasMember("electrical_pin_feedback") && d["electrical_pin_feedback"].GetBool();
  for (const auto& point : d["path"].GetArray()) {
    result.path.emplace_back(point["pin"].GetString());
  }
  if (d.HasMember("time_unit_ps")) {
    result.time_unit_ps = d["time_unit_ps"].GetDouble();
  }
  std::map<std::string, std::string> physical_names;
  if (d.HasMember("physical_pins")) {
    for (const auto& pin : d["physical_pins"].GetArray()) {
      Timing_feedback::Pin p;
      const auto           number = [&](const char* key) -> std::optional<double> {
        return pin.HasMember(key) ? std::optional<double>{pin[key].GetDouble()} : std::nullopt;
      };
      p.arrival           = number("arrival");
      p.required          = number("required");
      p.slew              = number("slew");
      p.load_ff           = number("load_ff");
      p.slack             = number("slack");
      p.capacitance_ff    = number("capacitance_ff");
      p.clock             = pin["clock"].GetBool();
      p.violations        = pin.HasMember("violations") ? pin["violations"].GetUint() : 0;
      p.electrical_excess = number("electrical_excess").value_or(0);
      result.pins.emplace(pin["pin"].GetString(), p);
      physical_names.emplace(pin["timer_pin"].GetString(), pin["pin"].GetString());
    }
  }
  for (auto& point : result.path) {
    if (const auto it = physical_names.find(point); it != physical_names.end()) {
      point = it->second;
    }
  }
  return result;
}
// The LEF, inventory and Liberty scans do not depend on the graph; repair_floorplan reads them once.
struct Floorplan_library {
  physical::Technology               tech;
  Inventory                          cells;
  Special_pins                       special;
  std::vector<liberty::Output_drive> drives;
};
static Floorplan_library read_floorplan_library(const Options& options) {
  Floorplan_library library;
  library.tech    = physical::read_lef(options.lefs);
  library.cells   = inventory(options, library.tech);
  library.special = scan_special_pins(options.liberty, !options.repair_reset);
  library.drives  = liberty::scan_output_drives(options.liberty);
  return library;
}
static Floorplan_resize resize_floorplan(Result& state, const Options& options, const Timing_feedback& before,
                                         const Evaluate& evaluate, const std::function<void(Result&)>& refresh,
                                         const Floorplan_library& library) {
  const auto& tech            = library.tech;
  const auto& cells           = library.cells;
  const auto  protected_names = protected_buffers(*state.graph, cells, library.special, true, options.control_ports);
  const auto  candidates      = edits(*state.graph, cells, before, protected_names, state, options, tech, &library.drives);
  const auto  saved           = state.floorplan;
  struct Change {
    Node          node;
    const Buffer* from;
    const Buffer* to;
  };
  std::vector<Change>                         changes;
  std::set<std::string>                       selected;
  Node_index                                  node_index;
  std::map<std::string, physical::Component*> components;
  for (auto n : state.graph->body().nodes()) {
    node_index.emplace(std::string(n.get_name()), n);
  }
  for (auto& c : state.floorplan.components) {
    components.emplace(c.name, &c);
  }
  const auto restore = [&] {
    for (auto it = changes.rbegin(); it != changes.rend(); ++it) {
      change_master(it->node, *it->to, *it->from);
    }
    state.floorplan = saved;
    refresh(state);
  };
  try {
    for (const auto& edit : candidates) {
      if ((edit.kind != "resize" && edit.kind != "gate_resize") || selected.contains(edit.node)) {
        continue;
      }
      auto        node = node_index.at(edit.node);
      const auto* from = combinational(node, cells);
      const auto& to   = cells.at(edit.master);
      // A bounded next-drive upsize; electrical/timing acceptance below decides
      // the batch. Macro, state, protected and interface-changing edits are absent.
      if (!from) {
        continue;
      }
      const auto pin = before.pins.find(physical_label(output(node, *from)));
      if (pin == before.pins.end()) {
        continue;
      }
      const auto& p = pin->second;
      if (!p.load_ff || edit.gain_ps < -1e-6 || (std::abs(edit.gain_ps) <= 1e-6 && to.area <= from->area)) {
        continue;
      }
      const bool stressed = (p.slack && *p.slack < 0)
                            || (from->max_capacitance_ff && p.load_ff && *p.load_ff > *from->max_capacitance_ff)
                            || (from->max_transition_ps && p.slew && *p.slew * before.time_unit_ps > *from->max_transition_ps)
                            || (from->max_fanout && sinks(output(node, *from)).size() > *from->max_fanout);
      if (!stressed) {
        continue;
      }
      const auto found = components.find(edit.node);
      if (found == components.end()) {
        continue;
      }
      auto* component = found->second;
      if (component->place.status == Ann_place::Status::FIXED || component->place.status == Ann_place::Status::LOCKED) {
        continue;
      }
      const auto& master = tech.macros.at(edit.master);
      if (std::abs(master.height * tech.dbu - component->place.height) > 1e-6) {
        continue;
      }
      change_master(node, *from, to);
      changes.push_back({node, from, &to});
      component->macro        = edit.master;
      component->place.width  = std::llround(master.width * tech.dbu);
      component->place.height = std::llround(master.height * tech.dbu);
      selected.insert(edit.node);
    }
    refresh(state);
    const auto after    = evaluate(state);  // Always a second STA when FP resize is enabled.
    const bool keep     = !changes.empty() && retain(before, after);
    const auto proposed = static_cast<unsigned>(changes.size());
    if (!keep && !changes.empty()) {
      restore();
    }
    Floorplan_resize result;
    result.timing   = keep ? after : before;
    result.proposed = proposed;
    result.accepted = keep ? proposed : 0;
    return result;
  } catch (...) {
    if (!changes.empty()) {
      restore();
    }
    throw;
  }
}
Floorplan_resize resize_floorplan(Result& state, const Options& options, const Timing_feedback& before, const Evaluate& evaluate,
                                  const std::function<void(Result&)>& refresh) {
  return resize_floorplan(state, options, before, evaluate, refresh, read_floorplan_library(options));
}
Floorplan_resize repair_floorplan(Result& state, const Options& options, const Timing_feedback& before, const Evaluate& evaluate,
                                  const std::function<void(Result&)>& refresh) {
  const auto                                 scanned = read_floorplan_library(options);
  const auto&                                tech    = scanned.tech;
  const auto&                                cells   = scanned.cells;
  auto                                       result  = resize_floorplan(state, options, before, evaluate, refresh, scanned);
  std::set<std::string>                      tried;
  rapidjson::StringBuffer                    trace;
  rapidjson::Writer<rapidjson::StringBuffer> w(trace);
  w.StartObject();
  w.Key("model");
  w.String("fp_placement_rc_gate_buffer_steiner_repair");
  w.Key("trials");
  w.StartArray();
  if (!result.timing.pins.empty()) {
    w.StartObject();
    w.Key("kind");
    w.String("forest_sweep");
    bool                                             keep    = false;
    unsigned                                         changed = 0;
    std::vector<std::pair<std::string, std::string>> failures;
    try {
      auto library = std::make_shared<hhds::GraphLibrary>();
      auto graph
          = partition::flatten_hierarchy(state.graph.get(), library.get(), state.graph->get_name(), nullptr, false, {}, {}, true);
      if (!graph) {
        throw std::runtime_error("cannot clone FP electrical sweep");
      }
      const auto            protected_names = protected_buffers(*state.graph, cells, scanned.special, true, options.control_ports);
      auto        candidates = edits(*state.graph, cells, result.timing, protected_names, state, options, tech, &scanned.drives);
      Apply_index index(*graph, state);
      std::set<std::string> visited;
      for (const auto& edit : candidates) {
        // Pin-level trees already cover every retained fork; a legacy rebuild of the same
        // buffer would reuse stale pre-sweep connectivity (see repair_buffers).
        if (edit.kind != "tree" || !visited.insert(edit.driver.empty() ? edit.node : edit.driver).second
            || (edit.driver.empty() && !index.nodes.contains(edit.node))) {
          continue;
        }
        try {
          apply(*graph, cells, edit, protected_names, state, result.timing, options, tech, &index);
          ++changed;
        } catch (const std::runtime_error& error) {
          failures.emplace_back(edit.node, error.what());
        }
      }
      if (changed) {
        Result trial      = state;
        trial.fp_library  = std::move(library);
        trial.graph       = std::move(graph);
        trial.net_weights = std::move(index.weights);
        trial.fp_buffer_origins.insert(index.origins.begin(), index.origins.end());
        refresh(trial);
        const auto next = evaluate(trial);
        keep            = retain(result.timing, next);
        if (keep) {
          const auto previous_owner = state.fp_library;  // outlive the assignment that drops the old graph
          state                     = std::move(trial);
          result.timing             = next;
          ++result.repair_accepted;  // one accepted STA proposal, same unit as repair_proposed
        }
      }
    } catch (const std::exception& error) {
      w.Key("reason");
      w.String(error.what());
    }
    w.Key("eligibility_failures");
    w.StartArray();
    for (const auto& [name, reason] : failures) {
      w.StartObject();
      w.Key("instance");
      w.String(name.c_str());
      w.Key("reason");
      w.String(reason.c_str());
      w.EndObject();
    }
    w.EndArray();
    if (changed) {
      ++result.repair_proposed;  // one STA proposal; the batch size is reported as "changed"
    }
    if (!keep) {
      refresh(state);
    }
    w.Key("changed");
    w.Uint(changed);
    w.Key("accepted");
    w.Bool(keep);
    w.EndObject();
  }
  // FP coordinates can overlap. Do not run ECO legalization or freeze macros:
  // trial only the wiring/masters, seeded Steiner buffers and fresh physical RC.
  unsigned individual          = 0;  // the forest batch above is not charged against the individual trials
  unsigned individual_accepted = 0;
  while (individual < 8 && individual_accepted < 4) {
    const auto protected_names = protected_buffers(*state.graph, cells, scanned.special, true, options.control_ports);
    const auto candidates      = edits(*state.graph, cells, result.timing, protected_names, state, options, tech, &scanned.drives);
    bool       progress        = false;
    for (const auto& edit : candidates) {
      if (edit.kind == "resize" || edit.kind == "gate_resize") {
        continue;  // batch above
      }
      const auto key = std::format("{}:{}:{}:{}:{}", edit.kind, edit.node, edit.master, edit.fanout, edit.driver);
      if (!tried.insert(key).second) {
        continue;
      }
      ++result.repair_proposed;
      ++individual;
      w.StartObject();
      w.Key("kind");
      w.String(edit.kind.c_str());
      w.Key("node");
      w.String(edit.node.c_str());
      bool keep = false;
      try {
        auto library = std::make_shared<hhds::GraphLibrary>();
        auto graph
            = partition::flatten_hierarchy(state.graph.get(), library.get(), state.graph->get_name(), nullptr, false, {}, {}, true);
        if (!graph) {
          throw std::runtime_error("cannot clone FP repair trial");
        }
        Apply_index trial_index(*graph, state);
        const auto  tree = apply(*graph, cells, edit, protected_names, state, result.timing, options, tech, &trial_index);
        if (tree) {
          w.Key("topology");
          w.String("flute_steiner");
          w.Key("buffers");
          w.Uint64(tree->buffers.size());
        }
        Result trial      = state;
        trial.fp_library  = std::move(library);
        trial.graph       = std::move(graph);
        trial.net_weights = std::move(trial_index.weights);
        trial.fp_buffer_origins.insert(trial_index.origins.begin(), trial_index.origins.end());
        refresh(trial);
        const auto next = evaluate(trial);
        keep            = retain(result.timing, next);
        w.Key("delay");
        w.Double(next.delay);
        w.Key("area");
        w.Double(next.area);
        if (keep) {
          const auto previous_owner = state.fp_library;  // outlive the assignment that drops the old graph
          state                     = std::move(trial);
          result.timing             = next;
          ++result.repair_accepted;
          ++individual_accepted;
          progress = true;
        }
      } catch (const std::exception& error) {
        w.Key("reason");
        w.String(error.what());
      }
      if (!keep) {
        refresh(state);  // discard graph, pins, RC and centroid remapping together
      }
      w.Key("accepted");
      w.Bool(keep);
      w.EndObject();
      if (progress || individual >= 8) {
        break;
      }
    }
    if (!progress) {
      break;
    }
  }
  w.EndArray();
  w.Key("accepted");
  w.Uint(result.repair_accepted);
  w.EndObject();
  result.report = trace.GetString();
  // The FP caller refreshes the checkpoint weights from result.timing.
  return result;
}
Repair_result repair_buffers(Result initial, const Options& requested, const Evaluate& evaluate, Repair_scope scope,
                             std::optional<Timing_feedback> prior_timing) {
  std::optional<Options> resolved_options;
  if (requested.cell_halo) {
    resolved_options = final_cell_options(requested);
  }
  const auto& options = resolved_options ? *resolved_options : requested;
  if (options.cell_halo) {
    check_placed_legality(initial, options);
  }
  const auto        prior_qor  = initial.qor;
  const bool        design     = scope == Repair_scope::design || scope == Repair_scope::forest;
  const std::string report_key = scope == Repair_scope::recovery ? "buffer_recovery" : design ? "repair" : "resize";
  if (!options.full) {
    throw std::invalid_argument("buffer repair requires legal full placement");
  }
  Repair_result result;
  result.placed                           = std::move(initial);
  const auto              tech            = physical::read_lef(options.lefs);
  const auto              cells           = inventory(options, tech);
  auto                    score           = prior_timing ? std::move(*prior_timing) : evaluate(result.placed);
  const auto protected_names = protected_buffers(*result.placed.graph, cells, options.liberty, true, options.control_ports);
  std::set<std::string>   tried;
  rapidjson::StringBuffer trace;
  rapidjson::Writer<rapidjson::StringBuffer> w(trace);
  w.StartObject();
  w.Key("model");
  w.String(design ? "placement_rc_steiner_repair" : "placement_rc_gate_and_buffer_repair");
  w.Key("before_delay");
  w.Double(score.delay);
  w.Key("before_area");
  w.Double(score.area);
  w.Key("trials");
  w.StartArray();
  unsigned       accepted = 0, attempt = 0;
  const bool     physical_recovery = !score.pins.empty() && scope != Repair_scope::sizing && scope != Repair_scope::forest;
  const unsigned sweep_budget      = physical_recovery ? 12 : 24;
  const auto     directory         = options.output_dir + "/" + report_key;
  fs::create_directories(directory);
  // Real physical feedback enables a design-wide electrical sweep. One
  // proposal per driver is applied transactionally, then legalized and timed
  // once for the batch. Rejected batches are bisected; the STA budget bounds
  // evaluations, not which two instance names happen to appear first.
  if (!score.pins.empty() && scope != Repair_scope::recovery) {
    const auto sweep = [&](bool trees) {
      const auto phase_limit = std::min(sweep_budget, attempt + 12);
      const auto candidates  = edits(*result.placed.graph, cells, score, protected_names, result.placed, options, tech);
      Node_index index;
      for (auto n : result.placed.graph->body().nodes()) {
        if (!n.get_name().empty()) {
          index.emplace(std::string(n.get_name()), n);
        }
      }
      std::vector<Edit>     selected;
      std::set<std::string> selected_names;
      for (const auto& edit : candidates) {
        const bool tree = edit.kind == "tree" || edit.kind == "rebuild";
        if (tree != trees || (!tree && edit.kind != "resize" && edit.kind != "gate_resize")) {
          continue;
        }
        // Pin-level tree entries cover ports and every retained distribution
        // fork. A second legacy rebuild at a fork would delete that cut point
        // using the stale pre-sweep connectivity after its nets were repaired.
        if (trees && edit.kind != "tree") {
          continue;
        }
        const auto key = edit.driver.empty() ? edit.node : edit.driver;
        if (selected_names.contains(key)) {
          continue;
        }
        if (!trees) {
          auto        node = index.at(edit.node);
          const auto* from = combinational(node, cells);
          const auto& to   = cells.at(edit.master);
          const auto  pin  = score.pins.find(physical_label(output(node, *from)));
          if (pin == score.pins.end() || !pin->second.load_ff) {
            continue;
          }
          const auto&  feedback     = pin->second;
          const double slack_ps     = feedback.slack.value_or(0) * score.time_unit_ps;
          const bool   recover_area = slack_ps > 0 && drive_stress(*from, feedback, score.time_unit_ps) <= 1;
          if (recover_area) {
            if (to.area >= from->area || -edit.gain_ps > .5 * slack_ps
                || (to.max_capacitance_ff && *feedback.load_ff > *to.max_capacitance_ff)) {
              continue;
            }
          } else if (edit.gain_ps <= 1e-6) {
            continue;
          }
          if (std::abs(tech.macros.at(to.name).height - tech.macros.at(from->name).height) > 1e-6) {
            continue;
          }
        }
        selected.push_back(edit);
        selected_names.insert(key);
      }
      if (trees) {
        std::stable_sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
          // Non-buffer drivers own entire forests. Process them before residual
          // buffer roots (e.g. behind primary ports), so nested branches vanish.
          return (a.kind == "tree") > (b.kind == "tree");
        });
      }
      index.clear();
      std::vector<std::vector<size_t>> pending(1);
      pending.front().resize(selected.size());
      std::iota(pending.front().begin(), pending.front().end(), 0);
      const auto run = [&](const std::vector<size_t>& batch) {
        if (batch.empty() || attempt >= phase_limit) {
          return;
        }
        w.StartObject();
        w.Key("action");
        w.String(trees ? "forest_sweep" : "sizing_sweep");
        w.Key("drivers");
        w.Uint64(batch.size());
        std::vector<size_t>                              retry;
        size_t                                           feedback_pruned = 0;
        bool                                             kept            = false;
        size_t                                           changed = 0, buffers_added = 0, sequential_drivers = 0;
        size_t                                           upsized = 0, downsized = 0, same_area = 0;
        const auto                                       trial_start = std::chrono::steady_clock::now();
        std::vector<std::pair<std::string, std::string>> failures;
        try {
          hhds::GraphLibrary draft;
          auto               graph = partition::flatten_hierarchy(result.placed.graph.get(),
                                                                  &draft,
                                                                  result.placed.graph->get_name(),
                                                                  nullptr,
                                                                  false,
                                                                  {},
                                                                  {},
                                                                  true);
          if (!graph) {
            throw std::runtime_error("cannot clone electrical sweep");
          }
          Apply_index              draft_index(*graph, result.placed);
          std::vector<std::string> changed_names;
          for (const auto i : batch) {
            const auto& edit = selected[i];
            if (edit.driver.empty() && !draft_index.nodes.contains(edit.node)) {
              continue;  // covered by an upstream forest
            }
            try {
              const Buffer* from = trees ? nullptr : combinational(draft_index.nodes.at(edit.node), cells);
              const auto    tree = apply(*graph, cells, edit, protected_names, result.placed, score, options, tech, &draft_index);
              if (tree) {
                buffers_added += tree->buffers.size();
              }
              if (from) {  // counted only once apply() succeeded
                const auto& to  = cells.at(edit.master);
                upsized        += to.area > from->area;
                downsized      += to.area < from->area;
                same_area      += to.area == from->area;
              }
              changed_names.push_back(edit.node);
              ++changed;
            } catch (const std::runtime_error& error) {
              // apply plans and validates the complete tree before changing it.
              // Eligibility failures are reported, not charged as STA trials.
              failures.emplace_back(edit.node, error.what());
            }
          }
          sequential_drivers = draft_index.sequential_drivers;
          if (changed) {
            ++attempt;
            auto trial_options = options;
            trial_options.cache_dir.clear();
            trial_options.legalize_cells = true;
            trial_options.net_weights    = std::move(draft_index.weights);
            trial_options.previous       = std::make_shared<physical::Floorplan>(result.placed.floorplan);
            trial_options.changed_cells  = std::move(changed_names);
            trial_options.output_dir     = directory + "/sweep_" + std::to_string(attempt);
            trial_options.floorplan      = directory + "/floorplan.def";
            auto frame                   = result.placed.floorplan;
            std::erase_if(frame.components, [](const auto& c) {
              return c.place.status != Ann_place::Status::FIXED && c.place.status != Ann_place::Status::LOCKED;
            });
            for (auto& port : frame.terminals) {
              port.status = Ann_place::Status::FIXED;
            }
            physical::write_def(frame, trial_options.floorplan);
            const auto placement_start = std::chrono::steady_clock::now();
            auto       owner           = std::make_unique<hhds::GraphLibrary>();
            auto       placed          = place(*graph, *owner, trial_options);
            const auto timing_start    = std::chrono::steady_clock::now();
            auto       next            = evaluate(placed);
            const auto timing_done     = std::chrono::steady_clock::now();
            w.Key("prepare_s");
            w.Double(std::chrono::duration<double>(placement_start - trial_start).count());
            w.Key("placement_rc_s");
            w.Double(std::chrono::duration<double>(timing_start - placement_start).count());
            w.Key("timing_s");
            w.Double(std::chrono::duration<double>(timing_done - timing_start).count());
            w.Key("delay");
            w.Double(next.delay);
            w.Key("area");
            w.Double(next.area);
            kept = retain(score, next);
            w.Key("rejection_reasons");
            w.StartArray();
            for (const auto reason : retention_failures(score, next)) {
              w.String(reason.data(), reason.size());
            }
            w.EndArray();
            if (!kept && !trees) {
              const auto bad = regressed_nodes(score, next, *placed.parasitics);
              for (const auto i : batch) {
                if (!bad.contains(selected[i].node)) {
                  retry.push_back(i);
                }
              }
              feedback_pruned = batch.size() - retry.size();
            }
            if (kept) {
              result.placed = std::move(placed);
              // place owns a complete independent clone; old accepted libraries
              // no longer back any live result and must not grow with trials.
              result.libraries.clear();
              result.libraries.push_back(std::move(owner));
              score = std::move(next);
              refresh_repair_weights(result.placed, options, score);
              ++accepted;
            }
          }
        } catch (const std::exception& error) {
          w.Key("reason");
          w.String(error.what());
        }
        w.Key("eligibility_failures");
        w.StartArray();
        for (const auto& [name, reason] : failures) {
          w.StartObject();
          w.Key("instance");
          w.String(name.c_str());
          w.Key("reason");
          w.String(reason.c_str());
          w.EndObject();
        }
        w.EndArray();
        w.Key("changed");
        w.Uint64(changed);
        w.Key("upsized");
        w.Uint64(upsized);
        w.Key("downsized");
        w.Uint64(downsized);
        w.Key("same_area_resized");
        w.Uint64(same_area);
        w.Key("buffers");
        w.Uint64(buffers_added);
        w.Key("sequential_driver_models");
        w.Uint64(sequential_drivers);
        w.Key("accepted");
        w.Bool(kept);
        w.Key("feedback_pruned");
        w.Uint64(feedback_pruned);
        w.Key("total_s");
        w.Double(std::chrono::duration<double>(std::chrono::steady_clock::now() - trial_start).count());
        w.EndObject();
        if (!kept && changed && batch.size() > 1 && attempt < phase_limit) {
          if (feedback_pruned && !retry.empty()) {
            pending.push_back(std::move(retry));
          } else {
            const auto middle = batch.begin() + batch.size() / 2;
            pending.emplace_back(batch.begin(), middle);
            pending.emplace_back(middle, batch.end());
          }
        }
      };
      // Breadth first: a pathological subset must not spend the whole timing
      // budget before a disjoint half of the design gets an evaluation.
      for (size_t cursor = 0; cursor < pending.size() && attempt < phase_limit; ++cursor) {
        // Own the indices while run() appends retries to pending.
        const auto batch = std::move(pending[cursor]);
        run(batch);
      }
    };
    if (scope != Repair_scope::design && scope != Repair_scope::forest) {
      sweep(false);
    }
    if (scope != Repair_scope::sizing) {
      sweep(true);
      // Buffer insertion changes the gate loads. Recover area on slack-positive
      // gates and size remaining stressed gates using fresh post-repair timing.
      // Transactional STA/RC checks still guard WNS, TNS, hold and electrical QoR.
      if (scope != Repair_scope::forest) {
        sweep(false);
      }
    }
  }
  // Reserve half the shared 24-trial budget for final physical recovery.
  // Every accepted bypass refreshes candidates against the new legal geometry.
  unsigned recovery_attempts = 0, recovery_accepted = 0;
  while ((physical_recovery && attempt < 24 && recovery_attempts < 12 && recovery_accepted < 8)
         || (score.pins.empty() && scope != Repair_scope::recovery && attempt < 24 && accepted < 8)) {
    auto candidates = physical_recovery ? final_buffer_candidates(result.placed, cells, protected_names, tech)
                                        : edits(*result.placed.graph, cells, score, protected_names, result.placed, options, tech);
    if (!physical_recovery) {
      std::erase_if(candidates, [&](const auto& edit) {
        const bool tree = edit.kind == "tree" || edit.kind == "rebuild";
        return scope == Repair_scope::sizing                                      ? tree
               : (scope == Repair_scope::design || scope == Repair_scope::forest) ? !tree
                                                                                  : false;
      });
    }
    bool progress = false;
    for (const auto& edit : candidates) {
      const auto key = std::format("{}:{}:{}:{}:{}", edit.kind, edit.node, edit.master, edit.fanout, edit.driver);
      if (!tried.insert(key).second) {
        continue;
      }
      ++attempt;
      recovery_attempts += physical_recovery;
      w.StartObject();
      w.Key("action");
      w.String(edit.kind.c_str());
      w.Key("instance");
      w.String(edit.node.c_str());
      w.Key("master");
      w.String(edit.master.c_str());
      bool kept = false;
      try {
        hhds::GraphLibrary draft;
        auto               graph = partition::flatten_hierarchy(result.placed.graph.get(),
                                                                &draft,
                                                                result.placed.graph->get_name(),
                                                                nullptr,
                                                                false,
                                                                {},
                                                                {},
                                                                true);
        if (!graph) {
          throw std::runtime_error("cannot clone buffer trial");
        }
        Apply_index trial_index(*graph, result.placed);
        const auto  tree = apply(*graph, cells, edit, protected_names, result.placed, score, options, tech, &trial_index);
        if (tree) {
          w.Key("topology");
          w.String("flute_steiner");
          w.Key("buffers");
          w.Uint64(tree->buffers.size());
        }
        auto trial_options = options;
        trial_options.cache_dir.clear();
        trial_options.legalize_cells = true;
        trial_options.net_weights    = std::move(trial_index.weights);
        trial_options.previous       = std::make_shared<physical::Floorplan>(result.placed.floorplan);
        trial_options.changed_cells  = {edit.node};
        trial_options.output_dir     = directory + "/trial_" + std::to_string(attempt);
        trial_options.floorplan      = directory + "/floorplan.def";
        auto floorplan               = result.placed.floorplan;
        std::erase_if(floorplan.components, [](const auto& c) {
          return c.place.status != Ann_place::Status::FIXED && c.place.status != Ann_place::Status::LOCKED;
        });
        for (auto& terminal : floorplan.terminals) {
          terminal.status = Ann_place::Status::FIXED;
        }
        physical::write_def(floorplan, trial_options.floorplan);
        auto       library = std::make_unique<hhds::GraphLibrary>();
        auto       placed  = place(*graph, *library, trial_options);
        const auto next    = evaluate(placed);
        w.Key("delay");
        w.Double(next.delay);
        w.Key("area");
        w.Double(next.area);
        kept = retain(score, next);
        if (kept) {
          result.placed = std::move(placed);
          result.libraries.clear();
          result.libraries.push_back(std::move(library));
          score = next;
          refresh_repair_weights(result.placed, options, score);
          ++accepted;
          recovery_accepted += physical_recovery;
          if (physical_recovery) {
            tried.clear();  // the aggregate-only fallback never retries a rejected edit
          }
          progress = true;
        }
      } catch (const std::exception& error) {
        w.Key("reason");
        w.String(error.what());
      }
      w.Key("accepted");
      w.Bool(kept);
      w.EndObject();
      if (progress || attempt >= 24 || (physical_recovery && recovery_attempts >= 12)) {
        break;
      }
    }
    if (!progress) {
      break;
    }
  }
  w.EndArray();
  w.Key("accepted");
  w.Uint(accepted);
  w.Key("attempts");
  w.Uint(attempt);
  w.Key("recovery_attempts");
  w.Uint(recovery_attempts);
  w.Key("recovery_accepted");
  w.Uint(recovery_accepted);
  w.Key("after_delay");
  w.Double(score.delay);
  w.Key("after_area");
  w.Double(score.area);
  w.Key("setup_wns");
  if (score.wns) {
    w.Double(*score.wns);
  } else {
    w.Null();
  }
  w.Key("hold_wns");
  if (score.hold_wns) {
    w.Double(*score.hold_wns);
  } else {
    w.Null();
  }
  w.EndObject();
  result.report = trace.GetString();
  // Publish only the accepted state; trials never overwrite the original DEF.
  physical::write_def(result.placed.floorplan, options.output_dir + "/out.def");
  rapidjson::Document qor;
  qor.Parse(result.placed.qor.c_str());
  rapidjson::Document repair;
  repair.Parse(result.report.c_str());
  rapidjson::Document prior;
  prior.Parse(prior_qor.c_str());
  for (const auto* key : {"timing_driven", "resize", "repair", "satlegalize"}) {
    if (prior.HasMember(key) && !qor.HasMember(key) && key != report_key) {
      qor.AddMember(rapidjson::Value(key, qor.GetAllocator()),
                    rapidjson::Value(prior[key], qor.GetAllocator()),
                    qor.GetAllocator());
    }
  }
  if (accepted > 0 && qor.HasMember("satlegalize") && qor["satlegalize"].IsObject()) {
    auto& alignment = qor["satlegalize"];
    if (alignment.HasMember("retained")) {
      if (!alignment.HasMember("previous_retained")) {
        alignment.AddMember("previous_retained", rapidjson::Value(alignment["retained"], qor.GetAllocator()), qor.GetAllocator());
      }
      alignment["retained"].SetUint(0);
    }
    if (alignment.HasMember("relaxed_by")) {
      alignment["relaxed_by"].SetString("physical_repair", qor.GetAllocator());
    } else {
      alignment.AddMember("relaxed_by", "physical_repair", qor.GetAllocator());
    }
  }
  if (qor.HasMember(report_key.c_str())) {
    qor.RemoveMember(report_key.c_str());
  }
  qor.AddMember(rapidjson::Value(report_key.c_str(), qor.GetAllocator()),
                rapidjson::Value(repair, qor.GetAllocator()),
                qor.GetAllocator());
  rapidjson::StringBuffer                    out;
  rapidjson::Writer<rapidjson::StringBuffer> qw(out);
  qor.Accept(qw);
  result.placed.qor = out.GetString();
  std::ofstream(options.output_dir + "/qor.json") << result.placed.qor << '\n';
  std::ofstream(options.output_dir + "/" + report_key + ".json") << result.report << '\n';
  result.timing = std::move(score);
  return result;
}
}  // namespace livehd::placement
