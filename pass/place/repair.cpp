// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "repair.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

#include "attrs.hpp"
#include "node_util.hpp"
#include "pass/liberty/liberty_dff.hpp"
#include "pass/partition/flatten.hpp"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "steiner_repair.hpp"
#include "technology_rc.hpp"

namespace livehd::placement {
namespace {
namespace gu    = graph_util;
namespace fs    = std::filesystem;
using Node      = hhds::Node_class;
using Pin       = hhds::Pin_class;
using Buffer    = liberty::Comb_cell;
using Inventory = std::map<std::string, Buffer>;
struct Edit {
  std::string kind, node, master;
  unsigned    fanout = 4;
};
Node named(hhds::Graph& graph, const std::string& name) {
  for (auto n : graph.body().nodes()) {
    if (n.get_name() == name) {
      return n;
    }
  }
  throw std::runtime_error("physical edit lost instance: " + name);
}
const Buffer* combinational(Node n, const Inventory& cells) {
  const auto io = n.get_subnode_io();
  if (!io) {
    return nullptr;
  }
  const auto it = cells.find(std::string(io->get_name()));
  return it == cells.end() ? nullptr : &it->second;
}
bool identity(const Buffer& cell) { return cell.inputs.size() == 1 && liberty::cell_truth(cell) == uint64_t{0xaaaaaaaaaaaaaaaa}; }
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
// matching covers uncharacterized macros and primary clocks too.
std::set<std::string> protected_buffers(hhds::Graph& graph, const Inventory& cells, const std::string& libs) {
  std::set<std::string>                        protected_names;
  std::set<uint64_t>                           visited;
  std::map<std::string, std::set<std::string>> special;
  auto                                         state = liberty::scan_dff_cells(libs);
  const auto                                   async = liberty::scan_async_dff_cells(libs);
  state.insert(state.end(), async.begin(), async.end());
  const auto latch = liberty::scan_latch_cells(libs);
  state.insert(state.end(), latch.begin(), latch.end());
  for (const auto& c : state) {
    special[c.name] = {c.clk_pin, c.reset0_pin, c.reset1_pin};
  }
  for (const auto& c : liberty::scan_icg_cells(libs)) {
    special[c.name] = {c.clk_pin, c.en_pin, c.test_pin};
  }
  const auto protect = [&](auto&& self, Pin driver) -> void {
    if (driver.is_invalid() || driver.is_const()) {
      return;
    }
    auto        n = driver.get_master_node();
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
  for (auto n : graph.body().nodes()) {
    const auto io = n.get_subnode_io();
    for (auto sink : n.inp_sorted_pins()) {
      std::string name(sink.get_pin_name());
      if (io) {
        for (const auto& decl : io->get_input_pin_decls()) {
          if (decl.port_id == sink.get_port_id()) {
            name = decl.name;
          }
        }
      }
      auto lower = name;
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
      const auto op  = gu::type_op_of(n);
      const auto off = sink.get_port_id() % Ntype::Memory_port_stride;
      if ((op == Ntype_op::Memory && (off == 2 || off == 14))
          || ((op == Ntype_op::Flop || op == Ntype_op::Latch)
              && (Ntype::get_sink_name(op, sink.get_port_id()) == "clock_pin"
                  || Ntype::get_sink_name(op, sink.get_port_id()) == "reset_pin"))
          || (io && special[std::string(io->get_name())].contains(name)) || lower.find("clock") != std::string::npos
          || lower.find("clk") != std::string::npos || lower.find("reset") != std::string::npos
          || lower.find("rst") != std::string::npos) {
        for (auto driver : sink.get_driver_pins()) {
          protect(protect, driver);
        }
      }
    }
  }
  return protected_names;
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
bool needs_repair(Node n, const Inventory& cells, const Timing_feedback& timing, const Result& state, double limit_dbu) {
  const auto io = n.get_subnode_io();
  if (!io || io->get_output_pin_decls().size() != 1) {
    return false;
  }
  auto out = n.get_driver_pin(io->get_output_pin_decls().front().port_id);
  if (out.is_invalid() || gu::bits_of(out) != 1) {
    return false;
  }
  const auto label    = physical_label(out);
  const auto physical = std::find_if(state.parasitics->nets.begin(), state.parasitics->nets.end(), [&](const auto& net) {
    return net.driver == label;
  });
  if (physical == state.parasitics->nets.end()) {
    return false;
  }
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
std::vector<Edit> edits(hhds::Graph& graph, const Inventory& cells, const Timing_feedback& timing,
                        const std::set<std::string>& protected_names, const Result& placed, const Options& options,
                        const physical::Technology& tech) {
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
  std::sort(buffers.begin(), buffers.end(), [&](const auto& a, const auto& b) {
    return critical(a) != critical(b) ? critical(a) > critical(b) : a.get_name() < b.get_name();
  });
  const Buffer* preferred = nullptr;
  for (const auto& [name, c] : cells) {
    if (identity(c) && (!preferred || std::tie(c.area, name) < std::tie(preferred->area, preferred->name))) {
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
    std::vector<const Buffer*> ladder;
    for (const auto& [name, c] : cells) {
      if (name != current->name && c.inputs == current->inputs && c.output == current->output && c.footprint == current->footprint
          && liberty::cell_truth(c) == liberty::cell_truth(*current)) {
        ladder.push_back(&c);
      }
    }
    std::sort(ladder.begin(), ladder.end(), [](const auto* a, const auto* b) { return a->area < b->area; });
    for (const auto* c : ladder) {
      result.push_back({is_buffer ? "resize" : "gate_resize", std::string(n.get_name()), c->name, 0});
    }
    if (is_buffer && needs_repair(n, cells, timing, placed, limit_dbu)) {
      result.push_back({"rebuild", std::string(n.get_name()), current->name, 2});
      if (!ladder.empty()) {
        result.push_back({"rebuild", std::string(n.get_name()), ladder.back()->name, 4});
      }
    }
  }
  for (auto n : graph.body().nodes()) {
    if (!n.get_subnode_io() || buffer(n, cells) || fixed(n) || protected_names.contains(std::string(n.get_name()))) {
      continue;
    }
    const auto io = n.get_subnode_io();
    if (io->get_output_pin_decls().size() != 1) {
      continue;
    }
    auto p = n.get_driver_pin(io->get_output_pin_decls().front().port_id);
    if (p.is_invalid() || gu::bits_of(p) != 1 || !needs_repair(n, cells, timing, placed, limit_dbu)) {
      continue;
    }
    for (const auto& [name, c] : cells) {
      if (!preferred || !identity(c) || c.footprint != preferred->footprint) {
        continue;
      }
      result.push_back({"tree", std::string(n.get_name()), name, 4});
    }
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
  for (auto p : targets) {
    p.del_sink(old_output);
  }
  n.set_subnode(io);
  for (const auto& name : to.inputs) {
    n.create_sink_pin(name).connect_driver(drivers.at(name));
  }
  auto out = n.create_driver_pin(to.output);
  gu::set_bits(out, 1);
  for (auto p : targets) {
    p.connect_driver(out);
  }
}
struct Physical_sink {
  Pin      pin;
  unsigned bit = 0, width = 1;
};
Physical_sink physical_sink(hhds::Graph& graph, const std::string& label) {
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
    for (const auto& p : graph.get_io()->get_output_pin_decls()) {
      if (p.name == name) {
        return {graph.get_output_pin(p.name), 0, static_cast<unsigned>(std::max<int64_t>(1, graph.get_io()->get_bits(p.name)))};
      }
    }
    for (const auto& p : graph.get_io()->get_output_pin_decls()) {
      if (const auto b = bit(name, p.name); b) {
        const auto width = static_cast<unsigned>(graph.get_io()->get_bits(p.name));
        if (*b < width) {
          return {graph.get_output_pin(p.name), *b, width};
        }
      }
    }
  } else {
    auto n = named(graph, label.substr(0, colon));
    if (const auto io = n.get_subnode_io()) {
      for (const auto& p : io->get_input_pin_decls()) {
        if (p.name == name) {
          return {n.get_sink_pin(p.port_id), 0, static_cast<unsigned>(std::max<int64_t>(1, io->get_bits(p.name)))};
        }
      }
      for (const auto& p : io->get_input_pin_decls()) {
        if (const auto b = bit(name, p.name); b) {
          const auto width = static_cast<unsigned>(io->get_bits(p.name));
          if (*b < width) {
            return {n.get_sink_pin(p.port_id), *b, width};
          }
        }
      }
    } else if (gu::type_op_of(n) == Ntype_op::Memory && name.starts_with("in_")) {
      const auto open         = name.find('[');
      const auto pid_text     = name.substr(3, open - 3);
      unsigned   pid          = 0;
      const auto [end, error] = std::from_chars(pid_text.data(), pid_text.data() + pid_text.size(), pid);
      const auto b            = bit(name, "in_" + pid_text);
      if (error == std::errc{} && end == pid_text.data() + pid_text.size() && b) {
        auto       p     = n.get_sink_pin(pid);
        const auto width = static_cast<unsigned>(std::max(1, gu::bits_of(p.get_driver_pin())));
        if (*b < width) {
          return {p, *b, width};
        }
      }
    }
  }
  throw std::runtime_error("cannot resolve physical repair sink: " + label);
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
std::optional<Buffer_tree> apply(hhds::Graph& graph, const Inventory& cells, const Edit& edit,
                                 const std::set<std::string>& protected_names, const Result& placed, const Timing_feedback& timing,
                                 const Options& options, const physical::Technology& tech) {
  auto        n       = named(graph, edit.node);
  const auto* current = buffer(n, cells);
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
    n.del_node();
    return {};
  }
  const auto& b    = cells.at(edit.master);
  Pin         root = current ? input(n, *current) : n.get_driver_pin(n.get_subnode_io()->get_output_pin_decls().front().port_id);
  if (root.is_const() || gu::bits_of(root) != 1) {
    throw std::runtime_error("buffer tree root is not scalar");
  }
  const auto                                     owner_color  = gu::node_color_of(n);
  const auto                                     block_attr   = n.attr(attrs::placement_block);
  const std::string                              owner_block  = block_attr.has() ? block_attr.get() : std::string{};
  const auto                                     region_attr  = n.attr(attrs::synth_region_id);
  const auto                                     owner_region = region_attr.has() ? region_attr.get() : 0;
  std::map<std::string, const physical::Rc_net*> nets;
  for (const auto& net : placed.parasitics->nets) {
    nets.emplace(net.driver, &net);
  }
  std::vector<Node>          removed;
  std::vector<Physical_sink> leaves;
  std::vector<std::string>   leaf_names;
  std::set<std::string>      visited, leaf_set;
  const auto                 collect = [&](auto&& self, const std::string& driver) -> void {
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
      const auto leaf = physical_sink(graph, point.pin);
      auto       node = leaf.pin.get_master_node();
      if (protected_names.contains(std::string(node.get_name()))) {
        throw std::runtime_error("protected tree leaf");
      }
      if (const auto* next = buffer(node, cells); next && !fixed(node) && leaf.width == 1) {
        removed.push_back(node);
        self(self, physical_label(output(node, *next)));
      } else if (leaf_set.insert(point.pin).second) {
        leaves.push_back(leaf);
        leaf_names.push_back(point.pin);
      }
    }
  };
  std::string root_name = physical_label(root);
  if (current) {
    removed.push_back(n);
    const auto entry = physical_label(n.get_sink_pin(current->inputs.front()));
    for (const auto& net : placed.parasitics->nets) {
      if (std::any_of(net.nodes.begin(), net.nodes.end(), [&](const auto& p) { return p.pin == entry; })) {
        root_name = net.driver;
        break;
      }
    }
    collect(collect, physical_label(output(n, *current)));
  } else {
    collect(collect, root_name);
  }
  if (leaves.empty() || leaves.size() > 4096) {
    throw std::runtime_error("tree leaf count outside repair budget");
  }
  const auto root_xy = placed.pin_locations.find(root_name);
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
  const auto [r, c]      = wire_rc(options, tech);
  const double input_cap = b.input_capacitance_ff.empty() ? 0 : b.input_capacitance_ff.front();
  const double cap_limit = b.max_capacitance_ff.value_or(std::numeric_limits<double>::infinity()) * 0.9;
  const auto   fanout    = b.max_fanout ? std::min(edit.fanout, static_cast<unsigned>(std::max(0.0, *b.max_fanout))) : edit.fanout;
  const auto   plan      = plan_buffers(root_xy->second,
                                        locations,
                                        capacitances,
                                        input_cap,
                                        cap_limit,
                                        fanout,
                                        wire_limit(b, r, c) * tech.dbu,
                                        c / tech.dbu);
  if (plan.buffers.empty() && !current) {
    throw std::runtime_error("Steiner split is not needed for this drive");
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
    removed_cell.del_node();
  }
  std::vector<Node>     nodes;
  std::vector<Pin>      outputs;
  unsigned              serial = 0;
  std::set<std::string> names;
  for (auto other : graph.body().nodes()) {
    names.emplace(other.get_name());
  }
  const auto& macro = tech.macros.at(b.name);
  for (const auto& branch : plan.buffers) {
    auto cell = gu::create_typed_node(graph, Ntype_op::Sub);
    cell.set_subnode(buffer_io(graph, b));
    std::string name;
    do {
      name = std::format("__place_rebuffer_{}_{}", edit.node, serial++);
    } while (!names.insert(name).second);
    cell.set_name(name);
    cell.attr(attrs::color).set(owner_color);
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
  return plan;
}

bool retain(const Timing_feedback& before, const Timing_feedback& after) {
  constexpr double epsilon   = 1e-6;
  const auto       not_worse = [&](auto a, auto b) { return !a || (b && *b + epsilon >= *a); };
  if (!std::isfinite(after.delay) || after.delay > before.delay + epsilon || !not_worse(before.wns, after.wns)
      || !not_worse(before.tns, after.tns)
      || !(!before.hold_wns || (after.hold_wns && std::min(0.0, *after.hold_wns) + epsilon >= std::min(0.0, *before.hold_wns)))
      || after.violations > before.violations || after.electrical_excess > before.electrical_excess + epsilon) {
    return false;
  }
  return after.delay + epsilon < before.delay || after.area + epsilon < before.area || after.violations < before.violations
         || after.electrical_excess + epsilon < before.electrical_excess;
}
}  // namespace
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
  result.violations        = d["electrical"]["violations"].GetUint();
  result.electrical_excess = d["electrical"]["excess"].GetDouble();
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
      p.arrival        = number("arrival");
      p.required       = number("required");
      p.slew           = number("slew");
      p.load_ff        = number("load_ff");
      p.slack          = number("slack");
      p.capacitance_ff = number("capacitance_ff");
      p.clock          = pin["clock"].GetBool();
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
Repair_result repair_buffers(Result initial, const Options& options, const Evaluate& evaluate, Repair_scope scope) {
  const auto        prior_qor  = initial.qor;
  const bool        design     = scope == Repair_scope::design;
  const std::string report_key = design ? "repair" : "resize";
  if (!options.full) {
    throw std::invalid_argument("buffer repair requires legal full placement");
  }
  Repair_result result;
  result.placed                                              = std::move(initial);
  const auto                                 tech            = physical::read_lef(options.lefs);
  const auto                                 cells           = inventory(options, tech);
  auto                                       score           = evaluate(result.placed);
  const auto                                 protected_names = protected_buffers(*result.placed.graph, cells, options.liberty);
  std::set<std::string>                      tried;
  rapidjson::StringBuffer                    trace;
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
  unsigned   accepted = 0, attempt = 0;
  const auto directory = options.output_dir + "/" + report_key;
  fs::create_directories(directory);
  while (attempt < 24 && accepted < 8) {
    auto candidates = edits(*result.placed.graph, cells, score, protected_names, result.placed, options, tech);
    std::erase_if(candidates, [&](const auto& edit) {
      const bool tree = edit.kind == "tree" || edit.kind == "rebuild";
      return scope == Repair_scope::sizing ? tree : scope == Repair_scope::design ? !tree : false;
    });
    bool progress = false;
    for (const auto& edit : candidates) {
      const auto key = std::format("{}:{}:{}:{}", edit.kind, edit.node, edit.master, edit.fanout);
      if (!tried.insert(key).second) {
        continue;
      }
      ++attempt;
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
        const auto tree = apply(*graph, cells, edit, protected_names, result.placed, score, options, tech);
        if (tree) {
          w.Key("topology");
          w.String("flute_steiner");
          w.Key("buffers");
          w.Uint64(tree->buffers.size());
        }
        auto trial_options = options;
        trial_options.cache_dir.clear();
        trial_options.legalize_cells = true;
        trial_options.net_weights    = result.placed.net_weights;
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
          result.libraries.push_back(std::move(library));
          score = next;
          ++accepted;
          progress = true;
        }
      } catch (const std::exception& error) {
        w.Key("reason");
        w.String(error.what());
      }
      w.Key("accepted");
      w.Bool(kept);
      w.EndObject();
      if (progress || attempt >= 24) {
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
  for (const auto* key : {"timing_driven", "resize", "repair"}) {
    if (prior.HasMember(key) && !qor.HasMember(key) && key != report_key) {
      qor.AddMember(rapidjson::Value(key, qor.GetAllocator()),
                    rapidjson::Value(prior[key], qor.GetAllocator()),
                    qor.GetAllocator());
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
  return result;
}
}  // namespace livehd::placement
