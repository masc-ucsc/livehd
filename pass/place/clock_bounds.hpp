// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/interned_name.hpp"

namespace livehd::placement {
using Endpoint_hold                      = std::map<Pin_label, float>;
inline constexpr double slack_epsilon_ps = 1e-4;

struct Schedule_leaf {
  uint64_t anchor;
  unsigned steps;
  bool     fixed;
  uint64_t member;
  double   period_ps = 0;
  Name     parent;  // Immediate buffered clock parent. Empty at a source.
};
// Additional leaf sizes for a capture upsize that missed hold floors on siblings.
std::map<uint64_t, unsigned> sibling_hold_drives(const Schedule_leaf& target, const std::map<Pin_label, Schedule_leaf>& captures,
                                                 const Endpoint_hold& before, const Endpoint_hold& after, double hold_margin_ps);

struct Sink_waveform {
  Name               clock;
  double             period_ps;
  std::vector<float> arrivals;
};
using Sink_arrivals = std::map<Pin_label, Sink_waveform>;
// Vertex zero is a fixed clock or a port. For setup, 'to' is capture;
// for hold it is launch. A positive difference increases the respective slack.
struct Leaf_slack_edge {
  size_t from, to;
  double slack_ps, margin_ps;
};
std::vector<double> solve_leaf_arrivals(const std::vector<double>& budgets, const std::vector<Leaf_slack_edge>& edges);
struct Clock_bound_violation {
  std::string pin;
  double      reference_ps, trial_ps, limit_ps, excess_ps;
};
struct Clock_bound_check {
  bool                                 same_coverage = false;
  double                               max_shift_ps  = 0;
  std::optional<Clock_bound_violation> offset, hold;
  bool                                 allowed() const { return same_coverage && !offset && !hold; }
};
Clock_bound_check check_clock_bounds(const Endpoint_hold& previous_hold, const Endpoint_hold& trial_hold,
                                     const Sink_arrivals& original, const Sink_arrivals& trial, double period_fraction,
                                     double hold_margin_ps);
}  // namespace livehd::placement
