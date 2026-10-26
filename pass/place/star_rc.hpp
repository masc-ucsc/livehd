// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "rc_cache.hpp"
#include "rc_tree.hpp"

namespace livehd::placement::late {
struct Star_pin {
  std::string           name;
  double                x = 0, y = 0;  // absolute physical position, DBU
  std::array<double, 2> load_ff{};     // actual pin capacitance only
};
struct Star_net {
  // Driver first, then sinks; order and membership stay fixed between calibrations.
  std::vector<Star_pin>   pins;
  const physical::Rc_net* reference = nullptr;  // wire-only Steiner RC at refresh
};
struct Star_stats {
  size_t calibrated = 0, reused = 0, load_rebound = 0, removed = 0, pins_evaluated = 0;
  size_t zero_wire_anchors         = 0;
  size_t displacement_refreshed    = 0;
  double max_displacement_fraction = 0;
};
struct Star_options {
  bool                  calibrate_admittance = false;
  // Max individual pin L1 displacement / anchor bounding-box half-perimeter.
  // A one-DBU denominator handles coincident pins. Unset preserves v1 refresh.
  std::optional<double> max_pin_displacement_fraction;
  bool                  branch_relative_displacement = false;
};
struct Star_state {
  std::vector<std::string> pins;
  Rc_tree_state            timing;
  double                   wire_cap_ff = 0, wire_cap_ratio = 1;
};
// Continuous driver-to-sink star, calibrated to wire load and per-sink Elmore
// delay. Optional passive admittance correction matches the anchor pi; prediction
// after movement still requires independent numerical acceptance.
class Star_bank {
  struct Calibration;
  std::map<std::string, std::shared_ptr<const Calibration>> entries;

public:
  struct Snapshot {
    std::vector<Star_state> nets;
    Star_stats              stats;
  };
  // Complete-frame update, atomic on failure. Movement changes star geometry
  // continuously without recalibration. Membership/policy revisions or refresh
  // create new anchors. Changed loads recompute compensation against the cached
  // wire-only reference at its anchor, without reconstructing Steiner geometry.
  Snapshot evaluate(const std::vector<Star_net>& nets, const Rc_policy& policy, bool refresh = false,
                    const Star_options& options = {});
};
struct Star_context {
  Star_bank                                        bank;
  Rc_policy                                        policy;
  std::map<std::string, std::pair<double, double>> locations;
  bool                                             refresh = false;
  Star_stats                                       stats;
  Star_options                                     options;
};
}  // namespace livehd::placement::late
