// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include "dreamplace/native/rudy.hpp"
#include "route_capacity.hpp"

namespace livehd::placement {
class Preroute_demand;
struct Preroute_demand_trial {
  Remaining_route_demand remaining;

private:
  friend class Preroute_demand;
  const Preroute_demand* owner = nullptr;
  size_t                 epoch = 0, net = 0;
};
// Owns one immutable-position snapshot of the final placement. The host supplies
// its existing physical net indices and masks (including clock/reset exclusions).
// No copy of the design per trial; only the candidate mask changes temporarily.
// Accepted pre-routes remain excluded. A rejected trial changes no masks.
// Rebuild this object after any netlist, placement, or pin-offset change.
class Preroute_demand {
  DreamPlace::native::Design                          design_;
  DreamPlace::native::RudyOptions                     options_;
  size_t                                              epoch_ = 0;
  std::optional<DreamPlace::native::RudyMaskCursor> cursor_;

public:
  // realized_nets are already present as physical occupancy/capacity usage;
  // remove their estimated demand before preparing the immutable RSMT cache.
  Preroute_demand(const DreamPlace::native::Design&, DreamPlace::native::RudyOptions, const Route_capacity_grid&,
                  const std::vector<size_t>& realized_nets = {});
  Preroute_demand(const Preroute_demand&)                 = delete;
  Preroute_demand&      operator=(const Preroute_demand&) = delete;
  // Optional acceleration for an immutable placement. Failure leaves queries
  // on the original path, which can still succeed after excluding a bad net.
  bool                  prepare();
  Preroute_demand_trial without(size_t net);
  void                  commit(const Preroute_demand_trial&);
};
}  // namespace livehd::placement
