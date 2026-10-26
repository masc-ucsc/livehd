// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <optional>

#include "hhds/graph.hpp"
#include "physical.hpp"

namespace livehd::physical {
struct Stored_route {
  Net               net;
  Route_width_rules rules;
};
// Persistent committed geometry on the graph's driver pin. The caller supplies
// a nonempty context identity covering technology, placement and pin geometry.
// Changing any of those inputs must change that identity. Connectivity and DBU
// are additionally checked explicitly when loading. Search state is not saved.
void store_route(const hhds::Pin_class&, int64_t dbu, const Net&, const Route_width_rules&, const std::string& context);
// Absent or stale data returns nullopt; malformed stored data throws. The frame
// remains untouched: the host still owns installation of geometry and rules.
std::optional<Stored_route> load_route(const hhds::Pin_class&, int64_t dbu, const Net& current, const std::string& context);
void                        clear_route(const hhds::Pin_class&);
// Bind physical names once at the graph boundary, then store/query by driver
// handles. The first net connection is its driver, as in LiveHD's exporter.
// Publish only on a private candidate graph. Replay validates every candidate
// before modifying the frame, and rejects stale placement/technology context.
// These functions do not replace DRC, occupancy or timing acceptance checks.
std::string                 route_technology_identity(const std::vector<std::string>& lefs);
size_t                      publish_routes(hhds::Graph&, const Floorplan&, const std::string& technology_identity);
size_t                      replay_routes(hhds::Graph&, Floorplan&, const std::string& technology_identity);
}  // namespace livehd::physical
