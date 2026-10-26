// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "pass/place/objective/proxy_bridge.hpp"

namespace livehd::placement::timeclosure {
struct Pin_weight_settings {
  Objective_settings objective;
  double             graph_fraction = .5, blend_base = .4, rising_base = 5, rising_exponent = 2;
  void               validate() const;
};
struct Pin_weight {
  std::string driver;
  double      graph = 0, path = 0, raw = 0, blend = 0, weight = 0;
};
struct Pin_weights {
  Objective_settings                objective;  // exact window/temperature used by this shared LR state
  std::map<std::string, Pin_weight> pins;
  size_t                            critical_endpoints = 0, removed = 0;
  bool                              epoch_reset              = false;
  // Positive related-output arc contributions on ranked endpoint paths.
  // A shared arc visited by two ranked paths counts twice.
  size_t                            related_output_path_arcs = 0;
};
// Shared LR/path state, independent of the placer. Graph mass is on the
// physical driver-to-sink arc. Ranked endpoint paths add max(delay,0)/k^2
// to the sink for wire arcs and influencing input for cell arcs. Each
// component is max-normalized, then raw=alpha*graph+path. Rise/fall and launch
// domains accumulate by physical pin; clock pins never receive force weights.
// Related-output cell arcs retain their positive delay charge along the same
// ranked path until its declared cell's influencing physical input is reached.
// A missing input, direction mismatch or crossing of ownership is unsupported.
// Per-pin blending follows eta0*a^(max(raw-old,0)^b), clamped to [0,1].
// Pins leaving the window disappear immediately, without a decaying residual.
// Missing/inconsistent worst predecessors and unsupported timing are errors.
// Copy the bank with a retained placement to snapshot/rollback multiplier state.
class Pin_weight_bank {
  Pin_weight_settings                                       settings;
  std::optional<std::tuple<uint64_t, uint64_t, bool, bool>> epoch;
  std::map<std::string, Pin_weight>                         previous;

public:
  explicit Pin_weight_bank(Pin_weight_settings settings = {});
  Pin_weights update(const late::Result& timing);
  void        clear();
  // Change only the dimensionless LR smoothing temperature; retain pin history.
  void        set_temperature_fraction(double value);
};
}  // namespace livehd::placement::timeclosure
