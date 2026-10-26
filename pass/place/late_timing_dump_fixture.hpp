// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <format>
#include <optional>
#include <string>

#include "late_timing.hpp"

namespace livehd::placement::test_fixture {
// Bit-exact text image of every observable late::Result field (hexfloat
// times), for characterization tests that must detect any timing change.
inline std::string late_timing_dump(const late::Result& r) {
  std::string out;
  const auto  number = [](std::optional<double> v) { return v ? std::format("{:a}", *v) : std::string("-"); };
  const auto  state  = [&](const auto& s) {
    std::string text = number(s.arrival_ps) + " " + number(s.required_ps);
    if constexpr (requires { s.slew_ps; }) {
      text += " " + number(s.slew_ps) + " " + number(s.load_ff);
    }
    text += s.predecessor ? std::format(" p{}.{}", s.predecessor->pin, s.predecessor->transition) : std::string(" p-");
    return text;
  };
  out += std::format("certified {} wns {} tns {} period {}\n",
                     r.certified,
                     number(r.wns_ps),
                     number(r.tns_ps),
                     number(r.achievable_period_ps));
  out += std::format("generation {} {} policy {} {}\n",
                     r.topology_generation,
                     r.model_generation,
                     r.characterized_boundary_policy,
                     r.bounded_boundary_policy);
  out += std::format("counts {} {} {} {} {} {} | {} {} {} {} {} {} {}\n",
                     r.visited_pins,
                     r.evaluated_arcs,
                     r.waveform_pins,
                     r.waveform_arcs,
                     r.required_pins,
                     r.delay_only_wire_drivers,
                     r.driver_fits,
                     r.distributed_driver_fits,
                     r.lumped_driver_fits,
                     r.driver_fit_failures,
                     r.table_boundary_driver_fits,
                     r.full_load_boundary_driver_fits,
                     r.crossing_boundary_driver_fits);
  for (const auto& text : r.unsupported) {
    out += "unsupported " + text + "\n";
  }
  for (const auto& text : r.warnings) {
    out += "warning " + text + "\n";
  }
  for (const auto& [name, clock] : r.clocks) {
    out += std::format("clock {} {} wns {} tns {} period {} endpoints {} independent {}\n",
                       name,
                       number(clock.period_ps),
                       number(clock.wns_ps),
                       number(clock.tns_ps),
                       number(clock.achievable_period_ps),
                       clock.timed_endpoints,
                       clock.independent_period);
  }
  for (size_t i = 0; i < r.pins.size(); ++i) {
    const auto& pin  = r.pins[i];
    out             += std::format("pin {} {} clock {} endpoint {} constant {} port {} | {} | {}\n",
                       i,
                       pin.name,
                       pin.clock,
                       pin.endpoint,
                       pin.constant ? (*pin.constant ? "1" : "0") : "-",
                       pin.declaration_port,
                       state(pin.transitions[0]),
                       state(pin.transitions[1]));
  }
  for (size_t d = 0; d < r.domains.size(); ++d) {
    const auto& domain  = r.domains[d];
    out                += std::format("domain {} {}\n", d, domain.launch_clock.value_or("-"));
    for (size_t i = 0; i < domain.pins.size(); ++i) {
      out += std::format("  {} | {} | {}\n", i, state(domain.pins[i][0]), state(domain.pins[i][1]));
    }
  }
  if (r.feasible_graph) {
    const auto& g  = *r.feasible_graph;
    out           += "graph reference " + number(g.reference_period_ps) + " domains";
    for (const auto& launch : g.launch_domains) {
      out += " " + launch.value_or("-");
    }
    out += "\n";
    for (const auto& v : g.vertices) {
      out += std::format("  v {} {} {} {} {}\n",
                         v.pin,
                         v.domain,
                         v.transition,
                         number(v.boundary_arrival_ps),
                         number(v.endpoint_required_ps));
    }
    for (const auto& a : g.arcs) {
      out += std::format("  a {} {} {} {} {}\n", a.from, a.to, a.timing_edge, number(a.delay_ps), a.wire);
    }
  }
  return out;
}
}  // namespace livehd::placement::test_fixture
