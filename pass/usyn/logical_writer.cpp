// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "logical_writer.hpp"

#include <array>
#include <bit>
#include <format>
#include <set>

#include "hhds/attrs/name.hpp"
#include "hhds/attrs/srcid.hpp"
#include "node_util.hpp"

namespace livehd::usyn {
namespace gu = graph_util;

namespace {
Logical_module_result write_module(const synth::Source_state_table& source, std::span<const uint32_t> state_bits,
                                   const Endpoint_netlist* frozen, const Xag_region* legacy, std::string_view name, Budget& work,
                                   uint32_t max_nodes) {
  Logical_module_result result;
  const auto            invalid = [&](std::string reason) {
    result.reason = std::move(reason);
    return std::move(result);
  };
  const auto exhausted = [&] {
    result.status = Status::search_exhausted;
    result.reason = "logical graph emission budget";
    return std::move(result);
  };
  if (name.empty() || !source.logical_boundary || max_nodes == 0) {
    return invalid("logical graph emission requires a named module and complete logical controls");
  }
  if (source.sources.size() > max_nodes || source.bits.size() > max_nodes || source.controls.size() > max_nodes
      || state_bits.size() > max_nodes || !work.spend(source.sources.size() + source.bits.size() + source.controls.size())) {
    return exhausted();
  }
  uint64_t control_lanes = 0;
  for (const auto& control : source.controls) {
    control_lanes += control.outputs.size();
    if (control_lanes > max_nodes || !work.spend(control.outputs.size())) {
      return exhausted();
    }
  }
  Xag_region expanded;
  if (frozen) {
    expanded = expand_endpoint_netlist(*frozen, work, max_nodes);
    if (expanded.status != Status::feasible) {
      result.status = expanded.status;
      return invalid(std::move(expanded.reason));
    }
  }
  auto exported = export_lnet(frozen ? expanded : *legacy, work, max_nodes);
  if (!exported.net) {
    result.status = exported.status;
    return invalid(std::move(exported.reason));
  }
  const auto& net = *exported.net;
  if (!synth::validate_state_controls(source, net) || state_bits.size() != net.latches().size()
      || source.bits.size() != net.latches().size()) {
    return invalid("invalid logical state/control correspondence");
  }
  // Reserve the writer's worst case before creating the private graph: one
  // gate plus one memoized inverse per Lnet id, one Flop per latch and one
  // Concat per control (constants are pooled pins, not nodes). A looser factor
  // refused regions only after their complete search; region_emit re-checks the
  // emitted body. Per-loop work still samples memory admission. HHDS stores a
  // pin's port id in a Port_bits-wide field, so bound IO and Concat lanes by
  // Port_invalid rather than the Port_id type.
  const uint64_t reserved = 2 * uint64_t{net.size()} + net.latches().size() + source.controls.size() + net.inputs().size()
                            + net.outputs().size() + 4;
  if (reserved > max_nodes || net.inputs().size() + net.outputs().size() >= hhds::Port_invalid
      || 2 * control_lanes + 1 >= hhds::Port_invalid) {
    return exhausted();
  }
  std::vector<bool>                 seen(source.bits.size());
  std::vector<uint64_t>             counts(source.sources.size());
  std::set<std::array<uint32_t, 3>> identities;
  std::set<std::string>             names;
  for (size_t i = 0; i < state_bits.size(); ++i) {
    if (!work.spend()) {
      return exhausted();
    }
    const auto index = state_bits[i];
    if (index >= source.bits.size() || seen[index]) {
      return invalid("invalid or repeated source state bit");
    }
    seen[index]     = true;
    const auto& bit = source.bits[index];
    if (bit.source >= source.sources.size()) {
      return invalid("invalid source state row");
    }
    const auto& row = source.sources[bit.source];
    if ((row.async_reset && !row.reset.present) || row.power_on != (row.initial.present && !row.reset.present)) {
      return invalid("inconsistent source reset/initialization semantics");
    }
    if (row.role != synth::State_role::register_candidate || !row.clock_edge_known) {
      result.status = Status::unsupported;
      return invalid("logical writer requires ordinary registers with known clock polarity");
    }
    if (bit.bit >= row.bits || bit.stage >= row.stages || !identities.insert({bit.source, bit.stage, bit.bit}).second
        || net.latch(i).name.empty() || !names.insert(net.latch(i).name).second) {
      return invalid("invalid or duplicate logical state identity");
    }
    ++counts[bit.source];
  }
  for (size_t i = 0; i < counts.size(); ++i) {
    const auto& row = source.sources[i];
    if (row.role == synth::State_role::register_candidate
        && (counts[i] == 0 || counts[i] != uint64_t{row.bits} * row.stages || counts[i] != row.translated_bits)) {
      return invalid("incomplete ordinary register emission");
    }
  }
  auto       module    = std::make_unique<Logical_module>();
  auto       io        = module->library.create_io(name);
  const auto port_name = [&](std::string base) {
    while (!names.insert(base).second) {
      if (!work.spend()) {
        return std::string{};
      }
      base += '_';
    }
    return base;
  };
  for (size_t i = 0; i < net.inputs().size(); ++i) {
    if (!work.spend()) {
      return exhausted();
    }
    module->inputs.push_back(port_name(std::format("__usyn_i{}", i)));
    if (module->inputs.back().empty()) {
      return exhausted();
    }
    io->add_input(module->inputs.back(), static_cast<hhds::Port_id>(i + 1));
    io->set_bits(module->inputs.back(), 1);
    io->set_unsign(module->inputs.back(), true);
  }
  for (size_t i = 0; i < net.outputs().size(); ++i) {
    if (!work.spend()) {
      return exhausted();
    }
    module->outputs.push_back(port_name(std::format("__usyn_o{}", i)));
    if (module->outputs.back().empty()) {
      return exhausted();
    }
    io->add_output(module->outputs.back(), static_cast<hhds::Port_id>(net.inputs().size() + i + 1));
    io->set_bits(module->outputs.back(), 1);
    io->set_unsign(module->outputs.back(), true);
  }
  module->graph       = io->create_graph();
  auto&      graph    = *module->graph;
  const auto constant = [&](int value) { return gu::create_const(graph, *Dlop::create_integer(value)); };
  const auto connect  = [&](hhds::Node_class node, std::string_view pin, hhds::Pin_class driver) {
    driver.connect_sink(gu::setup_sink_by_name(node, pin));
  };
  const auto output = [&](hhds::Node_class node) {
    auto pin = node.create_driver_pin(0);
    gu::set_ubits(pin, 1);
    return pin;
  };
  std::vector<hhds::Pin_class> pins(net.size()), inverse(net.size());
  const auto                   invert = [&](synth::Lid id) {
    if (inverse[id].is_invalid()) {
      // XOR with one is an exact 0/1 complement. An unlimited-width Not
      // would produce a negative integer despite a one-bit width hint.
      auto node = gu::create_typed_node(graph, Ntype_op::Xor);
      pins[id].connect_sink(gu::setup_sink_pid(node, 0));
      constant(1).connect_sink(gu::setup_sink_pid(node, 0));
      inverse[id] = output(node);
    }
    return inverse[id];
  };
  // HHDS pin insertion scans its sorted list: descending port creation
  // avoids quadratic work on a wide bit-level interface.
  for (size_t end = net.inputs().size(); end != 0; --end) {
    const auto i = end - 1;
    if (!work.spend()) {
      return exhausted();
    }
    pins[net.inputs()[i].node] = graph.get_input_pin(module->inputs[i]);
    gu::set_ubits(pins[net.inputs()[i].node], 1);
  }
  for (size_t i = 0; i < net.latches().size(); ++i) {
    if (!work.spend()) {
      return exhausted();
    }
    auto node = gu::create_typed_node(graph, Ntype_op::Flop);
    node.attr(hhds::attrs::name).set(net.latch(i).name);
    auto q = output(node);
    gu::set_pin_name(q, net.latch(i).name);
    pins[net.latch(i).q] = q;
    module->state.push_back(node);
    const auto& span = source.sources[source.bits[state_bits[i]].source].span;
    if (!span.file.empty() && span.start_line) {
      auto&      locator = graph.source_locator();
      const auto sid     = span.start_byte && span.end_byte
                               ? locator.mint(span.file, *span.start_byte, *span.end_byte, *span.start_line)
                               : locator.mint_line(span.file, *span.start_line);
      node.attr(hhds::attrs::srcid).set(sid);
    }
  }
  for (synth::Lid id = 0; id < net.size(); ++id) {
    if (!work.spend(5)) {
      return exhausted();
    }
    if (net.kind(id) == synth::Lnet::Kind::source) {
      continue;
    }
    if (net.kind(id) == synth::Lnet::Kind::constant) {
      pins[id] = constant(net.eval(id, 0));
      continue;
    }
    const auto fanins = net.fanins(id);
    if (fanins.size() == 1) {
      if (net.eval(id, 0) == net.eval(id, 1)) {
        pins[id] = constant(net.eval(id, 0));
      } else {
        pins[id] = net.eval(id, 0) ? invert(fanins[0]) : pins[fanins[0]];
      }
      continue;
    }
    if (fanins.size() != 2) {
      return invalid("CMOS expansion contains a non-binary LUT");
    }
    const auto fn = static_cast<unsigned>(net.fn(id) & 15);
    if (fn == 6 || fn == 9) {
      auto node = gu::create_typed_node(graph, Ntype_op::Xor);
      pins[fanins[0]].connect_sink(gu::setup_sink_pid(node, 0));
      pins[fanins[1]].connect_sink(gu::setup_sink_pid(node, 0));
      pins[id] = output(node);
      if (fn == 9) {
        const auto positive = pins[id];
        pins[id]            = invert(id);
        inverse[id]         = positive;
      }
    } else if (std::popcount(fn) == 1 || std::popcount(fn) == 3) {
      const bool     complemented = std::popcount(fn) == 3;
      const unsigned term         = std::countr_zero(complemented ? fn ^ 15U : fn);
      auto           node         = gu::create_typed_node(graph, Ntype_op::And);
      ((term & 1) ? pins[fanins[0]] : invert(fanins[0])).connect_sink(gu::setup_sink_pid(node, 0));
      ((term & 2) ? pins[fanins[1]] : invert(fanins[1])).connect_sink(gu::setup_sink_pid(node, 0));
      pins[id] = output(node);
      if (complemented) {
        const auto positive = pins[id];
        pins[id]            = invert(id);
        inverse[id]         = positive;
      }
    } else {
      return invalid("CMOS expansion contains an unexpected binary function");
    }
  }
  // Assemble original-width event controls. A multibit reset is a Boolean
  // condition on the complete value, not merely its least significant bit.
  std::vector<std::array<hhds::Pin_class, 3>> controls(source.sources.size());
  for (const auto& control : source.controls) {
    if (!work.spend(control.outputs.size() + 1)) {
      return exhausted();
    }
    if (control.kind == synth::State_control_kind::initial_value) {
      continue;  // selected by source bit below, never reassembled
    }
    auto& value = controls[control.source][static_cast<size_t>(control.kind)];
    if (control.outputs.size() == 1) {
      value = pins[net.outputs()[control.outputs[0]].node];
    } else {
      auto node = gu::create_typed_node(graph, Ntype_op::Concat);
      for (size_t b = 0; b < control.outputs.size(); ++b) {
        const auto pid = static_cast<hhds::Port_id>(2 * (control.outputs.size() - 1 - b));
        constant(1).connect_sink(gu::setup_sink_pid(node, pid + 1));
        pins[net.outputs()[control.outputs[b]].node].connect_sink(node.create_sink_pin(pid));
      }
      value = node.create_driver_pin(0);
      gu::set_ubits(value, control.outputs.size());
    }
  }
  std::vector<const synth::State_control*> initial(source.sources.size());
  for (const auto& control : source.controls) {
    if (control.kind == synth::State_control_kind::initial_value) {
      initial[control.source] = &control;
    }
  }
  for (size_t i = 0; i < module->state.size(); ++i) {
    if (!work.spend(12)) {
      return exhausted();
    }
    const auto  node = module->state[i];
    const auto& bit  = source.bits[state_bits[i]];
    const auto& row  = source.sources[bit.source];
    connect(node, "din", pins[net.latch(i).d]);
    const auto attach = [&](std::string_view pin, const synth::Source_signal& signal, synth::State_control_kind kind) {
      if (signal.present) {
        connect(node,
                pin,
                signal.constant ? gu::create_const(graph, signal.value) : controls[bit.source][static_cast<size_t>(kind)]);
      }
    };
    attach("clock_pin", row.clock, synth::State_control_kind::clock);
    if (row.neg_clock) {
      connect(node, "posclk", constant(0));
    }
    if (row.async_reset) {
      attach("reset_pin", row.reset, synth::State_control_kind::async_reset);
      connect(node, "async", constant(1));
      if (row.neg_reset) {
        connect(node, "negreset", constant(1));
      }
    }
    if ((row.async_reset || row.power_on) && row.initial.present) {
      hhds::Pin_class value;
      if (row.initial.constant) {
        const char digit = row.initial.value.unknown_bit_test(bit.bit) ? '?' : row.initial.value.bit_test(bit.bit) ? '1' : '0';
        value            = gu::create_const(graph, *Dlop::from_binary(std::string(1, digit), true));
      } else {
        value = pins[net.outputs()[initial[bit.source]->outputs[bit.bit]].node];
      }
      connect(node, "initial", value);
    }
  }
  for (size_t end = net.outputs().size(); end != 0; --end) {
    const auto i = end - 1;
    if (!work.spend()) {
      return exhausted();
    }
    pins[net.outputs()[i].node].connect_sink(graph.get_output_pin(module->outputs[i]));
  }
  graph.commit();
  result.status = Status::feasible;
  result.module = std::move(module);
  return result;
}

}  // namespace

Logical_module_result write_logical_module(const Stateful_region& region, std::string_view name, Budget& work, uint32_t max_nodes) {
  return write_module(region.source,
                      region.state_bits,
                      region.frozen ? &*region.frozen : nullptr,
                      &region.selected.logic,
                      name,
                      work,
                      max_nodes);
}

Logical_module_result write_logical_module(const Frozen_region& region, Budget& work, uint32_t max_nodes) {
  return write_module(region.source, region.state_bits, &region.netlist, nullptr, region.module_name, work, max_nodes);
}

}  // namespace livehd::usyn
