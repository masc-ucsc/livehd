// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "bind_graph.hpp"

#include <format>
#include <optional>
#include <stdexcept>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "attrs.hpp"
#include "node_util.hpp"

namespace livehd::timing::fast {
namespace gu = livehd::graph_util;
namespace {
std::string bit_name(const std::string& name, int bits, int bit) { return bits > 1 ? std::format("{}[{}]", name, bit) : name; }

// A node or pin of one instance context by number: the context (an instance of a definition, so one graph), the id
// in that graph, and a bit.
struct Handle_key {
  uint32_t ctx = 0;
  uint64_t id  = 0;
  int32_t  bit = 0;
  bool     operator==(const Handle_key&) const = default;
  template <typename H>
  friend H AbslHashValue(H h, const Handle_key& k) {
    return H::combine(std::move(h), k.ctx, k.id, k.bit);
  }
};

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

// The declared width of the pin of a Sub instance (1 when unknown).
int declared_bits(const hhds::Pin_class& pin) {
  const auto node = pin.get_master_node();
  const auto io   = node.get_subnode_io();
  if (io) {
    const auto& declarations = pin.is_sink() ? io->get_input_pin_decls() : io->get_output_pin_decls();
    for (const auto& declaration : declarations) {
      if (declaration.port_id == pin.get_port_id()) {
        return std::max<int>(1, static_cast<int>(declaration.bits));
      }
    }
  }
  return 1;
}

// The cell-type pin of bit `bit` of a Sub instance pin: the pin itself when the Liberty cell has it by that name
// (a one-bit pin), else the Liberty bus member `name[bit]` (pass/liberty expands a bus into one pin per bit).
uint32_t cell_pin_of_bit(const Cell_types& types, uint32_t type, const std::string& name, int bits, int bit) {
  if (bits <= 1) {
    if (const auto local = types.find_pin(type, name); local != no_index) {
      return bit == 0 ? local : no_index;
    }
  }
  return types.find_pin(type, std::format("{}[{}]", name, bit));
}

struct Terminal {
  enum class Kind : uint8_t { unknown, constant, driver, undriven };
  Kind     kind  = Kind::unknown;
  uint32_t pin   = no_index;  // the netlist pin that drives
  bool     value = false;     // of a constant
};

// The binder. Flat: one context, the graph body, where an instance of a module is not a cell. Hierarchical: the
// instance tree of the top walked in place (no copy); every instance of a module with a body is a context, and a
// connection crosses an instance boundary only at the module IO, as a flattened copy would rewire it
// (pass/partition/flatten): a module input resolves to the parent's driver of that instance pin, an instance output to
// the child's driver of that output, and the declared port is a cast (an unsigned port of w bits reads 0 above w, a
// signed one repeats bit w-1).
class Binder {
public:
  Binder(hhds::Graph& top, const Cell_types& types, Bound_graph& out, bool hier,
         absl::flat_hash_map<uint32_t, bool>* constants = nullptr)
      : top_(top), types_(types), out_(out), hier_(hier), constants_(constants) {}

