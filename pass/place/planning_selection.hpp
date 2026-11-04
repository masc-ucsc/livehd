// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "core/timing_feedback.hpp"

namespace livehd::placement {
// Placement estimates from the same physical timing view, never routed WNS.
// The first measured candidate remains the timing reference for every round.
class Planning_selection {
public:
  struct Sample {
    unsigned              round         = 0;
    size_t                routing_score = 0, data_pins = 0;
    std::optional<double> worst_data_slack_ps;
    bool                  ranking_only = false, accepted = false;
    std::string           reason;
  };
  explicit Planning_selection(double max_degradation_ps = -1);
  bool                    consider(unsigned round, size_t routing_score, const timing::Feedback&);
  const Sample&           last() const { return samples.back(); }
  std::optional<unsigned> selected_round() const { return selected; }
  void                    write(std::ostream&) const;

private:
  double                  limit;
  uint64_t                reference_scope = 0;
  size_t                  incumbent_score = 0;
  std::optional<unsigned> selected;
  std::vector<Sample>     samples;
};
}  // namespace livehd::placement
