// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bind_graph.hpp"

#include <format>
#include <stdexcept>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "node_util.hpp"

namespace livehd::timing::fast {
namespace gu = livehd::graph_util;
namespace {
std::string bit_name(const std::string& name, int bits, int bit) { return bits > 1 ? std::format("{}[{}]", name, bit) : name; }

// The declared name of the pin of a Sub instance.
std::string declared_name(const hhds::Pin_class& pin) {
  const auto node = pin.get_master_node();
  const auto io   = node.get_subnode_io();
  if (io) {
    const auto& declarations = pin.is_sink() ? io->get_input_pin_decls() : io->get_output_pin_decls();
    for (const auto& declaration : declarations) {
      if (declaration.port_id == pin.get_port_id()) {
        return declaration.name;
      }
    }
  }
  return {};
}

struct Terminal {
  enum class Kind : uint8_t { unknown, constant, driver };
  Kind     kind  = Kind::unknown;
  uint32_t pin   = no_index;  // the netlist pin that drives
  bool     value = false;     // of a constant
};

class Binder {
public:
  Binder(const hhds::Graph& flat, const Cell_types& types, Bound_graph& out) : flat_(flat), types_(types), out_(out) {}

  void run() {
    auto& netlist = *out_.netlist;
    // The ports, one cell per bit.
    for (const auto& decl : flat_.get_io()->get_input_pin_decls()) {
      const int bits = std::max<int>(1, static_cast<int>(decl.bits));
      for (int b = 0; b < bits; ++b) {
        const auto name = bit_name(decl.name, bits, b);
        const auto cell = netlist.add_input_port(name);
        out_.cell_node.emplace_back();
        input_bit_.emplace(name, netlist.pin(cell, 0u));
      }
    }
    struct Output_bit {
      std::string name;
      int         bit;
      uint32_t    cell;
    };
    std::vector<Output_bit> outputs;
    for (const auto& decl : flat_.get_io()->get_output_pin_decls()) {
      const int bits = std::max<int>(1, static_cast<int>(decl.bits));
      for (int b = 0; b < bits; ++b) {
        const auto cell = netlist.add_output_port(bit_name(decl.name, bits, b));
        out_.cell_node.emplace_back();
        outputs.push_back({decl.name, b, cell});
      }
    }
    // The cells.
    std::vector<hhds::Node_class> cells;
    for (auto node : flat_.body().nodes(hhds::Node_order::forward)) {
      if (node.get_debug_nid() == hhds::Graph::INPUT_NODE || node.get_debug_nid() == hhds::Graph::OUTPUT_NODE) {
        continue;
      }
      const auto op = gu::type_op_of(node);
      if (op != Ntype_op::Sub) {
        if (op != Ntype_op::Get_mask && op != Ntype_op::Set_mask && op != Ntype_op::Concat && op != Ntype_op::And
            && op != Ntype_op::Or && op != Ntype_op::SHL && op != Ntype_op::SRA && op != Ntype_op::Sext) {
          ++out_.unmapped_nodes;
        }
        continue;
      }
      const auto declaration = node.get_subnode_io();
      if (!declaration || declaration->get_graph()) {
        out_.unsupported.push_back("not a flat cell instance: " + std::string(node.get_name()));
        ++out_.opaque_cells;
        continue;
      }
      const auto type = types_.find(declaration->get_name());
      if (type == no_index) {
        out_.unsupported.push_back(
            std::format("uncharacterized cell: {} ({})", std::string(node.get_name()), std::string(declaration->get_name())));
        ++out_.opaque_cells;
        continue;
      }
      const auto cell = netlist.add_cell(type);
      out_.cell_node.push_back(node);
      cell_of_node_.emplace(node, cell);
      cells.push_back(node);
      ++out_.mapped_cells;
    }
    if (out_.unmapped_nodes != 0) {
      out_.unsupported.push_back(
          std::format("{} logic nodes are not mapped to cells: the pins behind them are undriven", out_.unmapped_nodes));
    }
    // The nets: every input pin of every cell, and every output port bit, to its resolved driver.
    for (const auto& node : cells) {
      const auto cell = cell_of_node_.at(node);
      for (auto sink : node.inp_sorted_pins()) {
        const auto name  = declared_name(sink);
        const auto local = types_.find_pin(netlist.type_of(cell), name);
        if (local == no_index) {
          out_.unsupported.push_back(std::format("missing Liberty pin: {}:{}", std::string(node.get_name()), name));
          continue;
        }
        connect(sink.get_driver_pins(), 0, netlist.pin(cell, local), std::string(node.get_name()) + ":" + name);
      }
    }
    for (const auto& output : outputs) {
      const auto pin = flat_.get_output_pin(output.name);
      if (pin.is_invalid()) {
        continue;
      }
      connect(pin.get_driver_pins(), output.bit, netlist.pin(output.cell, 0u), output.name);
    }
    netlist.finalize();
    if (out_.constant_sinks != 0) {
      out_.unsupported.push_back(std::format("{} sink pins are driven by a constant: not timed", out_.constant_sinks));
    }
    if (out_.multiple_driver_sinks != 0) {
      out_.unsupported.push_back(std::format("{} sink pins have more than one driver: not timed", out_.multiple_driver_sinks));
    }
    if (out_.unresolved_sinks != 0) {
      out_.unsupported.push_back(
          std::format("{} sink pins are driven through wiring that could not be resolved: not timed", out_.unresolved_sinks));
    }
  }

private:
  template <class Drivers>
  void connect(const Drivers& drivers, int bit, uint32_t sink, const std::string& label) {
    if (drivers.empty()) {
      ++out_.undriven_sinks;
      return;
    }
    if (drivers.size() > 1) {
      ++out_.multiple_driver_sinks;
      return;
    }
    Terminal terminal;
    try {
      terminal = resolve(drivers.front(), bit, 0);
    } catch (const std::exception& error) {
      out_.unsupported.push_back(std::format("{}: {}", label, error.what()));
      ++out_.unresolved_sinks;
      return;
    }
    switch (terminal.kind) {
      case Terminal::Kind::driver  : out_.netlist->connect(terminal.pin, sink); break;
      case Terminal::Kind::constant: ++out_.constant_sinks; break;
      case Terminal::Kind::unknown : ++out_.unresolved_sinks; break;
    }
  }

