// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "source_state.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "attrs.hpp"
#include "node_util.hpp"
#include "region_blast.hpp"

namespace livehd::synth {
namespace gu = graph_util;
namespace {
Source_signal snapshot(hhds::Pin_class pin) {
  Source_signal out;
  if (pin.is_invalid()) {
    return out;
  }
  out.present  = true;
  out.node     = pin.get_master_node().get_debug_nid();
  out.port     = pin.get_port_id();
  out.constant = pin.is_const();
  out.bits     = static_cast<uint32_t>(std::max(1, gu::bits_of(pin)));
  if (out.constant) {
    out.value = gu::const_of(pin);
  }
  return out;
}
}  // namespace

Source_state_table inspect_source_state(const partition::Region_body& rb, const Clock_gates& clocks, State_scope scope) {
  if (scope == State_scope::logic && rb.src && rb.src->get_input_node().attr(attrs::memory_module).has()) {
    scope = State_scope::memory;
  }
  Source_state_table table;
  table.source_graph = rb.src ? std::string{rb.src->get_name()} : rb.module_name;
  table.scope        = scope;
  absl::flat_hash_set<hhds::Pin_class>           boundaries;
  absl::flat_hash_map<hhds::Node_class, int32_t> gate_latches;
  for (const auto& p : rb.inputs) {
    boundaries.insert(p.src_driver);
  }
  for (size_t i = 0; i < clocks.gates.size(); ++i) {
    gate_latches.emplace(clocks.gates[i].latch, static_cast<int32_t>(i));
    const auto& gate = clocks.gates[i];
    table.clocks.push_back({snapshot(gate.gclk),
                            snapshot(gate.clk_src),
                            snapshot(gate.en_drv),
                            gate.latch.get_debug_nid(),
                            gate.parent,
                            gate.name,
                            node_span(rb, gate.latch)});
  }
  for (const auto& n : rb.nodes) {
    const auto op = gu::type_op_of(n);
    if (op != Ntype_op::Flop && op != Ntype_op::Latch && op != Ntype_op::Memory && op != Ntype_op::Clock_cell
        && op != Ntype_op::Sub) {
      continue;
    }
    Source_state s;
    s.node = n.get_debug_nid();
    s.span = node_span(rb, n);
    if (op == Ntype_op::Memory) {
      s.role = State_role::memory;
    } else if (op == Ntype_op::Clock_cell) {
      s.role = State_role::icg;
    } else if (op == Ntype_op::Sub) {
      s.role = n.get_subnode_graph() ? State_role::hierarchy : State_role::opaque;
    } else {
      const auto q = n.get_driver_pin(0);
      s.q          = snapshot(q);
      s.name       = q.is_invalid() ? std::string{} : gu::wire_name(q);
      if (s.name.empty()) {
        s.name = std::format("{}__state{}", rb.module_name, s.node);
      }
      s.bits          = q.is_invalid() ? 0 : static_cast<uint32_t>(std::max(1, gu::bits_of(q)));
      s.width_known   = !q.is_invalid() && gu::bits_of(q) > 0;
      s.stages        = op == Ntype_op::Flop ? static_cast<uint32_t>(pipeline_depth(n)) : 1;
      s.data          = snapshot(gu::get_driver_of_sink_name(n, "din"));
      s.enable        = snapshot(gu::get_driver_of_sink_name(n, "enable"));
      s.reset         = snapshot(gu::get_driver_of_sink_name(n, "reset_pin"));
      s.initial       = snapshot(gu::get_driver_of_sink_name(n, "initial"));
      auto clock      = gu::get_driver_of_sink_name(n, op == Ntype_op::Flop ? "clock_pin" : "enable");
      s.clock         = snapshot(clock);
      const auto root = clock_root(clock, boundaries);
      s.clock_root    = snapshot(root);
      if (auto gate = clocks.by_output.find(root); gate != clocks.by_output.end()) {
        s.icg = gate->second;
      }
      auto edge           = gu::get_driver_of_sink_name(n, "posclk");
      s.clock_edge_known  = edge.is_invalid() || (edge.is_const() && !gu::const_of(edge).has_unknowns());
      s.neg_clock         = edge.is_const() && gu::const_of(edge).is_known_false();
      auto negative_reset = gu::get_driver_of_sink_name(n, "negreset");
      s.neg_reset         = negative_reset.is_const() && gu::const_of(negative_reset).bit_test(0);
      auto async          = gu::get_driver_of_sink_name(n, "async");
      s.async_reset       = s.reset.present && async.is_const() && !gu::const_of(async).is_known_false();
      s.power_on          = s.initial.present && !s.reset.present;
      if (scope == State_scope::memory) {
        s.role = State_role::memory;
      } else if (scope == State_scope::opaque) {
        s.role = State_role::opaque;
      } else if (auto gate = gate_latches.find(n); gate != gate_latches.end()) {
        s.role = State_role::icg;
        s.icg  = gate->second;
      } else {
        s.role = op == Ntype_op::Flop ? State_role::register_candidate : State_role::transparent_latch;
      }
    }
    if (s.name.empty()) {
      s.name = node_identity(n);
    }
    table.sources.push_back(std::move(s));
  }
  return table;
}

bool validate_state_target(const Source_state_table& table, State_target target, std::string_view pass) {
  if (target == State_target::cmos) {
    return true;
  }
  bool valid = true;
  for (const auto& s : table.sources) {
    if (s.role == State_role::transparent_latch) {
      diag::err(pass, "domino-source-latch", "unsupported")
          .at(s.span)
          .msg("DOMINO target cannot convert source latch '{}' in '{}'; only recognized ICG latches are supported",
               s.name,
               table.source_graph)
          .emit();
      valid = false;
    } else if (s.role == State_role::register_candidate && (!s.clock_edge_known || s.neg_clock)) {
      diag::err(pass, s.neg_clock ? "domino-negedge-clock" : "domino-clock-edge", "unsupported")
          .at(s.span)
          .msg("DOMINO target requires a positive-edge clock for register '{}' in '{}'", s.name, table.source_graph)
          .emit();
      valid = false;
    }
  }
  return valid;
}

bool validate_source_design(std::span<const std::shared_ptr<hhds::Graph>> roots, State_target target, std::string_view pass) {
  if (target == State_target::cmos) {
    return true;
  }
  std::vector<std::shared_ptr<hhds::Graph>> pending(roots.begin(), roots.end());
  absl::flat_hash_set<const hhds::Graph*>   visited;
  bool                                      valid = true;
  while (!pending.empty()) {
    auto graph = std::move(pending.back());
    pending.pop_back();
    if (!graph || !visited.insert(graph.get()).second) {
      continue;
    }
    std::vector<hhds::Node_class> nodes;
    for (auto node : graph->body().nodes()) {
      nodes.push_back(node);
      if (gu::type_op_of(node) == Ntype_op::Sub) {
        if (auto child = node.get_subnode_graph()) {
          pending.push_back(std::move(child));
        }
      }
    }
    partition::Region_body rb;
    rb.src         = graph.get();
    rb.module_name = graph->get_name();
    rb.nodes       = nodes;
    for (auto pin : graph->get_input_node().out_pins()) {
      rb.inputs.push_back({{}, pin, gu::bits_of(pin), !gu::is_unsign(pin)});
    }
    const auto table = inspect_source_state(rb, analyze_clock_gates(rb));
    valid            = validate_state_target(table, target, pass) && valid;
  }
  return valid;
}

bool bind_source_state(Source_state_table& table, const Region_blast& blast) {
  absl::flat_hash_map<uint64_t, size_t> source_index;
  for (size_t i = 0; i < table.sources.size(); ++i) {
    if (!source_index.emplace(table.sources[i].node, i).second) {
      return false;
    }
  }
  std::vector<State_bit>                           bits;
  std::vector<uint64_t>                            counts(table.sources.size());
  absl::flat_hash_set<std::pair<size_t, uint32_t>> stages;
  std::vector<bool>                                seen(blast.lnet.latches().size());
  for (const auto& f : blast.flops) {
    auto found = source_index.find(f.node.get_debug_nid());
    if (found == source_index.end() || f.bits <= 0 || f.latch.size() != static_cast<size_t>(f.bits)
        || f.qbits.size() != f.latch.size()) {
      return false;
    }
    const auto  i      = found->second;
    const auto& source = table.sources[i];
    const auto  stage  = f.stage;
    if (source.bits != static_cast<uint32_t>(f.bits) || stage >= source.stages || !stages.emplace(i, stage).second) {
      return false;
    }
    for (uint32_t bit = 0; bit < source.bits; ++bit) {
      const auto k = f.latch[bit];
      if (k >= seen.size() || seen[k]) {
        return false;
      }
      const auto& latch = blast.lnet.latch(k);
      if (latch.q != f.qbits[bit] || latch.d >= blast.lnet.size()) {
        return false;
      }
      seen[k]   = true;
      auto name = stage + 1 < source.stages ? std::format("___pipe{}_{}", stage, source.name) : source.name;
      if (source.bits > 1) {
        name += std::format("[{}]", bit);
      }
      bits.push_back({
          static_cast<uint32_t>(i),
          stage,
          bit,
          k,
          std::move(name),
          {latch.q,        false},
          {latch.d, f.d_inverted}
      });
      ++counts[i];
    }
  }
  if (!std::all_of(seen.begin(), seen.end(), [](bool value) { return value; })) {
    return false;
  }
  for (size_t i = 0; i < counts.size(); ++i) {
    if (counts[i] != 0 && counts[i] != uint64_t{table.sources[i].bits} * table.sources[i].stages) {
      return false;
    }
  }
  table.bits = std::move(bits);
  for (size_t i = 0; i < counts.size(); ++i) {
    table.sources[i].translated_bits = counts[i];
  }
  return true;
}

bool validate_state_controls(const Source_state_table& table, const Lnet& net) {
  if (!table.logical_boundary) {
    return table.controls.empty();
  }
  std::vector<std::array<bool, 3>> seen(table.sources.size());
  for (const auto& control : table.controls) {
    const auto kind = static_cast<size_t>(control.kind);
    if (control.source >= table.sources.size() || kind >= 3 || seen[control.source][kind]) {
      return false;
    }
    seen[control.source][kind] = true;
    const auto& source         = table.sources[control.source];
    const auto& signal         = control.kind == State_control_kind::clock         ? source.clock
                                 : control.kind == State_control_kind::async_reset ? source.reset
                                                                                   : source.initial;
    if (source.role != State_role::register_candidate || !signal.present || signal.constant
        || (control.kind == State_control_kind::async_reset && !source.async_reset)
        || (control.kind == State_control_kind::initial_value && !source.async_reset && !source.power_on)
        || control.outputs.size() != (control.kind == State_control_kind::initial_value ? source.bits : signal.bits)) {
      return false;
    }
    if (control.outputs.empty() || std::any_of(control.outputs.begin(), control.outputs.end(), [&](uint32_t index) {
          return index >= net.outputs().size();
        })) {
      return false;
    }
  }
  for (size_t i = 0; i < table.sources.size(); ++i) {
    const auto& source = table.sources[i];
    if (source.role != State_role::register_candidate) {
      continue;
    }
    const auto required = [](const Source_signal& signal) { return signal.present && !signal.constant; };
    if (seen[i][0] != required(source.clock) || seen[i][1] != (source.async_reset && required(source.reset))
        || seen[i][2] != ((source.async_reset || source.power_on) && required(source.initial))) {
      return false;
    }
  }
  return true;
}

}  // namespace livehd::synth