  void run() {
    auto& netlist = *out_.netlist;
    if (hier_) {
      out_.prefixes.emplace_back();  // the top (also the prefix of the port cells)
    }
    // The ports, one cell per bit.
    for (const auto& decl : top_.get_io()->get_input_pin_decls()) {
      const int bits = std::max<int>(1, static_cast<int>(decl.bits));
      for (int b = 0; b < bits; ++b) {
        const auto name = bit_name(decl.name, bits, b);
        const auto cell = netlist.add_input_port(name);
        add_port_row(name);
        input_bit_.emplace(name, netlist.pin(cell, 0u));
      }
    }
    struct Output_bit {
      std::string name;
      int         bit;
      uint32_t    cell;
    };
    std::vector<Output_bit> outputs;
    for (const auto& decl : top_.get_io()->get_output_pin_decls()) {
      const int bits = std::max<int>(1, static_cast<int>(decl.bits));
      for (int b = 0; b < bits; ++b) {
        const auto name = bit_name(decl.name, bits, b);
        const auto cell = netlist.add_output_port(name);
        add_port_row(name);
        outputs.push_back({decl.name, b, cell});
      }
    }
    // The contexts and their cells.
    contexts_.push_back(Context{&top_, no_index, {}, 0, {}, {}});
    walk(0);
    if (out_.unmapped_nodes != 0) {
      out_.unsupported.push_back(
          std::format("{} logic nodes are not mapped to cells: the pins behind them are undriven", out_.unmapped_nodes));
    }
    // The nets: every input pin of every cell, and every output port bit, to its resolved driver.
    for (const auto& [ctx, node] : cells_) {
      const auto cell = cell_of_node_.at(Handle_key{ctx, static_cast<uint64_t>(node.get_debug_nid()), 0});
      for (const auto sink : node.inp_sorted_pins()) {
        const auto name = declared_name(sink);
        const auto type = netlist.type_of(cell);
        if (const auto local = types_.find_pin(type, name); local != no_index) {
          connect(ctx, sink.get_driver_pins(), 0, netlist.pin(cell, local), [&] { return name_of(ctx, node) + ":" + name; });
          continue;
        }
        // A multi-bit pin of a macro: one Liberty bus member per bit.
        const int bits  = declared_bits(sink);
        bool      bound = false;
        for (int b = 0; b < bits; ++b) {
          const auto local = cell_pin_of_bit(types_, type, name, bits, b);
          if (local == no_index) {
            continue;
          }
          bound = true;
          connect(ctx, sink.get_driver_pins(), b, netlist.pin(cell, local), [&] {
            return std::format("{}:{}[{}]", name_of(ctx, node), name, b);
          });
        }
        if (!bound) {
          out_.unsupported.push_back(std::format("missing Liberty pin: {}:{}", name_of(ctx, node), name));
        }
      }
    }
    for (const auto& output : outputs) {
      const auto pin = top_.get_output_pin(output.name);
      if (pin.is_invalid()) {
        continue;
      }
      connect(0, pin.get_driver_pins(), output.bit, netlist.pin(output.cell, 0u), [&] { return output.name; });
    }
    // Memory endpoints: every runtime bit a memory samples (address, write data, enable, whole-array update,
    // its enable and the reset), checked like an output with no delay. A constant bit has nothing to check.
    for (const auto& [ctx, node] : memories_) {
      for (const auto sink : node.inp_sorted_pins()) {
        const auto off = sink.get_port_id() % Ntype::Memory_port_stride;
        if (off != 0 && off != 3 && off != 4 && off != 12 && off != 13 && off != 14) {
          continue;
        }
        const auto drivers = sink.get_driver_pins();
        if (drivers.size() != 1 || drivers.front().is_const()) {
          continue;
        }
        const int bits = std::max(1, gu::bits_of(drivers.front()));
        for (int b = 0; b < bits; ++b) {
          Terminal terminal;
          try {
            terminal = resolve(ctx, drivers.front(), b, 0);
          } catch (const std::exception&) {
            ++out_.unresolved_sinks;
            continue;
          }
          if (terminal.kind != Terminal::Kind::driver) {
            continue;
          }
          const auto name = std::format("{}:in_{}[{}]", name_of(ctx, node), sink.get_port_id(), b);
          const auto cell = netlist.add_output_port(name);
          add_port_row(name);
          netlist.connect(terminal.pin, netlist.pin(cell, 0u));
        }
      }
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
  // One instance of a definition: the top, or an instance of a module with a body under its parent context.
  struct Context {
    hhds::Graph*                                 graph  = nullptr;
    uint32_t                                     parent = no_index;
    hhds::Node_class                             inst;      // the Sub in the parent's graph (invalid at the top)
    uint32_t                                     prefix = 0;  // out_.prefixes
    std::vector<hhds::Hier_attr_step>            steps;     // the instance path, as hierarchical attributes key it
    absl::flat_hash_map<hhds::Nid, uint32_t>     children;  // Sub (nid) in this graph -> its context
  };
  // The declared ports of a definition, for the boundary hops.
  struct Port_decl {
    std::string name;
    hhds::Port_id     port_id = 0;
    uint32_t    bits    = 0;
    bool        unsign  = true;
  };
  struct Ports {
    absl::flat_hash_map<std::string, Port_decl> inputs;   // by name
    absl::flat_hash_map<hhds::Port_id, Port_decl>     outputs;  // by port id
  };

  void walk(uint32_t ctx) {
    auto* graph = contexts_[ctx].graph;
    for (auto node : graph->body().nodes(hhds::Node_order::forward)) {
      if (node.get_debug_nid() == hhds::Graph::INPUT_NODE || node.get_debug_nid() == hhds::Graph::OUTPUT_NODE) {
        continue;
      }
      const auto op = gu::type_op_of(node);
      if (op == Ntype_op::Memory) {
        memories_.emplace_back(ctx, node);
        ++out_.memory_nodes;
        continue;
      }
      if (op != Ntype_op::Sub) {
        if (hier_ && (op == Ntype_op::IO || op == Ntype_op::Invalid)) {
          continue;  // what a flattened copy would not hold either
        }
        if (op != Ntype_op::Get_mask && op != Ntype_op::Set_mask && op != Ntype_op::Concat && op != Ntype_op::And
            && op != Ntype_op::Or && op != Ntype_op::SHL && op != Ntype_op::SRA && op != Ntype_op::Sext) {
          ++out_.unmapped_nodes;
        }
        continue;
      }
      const auto declaration = node.get_subnode_io();
      if (declaration && declaration->get_graph()) {
        if (hier_ && !node.is_loop_subnode()) {
          // An instance of a module: a context of its own, walked in place.
          const auto child = static_cast<uint32_t>(contexts_.size());
          Context    c;
          c.graph  = declaration->get_graph().get();
          c.parent = ctx;
          c.inst   = node;
          c.steps  = contexts_[ctx].steps;
          const auto site = node.get_definition_index();
          c.steps.push_back({site.gid, site.value, std::nullopt});
          c.prefix = intern_prefix(out_.prefixes[contexts_[ctx].prefix] + gu::logical_instance_prefix(node));
          contexts_.push_back(std::move(c));
          contexts_[ctx].children.emplace(node.get_debug_nid(), child);
          walk(child);
          continue;
        }
        out_.unsupported.push_back("not a flat cell instance: " + name_of(ctx, node));
        ++out_.opaque_cells;
        continue;
      }
      if (!declaration) {
        out_.unsupported.push_back("not a flat cell instance: " + name_of(ctx, node));
        ++out_.opaque_cells;
        continue;
      }
      const auto type = types_.find(declaration->get_name());
      if (type == no_index) {
        out_.unsupported.push_back(
            std::format("uncharacterized cell: {} ({})", name_of(ctx, node), std::string(declaration->get_name())));
        ++out_.opaque_cells;
        continue;
      }
      const auto cell = out_.netlist->add_cell(type);
      add_cell_row(ctx, node);
      cell_of_node_.emplace(Handle_key{ctx, static_cast<uint64_t>(node.get_debug_nid()), 0}, cell);
      cells_.emplace_back(ctx, node);
      ++out_.mapped_cells;
    }
  }

  uint32_t intern_prefix(std::string prefix) {
    if (const auto it = prefix_index_.find(prefix); it != prefix_index_.end()) {
      return it->second;
    }
    const auto index = static_cast<uint32_t>(out_.prefixes.size());
    prefix_index_.emplace(prefix, index);
    out_.prefixes.push_back(std::move(prefix));
    return index;
  }

  [[nodiscard]] std::string name_of(uint32_t ctx, const hhds::Node_class& node) const {
    if (!hier_) {
      return std::string(node.get_name());
    }
    return out_.prefixes[contexts_[ctx].prefix] + std::string(gu::has_name(node) ? gu::node_name_of(node) : node.get_name());
  }

  void add_port_row(const std::string& name) {
    out_.cell_node.emplace_back();
    if (hier_) {
      out_.cell_prefix.push_back(0);
      out_.cell_name.push_back(name);
      out_.cell_color.push_back(0);
    }
  }

  void add_cell_row(uint32_t ctx, const hhds::Node_class& node) {
    out_.cell_node.push_back(node);
    if (!hier_) {
      return;
    }
    const auto& c = contexts_[ctx];
    out_.cell_prefix.push_back(c.prefix);
    out_.cell_name.push_back(name_of(ctx, node));
    // The occurrence color (pass/partition Flat_origin::color): the hierarchical override at this instance path (a Sub
    // sits at its own call path), else the definition color.
    int32_t color = gu::node_color_of(node);
    if (ctx != 0 && c.graph->has_attr(livehd::attrs::hier_color)) {
      auto       steps = c.steps;
      const auto site  = node.get_definition_index();
      steps.push_back({site.gid, site.value, std::nullopt});
      const hhds::AttrRef<livehd::attrs::hier_color_t> attr(
          c.graph,
          hhds::make_node_attr_key(static_cast<uint64_t>(node.get_debug_nid() & ~static_cast<hhds::Nid>(3))),
          top_.get_gid(),
          std::move(steps));
      if (attr.has()) {
        color = attr.get();
      }
    }
    out_.cell_color.push_back(color);
  }

  const Ports& ports_of(hhds::Graph* graph) {
    auto [it, fresh] = ports_.try_emplace(graph);
    if (fresh) {
      for (const auto& d : graph->get_io()->get_input_pin_decls()) {
        it->second.inputs.emplace(d.name, Port_decl{d.name, static_cast<hhds::Port_id>(d.port_id), static_cast<uint32_t>(d.bits), d.unsign});
      }
      for (const auto& d : graph->get_io()->get_output_pin_decls()) {
        it->second.outputs.emplace(static_cast<hhds::Port_id>(d.port_id),
                                   Port_decl{d.name, static_cast<hhds::Port_id>(d.port_id), static_cast<uint32_t>(d.bits), d.unsign});
      }
    }
    return it->second;
  }

  // Bit `b` of a value crossing a declared port: the bit of the source to read, or nullopt for a zero.
  [[nodiscard]] static std::optional<int> port_bit(const Port_decl& decl, int b) {
    if (decl.bits == 0 || b < static_cast<int>(decl.bits)) {
      return b;
    }
    if (decl.unsign) {
      return std::nullopt;
    }
    return static_cast<int>(decl.bits) - 1;
  }

  template <class Drivers, class Label>
  void connect(uint32_t ctx, const Drivers& drivers, int bit, uint32_t sink, const Label& label) {
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
      terminal = resolve(ctx, *drivers.begin(), bit, 0);
    } catch (const std::exception& error) {
      out_.unsupported.push_back(std::format("{}: {}", label(), error.what()));
      ++out_.unresolved_sinks;
      return;
    }
    switch (terminal.kind) {
      case Terminal::Kind::driver:
        out_.netlist->connect(terminal.pin, sink);
        if (gu::is_published_clock_driver(drivers.front())) {
          out_.netlist->publish_clock_driver(terminal.pin);
        }
        break;
      case Terminal::Kind::constant:
        ++out_.constant_sinks;
        if (constants_) {
          constants_->emplace(sink, terminal.value);
        }
        break;
      case Terminal::Kind::undriven: ++out_.undriven_sinks; break;
      case Terminal::Kind::unknown : ++out_.unresolved_sinks; break;
    }
  }

  // The cell pin or port bit that drives bit `b` of `pin` (in context `ctx`), through the wiring nodes and the
  // instance boundaries.
  Terminal resolve(uint32_t ctx, const hhds::Pin_class& pin, int b, int depth) {
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
    const auto key = Handle_key{ctx, static_cast<uint64_t>(pin.get_debug_pid()), b};
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
      return resolve(ctx, driver, which, depth + 1);
    };
    Terminal out;
    if (node.is_input_node() && ctx != 0) {
      // Up: the parent's driver of this input of the instance.
      const auto& c     = contexts_[ctx];
      const auto& ports = ports_of(c.graph);
      const auto  decl  = ports.inputs.find(std::string(gu::pin_name_of(pin)));
      out               = {Terminal::Kind::undriven, no_index, false};
      if (decl != ports.inputs.end()) {
        const auto sink = c.inst.get_sink_pin(decl->second.port_id);
        if (!sink.is_invalid()) {
          const auto drivers = sink.get_driver_pins();
          if (!drivers.empty()) {
            const auto bit = port_bit(decl->second, b);
            out = bit ? resolve(c.parent, drivers.front(), *bit, depth + 1) : Terminal{Terminal::Kind::constant, no_index, false};
          }
        }
      }
    } else if (node.is_input_node()) {
      const auto name  = std::string(pin.get_pin_name());
      const auto width = std::max<int>(1, static_cast<int>(top_.get_io()->get_bits(name)));
      const auto at    = input_bit_.find(bit_name(name, width, std::min(b, width - 1)));
      if (b >= width && gu::is_unsign(pin)) {
        out = {Terminal::Kind::constant, no_index, false};
      } else if (at != input_bit_.end()) {
        out = {Terminal::Kind::driver, at->second, false};
      }
    } else if (const auto child = contexts_[ctx].children.find(node.get_debug_nid()); child != contexts_[ctx].children.end()) {
      // Down: the child's driver of this output of the instance.
      const auto& c     = contexts_[child->second];
      const auto& ports = ports_of(c.graph);
      const auto  decl  = ports.outputs.find(pin.get_port_id());
      out               = {Terminal::Kind::undriven, no_index, false};
      if (decl != ports.outputs.end()) {
        const auto sink = c.graph->get_output_pin(decl->second.name);
        if (!sink.is_invalid()) {
          const auto drivers = sink.get_driver_pins();
          if (!drivers.empty()) {
            const auto bit = port_bit(decl->second, b);
            out = bit ? resolve(child->second, drivers.front(), *bit, depth + 1)
                      : Terminal{Terminal::Kind::constant, no_index, false};
          }
        }
      }
    } else if (const auto cell = cell_of_node_.find(Handle_key{ctx, static_cast<uint64_t>(node.get_debug_nid()), 0});
               cell != cell_of_node_.end()) {
      const auto type  = out_.netlist->type_of(cell->second);
      const auto name  = declared_name(pin);
      const auto local = types_.find_pin(type, name);
      if (local != no_index && b == 0) {
        out = {Terminal::Kind::driver, out_.netlist->pin(cell->second, local), false};
      } else if (local != no_index) {
        out = {Terminal::Kind::constant, no_index, false};  // a one-bit cell output is zero-extended
      } else if (const int bits = declared_bits(pin); b >= bits) {
        out = {Terminal::Kind::constant, no_index, false};  // past a bus output's width
      } else if (const auto member = cell_pin_of_bit(types_, type, name, bits, b); member != no_index) {
        out = {Terminal::Kind::driver, out_.netlist->pin(cell->second, member), false};  // a macro bus output bit
      }
    } else {
      const auto op    = gu::type_op_of(node);
      const int  width = std::max(1, gu::bits_of(pin));
      if (b >= width) {
        // Past the declared width the value extends; a signed pin repeats its top bit.
        out = gu::is_unsign(pin) ? Terminal{Terminal::Kind::constant, no_index, false} : resolve(ctx, pin, width - 1, depth + 1);
      } else if (op == Ntype_op::Memory) {
        out = {Terminal::Kind::driver, memory_start(ctx, node, pin, b), false};
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
        const auto nkey   = Handle_key{ctx, static_cast<uint64_t>(node.get_debug_nid()), 0};
        auto       cached = lanes_.find(nkey);
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
          cached = lanes_.emplace(nkey, std::move(entry)).first;
        }
        out = {Terminal::Kind::constant, no_index, false};
        if (b >= 0 && static_cast<size_t>(b) < cached->second.lane_of_bit.size() && cached->second.lane_of_bit[b] >= 0) {
          const auto lane = cached->second.lanes[cached->second.lane_of_bit[b]];
          out             = resolve(ctx, lane.value, b - lane.offset, depth + 1);
        }
      } else if (op == Ntype_op::Or || op == Ntype_op::And) {
        // A constant that decides the gate, or the single remaining operand.
        const bool dominant = op == Ntype_op::Or;
        out                 = {Terminal::Kind::constant, no_index, !dominant};
        bool seen           = false;
        for (const auto sink : node.inp_sorted_pins()) {
          for (const auto& driver : sink.get_driver_pins()) {
            const auto operand = resolve(ctx, driver, b, depth + 1);
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
                          : resolve(ctx, value, b - static_cast<int>(shift), depth + 1);
        } else if (op == Ntype_op::SRA) {
          out = resolve(ctx, value, b + static_cast<int>(shift), depth + 1);
        } else {
          out = resolve(ctx, value, std::min(b, static_cast<int>(shift)), depth + 1);
        }
      }  // anything else (a memory, an unmapped operator) leaves the terminal unknown
    }
    busy_.erase(key);
    memo_.emplace(key, out);
    return out;
  }

