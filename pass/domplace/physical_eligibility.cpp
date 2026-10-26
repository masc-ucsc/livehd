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

std::vector<Eligibility> physical_eligibility(const usyn::Stateful_region& region, std::span<const std::string> input_names,
                                              std::string_view clock, std::span<const usyn::Physical_hint> gating) {
  if (!region.frozen || region.state_bits.size() != region.frozen->state.size()) {
    return {};
  }
  const auto& source = region.source;
  for (const auto bit : region.state_bits) {
    if (bit >= source.bits.size() || source.bits[bit].source >= source.sources.size()) {
      return {};
    }
  }
  std::vector<Eligibility> rows;
  for (uint32_t i = 0; i < region.state_bits.size(); ++i) {
    const auto  owner  = source.bits[region.state_bits[i]].source;
    const auto& s      = source.sources[owner];
    auto&       row    = rows.emplace_back();
    row.state          = i;
    // A folded register keeps its original gated clock if it stays CMOS,
    // whatever retains it.
    const bool folded  = owner < gating.size() && gating[owner].code == "fold";
    if (folded && gating[owner].gclk_input >= 0) {
      row.gated_clock = static_cast<uint32_t>(gating[owner].gclk_input);
    }
    if (folded && gating[owner].enable_input >= 0) {
      row.gate_enable = static_cast<uint32_t>(gating[owner].enable_input);
    }
    row.reason = [&] {
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
      if (s.async_reset && s.reset.constant) {
        return Reason::async_reset;  // a tied asynchronous reset has no RST net
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
      if (owner < gating.size() && !gating[owner].code.empty()) {
        const auto& gate = gating[owner];
        if (gate.code != "fold") {
          return reason_from_code(gate.code, Reason::derived_clock);
        }
        const auto root = gate.root_input;
        if (root < 0 || static_cast<size_t>(root) >= input_names.size()) {
          return input_names.empty() ? Reason::definition_clock : Reason::derived_clock;
        }
        if (input_names[root] != clock) {
          return Reason::clock_domain;
        }
        row.clock_root = static_cast<uint32_t>(root);
        return Reason::candidate;
      }
      if (s.icg >= 0) {
        return Reason::gated_clock;
      }
      const auto input = clock_input(region, owner);
      if (input && input_names.empty()) {
        return Reason::definition_clock;  // each instance may clock this port from a different root
      }
      if (!input || *input >= input_names.size() || input_names[*input].empty()) {
        return Reason::derived_clock;
      }
      return input_names[*input] == clock ? Reason::candidate : Reason::clock_domain;
    }();
    // U22: the reset leaves D for the storage's RST pin. Its net is the async
    // reset control, or the region input or register a synchronous one reads.
    if (row.reason != Reason::candidate || !s.reset.present || s.reset.constant) {
      continue;
    }
    const auto bit = source.bits[region.state_bits[i]].bit;
    if (s.reset.bits != 1
        || (s.initial.present && (!s.initial.constant || s.initial.value.unknown_bit_test(static_cast<int>(bit))))) {
      row.reason = s.reset.bits != 1 ? Reason::reset_internal : Reason::reset_value;
      continue;
    }
    usyn::Physical_reset reset;
    reset.async = s.async_reset;
    reset.value = s.initial.present && s.initial.value.bit_test(static_cast<int>(bit));
    if (s.async_reset) {
      const auto control = std::find_if(source.controls.begin(), source.controls.end(), [&](const auto& c) {
        return c.source == owner && c.kind == synth::State_control_kind::async_reset && c.outputs.size() == 1;
      });
      if (control == source.controls.end()) {
        row.reason = Reason::reset_internal;
        continue;
      }
      reset.source     = usyn::Physical_reset::Source::control;
      reset.index      = control->outputs[0];  // the control's frozen output
      reset.active_low = s.neg_reset;
    } else {
      const auto* hint = owner < gating.size() ? &gating[owner] : nullptr;
      if (!hint || (hint->reset_input < 0 && hint->reset_state < 0)) {
        row.reason = Reason::reset_internal;
        continue;
      }
      // The net level that asserts the reset pin, through its inversions.
      reset.active_low = s.neg_reset != hint->reset_inverted;
      reset.source     = hint->reset_input >= 0 ? usyn::Physical_reset::Source::input : usyn::Physical_reset::Source::state;
      reset.index      = static_cast<uint32_t>(hint->reset_input >= 0 ? hint->reset_input : hint->reset_state);
    }
    row.reset = reset;
  }
  return rows;
}

}  // namespace livehd::domplace