  // The cell pin or port bit that drives bit `b` of `pin`, through the wiring nodes.
  Terminal resolve(const hhds::Pin_class& pin, int b, int depth) {
    if (pin.is_invalid() || b < 0) {
      throw std::runtime_error("invalid wiring operand");
    }
    if (depth > 4096) {
      throw std::runtime_error("wiring deeper than 4096 nodes");
    }
    if (pin.is_const()) {
      const auto& value = gu::const_of(pin);
      if (value.unknown_bit_test(b)) {
        return {};
      }
      return {Terminal::Kind::constant, no_index, value.bit_test(b)};
    }
    const auto key = std::pair{static_cast<uint64_t>(pin.get_debug_pid()), b};
    if (const auto found = memo_.find(key); found != memo_.end()) {
      return found->second;
    }
    if (!busy_.insert(key).second) {
      throw std::runtime_error("loop in the wiring");
    }
    const auto node  = pin.get_master_node();
    const auto input = [&](std::string_view name, int which) {
      const auto driver = gu::get_driver_of_sink_name(node, name);
      if (driver.is_invalid()) {
        throw std::runtime_error(std::format("input {} of a wiring node is undriven", name));
      }
      return resolve(driver, which, depth + 1);
    };
    Terminal out;
    if (node.is_input_node()) {
      const auto name  = std::string(pin.get_pin_name());
      const auto width = std::max<int>(1, static_cast<int>(flat_.get_io()->get_bits(name)));
      const auto at    = input_bit_.find(bit_name(name, width, std::min(b, width - 1)));
      if (b >= width && gu::is_unsign(pin)) {
        out = {Terminal::Kind::constant, no_index, false};
      } else if (at != input_bit_.end()) {
        out = {Terminal::Kind::driver, at->second, false};
      }
    } else if (const auto cell = cell_of_node_.find(node); cell != cell_of_node_.end()) {
      const auto local = types_.find_pin(out_.netlist->type_of(cell->second), declared_name(pin));
      if (local != no_index && b == 0) {
        out = {Terminal::Kind::driver, out_.netlist->pin(cell->second, local), false};
      } else if (local != no_index) {
        out = {Terminal::Kind::constant, no_index, false};  // a one-bit cell output is zero-extended
      }
    } else {
      const auto op    = gu::type_op_of(node);
      const int  width = std::max(1, gu::bits_of(pin));
      if (b >= width) {
        // Past the declared width the value extends; a signed pin repeats its top bit.
        out = gu::is_unsign(pin) ? Terminal{Terminal::Kind::constant, no_index, false} : resolve(pin, width - 1, depth + 1);
      } else if (op == Ntype_op::Get_mask || op == Ntype_op::Set_mask) {
        const auto range = gu::bit_range(node);
        if (!range) {
          throw std::runtime_error("non-constant bit range");
        }
        if (op == Ntype_op::Get_mask) {
          out = input("a", b + range->first);
        } else {
          out = b >= range->first && b < range->second ? input("value", b - range->first) : input("a", b);
        }
      } else if (op == Ntype_op::Concat) {
        // The lanes of a concat are the same for every bit asked of it: build them, and the lane of each bit, once.
        auto cached = lanes_.find(node);
        if (cached == lanes_.end()) {
          Concat_cache entry;
          entry.lanes = gu::concat_lanes(node);
          if (entry.lanes.empty()) {
            throw std::runtime_error("malformed concat");
          }
          int total = 0;
          for (const auto& lane : entry.lanes) {
            total = std::max<int>(total, lane.offset + lane.width);
          }
          entry.lane_of_bit.assign(static_cast<size_t>(total), -1);
          for (size_t i = 0; i < entry.lanes.size(); ++i) {
            for (int k = 0; k < entry.lanes[i].width; ++k) {
              entry.lane_of_bit[static_cast<size_t>(entry.lanes[i].offset + k)] = static_cast<int32_t>(i);  // a later lane wins, as before
            }
          }
          cached = lanes_.emplace(node, std::move(entry)).first;
        }
        out = {Terminal::Kind::constant, no_index, false};
        if (b >= 0 && static_cast<size_t>(b) < cached->second.lane_of_bit.size() && cached->second.lane_of_bit[b] >= 0) {
          const auto& lane = cached->second.lanes[cached->second.lane_of_bit[b]];
          out              = resolve(lane.value, b - lane.offset, depth + 1);
        }
      } else if (op == Ntype_op::Or || op == Ntype_op::And) {
        // A constant that decides the gate, or the single remaining operand.
        const bool dominant = op == Ntype_op::Or;
        out                 = {Terminal::Kind::constant, no_index, !dominant};
        bool seen           = false;
        for (auto sink : node.inp_sorted_pins()) {
          for (auto driver : sink.get_driver_pins()) {
            const auto operand = resolve(driver, b, depth + 1);
            if (operand.kind == Terminal::Kind::constant) {
              if (operand.value == dominant) {
                out  = operand;
                seen = true;
                break;
              }
              continue;
            }
            if (!seen) {
              out  = operand;
              seen = true;
            } else if (out.kind != operand.kind || out.pin != operand.pin) {
              throw std::runtime_error("unmapped Boolean operation");
            }
          }
          if (out.kind == Terminal::Kind::constant && out.value == dominant) {
            break;
          }
        }
      } else if (op == Ntype_op::SHL || op == Ntype_op::SRA || op == Ntype_op::Sext) {
        const auto value  = gu::get_driver_of_sink_name(node, "a");
        const auto amount = gu::get_driver_of_sink_name(node, "b");
        if (!amount.is_const() || !gu::const_of(amount).is_just_i64()) {
          throw std::runtime_error("shift by a non-constant amount");
        }
        const auto shift = gu::const_of(amount).to_just_i64();
        if (shift < 0 || shift > std::numeric_limits<int>::max() - b) {
          throw std::runtime_error("invalid shift");
        }
        if (op == Ntype_op::SHL) {
          out = b < shift ? Terminal{Terminal::Kind::constant, no_index, false}
                          : resolve(value, b - static_cast<int>(shift), depth + 1);
        } else if (op == Ntype_op::SRA) {
          out = resolve(value, b + static_cast<int>(shift), depth + 1);
        } else {
          out = resolve(value, std::min(b, static_cast<int>(shift)), depth + 1);
        }
      }  // anything else (a memory, an unmapped operator) leaves the terminal unknown
    }
    busy_.erase(key);
    memo_.emplace(key, out);
    return out;
  }

