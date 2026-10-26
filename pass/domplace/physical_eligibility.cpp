// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "physical_eligibility.hpp"

#include <algorithm>

namespace livehd::domplace {
namespace {
using synth::State_role;
using synth::State_scope;

// The region input that drives this state's clock control output directly, or
// nullopt when the clock is computed (an inverted, logic or black-box signal).
std::optional<size_t> clock_input(const usyn::Stateful_region& region, uint32_t source) {
  const auto& net = *region.frozen;
  for (const auto& control : region.source.controls) {
    if (control.source != source || control.kind != synth::State_control_kind::clock) {
      continue;
    }
    if (control.outputs.size() != 1 || control.outputs[0] >= net.outputs.size()) {
      return std::nullopt;
    }
    const auto signal = net.outputs[control.outputs[0]].signal;
    if (signal.inverted || signal.id >= net.native.size() || net.native.node(signal.id).kind != usyn::Xag::Kind::source) {
      return std::nullopt;
    }
    const auto it = std::find(net.inputs.begin(), net.inputs.end(), signal);
    return it == net.inputs.end() ? std::nullopt : std::optional<size_t>{static_cast<size_t>(it - net.inputs.begin())};
  }
  return std::nullopt;
}
}  // namespace

std::vector<std::pair<uint32_t, Reason>> physical_eligibility(const usyn::Stateful_region& region,
                                                              std::span<const std::string> input_names, std::string_view clock) {
  if (!region.frozen || region.state_bits.size() != region.frozen->state.size()) {
    return {};
  }
  const auto& source = region.source;
  for (const auto bit : region.state_bits) {
    if (bit >= source.bits.size() || source.bits[bit].source >= source.sources.size()) {
      return {};
    }
  }
  std::vector<std::pair<uint32_t, Reason>> rows;
  for (uint32_t i = 0; i < region.state_bits.size(); ++i) {
    const auto  owner  = source.bits[region.state_bits[i]].source;
    const auto& s      = source.sources[owner];
    const auto  reason = [&] {
      // Barriers first: their clock fields are not register semantics.
      if (source.scope == State_scope::memory || s.role == State_role::memory) {
        return Reason::memory;
      }
      if (source.scope == State_scope::opaque || s.role == State_role::opaque || s.role == State_role::hierarchy) {
        return Reason::opaque;
      }
      if (s.role == State_role::transparent_latch) {
        return Reason::transparent_latch;
      }
      if (s.role == State_role::icg) {
        return Reason::icg;
      }
      if (!s.clock_edge_known) {
        return Reason::unknown_edge;
      }
      if (s.neg_clock) {
        return Reason::negative_edge;
      }
      if (s.async_reset) {
        return Reason::async_reset;
      }
      if (s.power_on || region.frozen->state[i].init != 'x') {
        return Reason::initial_value;
      }
      if (!s.clock.present || s.clock.constant) {
        return Reason::constant_clock;
      }
      if (s.clock.bits != 1) {
        return Reason::multibit_clock;
      }
      if (s.icg >= 0) {
        return Reason::gated_clock;
      }
      const auto input = clock_input(region, owner);
      if (!input || *input >= input_names.size() || input_names[*input].empty()) {
        return Reason::derived_clock;
      }
      return input_names[*input] == clock ? Reason::candidate : Reason::clock_domain;
    }();
    rows.emplace_back(i, reason);
  }
  return rows;
}

}  // namespace livehd::domplace
