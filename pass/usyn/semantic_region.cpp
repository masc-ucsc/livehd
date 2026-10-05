// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "semantic_region.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <tuple>

#include "cmos_cleanup.hpp"

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
  // Size against the logical writer before any search: a region whose
  // emission cannot fit max_nodes is refused here, never after its search.
  if (logic.graph.size()
      >= logical_search_nodes(max_nodes, logic.state.size(), source.controls.size(), logic.inputs.size(), logic.outputs.size())) {
    return exhausted();
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
  auto&      semantic = *imported.region;
  // The search may grow the network (endpoint lowering, residual rebuild):
  // cap it so every selection it can publish still fits the writer.
  const auto cap      = logical_search_nodes(options.max_nodes,
                                             semantic.logic.state.size(),
                                             semantic.source.controls.size(),
                                             semantic.logic.inputs.size(),
                                             semantic.logic.outputs.size());
  auto       bounded  = options;
  bounded.max_nodes   = std::min(options.max_nodes, cap);
  Residual_report p1_report;
  const auto      p1_before = search.consumed;
  if (options.pre_optimize && bounded.max_nodes) {
    auto light      = options.residual;
    light.max_nodes = bounded.max_nodes;
    light.and_cost  = options.endpoint.cost.static_and;
    light.xor_cost  = options.endpoint.cost.static_xor;
    light.sweep = light.balance = light.npn4 = true;
    // P1 is light and has its own sweep width (p1_sweep_inputs, default six). A
    // 16-input table costs up to 1024 words per gate: on br_arb_rr width 16
    // spent P1's whole stage budget before NPN4 could run (P1 cost 1846 -> 1214
    // vs 864 at six; mapped 293 vs 241 gates), while br_flow_arb_rr gained
    // (279 -> 267). The full residual pass uses sweep_inputs.
    light.sweep_inputs                       = light.p1_sweep_inputs;
    light.zero_gain                          = false;
    light.resubstitute                       = false;
    light.rewrite_cuts                       = std::min(light.rewrite_cuts, 4U);
    light.window_nodes                       = std::min(light.window_nodes, 64U);
    light.window_work                        = std::min(light.window_work, uint64_t{10000});
    light.stage_work                         = std::min(light.stage_work, uint64_t{1000000});
    auto trial_work                          = search.slice(light.stage_work, 16);
    auto trial                               = clean_cmos_expansion(semantic.logic, light, trial_work);
    search.absorb(trial_work);
    p1_report = std::move(trial.report);
    if (trial.status == Status::invalid) {
      result.status = Status::invalid;
      result.reason = "P1 preparation: " + trial.reason;
      return result;
    }
    if (trial.region) {
      semantic.logic = std::move(*trial.region);
    } else {
      p1_report.exhausted = true;
      p1_report.limits.push_back("P1 snapshot retained");
    }
  }
  const auto p1_work  = search.consumed - p1_before;
  auto       selected = synthesize_logical_region(semantic.logic, semantic.eligible, bounded, structural, search, semantic.domains);
  result.status       = selected.status;
  result.reason       = std::move(selected.reason);
  result.report       = std::move(selected.report);
  result.report.p1    = std::move(p1_report);
  result.report.work.p1    = p1_work;
  result.report.exhausted |= result.report.p1.exhausted;
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
