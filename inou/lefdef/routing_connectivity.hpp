// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <span>
#include <unordered_map>

#include "physical.hpp"

namespace livehd::physical {
// Reuse for routes in one floorplan. Component names and array membership must
// remain unchanged; placement/status updates preserve the stored pointers.
class Route_components {
  const Floorplan*                                  frame_;
  const Component*                                  data_;
  size_t                                            size_;
  std::unordered_map<std::string, const Component*> cells_;

public:
  explicit Route_components(const Floorplan&);
  bool matches(const Floorplan&) const;
  auto find(const std::string& name) const { return cells_.find(name); }
  auto end() const { return cells_.end(); }
};
struct Route_connectivity {
  bool        connected  = false;
  size_t      components = 0;
  std::string reason;
};
// Checks rectangular routed metal and explicit via cuts against every cell pin
// and placed die terminal in a net. Same-layer edge contact connects, point contact does not; a cut
// connects only the metal landings of its own via with positive-area overlap.
// Multiple shapes of one pin are electrically equivalent. Missing/unplaced IO,
// polygon pins and malformed geometry fail closed. This is not a DRC check.
Route_connectivity check_route_connectivity(const Technology&, const Floorplan&, const Net&, const Route_components* = nullptr,
                                            const Route_width_rules* trial_rules = nullptr);
// Native-coordinate conductor check without invented logical endpoints. All
// path/fixed-metal components are obligations, regardless of their labels.
// Only explicit native via cuts join routing layers. Each pin union is one
// actual library pin whose shapes have already been transformed into DBU;
// callers must establish that source identity before using pin equivalence.
// Fixed metal must be routing metal and has no intrinsic shape equivalence.
// One real pin with no new wire is allowed; an entirely empty input fails.
// This checks electrical connectivity, not DRC, source ownership or domains.
Route_connectivity check_conductor_connectivity(const Technology&, const Floorplan&, std::span<const Route_path>,
                                                std::span<const Shape>              fixed_metal,
                                                std::span<const std::vector<Shape>> physical_pin_unions,
                                                const Route_width_rules*            trial_rules = nullptr);
}  // namespace livehd::physical
