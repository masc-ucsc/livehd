// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <span>

#include "inou/lefdef/physical.hpp"
#include "pass/place/pcrs/clock_tree.hpp"

namespace livehd::placement {
struct Clone_branch {
  std::string instance, input;
};
struct Clock_clone_leaf {
  std::string                         gate, clk, enable, test, output, buffer_output;
  // Generated buffer path, from the chosen leaf toward the old gate.
  std::vector<Clone_branch>           path;
  std::vector<pcrs::Clock_sink>       sinks;
  const pcrs::Clock_driver*           driver;
  double                              priority_ps, wire_um;
  std::span<const pcrs::Clock_driver> alternative_drives{};
  std::optional<physical::Connection> upstream_tap{};  // Input pin of the native buffer driving the gate clock.
};
using Output_fanout_limits = std::map<std::pair<Name, Name>, double>;
struct Clock_clone {
  physical::Component                              cell;
  std::string                                      chunk;
  std::string                                      gate, clk, enable, test, output, leaf_net, detached_net;
  physical::Connection                             detached_pin, clock_driver;
  std::optional<physical::Connection>              upstream_tap;
  std::vector<std::pair<std::string, std::string>> inputs;  // pin and existing net
  std::vector<std::string>                         removed_buffers, removed_nets;
  std::vector<physical::Connection>                sinks;
  double                                           area_delta_um2 = 0;
};
// Prepare an edit against the selected buffered topology. No edit is published
// until the complete candidate layout has passed propagated timing.
std::optional<Clock_clone> plan_clock_clone(const physical::Floorplan&, const physical::Technology&, const Clock_clone_leaf&,
                                            const pcrs::Clock_limits&, const Output_fanout_limits&, std::string& decision,
                                            bool upstream = false, const pcrs::Clock_driver* requested_drive = nullptr);
void                       apply_clock_clone(physical::Floorplan&, const Clock_clone&);
}  // namespace livehd::placement
