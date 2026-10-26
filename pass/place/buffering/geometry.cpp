// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include "geometry.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <list>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

#include "checks.hpp"

namespace livehd::placement::buffering {
namespace {
using detail::Clock;
using detail::elapsed;
using detail::require;
constexpr auto missing = std::numeric_limits<size_t>::max();
uint64_t       distance(int64_t a, int64_t b) { return a >= b ? uint64_t(a) - uint64_t(b) : uint64_t(b) - uint64_t(a); }
bool           inside(Route_point p, Site_rect r) { return p.x >= r.xl && p.x <= r.xh && p.y >= r.yl && p.y <= r.yh; }
struct Key {
  std::array<int64_t, 4>              core;
  std::vector<Route_point>            terminals, sites;
  std::vector<std::array<int64_t, 4>> exclusions;
  auto                                operator<=>(const Key&) const = default;
};
Key normalize(const Geometry_request& request) {
  const auto core = request.core;
  require(core.xl < core.xh && core.yl < core.yh && request.clearance_dbu >= 0, "invalid routing core/clearance");
  require(!request.terminals.empty(), "routing geometry needs a driver terminal");
  Key key{
      {core.xl, core.yl, core.xh, core.yh},
      request.terminals,
      request.sites,
      {}
  };
  for (auto p : request.terminals) {
    require(inside(p, core), "routing terminal outside core");
  }
  for (auto p : request.sites) {
    require(inside(p, core), "routing site outside core");
  }
  std::sort(key.sites.begin(), key.sites.end());
  key.sites.erase(std::unique(key.sites.begin(), key.sites.end()), key.sites.end());
  const auto halo = request.clearance_dbu;
  for (auto r : request.routing_exclusions) {
    require(r.xl < r.xh && r.yl < r.yh, "invalid hard routing exclusion");
    require(r.xl >= std::numeric_limits<int64_t>::min() + halo && r.yl >= std::numeric_limits<int64_t>::min() + halo
                && r.xh <= std::numeric_limits<int64_t>::max() - halo && r.yh <= std::numeric_limits<int64_t>::max() - halo,
            "routing clearance coordinate overflow");
    r = {r.xl - halo, r.yl - halo, r.xh + halo, r.yh + halo};
    if (r.xh < core.xl || r.xl > core.xh || r.yh < core.yl || r.yl > core.yh) {
      continue;
    }
    key.exclusions.push_back({std::max(r.xl, core.xl), std::max(r.yl, core.yl), std::min(r.xh, core.xh), std::min(r.yh, core.yh)});
  }
  std::sort(key.exclusions.begin(), key.exclusions.end());
  key.exclusions.erase(std::unique(key.exclusions.begin(), key.exclusions.end()), key.exclusions.end());
  return key;
}
bool input_fits(const Geometry_request& request, const Geometry_limits& limits) {
  size_t remaining = limits.max_input_items;
  for (auto count : {request.terminals.size(), request.sites.size(), request.routing_exclusions.size()}) {
    if (count > remaining) {
      return false;
    }
    remaining -= count;
  }
  return true;
}
void validate(const Geometry_limits& limits) {
  require(std::isfinite(limits.max_seconds) && limits.max_seconds > 0, "invalid geometry time bound");
}
Geometry_result build(const Key& key, const Geometry_limits& limits, Clock::time_point start) {
  Geometry_result result;
  const auto      exhausted = [&]() {
    result.status = Geometry_status::budget_exhausted;
    result.graph.reset();
    result.seconds = elapsed(start);
    return result;
  };
  if (elapsed(start) >= limits.max_seconds) {
    return exhausted();
  }
  std::set<int64_t> xs{key.core[0], key.core[2]}, ys{key.core[1], key.core[3]};
  for (const auto& points : {key.terminals, key.sites}) {
    for (auto p : points) {
      xs.insert(p.x);
      ys.insert(p.y);
    }
  }
  for (const auto& r : key.exclusions) {
    if (r[0] > key.core[0]) {
      xs.insert(r[0] - 1);
    }
    if (r[2] < key.core[2]) {
      xs.insert(r[2] + 1);
    }
    if (r[1] > key.core[1]) {
      ys.insert(r[1] - 1);
    }
    if (r[3] < key.core[3]) {
      ys.insert(r[3] + 1);
    }
  }
  if (xs.size() > limits.max_grid_points / ys.size()) {
    return exhausted();
  }
  result.grid_points         = xs.size() * ys.size();
  auto                 graph = std::make_shared<Visibility_graph>();
  std::vector<size_t>  grid(result.grid_points, missing);
  std::vector<int64_t> x(xs.begin(), xs.end()), y(ys.begin(), ys.end());
  // Inclusive intersections also block seams inside a union of rectangles.
  const auto           clear = [&](Route_point a, Route_point b) -> std::optional<bool> {
    for (const auto& r : key.exclusions) {
      if (result.obstacle_checks == limits.max_obstacle_checks || elapsed(start) >= limits.max_seconds) {
        return std::nullopt;
      }
      ++result.obstacle_checks;
      if (std::max(a.x, b.x) >= r[0] && std::min(a.x, b.x) <= r[2] && std::max(a.y, b.y) >= r[1] && std::min(a.y, b.y) <= r[3]) {
        return false;
      }
    }
    return true;
  };
  std::map<Route_point, size_t> indices;
  for (size_t iy = 0; iy < y.size(); ++iy) {
    for (size_t ix = 0; ix < x.size(); ++ix) {
      if (elapsed(start) >= limits.max_seconds) {
        return exhausted();
      }
      Route_point p{x[ix], y[iy]};
      const auto  free = clear(p, p);
      if (!free) {
        return exhausted();
      }
      if (!*free) {
        continue;
      }
      const auto index         = graph->vertices.size();
      grid[iy * x.size() + ix] = index;
      indices.emplace(p, index);
      graph->vertices.push_back(p);
      for (auto prior : {ix ? grid[iy * x.size() + ix - 1] : missing, iy ? grid[(iy - 1) * x.size() + ix] : missing}) {
        if (prior == missing) {
          continue;
        }
        const auto q       = graph->vertices[prior];
        const auto allowed = clear(q, p);
        if (!allowed) {
          return exhausted();
        }
        if (*allowed) {
          if (graph->edges.size() == limits.max_edges) {
            return exhausted();
          }
          // Edges are axis-aligned, so only one nonzero difference occurs.
          graph->edges.push_back({prior, index, distance(q.x, p.x) + distance(q.y, p.y)});
        }
      }
    }
  }
  for (auto p : key.terminals) {
    auto it = indices.find(p);
    // A blocked terminal is represented explicitly; never dropped or moved.
    graph->terminals.push_back(it == indices.end() ? missing : it->second);
  }
  if (elapsed(start) >= limits.max_seconds) {
    return exhausted();
  }
  result.graph   = std::move(graph);
  result.seconds = elapsed(start);
  return result;
}
}  // namespace
Geometry_result build_visibility_graph(const Geometry_request& request, const Geometry_limits& limits) {
  const auto start = Clock::now();
  validate(limits);
  if (!input_fits(request, limits)) {
    return {.status = Geometry_status::budget_exhausted, .seconds = elapsed(start)};
  }
  return build(normalize(request), limits, start);
}
struct Geometry_cache::Impl {
  struct Entry {
    Key             key;
    Geometry_result result;
    size_t          weight = 0;
  };
  size_t                                    max_entries, max_weight, weight = 0;
  std::list<Entry>                          entries;
  std::map<Key, std::list<Entry>::iterator> index;
};
Geometry_cache::Geometry_cache(size_t max_entries, size_t max_weight)
    : impl(std::make_unique<Impl>(max_entries, max_weight, 0, std::list<Impl::Entry>{},
                                  std::map<Key, std::list<Impl::Entry>::iterator>{})) {}
Geometry_cache::~Geometry_cache() = default;
size_t          Geometry_cache::size() const { return impl->entries.size(); }
size_t          Geometry_cache::weight() const { return impl->weight; }
Geometry_result Geometry_cache::get(const Geometry_request& request, const Geometry_limits& limits) {
  const auto start = Clock::now();
  validate(limits);
  if (!input_fits(request, limits)) {
    return {.status = Geometry_status::budget_exhausted, .seconds = elapsed(start)};
  }
  auto       key = normalize(request);
  const auto it  = impl->index.find(key);
  if (it != impl->index.end()) {
    auto        result     = it->second->result;
    const auto& graph      = *result.graph;
    result.cache_hit       = true;
    result.obstacle_checks = 0;
    result.seconds         = elapsed(start);
    if (result.grid_points > limits.max_grid_points || graph.edges.size() > limits.max_edges
        || result.seconds >= limits.max_seconds) {
      result.status = Geometry_status::budget_exhausted;
      result.graph.reset();
      return result;
    }
    impl->entries.splice(impl->entries.begin(), impl->entries, it->second);
    return result;
  }
  auto result = build(key, limits, start);
  if (result.status != Geometry_status::complete || !impl->max_entries) {
    return result;
  }
  const auto& graph  = *result.graph;
  // Include the key in the accounting; many redundant obstacle coordinates
  // must not turn a tiny graph into an unbounded cache entry.
  size_t      weight = 0;
  for (auto count : {result.grid_points,
                     graph.edges.size(),
                     graph.terminals.size(),
                     key.terminals.size(),
                     key.sites.size(),
                     key.exclusions.size()}) {
    if (count > impl->max_weight - weight) {
      return result;
    }
    weight += count;
  }
  while (impl->entries.size() >= impl->max_entries || impl->weight > impl->max_weight - weight) {
    auto last     = std::prev(impl->entries.end());
    impl->weight -= last->weight;
    impl->index.erase(last->key);
    impl->entries.erase(last);
  }
  impl->entries.push_front({std::move(key), result, weight});
  impl->index.emplace(impl->entries.front().key, impl->entries.begin());
  impl->weight += weight;
  return result;
}
Point physical_point(Route_point p, double dbu_per_um) {
  const Point position{double(p.x) / dbu_per_um, double(p.y) / dbu_per_um};
  require(std::isfinite(position.x) && std::isfinite(position.y), "routing physical coordinate conversion overflow");
  return position;
}
Tree_segment visibility_segment(Route_point parent, Route_point child, uint64_t length_dbu, size_t parent_vertex,
                                size_t child_vertex, double dbu_per_um, const std::array<double, 4>& rc) {
  const bool   horizontal = parent.y == child.y;
  const double um         = double(length_dbu) / dbu_per_um;
  const double r = um * rc[horizontal ? 0 : 2], c = um * rc[horizontal ? 1 : 3];
  const auto   a = physical_point(parent, dbu_per_um), b = physical_point(child, dbu_per_um);
  const double physical_length = std::abs(a.x - b.x) + std::abs(a.y - b.y);
  require(std::isfinite(um) && std::isfinite(r) && std::isfinite(c) && std::isfinite(physical_length) && physical_length > 0
              && std::abs(physical_length - um) <= 1e-9 * std::max(1., um),
          "routing RC/unit conversion loses physical edge geometry");
  return {parent_vertex, child_vertex, r, c, um, true};
}
Route_result shortest_path_tree(const Visibility_graph& graph, double dbu_per_um, const std::array<double, 4>& rc,
                                const Route_limits& limits) {
  const auto start = Clock::now();
  require(std::isfinite(dbu_per_um) && dbu_per_um > 0 && std::isfinite(limits.max_seconds) && limits.max_seconds > 0,
          "invalid route units/time bound");
  for (auto value : rc) {
    require(std::isfinite(value) && value >= 0, "invalid explicit routing RC");
  }
  require(!graph.terminals.empty(), "route graph needs a driver terminal");
  Route_result result;
  const auto   finish = [&](Route_status status) {
    result.status  = status;
    result.seconds = elapsed(start);
    return result;
  };
  if (graph.vertices.size() > limits.max_vertices || graph.edges.size() > limits.max_edges) {
    return finish(Route_status::budget_exhausted);
  }
  // Conversion into the physical producer uses double coordinates. Reject
  // unsupported DBU magnitudes instead of silently merging distinct vertices.
  constexpr auto exact_limit = detail::exact_dbu_limit;
  for (auto p : graph.vertices) {
    require(p.x >= -exact_limit && p.x <= exact_limit && p.y >= -exact_limit && p.y <= exact_limit,
            "routing DBU exceeds exact physical coordinate range");
  }
  std::vector<std::vector<std::pair<size_t, uint64_t>>> adjacency(graph.vertices.size());
  for (const auto& e : graph.edges) {
    if (elapsed(start) >= limits.max_seconds) {
      return finish(Route_status::budget_exhausted);
    }
    require(e.a < graph.vertices.size() && e.b < graph.vertices.size() && e.a != e.b, "invalid visibility edge");
    const auto a = graph.vertices[e.a], b = graph.vertices[e.b];
    require((a.x == b.x) != (a.y == b.y), "visibility edge must be nonzero rectilinear");
    const auto length = a.x == b.x ? distance(a.y, b.y) : distance(a.x, b.x);
    require(e.length_dbu == length, "visibility edge length differs from geometry");
    adjacency[e.a].push_back({e.b, length});
    adjacency[e.b].push_back({e.a, length});
  }
  for (auto terminal : graph.terminals) {
    require(terminal == missing || terminal < graph.vertices.size(), "invalid routing terminal index");
    if (terminal == missing) {
      return finish(Route_status::disconnected);
    }
  }
  const auto                           root = graph.terminals.front();
  std::vector<std::optional<uint64_t>> dist(graph.vertices.size());
  std::vector<size_t>                  parent(graph.vertices.size(), missing);
  using Item = std::pair<uint64_t, size_t>;
  std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
  std::vector<bool>                                                target(graph.vertices.size());
  size_t                                                           pending = 0;
  for (auto terminal : graph.terminals) {
    if (!target[terminal]) {
      target[terminal] = true;
      ++pending;
    }
  }
  dist[root] = 0;
  queue.push({0, root});
  while (!queue.empty()) {
    if (elapsed(start) >= limits.max_seconds) {
      return finish(Route_status::budget_exhausted);
    }
    auto [d, v] = queue.top();
    queue.pop();
    if (dist[v] != d) {
      continue;
    }
    if (target[v]) {
      target[v] = false;
      if (!--pending) {
        break;
      }
    }
    for (auto [next, length] : adjacency[v]) {
      if (result.relaxations == limits.max_relaxations) {
        return finish(Route_status::budget_exhausted);
      }
      ++result.relaxations;
      if (d > std::numeric_limits<uint64_t>::max() - length) {
        throw std::overflow_error("routing path length overflow");
      }
      auto nd = d + length;
      if (!dist[next] || nd < *dist[next]) {
        dist[next]   = nd;
        parent[next] = v;
        queue.push({nd, next});
      }
    }
  }
  std::vector<bool> keep(graph.vertices.size());
  keep[root] = true;
  for (auto terminal : graph.terminals) {
    if (!dist[terminal]) {
      return finish(Route_status::disconnected);
    }
    for (auto v = terminal; !keep[v]; v = parent[v]) {
      keep[v] = true;
    }
  }
  Routing_tree tree;
  tree.attachment_rc = rc;
  std::vector<size_t>              indices(graph.vertices.size(), missing), order{root};
  std::vector<std::vector<size_t>> children(graph.vertices.size());
  for (size_t v = 0; v < keep.size(); ++v) {
    if (keep[v] && v != root) {
      children[parent[v]].push_back(v);
    }
  }
  for (size_t i = 0; i < order.size(); ++i) {
    const auto v = order[i];
    indices[v]   = i;
    const auto p = graph.vertices[v];
    tree.vertices.push_back({physical_point(p, dbu_per_um), {}, {}});
    for (auto child : children[v]) {
      order.push_back(child);
    }
    if (v == root) {
      continue;
    }
    const auto q      = graph.vertices[parent[v]];
    const auto length = p.y == q.y ? distance(p.x, q.x) : distance(p.y, q.y);
    if (result.length_dbu > std::numeric_limits<uint64_t>::max() - length) {
      throw std::overflow_error("routing total length overflow");
    }
    result.length_dbu += length;
    tree.segments.push_back(visibility_segment(q, p, length, indices[parent[v]], i, dbu_per_um, rc));
  }
  for (size_t s = 1; s < graph.terminals.size(); ++s) {
    tree.vertices[indices[graph.terminals[s]]].sinks.push_back(s - 1);
  }
  if (elapsed(start) >= limits.max_seconds) {
    return finish(Route_status::budget_exhausted);
  }
  result.tree = std::move(tree);
  return finish(Route_status::connected);
}
}  // namespace livehd::placement::buffering