  // The start point of one read-data bit of a memory: an input port cell, made on first use.
  uint32_t memory_start(uint32_t ctx, const hhds::Node_class& node, const hhds::Pin_class& pin, int b) {
    auto name = std::format("{}:out_{}[{}]", name_of(ctx, node), pin.get_port_id(), b);
    if (const auto found = input_bit_.find(name); found != input_bit_.end()) {
      return found->second;
    }
    const auto cell = out_.netlist->add_input_port(name);
    add_port_row(name);
    const auto start = out_.netlist->pin(cell, 0u);
    input_bit_.emplace(std::move(name), start);
    return start;
  }

  hhds::Graph&                                                  top_;
  const Cell_types&                                             types_;
  Bound_graph&                                                  out_;
  const bool                                                    hier_;
  absl::flat_hash_map<uint32_t, bool>*                          constants_;
  std::vector<Context>                                          contexts_;
  std::vector<std::pair<uint32_t, hhds::Node_class>>            cells_, memories_;
  absl::flat_hash_map<hhds::Graph*, Ports>                      ports_;
  absl::flat_hash_map<std::string, uint32_t>                    prefix_index_;
  absl::flat_hash_map<Handle_key, uint32_t>                     cell_of_node_;
  absl::flat_hash_map<std::string, uint32_t>                    input_bit_;
  absl::flat_hash_map<Handle_key, Terminal>                     memo_;
  struct Concat_cache {
    std::vector<gu::Concat_lane> lanes;
    std::vector<int32_t>         lane_of_bit;
  };
  absl::flat_hash_map<Handle_key, Concat_cache> lanes_;
  absl::flat_hash_set<Handle_key>               busy_;
};
}  // namespace

Bound_graph bind_graph(const hhds::Graph& flat, const Cell_types& types) {
  Bound_graph out;
  out.netlist = std::make_unique<Netlist>(types);
  Binder(const_cast<hhds::Graph&>(flat), types, out, false).run();
  return out;
}

Bound_graph bind_hier(hhds::Graph& top, const Cell_types& types) { return bind_hier(top, types, nullptr); }

Bound_graph bind_hier(hhds::Graph& top, const Cell_types& types, absl::flat_hash_map<uint32_t, bool>* constants) {
  if (constants) {
    constants->clear();
  }
  Bound_graph out;
  out.netlist = std::make_unique<Netlist>(types);
  Binder(top, types, out, true, constants).run();
  return out;
}

Bound_names::Bound_names(const Bound_graph& bound) : bound_(bound) {
  cells_.reserve(bound.cell_node.size());
  for (uint32_t cell = 0; cell < bound.cell_node.size(); ++cell) {
    if (bound.cell_node[cell].is_valid()) {
      cells_.emplace(bound.name(cell), cell);
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
      visit(cell_name(cell), cell);
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
      const auto type  = netlist.type_of(cell);
      const auto pname = declared_name(pin);
      // A flattened copy prefixes the wire names it copies with the instance path (pass/partition/flatten).
      auto name = gu::wire_name(pin);
      if (bound_.hierarchical()) {
        name.insert(0, bound_.prefixes[bound_.cell_prefix[cell]]);
      }
      if (const auto local = types.find_pin(type, pname); local != no_index) {
        nets_.emplace(std::move(name), netlist.pin(cell, local));
        continue;
      }
      // A macro bus output: one net per bit, `wire[bit]`.
      const int bits = declared_bits(pin);
      for (int b = 0; b < bits; ++b) {
        if (const auto member = cell_pin_of_bit(types, type, pname, bits, b); member != no_index) {
          nets_.emplace(std::format("{}[{}]", name, b), netlist.pin(cell, member));
        }
      }
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
