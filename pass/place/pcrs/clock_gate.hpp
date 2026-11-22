// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "inou/lefdef/physical.hpp"
#include "pass/liberty/liberty_dff.hpp"

namespace livehd::pcrs {
struct Gate_merge {
  std::string keep, drop, clock_pin, enable_pin, test_pin;
  size_t      clock_net, keep_net, drop_net;
  double      distance_um;
};
// Adjacent copies of a characterized integrated gate driving private flop clocks.
// Both survivor choices are returned in deterministic distance order.
std::vector<Gate_merge> gate_merges(const physical::Floorplan&, const liberty::Cell_library&, const std::vector<bool>& clock_nets);
struct Discrete_clock_gate {
  std::string instance, output, clock_pin, control_pin, latch, latch_clock, latch_data;
  size_t      upstream_net, output_net;
  std::string clock_fork_pin, latch_fork_pin;
  bool        inverted_output       = false;
  bool        edge_triggered_enable = false;
};
struct Gate_clock_paths {
  std::vector<Discrete_clock_gate>        gates;
  // A phase buffer/inverter with a private, unrouted path to this gate or latch clock pin.
  std::map<std::string, std::string>      private_phase_targets;
  // Physical sink pins at the last common driver on each gate's two clock paths.
  // Overlapping pairs are one group, identified by its first pin name.
  std::map<std::string, std::string>      leaf_group;
  // Keep these existing edges between a fork and its gate/latch inputs.
  std::map<size_t, std::set<std::string>> direct_sinks;
};
Gate_clock_paths discrete_gate_paths(const physical::Floorplan&, const liberty::Cell_library&, const std::vector<bool>& clock_nets);
}  // namespace livehd::pcrs