  const hhds::Graph&                                      flat_;
  const Cell_types&                                       types_;
  Bound_graph&                                            out_;
  absl::flat_hash_map<hhds::Node_class, uint32_t>         cell_of_node_;
  absl::flat_hash_map<std::string, uint32_t>              input_bit_;
  absl::flat_hash_map<std::pair<uint64_t, int>, Terminal> memo_;
  struct Concat_cache {
    std::vector<gu::Concat_lane> lanes;
    std::vector<int32_t>         lane_of_bit;
  };
  absl::flat_hash_map<hhds::Node_class, Concat_cache> lanes_;
  absl::flat_hash_set<std::pair<uint64_t, int>>           busy_;
};
}  // namespace

Bound_graph bind_graph(const hhds::Graph& flat, const Cell_types& types) {
  Bound_graph out;
  out.netlist = std::make_unique<Netlist>(types);
  Binder(flat, types, out).run();
  return out;
}

Bound_names::Bound_names(const Bound_graph& bound) : bound_(bound) {
  cells_.reserve(bound.cell_node.size());
  for (uint32_t cell = 0; cell < bound.cell_node.size(); ++cell) {
    if (bound.cell_node[cell].is_valid()) {
      cells_.emplace(bound.cell_node[cell].get_name(), cell);
    }
  }
}

uint32_t Bound_names::cell(std::string_view instance) const {
  if (const auto it = cells_.find(instance); it != cells_.end()) {
    return it->second;
  }
  // A constraint file written against a Yosys netlist names a register by its fine-grain cell,
  // `path.reg[3]$_DFF_PP0_`; here the flip-flop of that register is the instance `path.reg[3]`.
  if (instance.size() > 3 && instance.back() == '_') {
    if (const auto tail = instance.rfind("$_"); tail != std::string_view::npos && tail != 0) {
      const auto kind = instance.substr(tail + 2, instance.size() - tail - 3);
      if (!kind.empty() && kind.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") == std::string_view::npos) {
        if (const auto it = cells_.find(instance.substr(0, tail)); it != cells_.end()) {
          return it->second;
        }
      }
    }
  }
  return no_index;
}

void Bound_names::cells(const std::function<void(std::string_view, uint32_t)>& visit) const {
  for (uint32_t cell = 0; cell < bound_.cell_node.size(); ++cell) {
    if (bound_.cell_node[cell].is_valid()) {
      visit(bound_.cell_node[cell].get_name(), cell);
    }
  }
}

void Bound_names::gather_nets() const {
  if (nets_ready_) {
    return;
  }
  nets_ready_         = true;
  const auto& netlist = *bound_.netlist;
  const auto& types   = netlist.types();
  for (uint32_t cell = 0; cell < bound_.cell_node.size(); ++cell) {
    const auto& node = bound_.cell_node[cell];
    if (!node.is_valid()) {
      continue;
    }
    for (const auto& pin : node.out_pins()) {
      const auto local = types.find_pin(netlist.type_of(cell), declared_name(pin));
      if (local == no_index) {
        continue;
      }
      nets_.emplace(gu::wire_name(pin), netlist.pin(cell, local));
    }
  }
}

uint32_t Bound_names::net_driver(std::string_view net) const {
  // The net of a register carries the register's name in an RTL netlist; here that name is the flip-flop
  // instance, and its net is the one its output drives.
  if (const auto it = cells_.find(net); it != cells_.end()) {
    const auto& netlist = *bound_.netlist;
    const auto  type    = netlist.type_of(it->second);
    uint32_t    first   = no_index;
    for (uint32_t local = 0; local < netlist.types().type(type).pin_count; ++local) {
      const auto pin = netlist.pin(it->second, local);
      if (netlist.pin_type(pin).direction != Direction::output) {
        continue;
      }
      if (netlist.net_of(pin) != no_index) {
        return pin;
      }
      first = first == no_index ? pin : first;
    }
    if (first != no_index) {
      return first;
    }
  }
  gather_nets();
  const auto it = nets_.find(net);
  return it == nets_.end() ? no_index : it->second;
}

void Bound_names::nets(const std::function<void(std::string_view, uint32_t)>& visit) const {
  gather_nets();
  for (const auto& [name, driver] : nets_) {
    visit(name, driver);
  }
}

}  // namespace livehd::timing::fast
