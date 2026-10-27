// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "domino_delay.hpp"

#include <cmath>
#include <format>

namespace livehd::domplace {
namespace {
namespace model = liberty::timing;

std::optional<Fo4_derivation> refuse(std::string* why, std::string reason) {
  if (why) {
    *why = std::move(reason);
  }
  return std::nullopt;
}
}  // namespace

std::optional<Fo4_derivation> derive_fo4(const model::Cell& inverter, double c_in_ff, std::string* why) {
  if (!std::isfinite(c_in_ff) || !(c_in_ff > 0)) {
    return refuse(why, std::format("{} has no positive Liberty input capacitance", inverter.name));
  }
  const model::Pin* input = nullptr;
  for (const auto& [name, pin] : inverter.pins) {
    if (pin.direction == "input") {
      if (input) {
        return refuse(why, std::format("{} has more than one input pin", inverter.name));
      }
      input = &pin;
    }
  }
  if (!input) {
    return refuse(why, std::format("{} has no input pin", inverter.name));
  }
  const model::Arc* arc    = nullptr;
  const model::Pin* output = nullptr;
  for (const auto& [name, pin] : inverter.pins) {
    if (pin.direction != "output") {
      continue;
    }
    for (const auto& candidate : pin.arcs) {
      if (candidate.related_pin == input->name && (candidate.type.empty() || candidate.type == "combinational")
          && candidate.delay[0] && candidate.delay[1] && candidate.slew[0] && candidate.slew[1]) {
        arc    = &candidate;
        output = &pin;
        break;
      }
    }
  }
  if (!arc) {
    return refuse(why,
                  std::format("{} has no combinational NLDM arc from {} with cell_rise, cell_fall, rise_transition and "
                              "fall_transition tables",
                              inverter.name,
                              input->name));
  }
  Fo4_derivation out;
  out.inverter   = inverter.name;
  out.input_pin  = input->name;
  out.output_pin = output->name;
  out.c_in_ff    = c_in_ff;
  out.load_ff    = 4 * c_in_ff;
  // An output rise follows an input fall: in an FO4 chain each edge's input
  // slew is the previous stage's opposite output transition.
  const auto at  = [&](const model::Table& table, double slew) {
    return table.lookup({.input_slew_ps = slew, .output_load_ff = out.load_ff});
  };
  double rise = 0, fall = 0;
  for (out.iterations = 1; out.iterations <= 200; ++out.iterations) {
    const double next_rise = at(*arc->slew[0], fall);
    const double next_fall = at(*arc->slew[1], rise);
    const bool   done      = std::abs(next_rise - rise) < 1e-9 && std::abs(next_fall - fall) < 1e-9;
    rise                   = next_rise;
    fall                   = next_fall;
    if (!std::isfinite(rise) || !std::isfinite(fall) || rise < 0 || fall < 0) {
      break;
    }
    if (done) {
      out.slew_rise_ps  = rise;
      out.slew_fall_ps  = fall;
      out.delay_rise_ps = at(*arc->delay[0], fall);
      out.delay_fall_ps = at(*arc->delay[1], rise);
      out.fo4_ps        = (out.delay_rise_ps + out.delay_fall_ps) / 2;
      if (!(out.fo4_ps > 0) || !std::isfinite(out.fo4_ps)) {
        break;
      }
      return out;
    }
  }
  return refuse(why,
                std::format("{}: the FO4 slew iteration on its NLDM tables did not reach a positive fixed point", inverter.name));
}

Delay_model domino_delay_model(const Domino_reference& reference, const Delay_parameters& parameters) {
  Delay_model out;
  out.parameters  = parameters;
  const auto& inv = reference.inverter;
  if (!inv.input_capacitance_ff.empty() && inv.input_capacitance_ff.front() > 0) {
    out.c_in_ff = inv.input_capacitance_ff.front();
  }
  if (!reference.inverter_timing) {
    out.issue = std::format("{} has no Liberty NLDM timing model", inv.name);
    return out;
  }
  std::string why;
  out.fo4 = derive_fo4(*reference.inverter_timing, out.c_in_ff.value_or(0), &why);
  if (!out.fo4) {
    out.issue = why;
    return out;
  }
  out.tau_ps = out.fo4->fo4_ps / (4 + parameters.p_inv);
  return out;
}

std::vector<uint32_t> rail_devices(const usyn::Gate_formula& formula, std::span<const usyn::Gate_rail> rails) {
  std::vector<uint32_t> out(rails.size(), 0);
  for (const auto& node : formula.nodes) {
    if (node.kind != usyn::Gate_formula::Kind::literal) {
      continue;
    }
    for (size_t k = 0; k < rails.size(); ++k) {
      if (rails[k].input == node.variable && rails[k].negative == node.inverted) {
        ++out[k];
      }
    }
  }
  return out;
}

std::optional<Stage_delay> estimate_stage_delay(const Delay_model& model, const usyn::Gate_formula::Metrics& metrics,
                                                std::optional<double> register_d_ff, std::string* why) {
  const auto fail = [&](std::string reason) -> std::optional<Stage_delay> {
    if (why) {
      *why = std::move(reason);
    }
    return std::nullopt;
  };
  if (!model.tau_ps || !model.c_in_ff) {
    return fail("no FO4: " + model.issue);
  }
  if (!register_d_ff || !(*register_d_ff > 0)) {
    return fail("the register has no D input capacitance");
  }
  if (metrics.stack == 0 || metrics.branches == 0 || metrics.transistors < metrics.branches) {
    return fail("degenerate pull-down metrics");
  }
  const auto&  q = model.parameters;
  const double s = metrics.stack, b = metrics.branches, n = metrics.transistors;
  const double k = q.internal_load, w = q.precharge_width, p = q.p_inv, l = q.internal_node_weight;
  const double x = q.pulldown_width;
  Stage_delay  d;
  // Footed pull-down: s logic devices plus the foot in series, each an NMOS
  // of width x (U26: half a reference-inverter input, the same device the rail
  // pin capacitance counts), so its drive is x/(s+1) of a unit inverter and
  // its logical effort (x/3) * (s+1)/x = (s+1)/3. Its load is the output
  // inverter, k reference-inverter inputs (3k) over one device input (x).
  d.g_dynamic     = (s + 1) / 3;
  d.h_dynamic     = 3 * k / x;
  // Diffusion on the dynamic node: b branch-top drains (width x) and the
  // precharge drain (w); the n-b devices below a branch top add internal-node
  // diffusion at weight l. Times the pull-down resistance (s+1)/x over the
  // unit inverter's 3, in units of p_inv.
  d.p_dynamic     = p * (s + 1) * (b + l * (n - b) + w / x) / 3;
  // The output inverter (k reference inverters) drives the register D input.
  d.h_output      = *register_d_ff / (k * *model.c_in_ff);
  d.p_output      = p;
  d.eval_tau      = d.g_dynamic * d.h_dynamic + d.p_dynamic + d.h_output + d.p_output;
  // Precharge: the PMOS (drive w/2) charges the output inverter gate and the
  // dynamic-node diffusion (b branch tops of width x and its own drain);
  // internal stack nodes are not precharged.
  d.c_dynamic     = 3 * k + p * (x * b + w);
  d.precharge_tau = d.c_dynamic / (3 * w / 2);
  d.eval_ps       = d.eval_tau * *model.tau_ps;
  d.precharge_ps  = d.precharge_tau * *model.tau_ps;
  return d;
}

}  // namespace livehd::domplace
