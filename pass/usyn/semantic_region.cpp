// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "semantic_region.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <tuple>

namespace livehd::usyn {

Semantic_result import_semantic_region(const synth::Lnet& net, const synth::Source_state_table& source, synth::State_target target,
                                       Budget& work, uint32_t max_nodes) {
  Semantic_result result;
  const auto      invalid = [&](std::string reason) {
    result.reason = std::move(reason);
    return std::move(result);
  };
  const auto exhausted = [&] {
    result.status = Status::search_exhausted;
    result.reason = "semantic boundary import budget";
    return std::move(result);
  };
  if (max_nodes == 0) {
    return invalid("invalid semantic node budget");
  }
  if (net.size() > max_nodes || net.outputs().size() > max_nodes || source.sources.size() > max_nodes
      || source.bits.size() > max_nodes || source.clocks.size() > max_nodes || source.controls.size() > max_nodes
      || !work.spend(source.sources.size() + source.bits.size() + source.clocks.size() + source.controls.size())) {
    return exhausted();
  }
  uint64_t control_bits = 0;
  for (const auto& control : source.controls) {
    control_bits += control.outputs.size();
    if (control_bits > max_nodes || !work.spend(control.outputs.size())) {
      return exhausted();
    }
  }
  if (!synth::validate_state_controls(source, net)) {
    return invalid("invalid or incomplete logical state controls");
  }
  if (!synth::validate_state_target(source, target)) {
    return invalid("source state is incompatible with DOMINO target");
  }
  if (source.bits.size() != net.latches().size()) {
    return invalid("incomplete Lnet state correspondence");
  }
  std::vector<uint32_t>                state_bits(net.latches().size(), synth::Lnet::kNone);
  std::vector<uint64_t>                counts(source.sources.size());
  std::vector<uint64_t>                nodes;
  std::vector<std::array<uint32_t, 3>> identities;
  std::map<std::string, uint32_t>      names;
  nodes.reserve(source.sources.size());
  identities.reserve(source.bits.size());
  for (const auto& s : source.sources) {
    nodes.push_back(s.node);
  }
  std::sort(nodes.begin(), nodes.end());
  if (std::adjacent_find(nodes.begin(), nodes.end()) != nodes.end()) {
    return invalid("duplicate source state identity");
  }
  // Reserve PI names as well: a later mapper must not merge a register Q
  // with a primary input just because their source spellings coincide.
  for (const auto& pi : net.inputs()) {
    ++names[pi.name];
  }
  for (uint32_t i = 0; i < source.bits.size(); ++i) {
    const auto& bit = source.bits[i];
    if (bit.source >= source.sources.size() || bit.latch >= state_bits.size() || state_bits[bit.latch] != synth::Lnet::kNone) {
      return invalid("invalid or duplicate source bit correspondence");
    }
    const auto& s     = source.sources[bit.source];
    const auto& latch = net.latch(bit.latch);
    if (bit.stage >= s.stages || bit.bit >= s.bits || bit.name.empty() || bit.q.node != latch.q || bit.q.inverted
        || bit.d.node != latch.d || bit.d.node >= net.size() || (latch.init != '0' && latch.init != '1' && latch.init != 'x')) {
      return invalid("source bit does not match RAW state boundary");
    }
    identities.push_back({bit.source, bit.stage, bit.bit});
    state_bits[bit.latch] = i;
    ++counts[bit.source];
    ++names[bit.name];
  }
  std::sort(identities.begin(), identities.end());
  if (std::adjacent_find(identities.begin(), identities.end()) != identities.end()) {
    return invalid("duplicate source bit/stage identity");
  }
  for (size_t i = 0; i < source.sources.size(); ++i) {
    const auto& s        = source.sources[i];
    const auto  expected = uint64_t{s.bits} * s.stages;
    if (counts[i] != s.translated_bits || (counts[i] != 0 && counts[i] != expected)) {
      return invalid("incomplete source state translation");
    }
    if (s.role == synth::State_role::register_candidate && (expected == 0 || counts[i] != expected)) {
      return invalid("ordinary register requires complete logical translation: " + s.name);
    }
  }
  auto logic = import_lnet(net, work, max_nodes);
  if (logic.status != Status::feasible) {
    result.status = logic.status;
    result.reason = std::move(logic.reason);
    return result;
  }
  Semantic_region                                          region;
  std::map<std::tuple<uint64_t, uint32_t, bool>, uint32_t> domains;
  for (uint32_t i = 0; i < state_bits.size(); ++i) {
    if (!work.spend()) {
      return exhausted();
    }
    const auto& bit    = source.bits[state_bits[i]];
    const auto& s      = source.sources[bit.source];
    uint32_t    domain = unknown_clock_domain;
    if (s.clock_edge_known && s.clock.present && !s.clock.constant && s.clock.bits == 1) {
      // Use the actual event signal, not clock_root: independently gated
      // clocks may share a root while producing different capture events.
      const auto key = std::tuple{s.clock.node, s.clock.port, s.neg_clock};
      domain         = domains.try_emplace(key, static_cast<uint32_t>(domains.size())).first->second;
    }
    region.domains.push_back(domain);
    auto& state = logic.state[i];
    state.name  = bit.name;
    if (names.at(bit.name) > 1) {
      const auto base = std::format("{}__state{}_s{}_b{}", bit.name, s.node, bit.stage, bit.bit);
      state.name      = base;
      uint32_t suffix = 0;
      while (names.contains(state.name)) {
        if (!work.spend()) {
          return exhausted();
        }
        state.name = std::format("{}_{}", base, ++suffix);
      }
      names.emplace(state.name, 1);
    }
    state.d = logic.original_nodes[bit.d.node];
    if (bit.d.inverted) {
      state.d = ~state.d;
    }
    if (source.scope == synth::State_scope::logic && s.role == synth::State_role::register_candidate && s.clock_edge_known
        && !s.neg_clock) {
      region.eligible.push_back(i);
    }
  }
  // Charge the owned snapshot before copying; refusal never publishes a
  // partially renamed/normalized network or a metadata-only success.
  if (!work.spend(source.sources.size() + source.bits.size() + source.clocks.size() + source.controls.size() + control_bits)) {
    return exhausted();
  }
  region.logic      = std::move(logic);
  region.source     = source;
  region.state_bits = std::move(state_bits);
  result.region     = std::move(region);
  result.status     = Status::feasible;
  return result;
}

Stateful_result synthesize_stateful_region(const synth::Lnet& net, const synth::Source_state_table& source,
                                           synth::State_target target, const Logical_options& options, Budget& structural,
                                           Budget& search) {
  Stateful_result result;
  auto            imported = import_semantic_region(net, source, target, structural, options.max_nodes);
  if (!imported.region) {
    result.status = imported.status;
    result.reason = std::move(imported.reason);
    return result;
  }
  auto& semantic = *imported.region;
  auto  selected = synthesize_logical_region(semantic.logic, semantic.eligible, options, structural, search, semantic.domains);
  result.status  = selected.status;
  result.reason  = std::move(selected.reason);
  result.report  = std::move(selected.report);
  if (selected.region) {
    auto frozen = freeze_endpoint_netlist(*selected.region, options, structural, semantic.domains);
    if (!frozen.netlist) {
      result.status = frozen.status;
      result.reason = std::move(frozen.reason);
      return result;
    }
    result.region = Stateful_region{std::move(*selected.region),
                                    std::move(semantic.source),
                                    std::move(semantic.state_bits),
                                    std::move(frozen.netlist)};
  }
  return result;
}

Stateful_result synthesize_stateful_region(const synth::Lnet& net, const synth::Source_state_table& source,
                                           synth::State_target target, const Logical_options& options, Budget& work) {
  return synthesize_stateful_region(net, source, target, options, work, work);
}

}  // namespace livehd::usyn
