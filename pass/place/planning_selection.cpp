// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "planning_selection.hpp"

#include <cmath>
#include <iomanip>
#include <ostream>
#include <stdexcept>

namespace livehd::placement {
Planning_selection::Planning_selection(double max_degradation_ps) : limit(max_degradation_ps) {
  if (!std::isfinite(limit) || (limit < 0 && limit != -1)) {
    throw std::invalid_argument("legroute.plan.max_degradation_ps must be -1 (disabled) or finite and nonnegative");
  }
}

bool Planning_selection::consider(unsigned round, size_t routing_score, const timing::Feedback& timing) {
  if (!samples.empty() && (!selected || round <= samples.back().round)) {
    throw std::invalid_argument("planning candidates require a valid reference and increasing round numbers");
  }
  Sample sample;
  sample.round         = round;
  sample.routing_score = routing_score;
  sample.ranking_only  = timing.ranking_only;
  uint64_t   scope     = 14695981039346656037ULL;
  const auto include   = [&](std::string_view label, double slack, bool clock) {
    if (clock || !std::isfinite(slack) || !(timing.time_unit_ps > 0) || !std::isfinite(timing.time_unit_ps)) {
      return;
    }
    const double ps = slack * timing.time_unit_ps;
    if (!std::isfinite(ps)) {
      return;
    }
    sample.worst_data_slack_ps = sample.worst_data_slack_ps ? std::min(*sample.worst_data_slack_ps, ps) : ps;
    ++sample.data_pins;
    // All Feedback layouts enumerate labels in sorted order. Reject a changed
    // finite data-pin scope rather than reward a missing formerly critical pin.
    for (unsigned char c : label) {
      scope = (scope ^ c) * 1099511628211ULL;
    }
    scope *= 1099511628211ULL;  // NUL separator between physical pin identities
  };
  if (timing.ranking) {
    const auto& rank = *timing.ranking;
    if (rank.layout && rank.layout->labels.size() == rank.pins.size()) {
      for (size_t i = 0; i < rank.pins.size(); ++i) {
        include(rank.layout->labels[i], rank.pins[i].slack, rank.pins[i].clock);
      }
    }
  } else {
    timing.for_each_pin([&](const std::string& label, const timing::Feedback::Pin& pin) {
      if (pin.slack) {
        include(label, *pin.slack, pin.clock);
      }
    });
  }
  if (samples.empty()) {
    sample.accepted = limit < 0 || sample.worst_data_slack_ps.has_value();
    sample.reason   = sample.accepted ? "reference" : "reference_timing_missing";
    reference_scope = scope;
  } else {
    const auto& reference = samples.front();
    if (routing_score > incumbent_score) {
      sample.reason = "routing_worse";
    } else if (limit >= 0 && !sample.worst_data_slack_ps) {
      sample.reason = "timing_missing";
    } else if (limit >= 0
               && (sample.ranking_only != reference.ranking_only || scope != reference_scope
                   || sample.data_pins != reference.data_pins)) {
      sample.reason = "timing_view_changed";
    } else if (limit >= 0 && *sample.worst_data_slack_ps < *reference.worst_data_slack_ps - limit) {
      sample.reason = "timing_degraded";
    } else {
      sample.accepted = true;
      sample.reason   = "accepted";
    }
  }
  samples.push_back(std::move(sample));
  if (samples.back().accepted) {
    selected        = round;
    incumbent_score = routing_score;
  } else if (samples.size() == 1) {
    throw std::runtime_error(
        "legroute planning timing guard requires finite nonclock data-pin slack on the initial measured candidate");
  }
  return samples.back().accepted;
}

std::string Planning_selection::recovery_stop_reason(bool continue_rejected, unsigned last_round, bool plan_pass) const {
  if (samples.empty()) {
    throw std::logic_error("planning recovery needs a measured candidate");
  }
  const auto& candidate = samples.back();
  if (candidate.accepted && plan_pass) {
    return "plan_pass";
  }
  if (!candidate.accepted && !continue_rejected) {
    return "candidate_rejected_" + candidate.reason;
  }
  if (candidate.round >= last_round) {
    return "retry_budget_exhausted";
  }
  return {};
}

void Planning_selection::write(std::ostream& out) const {
  const auto number = [&](const std::optional<double>& value) {
    if (value) {
      out << *value;
    } else {
      out << "null";
    }
  };
  out << std::setprecision(17) << "{\"model\":\"estimated_data_pin_slack\",\"routed\":false,\"max_degradation_ps\":" << limit
      << ",\"guard_enabled\":" << (limit >= 0 ? "true" : "false") << ",\"reference_worst_data_slack_ps\":";
  number(samples.empty() ? std::nullopt : samples.front().worst_data_slack_ps);
  out << ",\"selected_round\":";
  if (selected) {
    out << *selected;
  } else {
    out << "null";
  }
  out << ",\"candidates\":[";
  for (size_t i = 0; i < samples.size(); ++i) {
    const auto& s = samples[i];
    out << (i ? "," : "") << "{\"round\":" << s.round << ",\"routing_score\":" << s.routing_score << ",\"worst_data_slack_ps\":";
    number(s.worst_data_slack_ps);
    out << ",\"data_pins\":" << s.data_pins << ",\"ranking_only\":" << (s.ranking_only ? "true" : "false")
        << ",\"accepted\":" << (s.accepted ? "true" : "false") << ",\"reason\":" << std::quoted(s.reason) << '}';
  }
  out << "]}\n";
}
}  // namespace livehd::placement
