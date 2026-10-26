// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <span>

#include "routing_rules.hpp"
#include "tech_profile.hpp"

namespace livehd::physical {
struct Pin_access_point {
  int64_t     x = 0, y = 0;
  std::string via;
  Shape       escape;       // minimum-area landing/escape rectangle, DEF DBU
  std::string pin_layer{};  // physical pin metal, independent of escape metal
};
struct Pin_access_result {
  std::vector<Pin_access_point> points;
  std::map<std::string, size_t> rejected;
  size_t                        examined  = 0;
  bool                          exhausted = false;
};
// Enumerate pin-metal -> horizontal escape access using supported single-cut
// LEF vias between the technology profile's access layers.
// Physical access is independent of USE (SIGNAL/CLOCK/POWER/GROUND); callers
// decide net routing eligibility separately.
// Obstacles must include nearby foreign-net pins, obstructions and existing
// routing/PG metal and cuts in global DEF DBU. The caller owns that occupancy
// query; this function must not be used with an incomplete neighbourhood.
// No new pin metal is allowed: the lower landing must be covered by this pin's
// existing LEF rectangle union. Candidate X positions are pin-layer track
// crossings and manufacturing-grid-snapped pin-rectangle centers; Y positions
// are escape-layer tracks. The escape is checked as new metal. Complete
// net connectivity and final route timing/capacity are separate obligations.
Pin_access_result enumerate_pin_access(const Technology&, const Floorplan&, const Component&, const std::string& pin,
                                       std::span<const Shape> obstacles, const Signal_metal_rules&, size_t budget,
                                       const Pin_access_layers&);
class Routing_occupancy;
// Indexed form for route search. Metal on own_net may merge; existing cuts on
// that same net still participate in cut-spacing checks.
Pin_access_result enumerate_pin_access(const Technology&, const Floorplan&, const Component&, const std::string& pin,
                                       const Routing_occupancy&, const Signal_metal_rules&, size_t budget,
                                       const std::string& own_net, const Pin_access_layers&);
}  // namespace livehd::physical
