// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>

#include "tree_search.hpp"

namespace livehd::placement::buffering {
struct Route_point {
  int64_t x = 0, y = 0;
  auto    operator<=>(const Route_point&) const = default;
};
struct Geometry_request {
  Site_rect                core;
  // Terminal zero is the driver; terminals k+1 are original sinks[k].
  // Duplicate coordinates retain every terminal identity.
  std::vector<Route_point> terminals;
  // Additional possible physical buffer/attachment locations extend the grid.
  std::vector<Route_point> sites;
  // CLOSED hard routing exclusions, independent of placement exclusions.
  // Legal grid lines run one integer DBU beyond grown rectangle boundaries.
  // This is conservative lattice geometry, not a routing-track/capacity model.
  std::vector<Site_rect>   routing_exclusions;
  int64_t                  clearance_dbu = 0;
};
struct Geometry_limits {
  size_t max_input_items = 100000, max_grid_points = 100000, max_edges = 200000, max_obstacle_checks = 10000000;
  double max_seconds = 1;
};
struct Visibility_edge {
  size_t   a = 0, b = 0;
  uint64_t length_dbu = 0;
};
struct Visibility_graph {
  std::vector<Route_point>     vertices;
  std::vector<Visibility_edge> edges;
  std::vector<size_t>          terminals;
};
enum class Geometry_status { complete, budget_exhausted };
struct Geometry_result {
  Geometry_status                         status = Geometry_status::complete;
  // No partially constructed graph is exposed as complete geometry.
  std::shared_ptr<const Visibility_graph> graph;
  bool                                    cache_hit   = false;
  size_t                                  grid_points = 0, obstacle_checks = 0;
  double                                  seconds = 0;
};

// Full Cartesian grid of terminal/site coordinates and outer obstacle corners,
// connected only by clear horizontal/vertical segments. Obstacle unions are
// respected including touching seams; no zero-width passage is manufactured.
// Invalid coordinates/bounds throw. Bounds exhaustion is not disconnection.
Geometry_result build_visibility_graph(const Geometry_request&, const Geometry_limits& = {});

// Bounded LRU geometry cache, independent of electrical RC/library state.
// Entry weight includes grid points, edges and normalized key coordinates. Limits are
// checked on every query, including cache hits. Only complete graphs are cached.
class Geometry_cache {
  struct Impl;
  std::unique_ptr<Impl> impl;

public:
  explicit Geometry_cache(size_t max_entries = 32, size_t max_weight = 1000000);
  ~Geometry_cache();
  Geometry_cache(const Geometry_cache&)            = delete;
  Geometry_cache& operator=(const Geometry_cache&) = delete;
  Geometry_result get(const Geometry_request&, const Geometry_limits& = {});
  size_t          size() const;
  size_t          weight() const;
};

// Physical micron position of an integer visibility vertex.
Point        physical_point(Route_point, double dbu_per_um);
// Wire-only segment between the routing-tree vertices of one axis-aligned
// visibility edge, with its directional technology RC (horizontal R/C,
// vertical R/C per micron). Lossy unit conversion throws.
Tree_segment visibility_segment(Route_point parent, Route_point child, uint64_t length_dbu, size_t parent_vertex,
                                size_t child_vertex, double dbu_per_um, const std::array<double, 4>& directional_rc);

enum class Route_status { connected, disconnected, budget_exhausted };
struct Route_limits {
  size_t max_vertices = 100000, max_edges = 200000, max_relaxations = 1000000;
  double max_seconds = 1;
};
struct Route_result {
  Route_status                status = Route_status::disconnected;
  std::optional<Routing_tree> tree;
  size_t                      relaxations = 0;
  uint64_t                    length_dbu  = 0;
  double                      seconds     = 0;
};
// Deterministic root shortest-path union, pruned to the original sinks. Every
// edge receives explicit directional technology RC (ohm/um, fF/um), never
// invented upper-layer RC. This supplies ONE obstacle-avoiding topology to
// expand_tree/search_tree, not CPB topology optimization or a Steiner optimum.
// Whole routing-layer/capacity, physical and timing checks remain mandatory.
Route_result shortest_path_tree(const Visibility_graph&, double dbu_per_um, const std::array<double, 4>& directional_rc,
                                const Route_limits& = {});
}  // namespace livehd::placement::buffering
